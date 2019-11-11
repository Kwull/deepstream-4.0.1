/*
 * Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 */

#ifndef _GST_NVMSGBROKER_H_
#define _GST_NVMSGBROKER_H_

#include <gst/base/gstbasesink.h>
#include "nvds_msgapi.h"

G_BEGIN_DECLS

#define GST_TYPE_NVMSGBROKER   (gst_nvmsgbroker_get_type())
#define GST_NVMSGBROKER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NVMSGBROKER,GstNvMsgBroker))
#define GST_NVMSGBROKER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NVMSGBROKER,GstNvMsgBrokerClass))
#define GST_IS_NVMSGBROKER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NVMSGBROKER))
#define GST_IS_NVMSGBROKER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NVMSGBROKER))

typedef struct _GstNvMsgBroker GstNvMsgBroker;
typedef struct _GstNvMsgBrokerClass GstNvMsgBrokerClass;

typedef NvDsMsgApiHandle (*nvds_msgapi_connect_ptr)(const char *connection_str,
    nvds_msgapi_connect_cb_t connect_cb, const char *config_path);

typedef NvDsMsgApiErrorType (*nvds_msgapi_send_ptr)(NvDsMsgApiHandle conn, const char *topic,
    const uint8_t *payload, size_t nbuf);

typedef NvDsMsgApiErrorType (*nvds_msgapi_send_async_ptr)(NvDsMsgApiHandle h_ptr,
    char *topic, const uint8_t *payload, size_t nbuf,
    nvds_msgapi_send_cb_t send_callback, void *user_ptr);

typedef void (*nvds_msgapi_do_work_ptr) (NvDsMsgApiHandle h_ptr);

typedef NvDsMsgApiErrorType (*nvds_msgapi_disconnect_ptr)(NvDsMsgApiHandle conn);

struct _GstNvMsgBroker
{
  GstBaseSink parent;

  GQuark dsMetaQuark;
  gpointer libHandle;
  gchar *configFile;
  gchar *protoLib;
  gchar *connStr;
  gchar *topic;
  guint compId;
  GMutex flowLock;
  GCond flowCond;
  GThread *doWorkThread;
  gboolean isRunning;
  gboolean asyncSend;
  gint pendingCbCount;
  NvDsMsgApiHandle connHandle;
  NvDsMsgApiErrorType lastError;
  nvds_msgapi_connect_ptr nvds_msgapi_connect;
  nvds_msgapi_send_ptr nvds_msgapi_send;
  nvds_msgapi_send_async_ptr nvds_msgapi_send_async;
  nvds_msgapi_do_work_ptr nvds_msgapi_do_work;
  nvds_msgapi_disconnect_ptr nvds_msgapi_disconnect;
};

struct _GstNvMsgBrokerClass
{
  GstBaseSinkClass parent_class;
};

GType gst_nvmsgbroker_get_type (void);

G_END_DECLS

#endif
