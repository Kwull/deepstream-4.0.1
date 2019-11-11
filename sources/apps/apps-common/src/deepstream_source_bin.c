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

#include <string.h>

#include "gstnvdsmeta.h"
#include "deepstream_common.h"
#include "deepstream_sources.h"
#include "deepstream_dewarper.h"
#include <gst/rtp/gstrtcpbuffer.h>

#define SRC_CONFIG_KEY "src_config"

GST_DEBUG_CATEGORY_EXTERN (NVDS_APP);
GST_DEBUG_CATEGORY_EXTERN (APP_CFG_PARSER_CAT);

static gboolean
set_camera_csi_params (NvDsSourceConfig * config, NvDsSrcBin * bin)
{
  g_object_set (G_OBJECT (bin->src_elem), "sensor-id",
      config->camera_csi_sensor_id, NULL);

  GST_CAT_DEBUG (NVDS_APP, "Setting csi camera params successful");

  return TRUE;
}

static void cb_rtsp_src_elem_added(GstBin     *bin,
               GstElement *element,
               gpointer    user_data);

static gboolean
set_camera_v4l2_params (NvDsSourceConfig * config, NvDsSrcBin * bin)
{
  gchar device[64];

  g_snprintf (device, sizeof (device), "/dev/video%d",
      config->camera_v4l2_dev_node);
  g_object_set (G_OBJECT (bin->src_elem), "device", device, NULL);

  GST_CAT_DEBUG (NVDS_APP, "Setting v4l2 camera params successful");

  return TRUE;
}

static gboolean
create_camera_source_bin (NvDsSourceConfig * config, NvDsSrcBin * bin)
{
  GstCaps *caps = NULL, *convertCaps = NULL;
  gboolean ret = FALSE;

  switch (config->type) {
    case NV_DS_SOURCE_CAMERA_CSI:
      bin->src_elem =
          gst_element_factory_make (NVDS_ELEM_SRC_CAMERA_CSI, "src_elem");
      g_object_set (G_OBJECT (bin->src_elem), "bufapi-version", TRUE, NULL);
      g_object_set (G_OBJECT (bin->src_elem), "maxperf", TRUE, NULL);
      break;
    case NV_DS_SOURCE_CAMERA_V4L2:
      bin->src_elem =
          gst_element_factory_make (NVDS_ELEM_SRC_CAMERA_V4L2, "src_elem");
      break;
    default:
      NVGSTDS_ERR_MSG_V ("Unsupported source type");
      goto done;
  }

  if (!bin->src_elem) {
    NVGSTDS_ERR_MSG_V ("Could not create 'src_elem'");
    goto done;
  }

  bin->cap_filter =
      gst_element_factory_make (NVDS_ELEM_CAPS_FILTER, "src_cap_filter");
  if (!bin->cap_filter) {
    NVGSTDS_ERR_MSG_V ("Could not create 'src_cap_filter'");
    goto done;
  }
  caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, config->source_width, "height", G_TYPE_INT,
      config->source_height, "framerate", GST_TYPE_FRACTION,
      config->source_fps_n, config->source_fps_d, NULL);

  if (config->type == NV_DS_SOURCE_CAMERA_CSI) {
    GstCapsFeatures *feature = NULL;
    feature = gst_caps_features_new ("memory:NVMM", NULL);
    gst_caps_set_features (caps, 0, feature);
  }

  //g_object_set (G_OBJECT (bin->cap_filter), "caps", caps, NULL);
  if (config->type == NV_DS_SOURCE_CAMERA_V4L2) {
    GstElement *nvvidconv1, *nvvidconv2;
    GstCapsFeatures *feature = NULL;


    nvvidconv1 = gst_element_factory_make ("videoconvert", "nvvidconv1");
    if (!nvvidconv1) {
      NVGSTDS_ERR_MSG_V ("Failed to create 'nvvidconv1'");
      goto done;
    }

    feature = gst_caps_features_new ("memory:NVMM", NULL);
    gst_caps_set_features (caps, 0, feature);
    g_object_set (G_OBJECT (bin->cap_filter), "caps", caps, NULL);

    nvvidconv2 = gst_element_factory_make (NVDS_ELEM_VIDEO_CONV, "nvvidconv2");
    if (!nvvidconv2) {
      NVGSTDS_ERR_MSG_V ("Failed to create 'nvvidconv2'");
      goto done;
    }

    g_object_set (G_OBJECT (nvvidconv2), "gpu-id", config->gpu_id,
        "nvbuf-memory-type", config->nvbuf_memory_type, NULL);

    gst_bin_add_many (GST_BIN (bin->bin), bin->src_elem, bin->cap_filter,
        nvvidconv1, nvvidconv2, bin->cap_filter,
        NULL);

    NVGSTDS_LINK_ELEMENT (bin->src_elem, nvvidconv1);

    NVGSTDS_LINK_ELEMENT (nvvidconv1, nvvidconv2);

    NVGSTDS_LINK_ELEMENT (nvvidconv2, bin->cap_filter);

    NVGSTDS_BIN_ADD_GHOST_PAD (bin->bin, bin->cap_filter, "src");

  } else {

    g_object_set (G_OBJECT (bin->cap_filter), "caps", caps, NULL);

    gst_bin_add_many (GST_BIN (bin->bin), bin->src_elem, bin->cap_filter, NULL);

    NVGSTDS_LINK_ELEMENT (bin->src_elem, bin->cap_filter);

    NVGSTDS_BIN_ADD_GHOST_PAD (bin->bin, bin->cap_filter, "src");
  }

  switch (config->type) {
    case NV_DS_SOURCE_CAMERA_CSI:
      if (!set_camera_csi_params (config, bin)) {
        NVGSTDS_ERR_MSG_V ("Could not set CSI camera properties");
        goto done;
      }
      break;
    case NV_DS_SOURCE_CAMERA_V4L2:
      if (!set_camera_v4l2_params (config, bin)) {
        NVGSTDS_ERR_MSG_V ("Could not set V4L2 camera properties");
        goto done;
      }
      break;
    default:
      NVGSTDS_ERR_MSG_V ("Unsupported source type");
      goto done;
  }

  ret = TRUE;

  GST_CAT_DEBUG (NVDS_APP, "Created camera source bin successfully");

done:
  if (caps)
    gst_caps_unref (caps);

  if (convertCaps)
    gst_caps_unref (convertCaps);

  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

static void
cb_newpad (GstElement * decodebin, GstPad * pad, gpointer data)
{
  GstCaps *caps = gst_pad_query_caps (pad, NULL);
  const GstStructure *str = gst_caps_get_structure (caps, 0);
  const gchar *name = gst_structure_get_name (str);

  if (!strncmp (name, "video", 5)) {
    NvDsSrcBin *bin = (NvDsSrcBin *) data;
    GstPad *sinkpad = gst_element_get_static_pad (bin->tee, "sink");
    if (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK) {

      NVGSTDS_ERR_MSG_V ("Failed to link decodebin to pipeline");
    } else {
      NvDsSourceConfig *config =
          (NvDsSourceConfig *) g_object_get_data (G_OBJECT (bin->cap_filter),
          SRC_CONFIG_KEY);

      gst_structure_get_int (str, "width", &config->source_width);
      gst_structure_get_int (str, "height", &config->source_height);
      gst_structure_get_fraction (str, "framerate", &config->source_fps_n,
          &config->source_fps_d);

      GST_CAT_DEBUG (NVDS_APP, "Decodebin linked to pipeline");
    }
    gst_object_unref (sinkpad);
  }
}

static void
cb_sourcesetup (GstElement * object, GstElement * arg0, gpointer data)
{
  NvDsSrcBin *bin = (NvDsSrcBin *) data;
  if (g_object_class_find_property (G_OBJECT_GET_CLASS (arg0), "latency")) {
    g_print ("cb_sourcesetup set %d latency\n", bin->latency);
    g_object_set (G_OBJECT (arg0), "latency", bin->latency, NULL);
  }
}

/*
 * Function to seek the source stream to start.
 * It is required to play the stream in loop.
 */
static gboolean
seek_decode (gpointer data)
{
  NvDsSrcBin *bin = (NvDsSrcBin *) data;
  gboolean ret = TRUE;

  gst_element_set_state (bin->bin, GST_STATE_PAUSED);

  ret = gst_element_seek (bin->bin, 1.0, GST_FORMAT_TIME,
      (GstSeekFlags) (GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_FLUSH),
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

  if (!ret)
    GST_WARNING ("Error in seeking pipeline");

  gst_element_set_state (bin->bin, GST_STATE_PLAYING);

  return FALSE;
}

/**
 * Probe function to drop certain events to support custom
 * logic of looping of each source stream.
 */
static GstPadProbeReturn
restart_stream_buf_prob (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
  GstEvent *event = GST_EVENT (info->data);
  NvDsSrcBin *bin = (NvDsSrcBin *) u_data;

  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
    GST_BUFFER_PTS(GST_BUFFER(info->data)) += bin->prev_accumulated_base;
  }
  if ((info->type & GST_PAD_PROBE_TYPE_EVENT_BOTH)) {
    if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
      g_timeout_add (1, seek_decode, bin);
    }

    if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
      GstSegment *segment;

      gst_event_parse_segment (event, (const GstSegment **) &segment);
      segment->base = bin->accumulated_base;
      bin->prev_accumulated_base = bin->accumulated_base;
      bin->accumulated_base += segment->stop;
    }
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        /* QOS events from downstream sink elements cause decoder to drop
         * frames after looping the file since the timestamps reset to 0.
         * We should drop the QOS events since we have custom logic for
         * looping individual sources. */
      case GST_EVENT_QOS:
      case GST_EVENT_SEGMENT:
      case GST_EVENT_FLUSH_START:
      case GST_EVENT_FLUSH_STOP:
        return GST_PAD_PROBE_DROP;
      default:
        break;
    }
  }
  return GST_PAD_PROBE_OK;
}


static void
decodebin_child_added (GstChildProxy * child_proxy, GObject * object,
    gchar * name, gpointer user_data)
{
  NvDsSrcBin *bin = (NvDsSrcBin *) user_data;
  NvDsSourceConfig *config = bin->config;
  if (g_strrstr (name, "decodebin") == name) {
    g_signal_connect (G_OBJECT (object), "child-added",
        G_CALLBACK (decodebin_child_added), user_data);
  }
  if (g_strrstr (name, "nvcuvid") == name) {
    g_object_set (object, "gpu-id", config->gpu_id, NULL);

    g_object_set (G_OBJECT (object), "cuda-memory-type",
        config->cuda_memory_type, NULL);

    g_object_set (object, "source-id", config->camera_id, NULL);
    g_object_set (object, "num-decode-surfaces", config->num_decode_surfaces,
        NULL);
    if (config->Intra_decode)
      g_object_set (object, "Intra-decode", config->Intra_decode, NULL);
  }
  if (g_strstr_len (name, -1, "omx") == name) {
    if (config->Intra_decode)
      g_object_set (object, "skip-frames", 2, NULL);
    g_object_set (object, "disable-dvfs", TRUE, NULL);
  }
  if (g_strstr_len (name, -1, "nvjpegdec") == name) {
    g_object_set (object, "DeepStream", TRUE, NULL);
  }
  if (g_strstr_len (name, -1, "nvv4l2decoder") == name) {
    if (config->Intra_decode)
      g_object_set (object, "skip-frames", 2, NULL);
#ifdef __aarch64__
    g_object_set (object, "enable-max-performance", TRUE, NULL);
    g_object_set (object, "bufapi-version", TRUE, NULL);
#else
    g_object_set (object, "gpu-id", config->gpu_id, NULL);
    g_object_set (G_OBJECT (object), "cudadec-memtype",
        config->cuda_memory_type, NULL);
#endif
    g_object_set (object, "drop-frame-interval", config->drop_frame_interval, NULL);
    g_object_set (object, "num-extra-surfaces", config->num_extra_surfaces,
        NULL);

    /* Seek only if file is the source. */
    if (config->loop && g_strstr_len(config->uri, -1, "file:/") == config->uri) {
      NVGSTDS_ELEM_ADD_PROBE (bin->src_buffer_probe, GST_ELEMENT(object),
          "sink", restart_stream_buf_prob,
          (GstPadProbeType) (GST_PAD_PROBE_TYPE_EVENT_BOTH |
              GST_PAD_PROBE_TYPE_EVENT_FLUSH | GST_PAD_PROBE_TYPE_BUFFER),
          bin);
    }
  }
done:
  return;
}

static void
cb_newpad2 (GstElement * decodebin, GstPad * pad, gpointer data)
{
  GstCaps *caps = gst_pad_query_caps (pad, NULL);
  const GstStructure *str = gst_caps_get_structure (caps, 0);
  const gchar *name = gst_structure_get_name (str);

  if (!strncmp (name, "video", 5)) {
    NvDsSrcBin *bin = (NvDsSrcBin *) data;
    GstPad *sinkpad = gst_element_get_static_pad (bin->cap_filter, "sink");
    if (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK) {

      NVGSTDS_ERR_MSG_V ("Failed to link decodebin to pipeline");
    } else {
      NvDsSourceConfig *config =
          (NvDsSourceConfig *) g_object_get_data (G_OBJECT (bin->cap_filter),
          SRC_CONFIG_KEY);

      gst_structure_get_int (str, "width", &config->source_width);
      gst_structure_get_int (str, "height", &config->source_height);
      gst_structure_get_fraction (str, "framerate", &config->source_fps_n,
          &config->source_fps_d);

      GST_CAT_DEBUG (NVDS_APP, "Decodebin linked to pipeline");
    }
    gst_object_unref (sinkpad);
  }
}


static void
cb_newpad3 (GstElement * decodebin, GstPad * pad, gpointer data)
{
  GstCaps *caps = gst_pad_query_caps (pad, NULL);
  const GstStructure *str = gst_caps_get_structure (caps, 0);
  const gchar *name = gst_structure_get_name (str);

  if (g_strrstr (name, "x-rtp")) {
    NvDsSrcBin *bin = (NvDsSrcBin *) data;
    GstPad *sinkpad = gst_element_get_static_pad (bin->depay, "sink");
    if (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK) {

      NVGSTDS_ERR_MSG_V ("Failed to link depay loader to rtsp src");
    }
    gst_object_unref (sinkpad);
  }
}

/* Select only video stream. Ignore other streams. */
static gboolean
cb_rtspsrc_select_stream (GstElement *rtspsrc, guint num, GstCaps *caps,
        gpointer user_data)
{
  GstStructure *str = gst_caps_get_structure (caps, 0);
  const gchar *media = gst_structure_get_string (str, "media");

  return !g_strcmp0 (media, "video");
}

static gboolean
create_rtsp_src_bin (NvDsSourceConfig * config, NvDsSrcBin * bin)
{
  gboolean ret = FALSE;
  gchar elem_name[50];
  bin->config = config;

  bin->latency = config->latency;

  g_snprintf (elem_name, sizeof (elem_name), "src_elem%d", bin->bin_id);
  bin->src_elem = gst_element_factory_make ("rtspsrc", elem_name);
  if (!bin->src_elem) {
    NVGSTDS_ERR_MSG_V ("Failed to create '%s'", elem_name);
    goto done;
  }

  if(bin->registered_rtcp_sender_report_cb)
  {
    /** User Requested for RTCP Sender reports */
    g_signal_connect (G_OBJECT(bin->src_elem), "element-added",
                      G_CALLBACK(cb_rtsp_src_elem_added),
                      bin);
  }

  g_signal_connect (G_OBJECT(bin->src_elem), "select-stream",
                    G_CALLBACK(cb_rtspsrc_select_stream),
                    bin);

  g_object_set (G_OBJECT (bin->src_elem), "location", config->uri, NULL);
  g_object_set (G_OBJECT (bin->src_elem), "latency", config->latency, NULL);
  g_object_set (G_OBJECT (bin->src_elem), "drop-on-latency", TRUE, NULL);
  g_object_set (G_OBJECT (bin->src_elem), "buffer-mode", 0, NULL);
  // 0x4 for TCP and 0x7 for All (UDP/UDP-MCAST/TCP)
  if ((config->select_rtp_protocol == 0x4)
      || (config->select_rtp_protocol == 0x7)) {
    g_object_set (G_OBJECT (bin->src_elem), "protocols",
        config->select_rtp_protocol, NULL);
    GST_DEBUG_OBJECT (bin->src_elem,
        "RTP Protocol=0x%x (0x4=TCP and 0x7=UDP,TCP,UDPMCAST)----\n",
        config->select_rtp_protocol);
  }
  g_signal_connect (G_OBJECT (bin->src_elem), "pad-added",
      G_CALLBACK (cb_newpad3), bin);

  g_snprintf (elem_name, sizeof (elem_name), "depay_elem%d", bin->bin_id);
  bin->depay = gst_element_factory_make ("rtph264depay", elem_name);
  if (!bin->depay) {
    NVGSTDS_ERR_MSG_V ("Failed to create '%s'", elem_name);
    goto done;
  }

  g_snprintf (elem_name, sizeof (elem_name), "dec_que%d", bin->bin_id);
  bin->dec_que = gst_element_factory_make ("queue", elem_name);
  if (!bin->dec_que) {
    NVGSTDS_ERR_MSG_V ("Failed to create '%s'", elem_name);
    goto done;
  }

  g_snprintf (elem_name, sizeof (elem_name), "decodebin_elem%d", bin->bin_id);
  bin->decodebin = gst_element_factory_make ("decodebin", elem_name);
  if (!bin->decodebin) {
    NVGSTDS_ERR_MSG_V ("Failed to create '%s'", elem_name);
    goto done;
  }

  g_signal_connect (G_OBJECT (bin->decodebin), "pad-added",
      G_CALLBACK (cb_newpad2), bin);
  g_signal_connect (G_OBJECT (bin->decodebin), "child-added",
      G_CALLBACK (decodebin_child_added), bin);


  g_snprintf (elem_name, sizeof (elem_name), "src_que%d", bin->bin_id);
  bin->cap_filter = gst_element_factory_make (NVDS_ELEM_QUEUE, elem_name);
  if (!bin->cap_filter) {
    NVGSTDS_ERR_MSG_V ("Failed to create '%s'", elem_name);
    goto done;
  }

  g_mutex_init (&bin->bin_lock);
  if (config->dewarper_config.enable) {
    if (!create_dewarper_bin (&config->dewarper_config, &bin->dewarper_bin)) {
      g_print ("Failed to create dewarper bin \n");
      goto done;
    }
    gst_bin_add_many (GST_BIN (bin->bin), bin->src_elem, bin->depay,
        bin->dec_que,
        bin->decodebin, bin->dewarper_bin.bin, bin->cap_filter, NULL);
  } else {
    gst_bin_add_many (GST_BIN (bin->bin), bin->src_elem, bin->depay,
        bin->dec_que, bin->decodebin, bin->cap_filter, NULL);
  }


  NVGSTDS_LINK_ELEMENT (bin->depay, bin->dec_que);
  NVGSTDS_LINK_ELEMENT (bin->dec_que, bin->decodebin);

  if (config->dewarper_config.enable) {
    NVGSTDS_LINK_ELEMENT (bin->cap_filter, bin->dewarper_bin.bin);
    NVGSTDS_BIN_ADD_GHOST_PAD (bin->bin, bin->dewarper_bin.bin, "src");
  } else
    NVGSTDS_BIN_ADD_GHOST_PAD (bin->bin, bin->cap_filter, "src");

  ret = TRUE;

  GST_CAT_DEBUG (NVDS_APP,
      "Decode bin created. Waiting for a new pad from decodebin to link");

done:

  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

static gboolean
create_uridecode_src_bin (NvDsSourceConfig * config, NvDsSrcBin * bin)
{
  gboolean ret = FALSE;
  bin->config = config;

  bin->src_elem = gst_element_factory_make (NVDS_ELEM_SRC_URI, "src_elem");
  if (!bin->src_elem) {
    NVGSTDS_ERR_MSG_V ("Could not create element 'src_elem'");
    goto done;
  }

  if (config->dewarper_config.enable) {
    if (!create_dewarper_bin (&config->dewarper_config, &bin->dewarper_bin)) {
      g_print ("Creating Dewarper bin failed \n");
      goto done;
    }
  }
  bin->latency = config->latency;

  if (g_strrstr (config->uri, "file:/")) {
    config->live_source = FALSE;
  }

  g_object_set (G_OBJECT (bin->src_elem), "uri", config->uri, NULL);
  g_signal_connect (G_OBJECT (bin->src_elem), "pad-added",
      G_CALLBACK (cb_newpad), bin);
  g_signal_connect (G_OBJECT (bin->src_elem), "child-added",
      G_CALLBACK (decodebin_child_added), bin);
  g_signal_connect (G_OBJECT (bin->src_elem), "source-setup",
      G_CALLBACK (cb_sourcesetup), bin);
  bin->cap_filter = gst_element_factory_make (NVDS_ELEM_QUEUE, "queue");
  if (!bin->cap_filter) {
    NVGSTDS_ERR_MSG_V ("Could not create 'queue'");
    goto done;
  }

  g_object_set_data (G_OBJECT (bin->cap_filter), SRC_CONFIG_KEY, config);

  gst_bin_add_many (GST_BIN (bin->bin), bin->src_elem, bin->cap_filter, NULL);

  NVGSTDS_BIN_ADD_GHOST_PAD (bin->bin, bin->cap_filter, "src");

  bin->fakesink = gst_element_factory_make ("fakesink", "src_fakesink");
  if (!bin->fakesink) {
    NVGSTDS_ERR_MSG_V ("Could not create 'src_fakesink'");
    goto done;
  }

  bin->fakesink_queue = gst_element_factory_make ("queue", "fakequeue");
  if (!bin->fakesink_queue) {
    NVGSTDS_ERR_MSG_V ("Could not create 'fakequeue'");
    goto done;
  }

  bin->tee = gst_element_factory_make ("tee", NULL);
  if (!bin->tee) {
    NVGSTDS_ERR_MSG_V ("Could not create 'tee'");
    goto done;
  }
  gst_bin_add_many (GST_BIN (bin->bin), bin->fakesink, bin->tee,
      bin->fakesink_queue, NULL);


  NVGSTDS_LINK_ELEMENT (bin->fakesink_queue, bin->fakesink);

  if (config->dewarper_config.enable) {
    gst_bin_add_many (GST_BIN (bin->bin), bin->dewarper_bin.bin, NULL);
    NVGSTDS_LINK_ELEMENT (bin->tee, bin->dewarper_bin.bin);
    NVGSTDS_LINK_ELEMENT (bin->dewarper_bin.bin, bin->cap_filter);
  } else {
    link_element_to_tee_src_pad (bin->tee, bin->cap_filter);
  }
  link_element_to_tee_src_pad (bin->tee, bin->fakesink_queue);

  g_object_set (G_OBJECT (bin->fakesink), "sync", FALSE, "async", FALSE, NULL);

  ret = TRUE;

  GST_CAT_DEBUG (NVDS_APP,
      "Decode bin created. Waiting for a new pad from decodebin to link");

done:

  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

gboolean
create_source_bin (NvDsSourceConfig * config, NvDsSrcBin * bin)
{
  static guint bin_cnt = 0;
  gchar bin_name[64];
  g_snprintf(bin_name, 64, "src_bin_%d", bin_cnt++);
  bin->bin = gst_bin_new (bin_name);
  if (!bin->bin) {
    NVGSTDS_ERR_MSG_V ("Failed to create 'src_bin'");
    return FALSE;
  }

  switch (config->type) {
    case NV_DS_SOURCE_CAMERA_V4L2:
      if (!create_camera_source_bin (config, bin)) {
        return FALSE;
      }
      break;
    case NV_DS_SOURCE_URI:
      if (!create_uridecode_src_bin (config, bin)) {
        return FALSE;
      }
      bin->live_source = config->live_source;
      break;
    case NV_DS_SOURCE_RTSP:
      if (!create_rtsp_src_bin (config, bin)) {
        return FALSE;
      }
      break;
    default:
      NVGSTDS_ERR_MSG_V ("Source type not yet implemented!\n");
      return FALSE;
  }

  GST_CAT_DEBUG (NVDS_APP, "Source bin created");

  return TRUE;
}

gboolean
create_multi_source_bin (guint num_sub_bins, NvDsSourceConfig * configs,
    NvDsSrcParentBin * bin)
{
  gboolean ret = FALSE;
  guint i = 0;

  bin->reset_thread = NULL;

  bin->bin = gst_bin_new ("multi_src_bin");
  if (!bin->bin) {
    NVGSTDS_ERR_MSG_V ("Failed to create element 'multi_src_bin'");
    goto done;
  }

  g_object_set (bin->bin, "message-forward", TRUE, NULL);

  bin->streammux =
      gst_element_factory_make (NVDS_ELEM_STREAM_MUX, "src_bin_muxer");
  if (!bin->streammux) {
    NVGSTDS_ERR_MSG_V ("Failed to create element 'src_bin_muxer'");
    goto done;
  }
  gst_bin_add (GST_BIN (bin->bin), bin->streammux);

  for (i = 0; i < num_sub_bins; i++) {
    if (!configs[i].enable) {
      continue;
    }

    gchar elem_name[50];
    g_snprintf (elem_name, sizeof (elem_name), "src_sub_bin%d", i);
    bin->sub_bins[i].bin = gst_bin_new (elem_name);
    if (!bin->sub_bins[i].bin) {
      NVGSTDS_ERR_MSG_V ("Failed to create '%s'", elem_name);
      goto done;
    }

    bin->sub_bins[i].bin_id = bin->sub_bins[i].source_id = i;
    configs[i].live_source = TRUE;
    bin->live_source = TRUE;
    bin->sub_bins[i].eos_done = TRUE;
    bin->sub_bins[i].reset_done = TRUE;

    switch (configs[i].type) {
      case NV_DS_SOURCE_CAMERA_CSI:
      case NV_DS_SOURCE_CAMERA_V4L2:
        if (!create_camera_source_bin (&configs[i], &bin->sub_bins[i])) {
          return FALSE;
        }
        break;
      case NV_DS_SOURCE_URI:
        if (!create_uridecode_src_bin (&configs[i], &bin->sub_bins[i])) {
          return FALSE;
        }
        bin->live_source = configs[i].live_source;
        break;
      case NV_DS_SOURCE_RTSP:
        bin->sub_bins[i].registered_rtcp_sender_report_cb = bin->rtcp_sender_report_cb;
        if (!create_rtsp_src_bin (&configs[i], &bin->sub_bins[i])) {
          return FALSE;
        }
        break;
      default:
        NVGSTDS_ERR_MSG_V ("Source type not yet implemented!\n");
        return FALSE;
    }

    gst_bin_add (GST_BIN (bin->bin), bin->sub_bins[i].bin);

    if (!link_element_to_streammux_sink_pad (bin->streammux,
            bin->sub_bins[i].bin, i)) {
      goto done;
    }
    bin->num_bins++;
  }

  NVGSTDS_BIN_ADD_GHOST_PAD (bin->bin, bin->streammux, "src");

  ret = TRUE;

done:
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

gboolean
reset_source_pipeline (gpointer data)
{
  NvDsSrcBin *src_bin = (NvDsSrcBin *) data;

  if (gst_element_set_state (src_bin->bin,
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (src_bin->bin, "Can't set source bin to NULL");
    return FALSE;
  }
  g_print ("Reset source pipeline %s %p\n,", __func__, src_bin);

  GST_CAT_INFO (NVDS_APP, "Reset source pipeline %s %p\n,", __func__, src_bin);
  if (!gst_element_sync_state_with_parent (src_bin->bin)) {
    GST_ERROR_OBJECT (src_bin->bin, "Couldn't sync state with parent");
  }
  return FALSE;
}

gboolean
set_source_to_playing (gpointer data)
{
  NvDsSrcBin *subBin = (NvDsSrcBin *) data;
  if (subBin->reconfiguring) {
    gst_element_set_state (subBin->bin, GST_STATE_PLAYING);
    GST_CAT_INFO (NVDS_APP, "Reconfiguring %s  %p\n,", __func__, subBin);

    subBin->reconfiguring = FALSE;
  }
  return FALSE;
}

gpointer
reset_encodebin (gpointer data)
{
  NvDsSrcBin *src_bin = (NvDsSrcBin *) data;
  g_usleep (10000);
  GST_CAT_INFO (NVDS_APP, "Reset called %s %p\n,", __func__, src_bin);

  GST_CAT_INFO (NVDS_APP, "Reset setting null for sink %s %p\n,", __func__,
      src_bin);
  //g_mutex_lock(&src_bin->bin_lock);
  src_bin->reset_done = TRUE;
  //g_mutex_unlock(&src_bin->bin_lock);

  return NULL;
}

static void
deepstream_rtp_bin_handle_sync (GstElement * jitterbuffer, GstStructure * s,
    gpointer * user_data)
{
  gboolean more;
  GstRTCPPacket packet;
  guint64 ntptime;
  guint32 rtptime;
  GstBuffer* buffer = gst_value_get_buffer (gst_structure_get_value (s, "sr-buffer"));
  guint64 gstreamer_time = g_value_get_uint64 (gst_structure_get_value(s, "base-time"));
  guint64 base_rtptime = g_value_get_uint64 (gst_structure_get_value(s, "base-rtptime"));
  guint64 sr_ext_rtptime = g_value_get_uint64 (gst_structure_get_value(s, "sr-ext-rtptime"));
  guint clock_rate = g_value_get_uint (gst_structure_get_value(s, "clock-rate"));

  gstreamer_time += ((sr_ext_rtptime - base_rtptime) * GST_SECOND / clock_rate);
  GstRTCPBuffer rtcp = { NULL, };
  NvDsSrcBin *dsSrcBin = (NvDsSrcBin*)user_data;

  if(!buffer)
  {
      return;
  }

  gst_rtcp_buffer_map (buffer, GST_MAP_READ, &rtcp);

  for(more = gst_rtcp_buffer_get_first_packet(&rtcp, &packet); more;
      more = gst_rtcp_packet_move_to_next(&packet)) {
    switch (gst_rtcp_packet_get_type (&packet)) {
      case GST_RTCP_TYPE_SR:

        /* get NTP and RTP times */
        gst_rtcp_packet_sr_get_sender_info (&packet, NULL, &ntptime, &rtptime,
            NULL, NULL);

        /** rtptime - which is the timestamp synchronized with corresponding
         * RTP Stream is dropped as we have the same mapped to Gstreamer clock
         * in rtpbin (manager) plugin which is eventually saved in
         * gstreamer_time as "base-time".
         * NOTE: This is the latest incoming RTP-buffer-time aligned to
         * gstreamer clock + clock-skew between receiver and sender
         * FOR OSS REFERENCE: Specific code in rtpmanager:
         * do_handle_sync(), rtp_jitter_buffer_get_sync() in
         * gst-plugins-good/gst/rtpmanager/gstrtpjitterbuffer.c
         */
        (void)rtptime;

        /** RTCP RFC 3550; The full-resolution
         * NTP timestamp is a 64-bit unsigned fixed-point number with
         * the integer part in the first 32 bits and the fractional part in the
         * last 32 bits.
         * The NTP timescale wraps around every 2^32 seconds (136 years);
         * the first rollover will occur in 2036.
         * The higher 32-bits carry epoch time + 2208988800LL (later is a const introduced by gstrtpbin.c)
         * To convert fractional part of NTP:
         * NTP fraction * (fraction of seconds) / 2 ^ 32.
         * For example of NTP fraction 1329481807, to convert to microsecond:
         * = 1329481807 * (10 ^ 6) / 2 ^ 32 = 309544us (roughly)
         */
        {
            struct timespec timespec_rtcp_ntp;
            /** Extract higher 32-bit epoch into timespec_rtcp_ntp.tv_sec */
            GST_TIME_TO_TIMESPEC((((ntptime >> 32) - 2208988800LL) * GST_SECOND), timespec_rtcp_ntp);
            /** Extract lower 32-bit fraction into timespec_rtcp_ntp.tv_nsec */
            timespec_rtcp_ntp.tv_nsec = (guint64)(( (ntptime & (0xFFFFFFFF)) * GST_SECOND ) >> 32);
            if(dsSrcBin->registered_rtcp_sender_report_cb)
            {
                dsSrcBin->registered_rtcp_sender_report_cb(dsSrcBin->bin_id,
                        GST_TIMESPEC_TO_TIME(timespec_rtcp_ntp),
                        gstreamer_time
                        );
            }
        }

        break;
      default:
        break;
    }
  }
}

static void
rtp_bin_new_jitter_buffer (GstBin  *rtpbin,
               GstElement *jitterbuffer,
               guint       session,
               guint       ssrc,
               gpointer    user_data)
{
  /** Request for the `handle-sync` signal
   * in jitterbuffer to lawfully tap
   * RTCP Sender Report
   */
  g_signal_connect(G_OBJECT(jitterbuffer), "handle-sync",
                   G_CALLBACK(deepstream_rtp_bin_handle_sync),
                   user_data);
}

void cb_rtsp_src_elem_added(GstBin     *bin,
               GstElement *element,
               gpointer    user_data)
{
  if(strstr(GST_ELEMENT_NAME(element), "manager"))
  {
      /** RtpBin: Request new-jitterbuffer signal */
      g_signal_connect(G_OBJECT(element), "new-jitterbuffer",
                       G_CALLBACK(rtp_bin_new_jitter_buffer),
                       user_data);
  }
}
