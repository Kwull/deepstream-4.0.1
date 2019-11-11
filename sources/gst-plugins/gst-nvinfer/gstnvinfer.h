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

#ifndef __GST_NVINFER_H__
#define __GST_NVINFER_H__

#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include <unordered_map>
#include <vector>

#include "cuda_runtime_api.h"
#include "nvbufsurftransform.h"
#include <nvdsinfer_context.h>

#include "gstnvdsinfer.h"

#include "gstnvdsmeta.h"

#include "nvtx3/nvToolsExt.h"

/* Package and library details required for plugin_init */
#define PACKAGE "nvinfer"
#define VERSION "1.0"
#define LICENSE "Proprietary"
#define DESCRIPTION "NVIDIA DeepStreamSDK TensorRT plugin"
#define BINARY_PACKAGE "NVIDIA DeepStreamSDK TensorRT plugin"
#define URL "http://nvidia.com/"


G_BEGIN_DECLS
/* Standard GStreamer boilerplate */
typedef struct _GstNvInfer GstNvInfer;
typedef struct _GstNvInferClass GstNvInferClass;

/* Standard GStreamer boilerplate */
#define GST_TYPE_NVINFER (gst_nvinfer_get_type())
#define GST_NVINFER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NVINFER,GstNvInfer))
#define GST_NVINFER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NVINFER,GstNvInferClass))
#define GST_NVINFER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_NVINFER, GstNvInferClass))
#define GST_IS_NVINFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NVINFER))
#define GST_IS_NVINFER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NVINFER))
#define GST_NVINFER_CAST(obj)  ((GstNvInfer *)(obj))

/**
 * Enum for all GObject properties for the element.
 */
enum
{
  PROP_0,
  PROP_UNIQUE_ID,
  PROP_PROCESS_MODE,
  PROP_CONFIG_FILE_PATH,
  PROP_OPERATE_ON_GIE_ID,
  PROP_OPERATE_ON_CLASS_IDS,
  PROP_MODEL_ENGINEFILE,
  PROP_BATCH_SIZE,
  PROP_INTERVAL,
  PROP_GPU_DEVICE_ID,
  PROP_OUTPUT_WRITE_TO_FILE,
  PROP_OUTPUT_CALLBACK,
  PROP_OUTPUT_CALLBACK_USERDATA,
  PROP_OUTPUT_TENSOR_META,
  PROP_LAST
};

/**
 * Holds the bounding box/object detection filtering parameters per class.
 */
typedef struct
{
  guint roiTopOffset;
  guint roiBottomOffset;
  guint detectionMinWidth;
  guint detectionMinHeight;
  guint detectionMaxWidth;
  guint detectionMaxHeight;
} GstNvInferDetectionFilterParams;

/**
 * Holds the bounding box coloring information for one class;
 */
typedef struct
{
  gboolean have_border_color;
  NvOSD_ColorParams border_color;

  gboolean have_bg_color;
  NvOSD_ColorParams bg_color;
} GstNvInferColorParams;

/** Holds the cached information of an object. */
typedef struct {
  /** Vector of cached classification attributes. */
  std::vector<NvDsInferAttribute> attributes;
  /** Cached string label. */
  std::string label;
} GstNvInferObjectInfo;

/**
 * Holds the inference information/history for one object based on it's
 * tracking id.
 */
typedef struct
{
  /** Boolean indicating if the object is already being inferred on. */
  gboolean under_inference;
  /** Bounding box co-ordinates of the object when it was last inferred on. */
  NvOSD_RectParams last_inferred_coords;
  /** Number of the frame in the stream when the object was last inferred on. */
  gulong last_inferred_frame_num;
  /** Number of the frame in the stream when the object was last accessed. This
   * is useful for clearing stale enteries in map of the object histories and
   * keeping the size of the map in check. */
  gulong last_accessed_frame_num;
  /** Cached object information. */
  GstNvInferObjectInfo cached_info;
} GstNvInferObjectHistory;

/**
 * Holds info about one frame in a batch for inferencing.
 */
typedef struct {
  /** Ratio by which the frame / object crop was scaled in the horizontal
   * direction. Required when scaling the detector boxes from the network
   * resolution to input resolution. Not required for classifiers. */
  gdouble scale_ratio_x;
  /** Ratio by which the frame / object crop was scaled in the vertical
   * direction. Required when scaling the detector boxes from the network
   * resolution to input resolution. Not required for classifiers. */
  gdouble scale_ratio_y;
  /** NvDsObjectParams belonging to the object to be classified. */
  NvDsObjectMeta *obj_meta;
  NvDsFrameMeta *frame_meta;
  /** Index of the frame in the batched input GstBuffer. Not required for
   * classifiers. */
  guint batch_index;
  /** Frame number of the frame from the source. */
  gulong frame_num;
  /** The buffer structure the object / frame was converted from. */
  NvBufSurfaceParams *input_surf_params;
  /** Pointer to the converted frame memory. This memory contains the frame
   * converted to RGB/RGBA and scaled to network resolution. This memory is
   * given to NvDsInferContext as input for pre-processing and inferencing. */
  gpointer converted_frame_ptr;
  /** Pointer to the structure holding inference history for the object. Should
   * be NULL when inferencing on frames. */
  GstNvInferObjectHistory *history;
} GstNvInferFrame;

/**
 * Holds information about the batch of frames to be inferred.
 */
typedef struct {
  /** Vector of frames in the batch. */
  std::vector<GstNvInferFrame> frames;
  /** Pointer to the input GstBuffer. */
  GstBuffer *inbuf = nullptr;
  /** Batch number of the input batch. */
  gulong inbuf_batch_num;
  /** Boolean indicating that the output thread should only push the buffer to
   * downstream element. If set to true, a corresponding batch has not been
   * queued at the input of NvDsInferContext and hence dequeuing of output is
   * not required. */
  gboolean push_buffer = FALSE;
  /** Boolean marking this batch as an event marker. This is only used for
   * synchronization. The output loop does not process on the batch.
   */
  gboolean event_marker = FALSE;
  /** Buffer containing the intermediate conversion output for the batch. */
  GstBuffer *conv_buf = nullptr;
  nvtxRangeId_t nvtx_complete_buf_range = 0;
} GstNvInferBatch;

/** Map type for maintaing inference history for objects based on their tracking ids.*/
typedef std::unordered_map<guint64, GstNvInferObjectHistory> GstNvInferObjectHistoryMap;

/**
 * Holds source-specific information.
 */
typedef struct
{
  /** Map of object tracking ID and the object infer history. */
  GstNvInferObjectHistoryMap object_history_map;
  /** Frame number of the buffer when the history map was last cleaned up. */
  gulong last_cleanup_frame_num;
  /** Frame number of the frame which . */
  gulong last_seen_frame_num;
} GstNvInferSourceInfo;

/**
 * Data type used for the refcounting and managing the usage of NvDsInferContext's
 * batch output and the output buffers contained in it. This is especially required
 * when the tensor output is flowed along with buffers as metadata or when the
 * segmentation output containing pointer to the NvDsInferContext allocated
 * memory is attached to buffers as metadata. Whenever the last ref on the buffer
 * is dropped, the callback to free the GstMiniObject-inherited GstNvInferTensorOutputObject
 * is called and the batch_output can be released back to the NvDsInferContext.
 */
typedef struct
{
  /** Parent type. Allows easy refcounting and destruction. Refcount will be
   * increased by 1 for each frame/object for which NvDsInferTensorMeta will be
   * generated. */
  GstMiniObject mini_object;
  /** Pointer to the nvinfer instance which generated the meta. */
  GstNvInfer *nvinfer;
  /** NvDsInferContextBatchOutput instance whose output tensor buffers are being
   * sent as meta data. This batch output will be released back to the
   * NvDsInferContext when the last ref on the mini_object is removed. */
  NvDsInferContextBatchOutput batch_output;
} GstNvInferTensorOutputObject;


/**
 * GstNvInfer element structure.
 */
struct _GstNvInfer {
  /** Should be the first member when extending from GstBaseTransform. */
  GstBaseTransform base_trans;

  /** NvDsInferContext to be used for inferencing. */
  NvDsInferContextHandle nvdsinfer_ctx;

  /** NvDsInferContext initialization params. */
  NvDsInferContextInitParams *init_params;

  /** Boolean indicating if the config parsing was successful. */
  gboolean config_file_parse_successful;

  /** Maximum batch size. */
  guint max_batch_size;

  /**
   * Unique ID of the element. The labels generated by the element will be
   * updated at index `unique_id` of attr_info array in NvDsObjectParams. For
   * detectors, this value will be assigned to gie_unique_id of NvDsFrameMeta
   * meta structure attached by the element to identify the meta attached by
   * this element.
   */
  guint unique_id;

  /**
   * Internal buffer pool for memory required for scaling input frames and
   * cropping object. */
  GstBufferPool *pool;

  /** Processing Queue and related synchronization structures. */
  GQueue *process_queue;
  GMutex process_lock;
  GCond process_cond;
  GQueue *input_queue;

  /** Output thread. */
  GThread *output_thread;
  GThread *input_queue_thread;

  /** Boolean to signal output thread to stop. */
  gboolean stop;

  /** Network input resolution. */
  gint network_width;
  gint network_height;

  /** Boolean indicating if entire frame should be inferred or crop objects
   * based on metadata recieved from the primary detector. */
  gboolean process_full_frame;

  /** Path to the configuration file for this instance of gst-nvinfer. */
  gchar *config_file_path;

  /** GstFlowReturn returned by the latest buffer pad push. */
  GstFlowReturn last_flow_ret;

  /** ID of the GPU this element uses for conversions / inference. */
  guint gpu_id;

  /** Cuda Stream to launch npp operations on. */
  cudaStream_t convertStream;

  /** Boolean indicating if aspect ratio should be maintained when scaling to
   * network resolution. Right/bottom areas will be filled with black areas. */
  gboolean maintain_aspect_ratio;

  /** Vector for per-class detection filtering parameters. */
  std::vector<GstNvInferDetectionFilterParams> *perClassDetectionFilterParams;

  /** Vector for per-class color parameters. */
  std::vector<GstNvInferColorParams> *perClassColorParams;

  /** Batch interval for full-frame processing. */
  guint interval;
  guint interval_counter;

  /** Frame interval after which objects should be reinferred on. */
  guint secondary_reinfer_interval;

  /** Input object size-based filtering parameters for object processing mode. */
  guint min_input_object_width;
  guint min_input_object_height;
  guint max_input_object_width;
  guint max_input_object_height;

  /** Source GIE ID and class-id based filtering parameters for object processing mode. */
  gint operate_on_gie_id;
  std::vector<gboolean> *operate_on_class_ids;

  /** Per source information. */
  std::unordered_map<gint, GstNvInferSourceInfo> *source_info;
  gulong last_map_cleanup_frame_num;

  /** Current batch number of the input batch. */
  gulong current_batch_num;

  /** Boolean indicating if the secondary classifier should run in asynchronous mode. */
  gboolean classifier_async_mode;

  /** Network input information. */
  NvDsInferNetworkInfo network_info;

  /** Vector of bound layers information. */
  std::vector<NvDsInferLayerInfo> *layers_info;

  /** Vector of bound output layers information. */
  std::vector<NvDsInferLayerInfo> *output_layers_info;

  /** Boolean indicating if the bound buffer contents should be written to file. */
  gboolean write_raw_buffers_to_file;

  /** Batch counter for writing buffer contents to file. */
  guint64 file_write_batch_num;

  /** Pointer to the callback function and userdata for application access to
   * the bound buffer contents. */
  gst_nvinfer_raw_output_generated_callback output_generated_callback;
  gpointer output_generated_userdata;

  /** Vector of booleans indicating if properties have been set through
   * GObject set method. */
  std::vector<gboolean> *is_prop_set;

  /** Config params required by NvBufSurfTransform API. */
  NvBufSurfTransformConfigParams transform_config_params;

  /** Parameters to use for transforming buffers. */
  NvBufSurfTransformParams transform_params;

  /** Temporary NvBufSurface for batched transformations. */
  NvBufSurface tmp_surf;

  /** Boolean indicating if tensor outputs should be attached as meta on
   * GstBuffers. */
  gboolean output_tensor_meta;

  /** PTS of input buffer when nvinfer last posted the warning about untracked
   * object. */
  GstClockTime untracked_object_warn_pts;

  /** NVTX Domain. */
  nvtxDomainHandle_t nvtx_domain;
};

/* GStreamer boilerplate. */
struct _GstNvInferClass {
  GstBaseTransformClass parent_class;
};

GType gst_nvinfer_get_type (void);

G_END_DECLS

#endif /* __GST_INFER_H__ */
