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

#ifndef _GST_NVMSGCONV_H_
#define _GST_NVMSGCONV_H_

#include <gst/base/gstbasetransform.h>
#include "nvmsgconv.h"

G_BEGIN_DECLS

#define GST_TYPE_NVMSGCONV   (gst_nvmsgconv_get_type())
#define GST_NVMSGCONV(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NVMSGCONV,GstNvMsgConv))
#define GST_NVMSGCONV_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NVMSGCONV,GstNvMsgConvClass))
#define GST_IS_NVMSGCONV(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NVMSGCONV))
#define GST_IS_NVMSGCONV_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NVMSGCONV))

typedef struct _GstNvMsgConv GstNvMsgConv;
typedef struct _GstNvMsgConvClass GstNvMsgConvClass;

typedef NvDsMsg2pCtx* (*nvds_msg2p_ctx_create_ptr) (const gchar *file, NvDsPayloadType type);

typedef void (*nvds_msg2p_ctx_destroy_ptr) (NvDsMsg2pCtx *ctx);

typedef NvDsPayload* (*nvds_msg2p_generate_ptr) (NvDsMsg2pCtx *ctx, NvDsEvent *events, guint size);

typedef void (*nvds_msg2p_release_ptr) (NvDsMsg2pCtx *ctx, NvDsPayload *payload);

struct _GstNvMsgConv
{
  GstBaseTransform parent;

  GQuark dsMetaQuark;
  gchar *configFile;
  gchar *msg2pLib;
  gpointer libHandle;
  gint compId;
  NvDsPayloadType paylodType;
  NvDsMsg2pCtx *pCtx;

  nvds_msg2p_ctx_create_ptr ctx_create;
  nvds_msg2p_ctx_destroy_ptr ctx_destroy;
  nvds_msg2p_generate_ptr msg2p_generate;
  nvds_msg2p_release_ptr msg2p_release;
};

struct _GstNvMsgConvClass
{
  GstBaseTransformClass parent_class;
};

GType gst_nvmsgconv_get_type (void);

G_END_DECLS

#endif
