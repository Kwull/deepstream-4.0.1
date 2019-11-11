/*
 * Copyright (c) 2019 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 */

#ifndef __DEEPSTREAM_TEST5_APP_H__
#define __DEEPSTREAM_TEST5_APP_H__

#include <gst/gst.h>
#include "deepstream_config.h"

typedef struct
{
  gint anomaly_count;
  gint meta_number;
  struct timespec timespec_first_frame;
  GstClockTime gst_ts_first_frame;
  GMutex lock_stream_rtcp_sr;
  GstClockTime rtcp_ntp_time_epoch_ns;
  GstClockTime rtcp_buffer_timestamp;
  guint32 id;
  gint frameCount;
  GstClockTime last_ntp_time;
} StreamSourceInfo;

typedef struct
{
  StreamSourceInfo streams[MAX_SOURCE_BINS];
} TestAppCtx;

struct timespec extract_utc_from_uri (gchar * uri);

#endif /**< __DEEPSTREAM_TEST5_APP_H__ */
