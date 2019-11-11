/*
 * Copyright (c) 2018-2019, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __NVGSTDS_SOURCES_H__
#define __NVGSTDS_SOURCES_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <gst/gst.h>
#include "deepstream_dewarper.h"

/** 
 * @brief  The callback from deepstream SDK
 *         with each RTCP Sender Report details as and when it is received for
 *         live multisrc RTSP streams
 *         NOTE: User shall register this callback with AppCtx->rtcp_sender_report_cb pointer
 *         (nullable)
 *         NOTE: User can find a GstBuffer's NTP Time by:
 *         buffer_ntp_ns = rtcp_ntp_time_epoch_ns + (GST_BUFFER_PTS(buffer) - rtcp_buffer_timestamp)
 * @param  multi_src_sub_bin_id [IN] The index, starting with zero - identifying the source ID
 *         according to its order of appearance in the DeepStream config file
 * @param  rtcp_ntp_time_epoch_ns [IN] The 64-bit RTCP NTP Timestamp (IETF RFC 3550; RTCP)
 *         converted to epoch time in nanoseconds - GstClockTime 
 * @param  rtcp_buffer_timestamp [IN] The Buffer PTS (as close as possible to the RTCP buffer
 *         timestamp which carried the Sender Report); This timestamp is 
 *         synchronized with the stream's RTP buffer timestamps on GStreamer clock
 */
typedef void (*rtcp_sender_report_callback) (guint32 multi_src_sub_bin_id,
    GstClockTime rtcp_ntp_time_epoch_ns, 
    GstClockTime rtcp_buffer_timestamp);

typedef enum
{
  NV_DS_SOURCE_CAMERA_V4L2 = 1,
  NV_DS_SOURCE_URI,
  NV_DS_SOURCE_URI_MULTIPLE,
  NV_DS_SOURCE_RTSP,
  NV_DS_SOURCE_CAMERA_CSI
} NvDsSourceType;

typedef struct
{
  NvDsSourceType type;
  gboolean enable;
  gboolean loop;
  gboolean live_source;
  gboolean Intra_decode;
  gint source_width;
  gint source_height;
  gint source_fps_n;
  gint source_fps_d;
  gint camera_csi_sensor_id;
  gint camera_v4l2_dev_node;
  gchar *uri;
  gint latency;
  guint num_sources;
  guint gpu_id;
  guint camera_id;
  guint select_rtp_protocol;
  guint num_decode_surfaces;
  guint num_extra_surfaces;
  guint nvbuf_memory_type;
  guint cuda_memory_type;
  NvDsDewarperConfig dewarper_config;
  guint drop_frame_interval;
} NvDsSourceConfig;

typedef struct
{
  GstElement *bin;
  GstElement *src_elem;
  GstElement *cap_filter;
  GstElement *depay;
  GstElement *enc_que;
  GstElement *dec_que;
  GstElement *decodebin;
  GstElement *enc_filter;
  GstElement *encbin_que;
  GstElement *tee;
  GstElement *fakesink_queue;
  GstElement *fakesink;
  gboolean do_record;
  guint64 pre_event_rec;
  GMutex bin_lock;
  guint bin_id;
  gulong src_buffer_probe;
  gpointer bbox_meta;
  GstBuffer *inbuf;
  gchar *location;
  gchar *file;
  gchar *direction;
  gint latency;
  gboolean got_key_frame;
  gboolean eos_done;
  gboolean reset_done;
  gboolean live_source;
  gboolean reconfiguring;
  NvDsDewarperBin dewarper_bin;
  gulong probe_id;
  guint64 accumulated_base;
  guint64 prev_accumulated_base;
  guint source_id;
  NvDsSourceConfig *config;
  rtcp_sender_report_callback registered_rtcp_sender_report_cb;
} NvDsSrcBin;

typedef struct
{
  GstElement *bin;
  GstElement *streammux;
  GThread *reset_thread;
  NvDsSrcBin sub_bins[MAX_SOURCE_BINS];
  guint num_bins;
  guint num_fr_on;
  gboolean live_source;
  rtcp_sender_report_callback rtcp_sender_report_cb;
} NvDsSrcParentBin;


gboolean create_source_bin (NvDsSourceConfig *config, NvDsSrcBin *bin);

/**
 * Initialize @ref NvDsSrcParentBin. It creates and adds source and
 * other elements needed for processing to the bin.
 * It also sets properties mentioned in the configuration file under
 * group @ref CONFIG_GROUP_SOURCE
 *
 * @param[in] num_sub_bins number of source elements.
 * @param[in] configs array of pointers of type @ref NvDsSourceConfig
 *            parsed from configuration file.
 * @param[in] bin pointer to @ref NvDsSrcParentBin to be filled.
 *
 * @return true if bin created successfully.
 */
gboolean
create_multi_source_bin (guint num_sub_bins, NvDsSourceConfig *configs,
                         NvDsSrcParentBin *bin);

gboolean reset_source_pipeline (gpointer data);
gboolean set_source_to_playing (gpointer data);
gpointer reset_encodebin (gpointer data);
#ifdef __cplusplus
}
#endif

#endif
