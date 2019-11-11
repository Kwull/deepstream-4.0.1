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
#include <gst/base/gstbasesink.h>
#include <dlfcn.h>
#include "gstnvmsgbroker.h"
#include "gstnvdsmeta.h"
#include "nvdsmeta_schema.h"
#include "nvdsmeta.h"


GST_DEBUG_CATEGORY_STATIC (gst_nvmsgbroker_debug_category);
#define GST_CAT_DEFAULT gst_nvmsgbroker_debug_category

static void gst_nvmsgbroker_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_nvmsgbroker_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_nvmsgbroker_finalize (GObject * object);

static gboolean gst_nvmsgbroker_set_caps (GstBaseSink * sink, GstCaps * caps);
static gboolean gst_nvmsgbroker_start (GstBaseSink * sink);
static gboolean gst_nvmsgbroker_stop (GstBaseSink * sink);
static GstFlowReturn gst_nvmsgbroker_render (GstBaseSink * sink,
    GstBuffer * buffer);


static void
nvds_msgapi_connect_callback (NvDsMsgApiHandle h_ptr, NvDsMsgApiEventType ds_evt)
{

}

static void
nvds_msgapi_send_callback (void *data, NvDsMsgApiErrorType status)
{
  GstNvMsgBroker *self = (GstNvMsgBroker *) data;

  g_mutex_lock (&self->flowLock);
  self->pendingCbCount--;
  self->lastError = status;

  if (status != NVDS_MSGAPI_OK) {
    GST_ERROR_OBJECT (self, "error(%d) in sending data", status);
  }
  g_mutex_unlock (&self->flowLock);
}

enum
{
  PROP_0,
  PROP_CONNECTION_STRING,
  PROP_CONFIG_FILE,
  PROP_PROTOCOL_LIBRARY,
  PROP_TOPIC,
  PROP_COMPONENT_ID
};

static GstStaticPadTemplate gst_nvmsgbroker_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE_WITH_CODE (GstNvMsgBroker, gst_nvmsgbroker, GST_TYPE_BASE_SINK,
    GST_DEBUG_CATEGORY_INIT (gst_nvmsgbroker_debug_category, "nvmsgbroker", 0,
        "debug category for nvmsgbroker element"));

static gpointer
gst_nvmsgbroker_do_work (gpointer data)
{
  GstNvMsgBroker *self = (GstNvMsgBroker *) data;

  while (self->isRunning) {
    g_mutex_lock (&self->flowLock);
    while (self->isRunning && self->pendingCbCount <= 0) {
      g_cond_wait (&self->flowCond, &self->flowLock);
    }
    g_mutex_unlock (&self->flowLock);

    if (!self->isRunning) {
      return NULL;
    }

    self->nvds_msgapi_do_work (self->connHandle);
    // wait 10ms.
    g_usleep (10 * 1000);
  }
  return self;
}

static void
gst_nvmsgbroker_class_init (GstNvMsgBrokerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_nvmsgbroker_sink_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Message Broker", "Sink/Metadata",
      "Sends payload metadata to remote server",
      "NVIDIA Corporation. Post on Deepstream for Tesla forum for any queries "
      "@ https://devtalk.nvidia.com/default/board/209/");

  gobject_class->set_property = gst_nvmsgbroker_set_property;
  gobject_class->get_property = gst_nvmsgbroker_get_property;
  gobject_class->finalize = gst_nvmsgbroker_finalize;
  base_sink_class->set_caps = GST_DEBUG_FUNCPTR (gst_nvmsgbroker_set_caps);
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_nvmsgbroker_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_nvmsgbroker_stop);
  base_sink_class->render = GST_DEBUG_FUNCPTR (gst_nvmsgbroker_render);

  g_object_class_install_property (gobject_class, PROP_PROTOCOL_LIBRARY,
      g_param_spec_string ("proto-lib", "Protocol library name",
      "Name of protocol adaptor library with absolute path.",
      NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CONNECTION_STRING,
      g_param_spec_string ("conn-str", "connection string",
      "connection string of backend server (e.g. foo.bar.com;80;dsapp1)",
      NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CONFIG_FILE,
      g_param_spec_string ("config", "configuration file name",
      "Name of configuration file with absolute path.",
      NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TOPIC,
     g_param_spec_string ("topic", "topic name", "Name of the message topic",
     NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_COMPONENT_ID,
      g_param_spec_uint ("comp-id", "Component Id ",
      "By default this element operates on all NvDsPayload type meta\n"
      "\t\t\tBut it can be restricted to a specific NvDsPayload meta\n"
      "\t\t\thaving this component id",
      0, G_MAXUINT, 0,
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void
gst_nvmsgbroker_init (GstNvMsgBroker * self)
{
  self->dsMetaQuark = g_quark_from_static_string (NVDS_META_STRING);
  self->libHandle = NULL;
  self->connHandle = NULL;
  self->connStr = NULL;
  self->topic = NULL;
  self->protoLib = NULL;
  self->configFile = NULL;
  self->isRunning = FALSE;
  self->pendingCbCount = 0;
  self->asyncSend = TRUE;
  self->lastError = NVDS_MSGAPI_OK;
  self->compId = 0;

  g_mutex_init (&self->flowLock);
  g_cond_init (&self->flowCond);
}

void
gst_nvmsgbroker_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNvMsgBroker *self = GST_NVMSGBROKER (object);

  GST_DEBUG_OBJECT (self, "set_property");

  switch (property_id) {
    case PROP_CONFIG_FILE:
      if (self->configFile)
        g_free (self->configFile);
      self->configFile = (gchar *) g_value_dup_string (value);
      break;
    case PROP_CONNECTION_STRING:
      if (self->connStr)
        g_free (self->connStr);
      self->connStr = (gchar *) g_value_dup_string (value);
      break;
    case PROP_PROTOCOL_LIBRARY:
      if (self->protoLib)
        g_free (self->protoLib);
      self->protoLib = (gchar *) g_value_dup_string (value);
      break;
    case PROP_TOPIC:
      if (self->topic)
        g_free (self->topic);
      self->topic = (gchar *) g_value_dup_string (value);
      g_strstrip (self->topic);
      if (!g_strcmp0 (self->topic, "")) {
        g_free (self->topic);
        self->topic = NULL;
      }
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
gst_nvmsgbroker_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstNvMsgBroker *self = GST_NVMSGBROKER (object);

  GST_DEBUG_OBJECT (self, "get_property");

  switch (property_id) {
    case PROP_CONFIG_FILE:
      g_value_set_string (value, self->configFile);
      break;
    case PROP_CONNECTION_STRING:
      g_value_set_string (value, self->connStr);
      break;
    case PROP_PROTOCOL_LIBRARY:
      g_value_set_string (value, self->protoLib);
      break;
    case PROP_TOPIC:
      g_value_set_string (value, self->topic);
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
gst_nvmsgbroker_finalize (GObject * object)
{
  GstNvMsgBroker *self = GST_NVMSGBROKER (object);

  GST_DEBUG_OBJECT (self, "finalize");

  if (self->configFile)
    g_free (self->configFile);

  if (self->connStr)
    g_free (self->connStr);

  if (self->topic)
    g_free (self->topic);

  if (self->protoLib)
    g_free (self->protoLib);

  g_mutex_clear(&self->flowLock);
  g_cond_clear(&self->flowCond);

  G_OBJECT_CLASS (gst_nvmsgbroker_parent_class)->finalize (object);
}

static gboolean
gst_nvmsgbroker_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  GstNvMsgBroker *nvmsgbroker = GST_NVMSGBROKER (sink);

  GST_DEBUG_OBJECT (nvmsgbroker, "set_caps");

  return TRUE;
}

static gboolean
gst_nvmsgbroker_start (GstBaseSink * sink)
{
  GstNvMsgBroker *self = GST_NVMSGBROKER (sink);
  gchar *error;
  gchar *temp = NULL;

  GST_DEBUG_OBJECT (self, "start");

  if (!self->protoLib) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, (NULL),
                       ("No protocol adaptor library provided"));
    return FALSE;
  }

  temp = g_strrstr (self->connStr, ";");
  if (temp)
    if(!self->topic)
      self->topic = g_strdup (temp+1);

  self->libHandle = dlopen(self->protoLib, RTLD_LAZY);
  if (!self->libHandle) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT, (NULL),
                       ("unable to open shared library"));
    return FALSE;
  }

  dlerror();    /* Clear any existing error */

  self->nvds_msgapi_connect = (nvds_msgapi_connect_ptr) dlsym (self->libHandle, "nvds_msgapi_connect");
  self->nvds_msgapi_send = (nvds_msgapi_send_ptr) dlsym (self->libHandle, "nvds_msgapi_send");
  self->nvds_msgapi_disconnect = (nvds_msgapi_disconnect_ptr) dlsym (self->libHandle, "nvds_msgapi_disconnect");
  if (self->asyncSend) {
    self->nvds_msgapi_send_async = (nvds_msgapi_send_async_ptr) dlsym (self->libHandle, "nvds_msgapi_send_async");
    self->nvds_msgapi_do_work = (nvds_msgapi_do_work_ptr) dlsym (self->libHandle, "nvds_msgapi_do_work");
  }

  if ((error = dlerror()) != NULL) {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
                       ("%s", error));
    return FALSE;
  }

  self->connHandle = self->nvds_msgapi_connect (self->connStr,
                               (nvds_msgapi_connect_cb_t) nvds_msgapi_connect_callback,
                               self->configFile);
  if (!self->connHandle) {
    if (self->libHandle) {
      dlclose (self->libHandle);
      self->libHandle = NULL;
    }
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
                       ("unable to connect to broker library"));
    return FALSE;
  }

  self->isRunning = TRUE;
  if (self->asyncSend) {
    self->doWorkThread = g_thread_new ("doWork_thread",
                                       gst_nvmsgbroker_do_work, (gpointer) self);
  }

  return TRUE;
}

static gboolean
gst_nvmsgbroker_stop (GstBaseSink * sink)
{
  GstNvMsgBroker *self = GST_NVMSGBROKER (sink);
  NvDsMsgApiErrorType err;

  GST_DEBUG_OBJECT (self, "stop");

  self->isRunning = FALSE;

  if (self->asyncSend) {
    g_cond_signal (&self->flowCond);
    g_thread_join (self->doWorkThread);
  }

  if (self->nvds_msgapi_disconnect) {
    err = self->nvds_msgapi_disconnect (self->connHandle);
    if (err != NVDS_MSGAPI_OK)
      GST_ERROR_OBJECT (self, "error(%d) in disconnect", err);
    self->connHandle = NULL;
  }

  if (self->libHandle) {
    dlclose (self->libHandle);
    self->libHandle = NULL;
  }
  return TRUE;
}

static GstFlowReturn
gst_nvmsgbroker_render (GstBaseSink * sink, GstBuffer * buf)
{
  GstNvMsgBroker *self = GST_NVMSGBROKER (sink);
  NvDsMeta *meta = NULL;
  NvDsBatchMeta *batch_meta = NULL;
  GstMeta *gstMeta = NULL;
  gpointer state = NULL;
  NvDsMsgApiErrorType err;
  NvDsPayload *payload;

  GST_DEBUG_OBJECT (self, "render");

#if 0
  g_mutex_lock (&self->flowLock);
  if (self->lastError != NVDS_MSGAPI_OK) {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
                       ("error(%d) in sending previous message.", self->lastError));

    g_mutex_unlock (&self->flowLock);
    return GST_FLOW_ERROR;
  }
  g_mutex_unlock (&self->flowLock);
#endif

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
    NvDsUserMeta *user_meta = NULL;

    for (l_frame = batch_meta->frame_meta_list; l_frame; l_frame = l_frame->next) {
      frame_meta = (NvDsFrameMeta *) l_frame->data;
      user_meta_list = frame_meta->frame_user_meta_list;

      for (l = user_meta_list; l; l = l->next) {
        user_meta = (NvDsUserMeta *)(l->data);

        if (user_meta && user_meta->base_meta.meta_type == NVDS_PAYLOAD_META) {
          payload = (NvDsPayload *) user_meta->user_meta_data;

          if (self->compId && payload->componentId != self->compId)
            continue;

          if (self->asyncSend) {
            g_mutex_lock (&self->flowLock);
            err = self->nvds_msgapi_send_async (self->connHandle, self->topic,
                                                (uint8_t *) payload->payload,
                                                payload->payloadSize,
                                                nvds_msgapi_send_callback, self);

            if (err != NVDS_MSGAPI_OK) {
              GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
                                 ("failed to send the message. err(%d)", err));

              g_mutex_unlock (&self->flowLock);
              return GST_FLOW_ERROR;
            }
            self->pendingCbCount++;
            g_cond_signal (&self->flowCond);
            g_mutex_unlock (&self->flowLock);
          } else {
            err = self->nvds_msgapi_send (self->connHandle, self->topic,
                                          (uint8_t *) payload->payload,
                                          payload->payloadSize);

            if (err != NVDS_MSGAPI_OK) {
              GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
                                 ("failed to send the message. err(%d)", err));

              return GST_FLOW_ERROR;
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
  return gst_element_register (plugin, "nvmsgbroker", GST_RANK_PRIMARY,
      GST_TYPE_NVMSGBROKER);
}

#define PACKAGE "nvmsgbroker"

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nvdsgst_msgbroker,
    "Message broker",
    plugin_init, DS_VERSION, "Proprietary", "NvMsgBroker", "http://nvidia.com")
