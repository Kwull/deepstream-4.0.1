/*
 * Copyright (c) 2018-2019 NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 */


#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <dlfcn.h>
#include "gstnvmsgconv.h"
#include "nvdsmeta_schema.h"
#include "nvdsmeta.h"
#include "gstnvdsmeta.h"

GST_DEBUG_CATEGORY_STATIC (gst_nvmsgconv_debug_category);
#define GST_CAT_DEFAULT gst_nvmsgconv_debug_category

#define DEFAULT_PAYLOAD_TYPE NVDS_PAYLOAD_DEEPSTREAM

#define GST_TYPE_NVMSGCONV_PAYLOAD_TYPE (gst_nvmsgconv_payload_get_type ())

static GType
gst_nvmsgconv_payload_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {NVDS_PAYLOAD_DEEPSTREAM, "Deepstream schema payload", "PAYLOAD_DEEPSTREAM"},
      {NVDS_PAYLOAD_DEEPSTREAM_MINIMAL, "Deepstream schema payload minimal", "PAYLOAD_DEEPSTREAM_MINIMAL"},
      {NVDS_PAYLOAD_RESERVED, "Reserved type", "PAYLOAD_RESERVED"},
      {NVDS_PAYLOAD_CUSTOM, "Custom schema payload", "PAYLOAD_CUSTOM"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstNvMsgConvPayloadType", values);
  }
  return qtype;
}

static void gst_nvmsgconv_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_nvmsgconv_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_nvmsgconv_finalize (GObject * object);
static gboolean gst_nvmsgconv_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_nvmsgconv_start (GstBaseTransform * trans);
static gboolean gst_nvmsgconv_stop (GstBaseTransform * trans);
static GstFlowReturn gst_nvmsgconv_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);

enum
{
  PROP_0,
  PROP_CONFIG_FILE,
  PROP_MSG2P_LIB_NAME,
  PROP_PAYLOAD_TYPE,
  PROP_COMPONENT_ID
};

static GstStaticPadTemplate gst_nvmsgconv_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_nvmsgconv_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE_WITH_CODE (GstNvMsgConv, gst_nvmsgconv, GST_TYPE_BASE_TRANSFORM,
    GST_DEBUG_CATEGORY_INIT (gst_nvmsgconv_debug_category, "nvmsgconv", 0,
        "debug category for nvmsgconv element"));



static void gst_nvmsgconv_free_meta (gpointer data, gpointer uData)
{
  g_return_if_fail (data);

  NvDsUserMeta *user_meta = (NvDsUserMeta *) data;
  NvDsPayload *srcPayload = (NvDsPayload *) user_meta->user_meta_data;
  GstNvMsgConv *self = (GstNvMsgConv *) user_meta->base_meta.uContext;

  if (self && srcPayload) {
    self->msg2p_release (self->pCtx, srcPayload);
  }
}

static gpointer gst_nvmsgconv_copy_meta (gpointer data, gpointer uData)
{
  GstNvMsgConv *self = (GstNvMsgConv *) uData;
  NvDsUserMeta *user_meta = (NvDsUserMeta *) data;
  NvDsPayload *srcPayload = (NvDsPayload *) user_meta->user_meta_data;
  NvDsPayload *outPayload = NULL;

  GST_DEBUG_OBJECT (self, "copying meta data");

  if (srcPayload) {
    outPayload = (NvDsPayload *) g_memdup (srcPayload, sizeof(NvDsPayload));
    outPayload->payload = g_memdup (srcPayload->payload, srcPayload->payloadSize);
    outPayload->payloadSize = srcPayload->payloadSize;
  }
  return outPayload;
}

static void
gst_nvmsgconv_class_init (GstNvMsgConvClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_nvmsgconv_src_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_nvmsgconv_sink_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Message Converter", "Filter/Metadata",
      "Transforms buffer meta to schema / payload meta",
      "NVIDIA Corporation. Post on Deepstream for Tesla forum for any queries "
      "@ https://devtalk.nvidia.com/default/board/209/");

  gobject_class->set_property = gst_nvmsgconv_set_property;
  gobject_class->get_property = gst_nvmsgconv_get_property;
  gobject_class->finalize = gst_nvmsgconv_finalize;
  base_transform_class->set_caps = GST_DEBUG_FUNCPTR (gst_nvmsgconv_set_caps);
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_nvmsgconv_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_nvmsgconv_stop);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_nvmsgconv_transform_ip);

  g_object_class_install_property (gobject_class, PROP_CONFIG_FILE,
      g_param_spec_string ("config", "configuration file name",
      "Name of configuration file with absolute path.",
      NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_MSG2P_LIB_NAME,
      g_param_spec_string ("msg2p-lib", "configuration file name",
      "Name of payload generation library with absolute path.",
      NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PAYLOAD_TYPE,
      g_param_spec_enum ("payload-type", "Payload type",
      "Type of payload to be generated", GST_TYPE_NVMSGCONV_PAYLOAD_TYPE,
      DEFAULT_PAYLOAD_TYPE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_COMPONENT_ID,
      g_param_spec_uint ("comp-id", "Component Id ",
      "By default this element operates on all NvDsEventMsgMeta\n"
      "\t\t\tBut it can be restricted to a specific NvDsEventMsgMeta meta\n"
      "\t\t\thaving this component id\n",
      0, G_MAXUINT, 0,
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void
gst_nvmsgconv_init (GstNvMsgConv * self)
{
  self->pCtx = NULL;
  self->msg2pLib = NULL;
  self->configFile = NULL;
  self->paylodType = DEFAULT_PAYLOAD_TYPE;
  self->libHandle = NULL;
  self->compId = 0;
  self->dsMetaQuark = g_quark_from_static_string (NVDS_META_STRING);

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), TRUE);
}

void
gst_nvmsgconv_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNvMsgConv *self = GST_NVMSGCONV (object);

  GST_DEBUG_OBJECT (self, "set_property");

  switch (property_id) {
    case PROP_CONFIG_FILE:
      if (self->configFile)
        g_free (self->configFile);
      self->configFile = (gchar *) g_value_dup_string (value);
      break;
    case PROP_MSG2P_LIB_NAME:
      if (self->msg2pLib)
        g_free (self->msg2pLib);
      self->msg2pLib = (gchar *) g_value_dup_string (value);
      break;
    case PROP_PAYLOAD_TYPE:
      self->paylodType = (NvDsPayloadType) g_value_get_enum (value);
      break;
    case PROP_COMPONENT_ID:
      self->compId = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_nvmsgconv_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstNvMsgConv *self = GST_NVMSGCONV (object);

  GST_DEBUG_OBJECT (self, "get_property");

  switch (property_id) {
    case PROP_CONFIG_FILE:
      g_value_set_string (value, self->configFile);
      break;
    case PROP_MSG2P_LIB_NAME:
      g_value_set_string (value, self->msg2pLib);
      break;
    case PROP_PAYLOAD_TYPE:
      g_value_set_enum (value, self->paylodType);
      break;
    case PROP_COMPONENT_ID:
      g_value_set_uint (value, self->compId);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_nvmsgconv_finalize (GObject * object)
{
  GstNvMsgConv *self = GST_NVMSGCONV (object);

  GST_DEBUG_OBJECT (self, "finalize");

  if (self->configFile)
    g_free (self->configFile);

  G_OBJECT_CLASS (gst_nvmsgconv_parent_class)->finalize (object);
}

static gboolean
gst_nvmsgconv_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstNvMsgConv *self = GST_NVMSGCONV (trans);

  GST_DEBUG_OBJECT (self, "set_caps");

  return TRUE;
}

static gboolean
gst_nvmsgconv_start (GstBaseTransform * trans)
{
  GstNvMsgConv *self = GST_NVMSGCONV (trans);
  gchar *error;

  GST_DEBUG_OBJECT (self, "start");

  if (self->paylodType == NVDS_PAYLOAD_CUSTOM) {
    if (self->msg2pLib == NULL) {
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, (NULL),
                         ("No converter library for custom payload type"));
      return FALSE;
    } else {
      self->libHandle = dlopen(self->msg2pLib, RTLD_LAZY);
      if (!self->libHandle) {
        GST_ELEMENT_ERROR (self, LIBRARY, INIT, (NULL),
                           ("unable to open converter library"));
        return FALSE;
      }

      dlerror();    /* Clear any existing error */
      self->ctx_create = (nvds_msg2p_ctx_create_ptr) dlsym (self->libHandle, "nvds_msg2p_ctx_create");
      self->ctx_destroy = (nvds_msg2p_ctx_destroy_ptr) dlsym (self->libHandle, "nvds_msg2p_ctx_destroy");
      self->msg2p_generate = (nvds_msg2p_generate_ptr) dlsym (self->libHandle, "nvds_msg2p_generate");
      self->msg2p_release = (nvds_msg2p_release_ptr) dlsym (self->libHandle, "nvds_msg2p_release");

      if ((error = dlerror()) != NULL) {
        GST_ERROR_OBJECT (self, "%s", error);
        return FALSE;
      }
    }
  } else {
    self->ctx_create = (nvds_msg2p_ctx_create_ptr) nvds_msg2p_ctx_create;
    self->ctx_destroy = (nvds_msg2p_ctx_destroy_ptr) nvds_msg2p_ctx_destroy;
    self->msg2p_generate = (nvds_msg2p_generate_ptr) nvds_msg2p_generate;
    self->msg2p_release = (nvds_msg2p_release_ptr) nvds_msg2p_release;
  }

  self->pCtx = self->ctx_create (self->configFile, self->paylodType);

  if (!self->pCtx) {
    if (self->libHandle) {
      dlclose (self->libHandle);
      self->libHandle = NULL;
    }
    GST_ERROR_OBJECT (self, "unable to create instance");
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_nvmsgconv_stop (GstBaseTransform * trans)
{
  GstNvMsgConv *self = GST_NVMSGCONV (trans);

  GST_DEBUG_OBJECT (self, "stop");

  if (self->pCtx) {
    self->ctx_destroy (self->pCtx);
    self->pCtx = NULL;
  }

  if (self->libHandle) {
    dlclose (self->libHandle);
    self->libHandle = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_nvmsgconv_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstNvMsgConv *self = GST_NVMSGCONV (trans);
  NvDsPayload *payload = NULL;
  NvDsEventMsgMeta *eventMsg = NULL;
  NvDsMeta *meta = NULL;
  NvDsBatchMeta *batch_meta = NULL;
  GstMeta *gstMeta = NULL;
  gpointer state = NULL;

  GST_DEBUG_OBJECT (self, "transform_ip");

  while ((gstMeta = gst_buffer_iterate_meta (buf, &state))) {
    if (gst_meta_api_type_has_tag (gstMeta->info->api, self->dsMetaQuark)) {
      meta = (NvDsMeta *) gstMeta;
      if (meta->meta_type == NVDS_BATCH_GST_META) {
        batch_meta = (NvDsBatchMeta *) meta->meta_data;
        break;
      }
    }
  }

  if (batch_meta) {
    NvDsMetaList *l = NULL;
    NvDsMetaList *l_frame = NULL;
    NvDsMetaList *user_meta_list = NULL;
    NvDsFrameMeta *frame_meta = NULL;
    NvDsUserMeta *user_event_meta = NULL;

    for (l_frame = batch_meta->frame_meta_list; l_frame; l_frame = l_frame->next) {
      frame_meta = (NvDsFrameMeta *) (l_frame->data);
      user_meta_list = frame_meta->frame_user_meta_list;

      if (self->paylodType == NVDS_PAYLOAD_DEEPSTREAM_MINIMAL) {
        NvDsEvent *eventList = g_new0 (NvDsEvent, g_list_length (user_meta_list));
        guint eventCount = 0;
        for (l = user_meta_list; l; l = l->next) {
          user_event_meta = (NvDsUserMeta *)(l->data);

          if (user_event_meta && user_event_meta->base_meta.meta_type == NVDS_EVENT_MSG_META) {
            eventMsg = (NvDsEventMsgMeta *) user_event_meta->user_meta_data;

            if (self->compId && eventMsg->componentId != self->compId)
              continue;

            eventList[eventCount].eventType = eventMsg->type;
            eventList[eventCount].metadata = eventMsg;
            eventCount++;
          }
        }

        if (eventCount) {
          payload = self->msg2p_generate (self->pCtx, eventList, eventCount);

          if (payload) {
            payload->componentId = self->compId;

            NvDsUserMeta *user_payload_meta = nvds_acquire_user_meta_from_pool (batch_meta);
            if (user_payload_meta) {
              user_payload_meta->user_meta_data = (void *) payload;
              user_payload_meta->base_meta.meta_type = NVDS_PAYLOAD_META;
              user_payload_meta->base_meta.copy_func = (NvDsMetaCopyFunc) gst_nvmsgconv_copy_meta;
              user_payload_meta->base_meta.release_func = (NvDsMetaReleaseFunc) gst_nvmsgconv_free_meta;
              user_payload_meta->base_meta.uContext = (void *) self;
              nvds_add_user_meta_to_frame (frame_meta, user_payload_meta);
            } else {
              GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
                                 ("Couldn't get user meta from pool"));
              return GST_FLOW_ERROR;
            }
          }
        }
        g_free (eventList);
      } else {
        for (l = user_meta_list; l; l = l->next) {
          user_event_meta = (NvDsUserMeta *)(l->data);

          if (user_event_meta && user_event_meta->base_meta.meta_type == NVDS_EVENT_MSG_META) {
            NvDsEvent event;

            eventMsg = (NvDsEventMsgMeta *) user_event_meta->user_meta_data;

            if (self->compId && eventMsg->componentId != self->compId)
              continue;

            event.eventType = eventMsg->type;
            event.metadata = eventMsg;

            payload = self->msg2p_generate (self->pCtx, &event, 1);

            if (payload) {
              payload->componentId = self->compId;

              NvDsUserMeta *user_payload_meta = nvds_acquire_user_meta_from_pool (batch_meta);
              if (user_payload_meta) {
                user_payload_meta->user_meta_data = (void *) payload;
                user_payload_meta->base_meta.meta_type = NVDS_PAYLOAD_META;
                user_payload_meta->base_meta.copy_func = (NvDsMetaCopyFunc) gst_nvmsgconv_copy_meta;
                user_payload_meta->base_meta.release_func = (NvDsMetaReleaseFunc) gst_nvmsgconv_free_meta;
                user_payload_meta->base_meta.uContext = (void *) self;
                nvds_add_user_meta_to_frame (frame_meta, user_payload_meta);
              } else {
                GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
                                   ("Couldn't get user meta from pool"));
                return GST_FLOW_ERROR;
              }
            }
          }
        }
      }
    }
  }
  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "nvmsgconv", GST_RANK_NONE,
      GST_TYPE_NVMSGCONV);
}

#define PACKAGE "nvmsgconv"

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nvdsgst_msgconv,
    "Metadata conversion",
    plugin_init, DS_VERSION, "Proprietary", "NvMsgConv", "http://nvidia.com")
