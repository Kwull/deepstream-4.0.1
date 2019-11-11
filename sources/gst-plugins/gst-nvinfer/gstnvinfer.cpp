/**
 * Copyright (c) 2018-2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 */

#include <string.h>
#include <sstream>
#include <sys/time.h>

#include "gst-nvevent.h"
#include "gstnvdsmeta.h"

#include "gstnvinfer.h"
#include "gstnvinfer_allocator.h"
#include "gstnvinfer_meta_utils.h"
#include "gstnvinfer_property_parser.h"

GST_DEBUG_CATEGORY_STATIC (gst_nvinfer_debug);
#define GST_CAT_DEFAULT gst_nvinfer_debug

#define INTERNAL_BUF_POOL_SIZE 3

#define NVDSINFER_CTX_OUT_POOL_SIZE_FLOW_META 6

/* Tracked objects will be reinferred only when their area in terms of pixels
 * increase by this ratio. */
#define REINFER_AREA_THRESHOLD 0.2

/* Tracked objects in the infer history map will be removed if they have not
 * been accessed for at least this number of frames. The tracker would definitely
 * have dropped references to an unseen object by 150 frames. */
#define CLEANUP_ACCESS_CRITERIA 150

/* Object history map cleanup interval. 1800 frames is a minute with a 30fps input */
#define MAP_CLEANUP_INTERVAL 1800

#define PROCESS_MODEL_FULL_FRAME 1
#define PROCESS_MODEL_OBJECTS 2

/* Warn about untracked objects in async mode every 5 minutes. */
#define UNTRACKED_OBJECT_WARN_INTERVAL (GST_SECOND * 60 * 5)

#define MIN_INPUT_OBJECT_WIDTH 16
#define MIN_INPUT_OBJECT_HEIGHT 16

extern const int DEFAULT_REINFER_INTERVAL = G_MAXINT;

#define IS_DETECTOR_INSTANCE(nvinfer) (nvinfer->init_params->networkType == NvDsInferNetworkType_Detector)
#define IS_CLASSIFIER_INSTANCE(nvinfer) (nvinfer->init_params->networkType == NvDsInferNetworkType_Classifier)
#define IS_SEGMENTATION_INSTANCE(nvinfer) (nvinfer->init_params->networkType == NvDsInferNetworkType_Segmentation)

static GQuark _dsmeta_quark = 0;

/* Default values for properties */
#define DEFAULT_UNIQUE_ID 15
#define DEFAULT_PROCESS_MODE PROCESS_MODEL_FULL_FRAME
#define DEFAULT_CONFIG_FILE_PATH ""
#define DEFAULT_BATCH_SIZE 1
#define DEFAULT_INTERVAL 0
#define DEFAULT_OPERATE_ON_GIE_ID -1
#define DEFAULT_GPU_DEVICE_ID 0
#define DEFAULT_OUTPUT_WRITE_TO_FILE FALSE
#define DEFAULT_OUTPUT_TENSOR_META FALSE

/* By default NVIDIA Hardware allocated memory flows through the pipeline. We
 * will be processing on this type of memory only. */
#define GST_CAPS_FEATURE_MEMORY_NVMM "memory:NVMM"
static GstStaticPadTemplate gst_nvinfer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_NVMM, "{ NV12, RGBA }")));

static GstStaticPadTemplate gst_nvinfer_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_NVMM, "{ NV12, RGBA }")));

/* Define our element type. Standard GObject/GStreamer boilerplate stuff */
#define gst_nvinfer_parent_class parent_class
G_DEFINE_TYPE (GstNvInfer, gst_nvinfer, GST_TYPE_BASE_TRANSFORM);

/* Implementation of the GObject/GstBaseTransform interfaces. */
static void gst_nvinfer_finalize (GObject * object);
static void gst_nvinfer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nvinfer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_nvinfer_start (GstBaseTransform * btrans);
static gboolean gst_nvinfer_stop (GstBaseTransform * btrans);
static gboolean gst_nvinfer_sink_event (GstBaseTransform * trans,
    GstEvent * event);

static GstFlowReturn gst_nvinfer_submit_input_buffer (GstBaseTransform *
    btrans, gboolean discont, GstBuffer * inbuf);
static GstFlowReturn gst_nvinfer_generate_output (GstBaseTransform *
    btrans, GstBuffer ** outbuf);

static gpointer gst_nvinfer_input_queue_loop (gpointer data);
static gpointer gst_nvinfer_output_loop (gpointer data);

static void gst_nvinfer_reset_init_params (GstNvInfer * nvinfer);

/* Create enum type for the process mode property. */
#define GST_TYPE_NVDSINFER_PROCESS_MODE (gst_nvinfer_process_mode_get_type ())
static GType
gst_nvinfer_process_mode_get_type (void)
{
  static volatile gsize process_mode_type = 0;
  static const GEnumValue process_mode[] = {
    {PROCESS_MODEL_FULL_FRAME, "Primary (Full Frame)", "primary"},
    {PROCESS_MODEL_OBJECTS, "Secondary (Objects)", "secondary"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&process_mode_type)) {
    GType tmp = g_enum_register_static ("GstNvInferProcessModeType",
        process_mode);
    g_once_init_leave (&process_mode_type, tmp);
  }

  return (GType) process_mode_type;
}

static inline int
get_element_size (NvDsInferDataType data_type)
{
  switch (data_type) {
    case FLOAT:
      return 4;
    case HALF:
      return 2;
    case INT32:
      return 4;
    case INT8:
      return 1;
    default:
      return 0;
  }
}

/* Install properties, set sink and src pad capabilities, override the required
 * functions of the base class, These are common to all instances of the
 * element.
 */
static void
gst_nvinfer_class_init (GstNvInferClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  /* Overide base class functions */
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_nvinfer_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_nvinfer_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_nvinfer_get_property);

  gstbasetransform_class->start = GST_DEBUG_FUNCPTR (gst_nvinfer_start);
  gstbasetransform_class->stop = GST_DEBUG_FUNCPTR (gst_nvinfer_stop);
  gstbasetransform_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_nvinfer_sink_event);

  gstbasetransform_class->submit_input_buffer =
      GST_DEBUG_FUNCPTR (gst_nvinfer_submit_input_buffer);
  gstbasetransform_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_nvinfer_generate_output);

  /* Install properties. Values set through these properties override the ones in
   * the config file. */
  g_object_class_install_property (gobject_class, PROP_UNIQUE_ID,
      g_param_spec_uint ("unique-id", "Unique ID",
          "Unique ID for the element. Can be used to "
          "identify output of the element", 0, G_MAXUINT, DEFAULT_UNIQUE_ID,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_PROCESS_MODE,
      g_param_spec_enum ("process-mode", "Process Mode",
          "Infer processing mode", GST_TYPE_NVDSINFER_PROCESS_MODE,
          DEFAULT_PROCESS_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_CONFIG_FILE_PATH,
      g_param_spec_string ("config-file-path", "Config File Path",
          "Path to the configuration file for this instance of nvinfer",
          DEFAULT_CONFIG_FILE_PATH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_BATCH_SIZE,
      g_param_spec_uint ("batch-size", "Batch Size",
          "Maximum batch size for inference",
          1, NVDSINFER_MAX_BATCH_SIZE, DEFAULT_BATCH_SIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_INTERVAL,
      g_param_spec_uint ("interval", "Interval",
          "Specifies number of consecutive batches to be skipped for inference",
          0, G_MAXINT, DEFAULT_INTERVAL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_OPERATE_ON_GIE_ID,
      g_param_spec_int ("infer-on-gie-id", "Infer on Gie ID",
          "Infer on metadata generated by GIE with this unique ID.\n"
          "\t\t\tSet to -1 to infer on all metadata.",
          -1, G_MAXINT, DEFAULT_OPERATE_ON_GIE_ID,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_OPERATE_ON_CLASS_IDS,
      g_param_spec_string ("infer-on-class-ids", "Infer on Class ids",
          "Infer on objects with specified class ids\n"
          "\t\t\tUse string with values of class ids in ClassID (int) to set the property.\n"
          "\t\t\t e.g. 0:2:3",
          "",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_MODEL_ENGINEFILE,
      g_param_spec_string ("model-engine-file", "Model Engine File",
          "Absolute path to the pre-generated serialized engine file for the model",
          "",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_GPU_DEVICE_ID,
      g_param_spec_uint ("gpu-id", "Set GPU Device ID",
          "Set GPU Device ID",
          0, G_MAXUINT, DEFAULT_GPU_DEVICE_ID,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_WRITE_TO_FILE,
      g_param_spec_boolean ("raw-output-file-write", "Raw Output File Write",
          "Write raw inference output to file",
          DEFAULT_OUTPUT_WRITE_TO_FILE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_CALLBACK,
      g_param_spec_pointer ("raw-output-generated-callback",
          "Raw Output Generated Callback",
          "Pointer to the raw output generated callback funtion\n"
          "\t\t\t(type: gst_nvinfer_raw_output_generated_callback in 'gstnvdsinfer.h')",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_CALLBACK_USERDATA,
      g_param_spec_pointer ("raw-output-generated-userdata",
          "Raw Output Generated UserData",
          "Pointer to the userdata to be supplied with raw output generated callback",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_TENSOR_META,
      g_param_spec_boolean ("output-tensor-meta", "Output Tensor Meta",
          "Attach inference tensor outputs as buffer metadata",
          DEFAULT_OUTPUT_TENSOR_META,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));


  /* Set sink and src pad capabilities */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_nvinfer_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_nvinfer_sink_template));

  /* Set metadata describing the element */
  gst_element_class_set_details_simple (gstelement_class, "NvInfer plugin",
      "NvInfer Plugin",
      "Nvidia DeepStreamSDK TensorRT plugin",
      "NVIDIA Corporation. Deepstream for Tesla forum: "
      "https://devtalk.nvidia.com/default/board/209");
}

static void
gst_nvinfer_init (GstNvInfer * nvinfer)
{
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (nvinfer);

  /* We will not be generating a new buffer. Just adding / updating
   * metadata. */
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (btrans), TRUE);
  /* We do not want to change the input caps. Set to passthrough. transform_ip
   * is still called. */
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (btrans), TRUE);

  /* Initialize all property variables to default values */
  nvinfer->init_params = new NvDsInferContextInitParams;
  NvDsInferContext_ResetInitParams (nvinfer->init_params);
  nvinfer->unique_id = DEFAULT_UNIQUE_ID;
  nvinfer->process_full_frame = DEFAULT_PROCESS_MODE;
  nvinfer->config_file_path = g_strdup (DEFAULT_CONFIG_FILE_PATH);
  nvinfer->operate_on_class_ids = new std::vector < gboolean >;
  nvinfer->output_tensor_meta = DEFAULT_OUTPUT_TENSOR_META;

  nvinfer->max_batch_size = nvinfer->init_params->maxBatchSize =
      DEFAULT_BATCH_SIZE;
  nvinfer->interval = DEFAULT_INTERVAL;
  nvinfer->operate_on_gie_id = DEFAULT_OPERATE_ON_GIE_ID;
  nvinfer->gpu_id = nvinfer->init_params->gpuID = DEFAULT_GPU_DEVICE_ID;
  nvinfer->is_prop_set = new std::vector < gboolean > (PROP_LAST, FALSE);

  nvinfer->untracked_object_warn_pts = GST_CLOCK_TIME_NONE;

  /* This quark is required to identify NvDsMeta when iterating through
   * the buffer metadatas */
  if (!_dsmeta_quark)
    _dsmeta_quark = g_quark_from_static_string (NVDS_META_STRING);
}

/* Free resources allocated during init. */
static void
gst_nvinfer_finalize (GObject * object)
{
  GstNvInfer *nvinfer = GST_NVINFER (object);

  delete[]nvinfer->init_params->perClassDetectionParams;
  g_strfreev (nvinfer->init_params->outputLayerNames);
  delete nvinfer->init_params;

  delete nvinfer->perClassDetectionFilterParams;
  delete nvinfer->perClassColorParams;
  delete nvinfer->is_prop_set;

  g_free (nvinfer->config_file_path);

  delete nvinfer->operate_on_class_ids;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* Function called when a property of the element is set. Standard boilerplate.
 */
static void
gst_nvinfer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNvInfer *nvinfer = GST_NVINFER (object);

  if (prop_id < PROP_LAST) {
    /* Mark the property as being set through g_object_set. */
    (*nvinfer->is_prop_set)[prop_id] = TRUE;
  }

  switch (prop_id) {
    case PROP_UNIQUE_ID:
      nvinfer->init_params->uniqueID = nvinfer->unique_id =
          g_value_get_uint (value);
      break;
    case PROP_PROCESS_MODE:
    {
      guint val = g_value_get_enum (value);
      nvinfer->process_full_frame = (val == PROCESS_MODEL_FULL_FRAME);
    }
      break;
    case PROP_CONFIG_FILE_PATH:
      g_free (nvinfer->config_file_path);
      nvinfer->config_file_path = g_value_dup_string (value);
      gst_nvinfer_reset_init_params (nvinfer);
      /* Parse the initialization parameters from the config file. This function
       * gives preference to values set through the set_property function over
       * the values set in the config file. */
      nvinfer->config_file_parse_successful =
          gst_nvinfer_parse_config_file (nvinfer, nvinfer->config_file_path);
      break;
    case PROP_OPERATE_ON_GIE_ID:
      nvinfer->operate_on_gie_id = g_value_get_int (value);
      break;
    case PROP_OPERATE_ON_CLASS_IDS:
    {
      std::stringstream str (g_value_get_string (value));
      std::vector < gint > class_ids;
      gint max_class_id = -1;

      while (str.peek () != EOF) {
        gint class_id;
        str >> class_id;
        class_ids.push_back (class_id);
        max_class_id = MAX (max_class_id, class_id);
        str.get ();
      }
      nvinfer->operate_on_class_ids->assign (max_class_id + 1, FALSE);
    for (auto & cid:class_ids)
        nvinfer->operate_on_class_ids->at (cid) = TRUE;
    }
      break;
    case PROP_BATCH_SIZE:
      nvinfer->max_batch_size = nvinfer->init_params->maxBatchSize =
          g_value_get_uint (value);
      break;
    case PROP_INTERVAL:
      nvinfer->interval = g_value_get_uint (value);
      break;
    case PROP_MODEL_ENGINEFILE:
      g_strlcpy (nvinfer->init_params->modelEngineFilePath,
          g_value_get_string (value), _PATH_MAX);
      break;
    case PROP_GPU_DEVICE_ID:
      nvinfer->gpu_id = nvinfer->init_params->gpuID = g_value_get_uint (value);
      break;
    case PROP_OUTPUT_WRITE_TO_FILE:
      nvinfer->write_raw_buffers_to_file = g_value_get_boolean (value);
      break;
    case PROP_OUTPUT_CALLBACK:
      nvinfer->output_generated_callback =
          (gst_nvinfer_raw_output_generated_callback)
          g_value_get_pointer (value);
      break;
    case PROP_OUTPUT_CALLBACK_USERDATA:
      nvinfer->output_generated_userdata = g_value_get_pointer (value);
      break;
    case PROP_OUTPUT_TENSOR_META:
      nvinfer->output_tensor_meta = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Function called when a property of the element is requested. Standard
 * boilerplate.
 */
static void
gst_nvinfer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNvInfer *nvinfer = GST_NVINFER (object);

  switch (prop_id) {
    case PROP_UNIQUE_ID:
      g_value_set_uint (value, nvinfer->unique_id);
      break;
    case PROP_PROCESS_MODE:
      g_value_set_enum (value,
          nvinfer->process_full_frame ? PROCESS_MODEL_FULL_FRAME :
          PROCESS_MODEL_OBJECTS);
      break;
    case PROP_CONFIG_FILE_PATH:
      g_value_set_string (value, nvinfer->config_file_path);
      break;
    case PROP_OPERATE_ON_GIE_ID:
      g_value_set_int (value, nvinfer->operate_on_gie_id);
      break;
    case PROP_OPERATE_ON_CLASS_IDS:
    {
      std::stringstream str;
      for (size_t i = 0; i < nvinfer->operate_on_class_ids->size (); i++) {
        if (nvinfer->operate_on_class_ids->at (i))
          str << i << ":";
      }
      g_value_set_string (value, str.str ().c_str ());
    }
      break;
    case PROP_MODEL_ENGINEFILE:
      g_value_set_string (value, nvinfer->init_params->modelEngineFilePath);
      break;
    case PROP_BATCH_SIZE:
      g_value_set_uint (value, nvinfer->max_batch_size);
      break;
    case PROP_INTERVAL:
      g_value_set_uint (value, nvinfer->interval);
      break;
    case PROP_GPU_DEVICE_ID:
      g_value_set_uint (value, nvinfer->gpu_id);
      break;
    case PROP_OUTPUT_WRITE_TO_FILE:
      g_value_set_boolean (value, nvinfer->write_raw_buffers_to_file);
      break;
    case PROP_OUTPUT_CALLBACK:
      g_value_set_pointer (value,
          (gpointer) nvinfer->output_generated_callback);
      break;
    case PROP_OUTPUT_CALLBACK_USERDATA:
      g_value_set_pointer (value, nvinfer->output_generated_userdata);
      break;
    case PROP_OUTPUT_TENSOR_META:
      g_value_set_boolean (value, nvinfer->output_tensor_meta);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nvinfer_logger (NvDsInferContextHandle handle, unsigned int unique_id,
    NvDsInferLogLevel log_level, const char *func_name, const char *log_message,
    void *user_ctx)
{
  GstNvInfer *nvinfer = GST_NVINFER (user_ctx);

  switch (log_level) {
    case NVDSINFER_LOG_ERROR:
      GST_ERROR_OBJECT (nvinfer, "NvDsInferContext[UID %d]:%s(): %s", unique_id,
          func_name, log_message);
      return;
    case NVDSINFER_LOG_WARNING:
      GST_WARNING_OBJECT (nvinfer, "NvDsInferContext[UID %d]:%s(): %s",
          unique_id, func_name, log_message);
      return;
    case NVDSINFER_LOG_INFO:
      GST_INFO_OBJECT (nvinfer, "NvDsInferContext[UID %d]:%s(): %s", unique_id,
          func_name, log_message);
      return;
    case NVDSINFER_LOG_DEBUG:
      GST_DEBUG_OBJECT (nvinfer, "NvDsInferContext[UID %d]:%s(): %s", unique_id,
          func_name, log_message);
      return;
  }
}

/**
 * Reset init_params structure while preserving property values set through
 * GObject set method. */
static void
gst_nvinfer_reset_init_params (GstNvInfer * nvinfer)
{
  NvDsInferContextInitParams *prev_params = nvinfer->init_params;
  nvinfer->init_params = new NvDsInferContextInitParams;
  NvDsInferContext_ResetInitParams (nvinfer->init_params);

  if (nvinfer->is_prop_set->at (PROP_MODEL_ENGINEFILE))
    g_strlcpy (nvinfer->init_params->modelEngineFilePath,
        prev_params->modelEngineFilePath, _PATH_MAX);

  if (nvinfer->is_prop_set->at (PROP_BATCH_SIZE))
    nvinfer->init_params->maxBatchSize = prev_params->maxBatchSize;

  if (nvinfer->is_prop_set->at (PROP_GPU_DEVICE_ID))
    nvinfer->init_params->gpuID = prev_params->gpuID;

  delete prev_params->perClassDetectionParams;
  g_strfreev (prev_params->outputLayerNames);
  delete prev_params;
}

/**
 * Called when an event is recieved on the sink pad. We need to make sure
 * serialized events and buffers are pushed downstream while maintaining the order.
 * To ensure this, we push all the buffers in the internal queue to the
 * downstream element before forwarding the serialized event to the downstream element.
 */
static gboolean
gst_nvinfer_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstNvInfer *nvinfer = GST_NVINFER (trans);
  gboolean ignore_serialized_event = FALSE;

  /** The TAG event is sent many times leading to drop in performance because of
   * buffer/event serialization. We can ignore such events which won't cause
   * issues if we don't serialize the events. */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      ignore_serialized_event = TRUE;
      break;
    default:
      break;
  }

  /* Serialize events. Wait for pending buffers to be processed and pushed
   * downstream. No need to wait in case of classifier async mode since all
   * the buffers are already pushed downstream. */
  if (GST_EVENT_IS_SERIALIZED (event) && !ignore_serialized_event &&
      !nvinfer->classifier_async_mode) {
    GstNvInferBatch *batch = new GstNvInferBatch;
    batch->event_marker = TRUE;

    g_mutex_lock (&nvinfer->process_lock);
    /* Push the event marker batch in the processing queue. */
    g_queue_push_tail (nvinfer->input_queue, batch);
    g_cond_broadcast (&nvinfer->process_cond);

    /* Wait for all the remaining batches in the queue including the event
     * marker to be processed. */
    while (!g_queue_is_empty (nvinfer->input_queue)) {
      g_cond_wait (&nvinfer->process_cond, &nvinfer->process_lock);
    }
    while (!g_queue_is_empty (nvinfer->process_queue)) {
      g_cond_wait (&nvinfer->process_cond, &nvinfer->process_lock);
    }
    g_mutex_unlock (&nvinfer->process_lock);
  }

  if ((GstNvEventType) GST_EVENT_TYPE (event) == GST_NVEVENT_PAD_ADDED) {
    /* New source added in the pipeline. Create a source info instance for it. */
    guint source_id;
    gst_nvevent_parse_pad_added (event, &source_id);
    nvinfer->source_info->emplace (source_id, GstNvInferSourceInfo ());
  }

  if ((GstNvEventType) GST_EVENT_TYPE (event) == GST_NVEVENT_PAD_DELETED) {
    /* Source removed from the pipeline. Remove the related structure. */
    guint source_id;
    gst_nvevent_parse_pad_deleted (event, &source_id);
    nvinfer->source_info->erase (source_id);
  }

  if ((GstNvEventType) GST_EVENT_TYPE (event) == GST_NVEVENT_STREAM_EOS) {
    /* Got EOS from a source. Clean up the object history map. */
    guint source_id;
    gst_nvevent_parse_stream_eos (event, &source_id);
    auto result = nvinfer->source_info->find (source_id);
    if (result != nvinfer->source_info->end ())
      result->second.object_history_map.clear ();
  }

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    nvinfer->interval_counter = 0;
  }

  /* Call the sink event handler of the base class. */
  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

/**
 * Initialize all resources and start the output thread
 */
static gboolean
gst_nvinfer_start (GstBaseTransform * btrans)
{
  GstNvInfer *nvinfer = GST_NVINFER (btrans);
  GstStructure *config = nullptr;
  GstAllocator *allocator = nullptr;
  GstAllocationParams allocation_params;
  cudaError_t cudaReturn;
  NvBufSurfaceColorFormat color_format;
  NvDsInferStatus status;
  std::string nvtx_str;

  nvtx_str = "GstNvInfer: UID=" + std::to_string(nvinfer->unique_id);
  nvinfer->nvtx_domain = nvtxDomainCreate(nvtx_str.c_str());

  /* Providing a valid config file is mandatory. */
  if (!nvinfer->config_file_path || strlen (nvinfer->config_file_path) == 0) {
    GST_ELEMENT_ERROR (nvinfer, LIBRARY, SETTINGS,
        ("Configuration file not provided"), (nullptr));
    goto error;
  }
  if (nvinfer->config_file_parse_successful == FALSE) {
    GST_ELEMENT_ERROR (nvinfer, LIBRARY, SETTINGS,
        ("Configuration file parsing failed"),
        ("Config file path: %s", nvinfer->config_file_path));
    goto error;
  }

  nvinfer->interval_counter = 0;

  /* Should not infer on objects smaller than MIN_INPUT_OBJECT_WIDTH x MIN_INPUT_OBJECT_HEIGHT
   * since it will cause hardware scaling issues. */
  nvinfer->min_input_object_width =
      MAX(MIN_INPUT_OBJECT_WIDTH, nvinfer->min_input_object_width);
  nvinfer->min_input_object_height =
      MAX(MIN_INPUT_OBJECT_HEIGHT, nvinfer->min_input_object_height);

  /* Ask NvDsInferContext to copy the input layer contents to host memory if
   * CPU needs to access it. */
  nvinfer->init_params->copyInputToHostBuffers =
      (nvinfer->write_raw_buffers_to_file ||
      (nvinfer->output_generated_callback != nullptr));

  /* Set the number of output buffers that should be allocated by NvDsInferContext.
   * Should allocate more buffers if the output tensor buffers will be attached
   * as meta to GstBuffers and pushed downstream. */
  nvinfer->init_params->outputBufferPoolSize = NVDSINFER_MIN_OUTPUT_BUFFERPOOL_SIZE;
  if (nvinfer->output_tensor_meta || IS_SEGMENTATION_INSTANCE (nvinfer))
      nvinfer->init_params->outputBufferPoolSize = NVDSINFER_CTX_OUT_POOL_SIZE_FLOW_META;

  /* Create the NvDsInferContext instance. */
  status =
      createNvDsInferContext (&nvinfer->nvdsinfer_ctx, *nvinfer->init_params,
      nvinfer, gst_nvinfer_logger);
  if (status != NVDSINFER_SUCCESS) {
    GST_ELEMENT_ERROR (nvinfer, RESOURCE, FAILED,
        ("Failed to create NvDsInferContext instance"),
        ("Config file path: %s, NvDsInfer Error: %s", nvinfer->config_file_path,
            NvDsInferContext_GetStatusName (status)));
    goto error;
  }

  /* Get the network resolution. */
  nvinfer->nvdsinfer_ctx->getNetworkInfo (nvinfer->network_info);
  nvinfer->network_width = nvinfer->network_info.width;
  nvinfer->network_height = nvinfer->network_info.height;

  /* Get information on all the bound layers. */
  nvinfer->layers_info = new std::vector < NvDsInferLayerInfo > ();
  nvinfer->nvdsinfer_ctx->fillLayersInfo (*nvinfer->layers_info);

  nvinfer->output_layers_info = new std::vector < NvDsInferLayerInfo > ();
for (auto & layer:*(nvinfer->layers_info)) {
    if (!layer.isInput)
      nvinfer->output_layers_info->push_back (layer);
  }

  nvinfer->file_write_batch_num = 0;

  /* Create a queue and the associated lock and condition for synchronization.
   * We will be using this queue to maintain the list of frames/objects
   * currently given to the algorithm for processing. */
  nvinfer->process_queue = g_queue_new ();
  g_mutex_init (&nvinfer->process_lock);
  g_cond_init (&nvinfer->process_cond);
  nvinfer->input_queue = g_queue_new ();

  /* Create a buffer pool for internal memory required for scaling frames to
   * network resolution / cropping objects. The pool allocates
   * INTERNAL_BUF_POOL_SIZE buffers at start and keeps reusing them. */
  nvinfer->pool = gst_buffer_pool_new ();

  config = gst_buffer_pool_get_config (nvinfer->pool);
  gst_buffer_pool_config_set_params (config, nullptr,
      sizeof (GstNvInferMemory), INTERNAL_BUF_POOL_SIZE, INTERNAL_BUF_POOL_SIZE);

  /* Based on the network input requirements decide the buffer pool color format. */
  switch (nvinfer->init_params->networkInputFormat) {
    case NvDsInferFormat_RGB:
    case NvDsInferFormat_BGR:
#ifdef IS_TEGRA
      color_format = NVBUF_COLOR_FORMAT_RGBA;
#else
      color_format = NVBUF_COLOR_FORMAT_RGB;
#endif
      break;
    case NvDsInferFormat_GRAY:
#ifdef IS_TEGRA
      color_format = NVBUF_COLOR_FORMAT_NV12;
#else
      color_format = NVBUF_COLOR_FORMAT_GRAY8;
#endif
      break;
    default:
      GST_ELEMENT_ERROR (nvinfer, LIBRARY, SETTINGS,
          ("Unsupported network input format: %d",
              nvinfer->init_params->networkInputFormat), (nullptr));
      goto error;
  }

  /* Create a new GstNvInferAllocator instance. Allocator has methods to allocate
   * and free custom memories. */
  allocator =
      gst_nvinfer_allocator_new (nvinfer->network_width,
      nvinfer->network_height, color_format, nvinfer->max_batch_size,
      nvinfer->gpu_id);
  memset (&allocation_params, 0, sizeof (allocation_params));
  gst_buffer_pool_config_set_allocator (config, allocator, &allocation_params);

  if (!gst_buffer_pool_set_config (nvinfer->pool, config)) {
    GST_ELEMENT_ERROR (nvinfer, RESOURCE, FAILED,
        ("Failed to set config on buffer pool"), (nullptr));
    goto error;
  }
  config = nullptr;

  /* Start the buffer pool and allocate all internal buffers. */
  if (!gst_buffer_pool_set_active (nvinfer->pool, TRUE)) {
    GST_ELEMENT_ERROR (nvinfer, RESOURCE, FAILED,
        ("Failed to set buffer pool to active"), (nullptr));
    goto error;
  }

  gst_object_unref (allocator);

  cudaReturn = cudaSetDevice (nvinfer->gpu_id);
  if (cudaReturn != cudaSuccess) {
    GST_ELEMENT_ERROR (nvinfer, RESOURCE, FAILED,
        ("Failed to set cuda device %d", nvinfer->gpu_id),
        ("cudaSetDevice failed with error %s", cudaGetErrorName (cudaReturn)));
    goto error;
  }

  cudaReturn =
      cudaStreamCreateWithFlags (&nvinfer->convertStream,
      cudaStreamNonBlocking);
  if (cudaReturn != cudaSuccess) {
    GST_ELEMENT_ERROR (nvinfer, RESOURCE, FAILED,
        ("Failed to create cuda stream"),
        ("cudaStreamCreateWithFlags failed with error %s",
            cudaGetErrorName (cudaReturn)));
    goto error;
  }

  /* Set the NvBufSurfTransform config parameters. */
  nvinfer->transform_config_params.compute_mode = NvBufSurfTransformCompute_Default;
  nvinfer->transform_config_params.gpu_id = nvinfer->gpu_id;
  nvinfer->transform_config_params.cuda_stream = nvinfer->convertStream;

  /* Create the intermediate NvBufSurface structure for holding an array of input
   * NvBufSurfaceParams for batched transforms. */
  nvinfer->tmp_surf.surfaceList = new NvBufSurfaceParams[nvinfer->max_batch_size];
  nvinfer->tmp_surf.batchSize = nvinfer->max_batch_size;
  nvinfer->tmp_surf.gpuId = nvinfer->gpu_id;

  /* Set up the NvBufSurfTransformParams structure for batched transforms. */
  nvinfer->transform_params.src_rect =
      new NvBufSurfTransformRect[nvinfer->max_batch_size];
  nvinfer->transform_params.dst_rect =
      new NvBufSurfTransformRect[nvinfer->max_batch_size];
  nvinfer->transform_params.transform_flag =
      NVBUFSURF_TRANSFORM_FILTER | NVBUFSURF_TRANSFORM_CROP_SRC |
      NVBUFSURF_TRANSFORM_CROP_DST;
  nvinfer->transform_params.transform_flip = NvBufSurfTransform_None;
  nvinfer->transform_params.transform_filter = NvBufSurfTransformInter_Default;

  /* Initialize the object history map for source 0. */
  nvinfer->source_info = new std::unordered_map < gint, GstNvInferSourceInfo >;
  nvinfer->source_info->emplace (0, GstNvInferSourceInfo {
      GstNvInferObjectHistoryMap (), 0}
  );

  if (nvinfer->classifier_async_mode) {
    if (nvinfer->process_full_frame || !IS_CLASSIFIER_INSTANCE (nvinfer)) {
      GST_ELEMENT_WARNING (nvinfer, LIBRARY, SETTINGS,
          ("NvInfer asynchronous mode is applicable for secondary"
              "classifiers only. Turning off asynchronous mode"), (nullptr));
      nvinfer->classifier_async_mode = FALSE;
    }
  }

  /* Start a thread which will pop output from the algorithm, form NvDsMeta and
   * push buffers to the next element. */
  nvinfer->output_thread =
      g_thread_new ("nvinfer-output-thread", gst_nvinfer_output_loop, nvinfer);

  /* Start a thread which will queue input to the NvDsInfer context since
   * queueInputBatch is a blocking function. This is done to parallelize
   * input conversion and queueInputBatch. */
  nvinfer->input_queue_thread =
      g_thread_new ("nvinfer-input-queue-thread", gst_nvinfer_input_queue_loop,
          nvinfer);

  return TRUE;
error:

  if (nvinfer->pool)
    gst_object_unref (nvinfer->pool);

  if (allocator)
    gst_object_unref (allocator);

  if (config)
    gst_structure_free (config);

  if (nvinfer->nvdsinfer_ctx)
    nvinfer->nvdsinfer_ctx->destroy ();

  nvtxDomainDestroy (nvinfer->nvtx_domain);

  return FALSE;
}

/**
 * Stop the output thread and free up all the resources
 */
static gboolean
gst_nvinfer_stop (GstBaseTransform * btrans)
{
  GstNvInfer *nvinfer = GST_NVINFER (btrans);

  g_mutex_lock (&nvinfer->process_lock);
  /* Wait till all the items in the two queues are handled. */
  while (!g_queue_is_empty (nvinfer->input_queue)) {
    g_cond_wait (&nvinfer->process_cond, &nvinfer->process_lock);
  }
  while (!g_queue_is_empty (nvinfer->process_queue)) {
    g_cond_wait (&nvinfer->process_cond, &nvinfer->process_lock);
  }
  nvinfer->stop = TRUE;
  g_cond_broadcast (&nvinfer->process_cond);
  g_mutex_unlock (&nvinfer->process_lock);

  g_thread_join (nvinfer->input_queue_thread);
  g_thread_join (nvinfer->output_thread);

  nvinfer->stop = FALSE;

  delete nvinfer->source_info;
  delete nvinfer->layers_info;
  delete nvinfer->output_layers_info;

  delete[] nvinfer->transform_params.src_rect;
  delete[] nvinfer->transform_params.dst_rect;
  delete[] nvinfer->tmp_surf.surfaceList;

  cudaSetDevice (nvinfer->gpu_id);

  if (nvinfer->convertStream)
    cudaStreamDestroy (nvinfer->convertStream);

  /* Free up the memory allocated by pool. */
  gst_object_unref (nvinfer->pool);

  g_queue_free (nvinfer->process_queue);
  g_mutex_clear (&nvinfer->process_lock);
  g_cond_clear (&nvinfer->process_cond);
  g_queue_free (nvinfer->input_queue);

  /* Destroy the INvInferContext instance. */
  nvinfer->nvdsinfer_ctx->destroy ();

  return TRUE;
}

/**
 * Calls the one of the required conversion functions based on the network
 * input format.
 */
static GstFlowReturn
get_converted_buffer (GstNvInfer * nvinfer, NvBufSurface * src_surf,
    NvBufSurfaceParams * src_frame, NvOSD_RectParams * crop_rect_params,
    NvBufSurface * dest_surf, NvBufSurfaceParams * dest_frame,
    gdouble & ratio_x, gdouble & ratio_y, void *destCudaPtr)
{
  guint src_left = GST_ROUND_UP_2 (crop_rect_params->left);
  guint src_top = GST_ROUND_UP_2 (crop_rect_params->top);
  guint src_width = GST_ROUND_DOWN_2 (crop_rect_params->width);
  guint src_height = GST_ROUND_DOWN_2 (crop_rect_params->height);
  guint dest_width, dest_height;

  if (nvinfer->maintain_aspect_ratio) {
    /* Calculate the destination width and height required to maintain
     * the aspect ratio. */
    double hdest = dest_frame->width * src_height / (double) src_width;
    double wdest = dest_frame->height * src_width / (double) src_height;
    int pixel_size;
    cudaError_t cudaReturn;

    if (hdest <= dest_frame->height) {
      dest_width = dest_frame->width;
      dest_height = hdest;
    } else {
      dest_width = wdest;
      dest_height = dest_frame->height;
    }

    switch (dest_frame->colorFormat) {
      case NVBUF_COLOR_FORMAT_RGBA:
        pixel_size = 4;
        break;
      case NVBUF_COLOR_FORMAT_RGB:
        pixel_size = 3;
        break;
      case NVBUF_COLOR_FORMAT_GRAY8:
      case NVBUF_COLOR_FORMAT_NV12:
        pixel_size = 1;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    /* Pad the scaled image with black color. */
    cudaReturn =
        cudaMemset2DAsync ((uint8_t *) destCudaPtr + pixel_size * dest_width,
        dest_frame->planeParams.pitch[0], 0,
        pixel_size * (dest_frame->width - dest_width), dest_frame->height,
        nvinfer->convertStream);
    if (cudaReturn != cudaSuccess) {
      GST_ERROR_OBJECT (nvinfer,
          "cudaMemset2DAsync failed with error %s while converting buffer",
          cudaGetErrorName (cudaReturn));
      return GST_FLOW_ERROR;
    }
    cudaReturn =
        cudaMemset2DAsync ((uint8_t *) destCudaPtr +
        dest_frame->planeParams.pitch[0] * dest_height,
        dest_frame->planeParams.pitch[0], 0, pixel_size * dest_width,
        dest_frame->height - dest_height, nvinfer->convertStream);
    if (cudaReturn != cudaSuccess) {
      GST_ERROR_OBJECT (nvinfer,
          "cudaMemset2DAsync failed with error %s while converting buffer",
          cudaGetErrorName (cudaReturn));
      return GST_FLOW_ERROR;
    }
  } else {
    dest_width = nvinfer->network_width;
    dest_height = nvinfer->network_height;
  }
  /* Calculate the scaling ratio of the frame / object crop. This will be
   * required later for rescaling the detector output boxes to input resolution.
   */
  ratio_x = (double) dest_width / src_width;
  ratio_y = (double) dest_height / src_height;

  /* Create temporary src and dest surfaces for NvBufSurfTransform API. */
  nvinfer->tmp_surf.surfaceList[nvinfer->tmp_surf.numFilled] = *src_frame;

  /* Set the source ROI. Could be entire frame or an object. */
  nvinfer->transform_params.src_rect[nvinfer->tmp_surf.numFilled] =
      {src_top, src_left, src_width, src_height};
  /* Set the dest ROI. Could be the entire destination frame or part of it to
   * maintain aspect ratio. */
  nvinfer->transform_params.dst_rect[nvinfer->tmp_surf.numFilled] =
      {0, 0, dest_width, dest_height};

  nvinfer->tmp_surf.numFilled++;

  return GST_FLOW_OK;
}

/* Helper function to queue a batch for inferencing and push it to the element's
 * processing queue. */
static gpointer
gst_nvinfer_input_queue_loop (gpointer data)
{
  GstNvInfer *nvinfer = (GstNvInfer *) data;
  std::string nvtx_str;
  nvtxEventAttributes_t eventAttrib = {0};
  eventAttrib.version = NVTX_VERSION;
  eventAttrib.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
  eventAttrib.colorType = NVTX_COLOR_ARGB;
  eventAttrib.color = 0xFFFF0000;
  eventAttrib.messageType = NVTX_MESSAGE_TYPE_ASCII;

  g_mutex_lock (&nvinfer->process_lock);

  while (nvinfer->stop == FALSE) {
    GstNvInferBatch *batch;
    GstNvInferMemory *mem;
    NvDsInferContextBatchInput input_batch;
    std::vector < void *>input_frames;
    unsigned int i;
    NvDsInferStatus status;

    /* Wait if input queue is empty. */
    if (g_queue_is_empty (nvinfer->input_queue)) {
      g_cond_wait (&nvinfer->process_cond, &nvinfer->process_lock);
      continue;
    }
    batch = (GstNvInferBatch *) g_queue_pop_head (nvinfer->input_queue);

    /* Check if this is a push buffer or event marker batch. If yes, no need to
     * queue the input for inferencing. */
    if (batch->push_buffer || batch->event_marker) {
      goto queue_batch;
    }

    mem = gst_nvinfer_buffer_get_memory (batch->conv_buf);

    /* Form the vector of input frame pointers. */
    for (i = 0; i < batch->frames.size (); i++) {
      input_frames.push_back (batch->frames[i].converted_frame_ptr);
    }

    input_batch.inputFrames = input_frames.data ();
    input_batch.numInputFrames = input_frames.size ();

    switch (mem->surf->surfaceList[0].colorFormat) {
      case NVBUF_COLOR_FORMAT_RGBA:
        input_batch.inputFormat = NvDsInferFormat_RGBA;
        break;
      case NVBUF_COLOR_FORMAT_RGB:
        input_batch.inputFormat = NvDsInferFormat_RGB;
        break;
      case NVBUF_COLOR_FORMAT_GRAY8:
      case NVBUF_COLOR_FORMAT_NV12:
        input_batch.inputFormat = NvDsInferFormat_GRAY;
        break;
      default:
        input_batch.inputFormat = NvDsInferFormat_Unknown;
        break;
    }
    input_batch.inputPitch = mem->surf->surfaceList[0].planeParams.pitch[0];

    input_batch.returnInputFunc =
        (NvDsInferContextReturnInputAsyncFunc) gst_buffer_unref;
    input_batch.returnFuncData = batch->conv_buf;

    g_mutex_unlock (&nvinfer->process_lock);

    nvtx_str = "queueInput batch_num=" + std::to_string(nvinfer->current_batch_num);
    eventAttrib.message.ascii = nvtx_str.c_str();
    nvtxDomainRangePushEx(nvinfer->nvtx_domain, &eventAttrib);

    status = nvinfer->nvdsinfer_ctx->queueInputBatch (input_batch);

    nvtxDomainRangePop(nvinfer->nvtx_domain);

    g_mutex_lock (&nvinfer->process_lock);

    if (status != NVDSINFER_SUCCESS) {
      GST_ELEMENT_ERROR (nvinfer, STREAM, FAILED,
          ("Failed to queue input batch for inferencing"), (nullptr));
      continue;
    }

queue_batch:
    /* Push the batch info structure in the processing queue and notify the
     * output thread that a new batch has been queued. */
    g_queue_push_tail (nvinfer->process_queue, batch);
    g_cond_broadcast (&nvinfer->process_cond);
  }
  g_mutex_unlock (&nvinfer->process_lock);

  return NULL;
}

static gboolean
convert_batch_and_push_to_input_thread (GstNvInfer *nvinfer,
    GstNvInferBatch *batch, GstNvInferMemory *mem)
{
  NvBufSurfTransform_Error err;
  std::string nvtx_str;

  /* Set the transform session parameters for the conversions executed in this
   * thread. */
  err = NvBufSurfTransformSetSessionParams (&nvinfer->transform_config_params);
  if (err != NvBufSurfTransformError_Success) {
    GST_ELEMENT_ERROR (nvinfer, STREAM, FAILED,
        ("NvBufSurfTransformSetSessionParams failed with error %d", err), (NULL));
    return FALSE;
  }

  nvtxEventAttributes_t eventAttrib = {0};
  eventAttrib.version = NVTX_VERSION;
  eventAttrib.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
  eventAttrib.colorType = NVTX_COLOR_ARGB;
  eventAttrib.color = 0xFFFF0000;
  eventAttrib.messageType = NVTX_MESSAGE_TYPE_ASCII;
  nvtx_str = "convert_buf batch_num=" + std::to_string(nvinfer->current_batch_num);
  eventAttrib.message.ascii = nvtx_str.c_str();

  nvtxDomainRangePushEx(nvinfer->nvtx_domain, &eventAttrib);

  /* Batched tranformation. */
  err = NvBufSurfTransform (&nvinfer->tmp_surf, mem->surf,
            &nvinfer->transform_params);

  nvtxDomainRangePop (nvinfer->nvtx_domain);

  if (err != NvBufSurfTransformError_Success) {
    GST_ELEMENT_ERROR (nvinfer, STREAM, FAILED,
        ("NvBufSurfTransform failed with error %d while converting buffer", err),
        (NULL));
    return FALSE;
  }

  g_mutex_lock (&nvinfer->process_lock);
  /* Push the batch info structure in the processing queue and notify the output
   * thread that a new batch has been queued. */
  g_queue_push_tail (nvinfer->input_queue, batch);
  g_cond_broadcast (&nvinfer->process_cond);
  g_mutex_unlock (&nvinfer->process_lock);

  return TRUE;
}

/* Process entire frames in the batched buffer. */
static GstFlowReturn
gst_nvinfer_process_full_frame (GstNvInfer * nvinfer, GstBuffer * inbuf,
    NvBufSurface * in_surf)
{
  NvOSD_RectParams rect_params;
  NvDsBatchMeta *batch_meta = NULL;
  guint num_filled = 0;
  GstNvInferBatch *batch = nullptr;
  GstBuffer *conv_gst_buf = nullptr;
  GstFlowReturn flow_ret;
  GstNvInferMemory *memory = nullptr;
  gdouble scale_ratio_x, scale_ratio_y;
  gboolean skip_batch;

  /* Process batch only when interval_counter is 0. */
  skip_batch = (nvinfer->interval_counter++ % (nvinfer->interval + 1) > 0);

  if (skip_batch) {
    return GST_FLOW_OK;
  }

  if (((in_surf->memType == NVBUF_MEM_DEFAULT || in_surf->memType == NVBUF_MEM_CUDA_DEVICE) &&
       ((int)in_surf->gpuId != (int)nvinfer->gpu_id)) ||
      (((int)in_surf->gpuId == (int)nvinfer->gpu_id) && (in_surf->memType == NVBUF_MEM_SYSTEM)))  {
    GST_ELEMENT_ERROR (nvinfer, RESOURCE, FAILED,
        ("Memory Compatibility Error:Input surface gpu-id doesnt match with configured gpu-id for element,"
         " please allocate input using unified memory, or use same gpu-ids OR,"
         " if same gpu-ids are used ensure appropriate Cuda memories are used"),
        ("surface-gpu-id=%d,%s-gpu-id=%d",in_surf->gpuId,GST_ELEMENT_NAME(nvinfer),
         nvinfer->gpu_id)); \
      return GST_FLOW_ERROR;
  }


  batch_meta = gst_buffer_get_nvds_batch_meta (inbuf);
  if (batch_meta == nullptr) {
    GST_ELEMENT_ERROR (nvinfer, STREAM, FAILED,
        ("NvDsBatchMeta not found for input buffer."), (NULL));
    return GST_FLOW_ERROR;
  }
  num_filled = batch_meta->num_frames_in_batch;

  /* Processing on full frames. Iterate through all the frames in the batched
   * input buffer. */
  for (guint i = 0; i < num_filled; i++) {
    guint idx;

    /* No existing GstNvInferBatch structure. Allocate a new structure,
     * acquire a buffer from our internal pool for conversions. */
    if (batch == nullptr) {
      batch = new GstNvInferBatch;
      batch->push_buffer = FALSE;
      batch->inbuf = inbuf;
      batch->inbuf_batch_num = nvinfer->current_batch_num;

      flow_ret =
          gst_buffer_pool_acquire_buffer (nvinfer->pool, &conv_gst_buf,
          nullptr);
      if (flow_ret != GST_FLOW_OK) {
        goto done;
      }
      memory = gst_nvinfer_buffer_get_memory (conv_gst_buf);
      if (!memory) {
        flow_ret = GST_FLOW_ERROR;
        goto done;
      }
      batch->conv_buf = conv_gst_buf;
    }

    idx = batch->frames.size ();
    /* Scale the entire frame to network resolution. */
    rect_params.left = 0;
    rect_params.top = 0;
    rect_params.width = in_surf->surfaceList[i].width;
    rect_params.height = in_surf->surfaceList[i].height;

    /* Scale and convert the buffer. */
    if (get_converted_buffer (nvinfer, in_surf, in_surf->surfaceList + i,
            &rect_params, memory->surf, memory->surf->surfaceList + idx,
            scale_ratio_x, scale_ratio_y,
            memory->frame_memory_ptrs[idx]) != GST_FLOW_OK) {
      GST_ELEMENT_ERROR (nvinfer, STREAM, FAILED, ("Buffer conversion failed"),
          (NULL));
      flow_ret = GST_FLOW_ERROR;
      goto done;
    }

    /* Adding a frame to the current batch. Set the frames members. */
    GstNvInferFrame frame;
    frame.converted_frame_ptr = memory->frame_memory_ptrs[idx];
    frame.scale_ratio_x = scale_ratio_x;
    frame.scale_ratio_y = scale_ratio_y;
    frame.obj_meta = nullptr;
    frame.frame_meta = nvds_get_nth_frame_meta (batch_meta->frame_meta_list, i);
    frame.frame_num = frame.frame_meta->frame_num;
    frame.batch_index = i;
    frame.history = nullptr;
    frame.input_surf_params = in_surf->surfaceList + i;
    batch->frames.push_back (frame);

    /* Submit batch if the batch size has reached max_batch_size or
     * if this is the last frame in the input batched buffer. */
    if (batch->frames.size () == nvinfer->max_batch_size || i == num_filled - 1) {
      if (!convert_batch_and_push_to_input_thread (nvinfer, batch, memory)) {
        flow_ret = GST_FLOW_ERROR;
        goto done;
      }

      /* Batch submitted. Set batch to nullptr so that a new GstNvInferBatch
       * structure can be allocated if required. */
      batch = nullptr;
      conv_gst_buf = nullptr;
      nvinfer->tmp_surf.numFilled = 0;
    }
  }
  flow_ret = GST_FLOW_OK;

done:
  delete batch;
  return flow_ret;
}

/* The object history map should be trimmed periodically to keep the map size
 * in check. */
static void
cleanup_history_map (GstNvInfer * nvinfer, GstBuffer * inbuf)
{
  g_mutex_lock (&nvinfer->process_lock);
  /* Find the history map for each source whose frames are present in the batch
   * and trim the map. */
  for (auto &source_iter : *(nvinfer->source_info)) {
    GstNvInferSourceInfo &source_info = source_iter.second;
    if (source_info.last_seen_frame_num - source_info.last_cleanup_frame_num <
        MAP_CLEANUP_INTERVAL)
      continue;
    source_info.last_cleanup_frame_num = source_info.last_seen_frame_num;

    /* Remove entries for objects which have not been seen for
     * CLEANUP_ACCESS_CRITERIA */
    auto iterator = source_info.object_history_map.begin ();
    while (iterator != source_info.object_history_map.end ()) {
      GstNvInferObjectHistory & history = iterator->second;
      if (!history.under_inference &&
          source_info.last_seen_frame_num - history.last_accessed_frame_num >
          CLEANUP_ACCESS_CRITERIA) {
        iterator = source_info.object_history_map.erase (iterator);
      } else {
        ++iterator;
      }
    }
  }
  g_mutex_unlock (&nvinfer->process_lock);
}


/* Function to decide if object should be inferred on. */
static inline gboolean
should_infer_object (GstNvInfer * nvinfer, GstBuffer * inbuf,
    NvDsObjectMeta * obj_meta, gulong frame_num,
    GstNvInferObjectHistory * history)
{
  if (nvinfer->operate_on_gie_id > -1 &&
      obj_meta->unique_component_id != nvinfer->operate_on_gie_id)
    return FALSE;

  if (obj_meta->rect_params.width < nvinfer->min_input_object_width)
    return FALSE;

  if (obj_meta->rect_params.height < nvinfer->min_input_object_height)
    return FALSE;

  if (nvinfer->max_input_object_width > 0 &&
      obj_meta->rect_params.width > nvinfer->max_input_object_width)
    return FALSE;

  if (nvinfer->max_input_object_height > 0 &&
      obj_meta->rect_params.height > nvinfer->max_input_object_height)
    return FALSE;

  /* Infer on object if the operate_on_class_ids list is empty or if
   * the flag at index  class_id is TRUE. */
  if (!nvinfer->operate_on_class_ids->empty () &&
      ((int) nvinfer->operate_on_class_ids->size () <= obj_meta->class_id ||
          nvinfer->operate_on_class_ids->at (obj_meta->class_id) == FALSE)) {
    return FALSE;
  }

  /* History is irrevelavant for detectors. */
  if (history && IS_CLASSIFIER_INSTANCE (nvinfer)) {
    /* Do not infer if the object is already being inferred on maybe from a
     * previous frame. */
    if (history->under_inference)
      return FALSE;

    gboolean should_reinfer = FALSE;

    /* Do not reinfer if the object area has not grown by the reinference area
     * threshold and reinfer interval criteria is not met. */
    if ((history->last_inferred_coords.width *
          history->last_inferred_coords.height * (1 +
            REINFER_AREA_THRESHOLD)) <
        (obj_meta->rect_params.width * obj_meta->rect_params.height))
      should_reinfer = TRUE;

    if (frame_num - history->last_inferred_frame_num >
         nvinfer->secondary_reinfer_interval)
      should_reinfer = TRUE;

    return should_reinfer;
  }

  return TRUE;
}

/* Process on objects detected by upstream detectors.
 *
 * Secondary classifiers can work in asynchronous mode as well. In this mode,
 * tracked objects are cropped and queued for inferencing. The input buffer
 * is pushed downstream (from the input thread itself) without waiting for results.
 * When the infer results are available they are stored in the object history map
 * in the output loop. After the results are available the new/updated results
 * are attached (in the input thread) to the object whenever it is found in the
 * frame again. */
static GstFlowReturn
gst_nvinfer_process_objects (GstNvInfer * nvinfer, GstBuffer * inbuf,
    NvBufSurface * in_surf)
{
  GstNvInferBatch *batch = nullptr;
  GstBuffer *conv_gst_buf = nullptr;
  GstNvInferMemory *memory = nullptr;
  GstFlowReturn flow_ret;
  gdouble scale_ratio_x, scale_ratio_y;
  gboolean warn_untracked_object = FALSE;

  NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (inbuf);
  if (batch_meta == nullptr) {
    GST_ELEMENT_ERROR (nvinfer, STREAM, FAILED,
        ("NvDsBatchMeta not found for input buffer."), (NULL));
    return GST_FLOW_ERROR;
  }

  for (NvDsMetaList * l_frame = batch_meta->frame_meta_list; l_frame != NULL;
      l_frame = l_frame->next) {
    NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) (l_frame->data);
    GstNvInferSourceInfo *source_info = nullptr;

    /* Find the source info instance. */
    auto iter = nvinfer->source_info->find (frame_meta->pad_index);
    if (iter == nvinfer->source_info->end ()) {
      GST_WARNING_OBJECT
          (nvinfer,
          "Source info not found for source %d. Maybe the GST_NVEVENT_PAD_ADDED"
          " event was never generated for the source.", frame_meta->pad_index);
      continue;
    } else {
      source_info = &iter->second;
    }
    source_info->last_seen_frame_num = frame_meta->frame_num;

    /* Iterate through all the objects. */
    for (NvDsMetaList * l_obj = frame_meta->obj_meta_list; l_obj != NULL;
        l_obj = l_obj->next) {
      NvDsObjectMeta *object_meta = (NvDsObjectMeta *) (l_obj->data);
      guint idx;
      GstNvInferObjectHistory *obj_history = nullptr;
      gulong frame_num = frame_meta->frame_num;

      /* Cannot infer on untracked objects in asynchronous mode. */
      if (nvinfer->classifier_async_mode && object_meta->object_id == UNTRACKED_OBJECT_ID) {
        if (!warn_untracked_object) {
          /* Warn periodically about untracked objects in the metadata. */
          if (nvinfer->untracked_object_warn_pts == GST_CLOCK_TIME_NONE ||
              (GST_BUFFER_PTS(inbuf) - nvinfer->untracked_object_warn_pts >
                   UNTRACKED_OBJECT_WARN_INTERVAL)) {
            GST_WARNING_OBJECT (nvinfer, "Untracked objects in metadata. Cannot"
                " infer on untracked objects in asynchronous mode.");
            nvinfer->untracked_object_warn_pts = GST_BUFFER_PTS(inbuf);
          }
        }
        warn_untracked_object = TRUE;
        continue;
      }

      g_mutex_lock (&nvinfer->process_lock);

      /* Find the object history if it exists only when tracking id is valid. */
      if (source_info != nullptr && object_meta->object_id != UNTRACKED_OBJECT_ID) {
        auto search =
            source_info->object_history_map.find (object_meta->object_id);
        if (search != source_info->object_history_map.end ()) {
          obj_history = &search->second;
        }
      }

      if (!should_infer_object (nvinfer, inbuf, object_meta, frame_num,
              obj_history)) {
        /* Should not infer again. */

        /* If this is a classifier and we have history we can attach the
         * last known classification attributes. */
        if (IS_CLASSIFIER_INSTANCE (nvinfer) && obj_history != nullptr) {
          GstNvInferFrame frame;
          frame.obj_meta = object_meta;
          attach_metadata_classifier (nvinfer, nullptr, frame,
              obj_history->cached_info);
          obj_history->last_accessed_frame_num = frame_meta->frame_num;
        }
        g_mutex_unlock (&nvinfer->process_lock);
        continue;
      }

      /* Asynchronous mode. If we have previous results for the tracked object,
       * attach the results. New results will be attached when inference on the
       * object is complete and the object is present in the frame after that. */
      if (obj_history && nvinfer->classifier_async_mode) {
        GstNvInferFrame frame;
        frame.obj_meta = object_meta;
        attach_metadata_classifier (nvinfer, nullptr, frame,
            obj_history->cached_info);
        obj_history->last_accessed_frame_num = frame_meta->frame_num;
      }

      /* Object has a valid tracking id but does not have any history. Create
       * an entry in the map for the object. */
      if (source_info != nullptr && object_meta->object_id != UNTRACKED_OBJECT_ID &&
          obj_history == nullptr) {
        auto ret_iter =
            source_info->object_history_map.emplace (object_meta->object_id,
            GstNvInferObjectHistory ());
        obj_history = &ret_iter.first->second;
      }

      /* Update the object history if it is found. */
      if (obj_history != nullptr) {
        obj_history->under_inference = TRUE;
        obj_history->last_inferred_frame_num = frame_num;
        obj_history->last_accessed_frame_num = frame_num;
        obj_history->last_inferred_coords = object_meta->rect_params;
      }

      g_mutex_unlock (&nvinfer->process_lock);

      /* No existing GstNvInferBatch structure. Allocate a new structure,
       * acquire a buffer from our internal pool for conversions. */
      if (batch == nullptr) {
        batch = new GstNvInferBatch;
        batch->push_buffer = FALSE;
        batch->inbuf = (nvinfer->classifier_async_mode) ? nullptr : inbuf;
        batch->inbuf_batch_num = nvinfer->current_batch_num;

        flow_ret =
            gst_buffer_pool_acquire_buffer (nvinfer->pool, &conv_gst_buf,
            nullptr);
        if (flow_ret != GST_FLOW_OK) {
          goto done;
        }
        memory = gst_nvinfer_buffer_get_memory (conv_gst_buf);
        if (!memory) {
          flow_ret = GST_FLOW_ERROR;
          goto done;
        }
        batch->conv_buf = conv_gst_buf;
      }
      idx = batch->frames.size ();

      /* Crop, scale and convert the buffer. */
      if (get_converted_buffer (nvinfer, in_surf,
              in_surf->surfaceList + frame_meta->batch_id,
              &object_meta->rect_params, memory->surf,
              memory->surf->surfaceList + idx, scale_ratio_x, scale_ratio_y,
              memory->frame_memory_ptrs[idx]) != GST_FLOW_OK) {
        GST_ELEMENT_ERROR (nvinfer, STREAM, FAILED,
            ("Buffer conversion failed"), (NULL));
        flow_ret = GST_FLOW_ERROR;
        goto done;
      }

      /* Adding a frame to the current batch. Set the frames members. */
      GstNvInferFrame frame;
      frame.converted_frame_ptr = memory->frame_memory_ptrs[idx];
      frame.scale_ratio_x = scale_ratio_x;
      frame.scale_ratio_y = scale_ratio_y;
      frame.obj_meta = (nvinfer->classifier_async_mode) ? nullptr : object_meta;
      frame.frame_meta = frame_meta;
      frame.frame_num = frame_num;
      frame.batch_index = frame_meta->batch_id;
      frame.history = obj_history;
      frame.input_surf_params =
          (nvinfer->classifier_async_mode) ? nullptr : (in_surf->surfaceList +
          frame_meta->batch_id);
      batch->frames.push_back (frame);

      /* Submit batch if the batch size has reached max_batch_size. */
      if (batch->frames.size () == nvinfer->max_batch_size) {
      if (!convert_batch_and_push_to_input_thread (nvinfer, batch, memory)) {
        flow_ret = GST_FLOW_ERROR;
        goto done;
      }
#if 0
        if (!submit_batch (nvinfer, batch, conv_gst_buf, memory)) {
          flow_ret = GST_FLOW_ERROR;
          goto done;
        }
#endif
        /* Batch submitted. Set batch to nullptr so that a new GstNvInferBatch
         * structure can be allocated if required. */
        batch = nullptr;
        conv_gst_buf = nullptr;
        nvinfer->tmp_surf.numFilled = 0;
      }
    }
  }

  /* Submit a non-full batch. */
  if (batch) {
      if (!convert_batch_and_push_to_input_thread (nvinfer, batch, memory)) {
        flow_ret = GST_FLOW_ERROR;
        goto done;
      }
#if 0
    if (!submit_batch (nvinfer, batch, conv_gst_buf, memory)) {
      flow_ret = GST_FLOW_ERROR;
      goto done;
    }
#endif
    conv_gst_buf = nullptr;
    batch = nullptr;
    nvinfer->tmp_surf.numFilled = 0;
  }
  flow_ret = GST_FLOW_OK;

  if (nvinfer->current_batch_num -
      nvinfer->last_map_cleanup_frame_num > MAP_CLEANUP_INTERVAL) {
    cleanup_history_map (nvinfer, inbuf);
    nvinfer->last_map_cleanup_frame_num = nvinfer->current_batch_num;
  }

done:
  delete batch;
  return flow_ret;
}

/**
 * Called when element recieves an input buffer from upstream element.
 */
static GstFlowReturn
gst_nvinfer_submit_input_buffer (GstBaseTransform * btrans,
    gboolean discont, GstBuffer * inbuf)
{
  GstNvInfer *nvinfer = GST_NVINFER (btrans);
  GstMapInfo in_map_info;
  NvBufSurface *in_surf;
  GstNvInferBatch *buf_push_batch;
  GstFlowReturn flow_ret;
  std::string nvtx_str;

  nvinfer->current_batch_num++;

  nvtxEventAttributes_t eventAttrib = {0};
  eventAttrib.version = NVTX_VERSION;
  eventAttrib.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
  eventAttrib.colorType = NVTX_COLOR_ARGB;
  eventAttrib.color = 0xFFFF0000;
  eventAttrib.messageType = NVTX_MESSAGE_TYPE_ASCII;
  nvtx_str = "buffer_process batch_num=" + std::to_string(nvinfer->current_batch_num);
  eventAttrib.message.ascii = nvtx_str.c_str();
  nvtxRangeId_t buf_process_range = nvtxDomainRangeStartEx(nvinfer->nvtx_domain, &eventAttrib);

  memset (&in_map_info, 0, sizeof (in_map_info));

  /* Map the buffer contents and get the pointer to NvBufSurface. */
  if (!gst_buffer_map (inbuf, &in_map_info, GST_MAP_READ)) {
    return GST_FLOW_ERROR;
  }
  in_surf = (NvBufSurface *) in_map_info.data;

  nvds_set_input_system_timestamp(inbuf, GST_ELEMENT_NAME(nvinfer));

  if (nvinfer->process_full_frame) {
   flow_ret = gst_nvinfer_process_full_frame (nvinfer, inbuf, in_surf);
  } else {
    flow_ret = gst_nvinfer_process_objects (nvinfer, inbuf, in_surf);
  }

  /* Unmap the input buffer contents. */
  if (in_map_info.data)
    gst_buffer_unmap (inbuf, &in_map_info);

  if (flow_ret == GST_FLOW_ERROR)
    return GST_FLOW_ERROR;

  if (nvinfer->classifier_async_mode) {
    /* Asynchronous mode. Push the buffer immediately instead of waiting for
     * the results. */
    nvtxDomainRangeEnd(nvinfer->nvtx_domain, buf_process_range);

    nvds_set_output_system_timestamp(inbuf, GST_ELEMENT_NAME(nvinfer));

    GstFlowReturn flow_ret = gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (nvinfer),
        inbuf);
    if (nvinfer->last_flow_ret != flow_ret) {
      switch (flow_ret) {
        /* Signal the application for pad push errors by posting a error message
         * on the pipeline bus. */
        case GST_FLOW_ERROR:
        case GST_FLOW_NOT_LINKED:
        case GST_FLOW_NOT_NEGOTIATED:
          GST_ELEMENT_ERROR (nvinfer, STREAM, FAILED,
              ("Internal data stream error."),
              ("streaming stopped, reason %s (%d)", gst_flow_get_name (flow_ret),
                  flow_ret));
          break;
        default:
          break;
      }
    }
    nvinfer->last_flow_ret = flow_ret;

    return flow_ret;
  } else {
    /* Queue a push buffer batch. This batch is not inferred. This batch is to
     * signal the input-queue and output thread that there are no more batches
     * belonging to this input buffer and this GstBuffer can be pushed to
     * downstream element once all the previous processing is done. */
    buf_push_batch = new GstNvInferBatch;
    buf_push_batch->inbuf = inbuf;
    buf_push_batch->push_buffer = TRUE;
    buf_push_batch->nvtx_complete_buf_range = buf_process_range;

    g_mutex_lock (&nvinfer->process_lock);
    g_queue_push_tail (nvinfer->input_queue, buf_push_batch);
    g_cond_broadcast (&nvinfer->process_cond);
    g_mutex_unlock (&nvinfer->process_lock);
  }

  return GST_FLOW_OK;
}

/**
 * If submit_input_buffer is implemented, it is mandatory to implement
 * generate_output. Buffers are not pushed to the downstream element from here.
 * Return the GstFlowReturn value of the latest pad push so that any error might
 * be caught by the application.
 */
static GstFlowReturn
gst_nvinfer_generate_output (GstBaseTransform * btrans, GstBuffer ** outbuf)
{
  GstNvInfer *nvinfer = GST_NVINFER (btrans);
  return nvinfer->last_flow_ret;
}

/** Writes contents of the bound input and output layers to files. */
static void
gst_nvinfer_output_generated_file_write (GstBuffer * buf,
    NvDsInferNetworkInfo * network_info, NvDsInferLayerInfo * layers_info,
    guint num_layers, guint batch_size, GstNvInfer * nvinfer)
{
  guint i;
  gchar file_name[256];
  gchar *iter;

  for (i = 0; i < num_layers; i++) {
    NvDsInferLayerInfo *info = &layers_info[i];
    gsize layer_size = info->dims.numElements * batch_size;
    FILE *file;

    g_snprintf (file_name, 256,
        "gstnvdsinfer_uid-%02d_layer-%s_batch-%010lu_batchsize-%02d.bin",
        nvinfer->unique_id, info->layerName,
        nvinfer->file_write_batch_num, batch_size);
    file_name[255] = '\0';

    /* Replace '/' in a layer name with '_' */
    for (iter = file_name; *iter != '\0'; iter++) {
      if (*iter == '/')
        *iter = '_';
    }

    file = fopen (file_name, "w");
    if (!file) {
      g_printerr ("Could not open file '%s' for writing:%s\n",
          file_name, strerror (errno));
      continue;
    }
    fwrite (info->buffer, get_element_size (info->dataType), layer_size, file);
    fclose (file);
  }
  nvinfer->file_write_batch_num++;
}

/* Called when the last ref on the GstMiniObject inside
 * GstNvInferTensorOutputObject is removed. The batch output can be released
 * back to the NvDsInferContext. */
static void
gst_nvinfer_tensoroutput_free (GstMiniObject * obj)
{
  GstNvInferTensorOutputObject *output_obj =
      (GstNvInferTensorOutputObject *) obj;
  output_obj->nvinfer->nvdsinfer_ctx->releaseBatchOutput (output_obj->
      batch_output);
  delete output_obj;
}

/**
 * Output loop used to pop output from inference, attach the output to the
 * buffer in form of NvDsMeta and push the buffer to downstream element.
 */
static gpointer
gst_nvinfer_output_loop (gpointer data)
{
  GstNvInfer *nvinfer = GST_NVINFER (data);
  NvDsInferStatus status;
  nvtxEventAttributes_t eventAttrib = {0};
  eventAttrib.version = NVTX_VERSION;
  eventAttrib.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
  eventAttrib.colorType = NVTX_COLOR_ARGB;
  eventAttrib.color = 0xFFFF0000;
  eventAttrib.messageType = NVTX_MESSAGE_TYPE_ASCII;
  std::string nvtx_str;

  nvtx_str = "gst-nvinfer_output-loop_uid=" + std::to_string(nvinfer->unique_id);

  g_mutex_lock (&nvinfer->process_lock);
  /* Run till signalled to stop. */
  while (!nvinfer->stop) {
    GstNvInferBatch *batch = nullptr;
    NvDsInferContextBatchOutput *batch_output;

    /* Wait if processing queue is empty. */
    if (g_queue_is_empty (nvinfer->process_queue)) {
      g_cond_wait (&nvinfer->process_cond, &nvinfer->process_lock);
      continue;
    }

    /* Pop a batch from the element's process queue. */
    batch = (GstNvInferBatch *) g_queue_pop_head (nvinfer->process_queue);
    g_cond_broadcast (&nvinfer->process_cond);

    /* Event marker used for synchronization. No need to process further. */
    if (batch->event_marker) {
      delete batch;
      continue;
    }

    g_mutex_unlock (&nvinfer->process_lock);

    /* Need to only push buffer to downstream element. This batch was not
     * actually submitted for inferencing. */
    if (batch->push_buffer) {
      nvtxDomainRangeEnd(nvinfer->nvtx_domain, batch->nvtx_complete_buf_range);

      nvds_set_output_system_timestamp(batch->inbuf, GST_ELEMENT_NAME(nvinfer));

      GstFlowReturn flow_ret =
          gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (nvinfer),
          batch->inbuf);
      if (nvinfer->last_flow_ret != flow_ret) {
        switch (flow_ret) {
          /* Signal the application for pad push errors by posting a error message
           * on the pipeline bus. */
          case GST_FLOW_ERROR:
          case GST_FLOW_NOT_LINKED:
          case GST_FLOW_NOT_NEGOTIATED:
            GST_ELEMENT_ERROR (nvinfer, STREAM, FAILED,
                ("Internal data stream error."),
                ("streaming stopped, reason %s (%d)", gst_flow_get_name (flow_ret),
                    flow_ret));
            break;
          default:
          break;
        }
      }
      nvinfer->last_flow_ret = flow_ret;
      delete batch;
      g_mutex_lock (&nvinfer->process_lock);
      continue;
    }

    nvtx_str = "dequeueOutputAndAttachMeta batch_num=" + std::to_string(batch->inbuf_batch_num);
    eventAttrib.message.ascii = nvtx_str.c_str();
    nvtxDomainRangePushEx(nvinfer->nvtx_domain, &eventAttrib);

    /* Create and initialize the object for managing the usage of batch_output. */
    GstNvInferTensorOutputObject *tensor_out_object = new GstNvInferTensorOutputObject;
    gst_mini_object_init (GST_MINI_OBJECT (tensor_out_object), 0, G_TYPE_POINTER, NULL,
        NULL, gst_nvinfer_tensoroutput_free);
    tensor_out_object->nvinfer = nvinfer;

    batch_output = &tensor_out_object->batch_output;
    /* Dequeue inferencing output from NvDsInferContext */
    status = nvinfer->nvdsinfer_ctx->dequeueOutputBatch (*batch_output);

    g_mutex_lock (&nvinfer->process_lock);

    if (status != NVDSINFER_SUCCESS) {
      GST_ELEMENT_ERROR (nvinfer, STREAM, FAILED,
          ("Failed to dequeue output from inferencing. NvDsInferContext error: %s",
              NvDsInferContext_GetStatusName (status)), (nullptr));
      delete batch;
      continue;
    }

    /* Get the host buffer pointers from the latest dequeued output. */
  for (auto & layer:*nvinfer->layers_info) {
      layer.buffer = batch_output->hostBuffers[layer.bindingIndex];
    }

    /* Write layer contents to file if enabled. */
    if (nvinfer->write_raw_buffers_to_file) {
      gst_nvinfer_output_generated_file_write (batch->inbuf,
          &nvinfer->network_info,
          nvinfer->layers_info->data (),
          nvinfer->layers_info->size (), batch->frames.size (), nvinfer);
    }

    /* Call the output generated callback if specified. */
    if (nvinfer->output_generated_callback) {
      nvinfer->output_generated_callback (batch->inbuf,
          &nvinfer->network_info,
          nvinfer->layers_info->data (),
          nvinfer->layers_info->size (),
          batch->frames.size (), nvinfer->output_generated_userdata);
    }



    /* For each frame attach metadata output. */
    for (guint i = 0; i < batch->frames.size (); i++) {
      GstNvInferFrame & frame = batch->frames[i];
      NvDsInferFrameOutput &frame_output = batch_output->frames[i];

      /* If we have an object's history and the buffer PTS is same as last
       * inferred PTS mark the object as not being inferred. This check could be
       * useful if object is inferred multiple times before completion of an
       * existing inference. */
      if (frame.history != nullptr) {
        if (frame.history->last_inferred_frame_num == frame.frame_num)
          frame.history->under_inference = FALSE;
      }

      if (IS_DETECTOR_INSTANCE (nvinfer)) {
        attach_metadata_detector (nvinfer, GST_MINI_OBJECT (tensor_out_object),
                frame, frame_output.detectionOutput);
      } else if (IS_CLASSIFIER_INSTANCE (nvinfer)) {
        NvDsInferClassificationOutput &classification_output = frame_output.classificationOutput;
        GstNvInferObjectInfo new_info;
        new_info.attributes.assign(classification_output.attributes,
            classification_output.attributes + classification_output.numAttributes);
        new_info.label.assign(classification_output.label);

        /* Object history is available merge the old and new classification
         * results. */
        if (frame.history != nullptr) {
          merge_classification_output (*frame.history, new_info);
        }

        /* Use the merged classification results if available otherwise use
         * the new results. */
        auto &  info = (frame.history) ? frame.history->cached_info : new_info;

        /* Attach metadata only if not operating in async mode. In async mode,
         * the GstBuffer and the associated metadata are not valid here, since
         * the buffer is already pushed downstream. The metadata will be updated
         * in the input thread. */
        if (nvinfer->classifier_async_mode == FALSE) {
          attach_metadata_classifier (nvinfer, GST_MINI_OBJECT (tensor_out_object),
                  frame, info);
        }
      } else if (IS_SEGMENTATION_INSTANCE (nvinfer)) {
        attach_metadata_segmentation (nvinfer, GST_MINI_OBJECT (tensor_out_object),
            frame, frame_output.segmentationOutput);
      }
    }

    if (nvinfer->output_tensor_meta && !nvinfer->classifier_async_mode) {
      /* Attach the tensor output as meta. */
      attach_tensor_output_meta (nvinfer, GST_MINI_OBJECT(tensor_out_object),
          batch, batch_output);
    }
    nvtxDomainRangePop (nvinfer->nvtx_domain);

    /* Drop the original ref owned by this thread. Dropping the ref will release
     * the batch output back to NvDsInferContext if */
    gst_mini_object_unref (GST_MINI_OBJECT (tensor_out_object));
    delete batch;
  }
  g_mutex_unlock (&nvinfer->process_lock);
  return nullptr;
}

/**
 * Boiler plate for registering a plugin and an element.
 */
static gboolean
nvinfer_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_nvinfer_debug, "nvinfer", 0, "nvinfer plugin");
  gst_debug_category_set_threshold (gst_nvinfer_debug, GST_LEVEL_INFO);

  return gst_element_register (plugin, "nvinfer", GST_RANK_PRIMARY,
      GST_TYPE_NVINFER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, nvdsgst_infer,
    DESCRIPTION, nvinfer_plugin_init, DS_VERSION, LICENSE, BINARY_PACKAGE, URL)
