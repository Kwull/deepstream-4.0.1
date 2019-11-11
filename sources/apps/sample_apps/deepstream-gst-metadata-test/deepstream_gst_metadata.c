/*
 * Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
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

#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include "stdlib.h"
#include "gstnvdsmeta.h"

#define PGIE_CLASS_ID_VEHICLE 0
#define PGIE_CLASS_ID_PERSON 2

/** set the user metadata type */
#define NVDS_DECODER_GST_META_EXAMPLE (nvds_get_user_meta_type("NVIDIA.DECODER.GST_USER_META"))

/* The muxer output resolution must be set if the input streams will be of
 * different resolution. The muxer will scale all the input frames to this
 * resolution. */
#define MUXER_OUTPUT_WIDTH 1920
#define MUXER_OUTPUT_HEIGHT 1080

/* Muxer batch formation timeout, for e.g. 40 millisec. Should ideally be set
 * based on the fastest source's framerate. */
#define MUXER_BATCH_TIMEOUT_USEC 4000000

gint frame_number = 0;
gchar pgie_classes_str[4][32] = { "Vehicle", "TwoWheeler", "Person",
  "Roadsign"
};

typedef struct _NvDecoderMeta
{
  guint frame_type;
  guint frame_num;
  gboolean dec_err;
} NvDecoderMeta;

/* gst meta copy function set by user */
static gpointer decoder_meta_copy_func(gpointer data, gpointer user_data)
{
  NvDecoderMeta *src_decoder_meta = (NvDecoderMeta *)data;
  NvDecoderMeta *dst_decoder_meta = (NvDecoderMeta*)g_malloc0(
      sizeof(NvDecoderMeta));
  memcpy(dst_decoder_meta, src_decoder_meta, sizeof(NvDecoderMeta));
  return (gpointer)dst_decoder_meta;
}

/* gst meta release function set by user */
static void decoder_meta_release_func(gpointer data, gpointer user_data)
{
  NvDecoderMeta *decoder_meta = (NvDecoderMeta *)data;
  if(decoder_meta) {
    g_free(decoder_meta);
    decoder_meta = NULL;
  }
}

/* gst to nvds transform function set by user. "data" holds a pointer to NvDsUserMeta */
static gpointer decoder_gst_to_nvds_meta_transform_func(gpointer data, gpointer user_data)
{
  NvDsUserMeta *user_meta = (NvDsUserMeta *)data;
  NvDecoderMeta *src_decoder_meta =
    (NvDecoderMeta*)user_meta->user_meta_data;
  NvDecoderMeta *dst_decoder_meta =
    (NvDecoderMeta *)decoder_meta_copy_func(src_decoder_meta, NULL);
  return (gpointer)dst_decoder_meta;
}

/* release function set by user to release gst to nvds transformed metadata.
 * "data" holds a pointer to NvDsUserMeta */
static void decoder_gst_nvds_meta_release_func(gpointer data, gpointer user_data)
{
  NvDsUserMeta *user_meta = (NvDsUserMeta *) data;
  NvDecoderMeta *decoder_meta = (NvDecoderMeta *)user_meta->user_meta_data;
  decoder_meta_release_func(decoder_meta, NULL);
}

/* nvinfer_src_pad_buffer_probe() will extract the metadata received on nvinfer
 * src pad.
 * It explains the mechanism to extract the decoder metadata (which is attached
 * using gstnvdsmeta API's in nvdecoder_src_pad_buffer_probe()),
 * now transformed into nvdsmeta. Decoder meta, attached to gst buffer
 * is set as user data at NvDsFrameMeta level
 */

static GstPadProbeReturn
nvinfer_src_pad_buffer_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
  GstBuffer *buf = (GstBuffer *) info->data;
  NvDsMetaList * l_frame = NULL;
  NvDsUserMeta *user_meta = NULL;
  NvDecoderMeta * decoder_meta = NULL;
  NvDsMetaList * l_user_meta = NULL;

  NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);

    for (l_frame = batch_meta->frame_meta_list; l_frame != NULL;
      l_frame = l_frame->next) {
        NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) (l_frame->data);

        for (l_user_meta = frame_meta->frame_user_meta_list; l_user_meta != NULL;
            l_user_meta = l_user_meta->next) {
          user_meta = (NvDsUserMeta *) (l_user_meta->data);
          if(user_meta->base_meta.meta_type == NVDS_DECODER_GST_META_EXAMPLE)
          {
            decoder_meta = (NvDecoderMeta *)user_meta->user_meta_data;
            g_print("Dec Meta retrieved as NVDS USER METADTA For Frame_Num = %d  \n",
                decoder_meta->frame_num);
            g_print("frame type = %d, frame_num = %d decode_error_status = %d\n\n",
                decoder_meta->frame_type, decoder_meta->frame_num,
                decoder_meta->dec_err);
          }
        }
    }
    return GST_PAD_PROBE_OK;
}

/* nvdecoder_src_pad_buffer_probe() will attach decoder metadata to gstreamer
 * buffer on src pad. The decoder can not attach to NvDsBatchMeta metadata because
 * batch level metadata is created by nvstreammux component. The decoder
 * component is present is before nvstreammmux. So it attached the metadata
 * using gstnvdsmeta API's.
 */
static GstPadProbeReturn
nvdecoder_src_pad_buffer_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
  GstBuffer *buf = (GstBuffer *) info->data;
  NvDsMeta *meta = NULL;

  NvDecoderMeta *decoder_meta = (NvDecoderMeta *)g_malloc0(sizeof(NvDecoderMeta));
  if(decoder_meta == NULL)
  {
    return GST_FLOW_ERROR;
  }
  /* Add dummy metadata */
  decoder_meta->frame_type = (frame_number % 3);
  decoder_meta->frame_num = frame_number++;
  decoder_meta->dec_err = ((frame_number % 4) / 3);

  /* Attach decoder metadata to gst buffer using gst_buffer_add_nvds_meta() */
  meta = gst_buffer_add_nvds_meta (buf, decoder_meta, NULL,
      decoder_meta_copy_func, decoder_meta_release_func);

  /* Set metadata type */
  meta->meta_type = (GstNvDsMetaType)NVDS_DECODER_GST_META_EXAMPLE;

  /* Set transform function to transform decoder metadata from Gst meta to
   * nvds meta */
  meta->gst_to_nvds_meta_transform_func = decoder_gst_to_nvds_meta_transform_func;

  /* Set release function to release the transformed nvds metadata */
  meta->gst_to_nvds_meta_release_func = decoder_gst_nvds_meta_release_func;

  g_print("GST Dec Meta attached with gst decoder output buffer for Frame_Num = %d\n",
      decoder_meta->frame_num);
  g_print("frame type = %d, frame_num = %d decode_error_status = %d\n\n",
      decoder_meta->frame_type, decoder_meta->frame_num,
      decoder_meta->dec_err);

  return GST_PAD_PROBE_OK;
}


static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;
      gst_message_parse_error (msg, &error, &debug);
      g_printerr ("ERROR from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), error->message);
      if (debug)
        g_printerr ("Error details: %s\n", debug);
      g_free (debug);
      g_error_free (error);
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop = NULL;
  GstElement *pipeline = NULL, *source = NULL, *h264parser = NULL,
      *decoder = NULL, *streammux = NULL, *sink = NULL, *pgie = NULL, *nvvidconv = NULL,
      *nvosd = NULL;
#ifdef PLATFORM_TEGRA
  GstElement *transform = NULL;
#endif
  GstBus *bus = NULL;
  guint bus_watch_id;
  GstPad *infer_src_pad = NULL;
  GstPad *decoder_src_pad = NULL;

  /* Check input arguments */
  if (argc != 2) {
    g_printerr ("Usage: %s <H264 filename>\n", argv[0]);
    return -1;
  }

  /* Standard GStreamer initialization */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* Create gstreamer elements */
  /* Create Pipeline element that will form a connection of other elements */
  pipeline = gst_pipeline_new ("dstest1-pipeline");

  /* Source element for reading from the file */
  source = gst_element_factory_make ("filesrc", "file-source");

  /* Since the data format in the input file is elementary h264 stream,
   * we need a h264parser */
  h264parser = gst_element_factory_make ("h264parse", "h264-parser");

  /* Use nvdec_h264 for hardware accelerated decode on GPU */
  decoder = gst_element_factory_make ("nvv4l2decoder", "nvv4l2-decoder");

  /* Create nvstreammux instance to form batches from one or more sources. */
  streammux = gst_element_factory_make ("nvstreammux", "stream-muxer");

  if (!pipeline || !streammux) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  /* Use nvinfer to run inferencing on decoder's output,
   * behaviour of inferencing is set through config file */
  pgie = gst_element_factory_make ("nvinfer", "primary-nvinference-engine");

  /* Use convertor to convert from NV12 to RGBA as required by nvosd */
  nvvidconv = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter");

  /* Create OSD to draw on the converted RGBA buffer */
  nvosd = gst_element_factory_make ("nvdsosd", "nv-onscreendisplay");

  /* Finally render the osd output */
#ifdef PLATFORM_TEGRA
  transform = gst_element_factory_make ("nvegltransform", "nvegl-transform");
#endif
  sink = gst_element_factory_make ("nveglglessink", "nvvideo-renderer");

  if (!source || !h264parser || !decoder || !pgie
      || !nvvidconv || !nvosd || !sink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

#ifdef PLATFORM_TEGRA
  if(!transform) {
    g_printerr ("One tegra element could not be created. Exiting.\n");
    return -1;
  }
#endif

  /* we set the input filename to the source element */
  g_object_set (G_OBJECT (source), "location", argv[1], NULL);

  g_object_set (G_OBJECT (streammux), "width", MUXER_OUTPUT_WIDTH, "height",
      MUXER_OUTPUT_HEIGHT, "batch-size", 1,
      "batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC, NULL);

  /* Set all the necessary properties of the nvinfer element,
   * the necessary ones are : */
  g_object_set (G_OBJECT (pgie),
      "config-file-path", "dsmeta_pgie_config.txt", NULL);

  /* we add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  /* Set up the pipeline */
  /* we add all elements into the pipeline */
#ifdef PLATFORM_TEGRA
  gst_bin_add_many (GST_BIN (pipeline),
      source, h264parser, decoder, streammux, pgie,
      nvvidconv, nvosd, transform, sink, NULL);
#else
  gst_bin_add_many (GST_BIN (pipeline),
      source, h264parser, decoder, streammux, pgie,
      nvvidconv, nvosd, sink, NULL);
#endif

  GstPad *sinkpad, *srcpad;
  gchar pad_name_sink[16] = "sink_0";
  gchar pad_name_src[16] = "src";

  sinkpad = gst_element_get_request_pad (streammux, pad_name_sink);
  if (!sinkpad) {
    g_printerr ("Streammux request sink pad failed. Exiting.\n");
    return -1;
  }

  srcpad = gst_element_get_static_pad (decoder, pad_name_src);
  if (!srcpad) {
    g_printerr ("Decoder request src pad failed. Exiting.\n");
    return -1;
  }

  if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK) {
      g_printerr ("Failed to link decoder to stream muxer. Exiting.\n");
      return -1;
  }

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  /* we link the elements together */
  /* file-source -> h264-parser -> nvh264-decoder ->
   * nvinfer -> nvvidconv -> nvosd -> video-renderer */

  if (!gst_element_link_many (source, h264parser, decoder, NULL)) {
    g_printerr ("Elements could not be linked: 1. Exiting.\n");
    return -1;
  }

#ifdef PLATFORM_TEGRA
  if (!gst_element_link_many (streammux, pgie,
      nvvidconv, nvosd, transform, sink, NULL)) {
    g_printerr ("Elements could not be linked: 2. Exiting.\n");
    return -1;
  }
#else
  if (!gst_element_link_many (streammux, pgie,
      nvvidconv, nvosd, sink, NULL)) {
    g_printerr ("Elements could not be linked: 2. Exiting.\n");
    return -1;
  }
#endif

  /* Lets add probe to access decoder metadata attached with NvDsMeta.
   * This metadata is tranformed into nvdsmeta and set as user metadata at
   * frame level.
   * We add probe to the src pad of the decoder element */
  decoder_src_pad = gst_element_get_static_pad (decoder, "src");
  if (!decoder_src_pad)
    g_print ("Unable to get source pad\n");
  else
    gst_pad_add_probe (decoder_src_pad, GST_PAD_PROBE_TYPE_BUFFER,
        nvdecoder_src_pad_buffer_probe, NULL, NULL);

  /* Lets add probe at decoder src pad to attach dummy decoder metadata using
   * gstnvdsmeta APIs.
   * This metadata is transformed into nvdsmeta and set as user metadata at
   * frame level */
  infer_src_pad = gst_element_get_static_pad (pgie, "src");
  if (!infer_src_pad)
    g_print ("Unable to get source pad\n");
  else
    gst_pad_add_probe (infer_src_pad, GST_PAD_PROBE_TYPE_BUFFER,
        nvinfer_src_pad_buffer_probe, NULL, NULL);

  /* Set the pipeline to "playing" state */
  g_print ("Now playing: %s\n", argv[1]);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait till pipeline encounters an error or EOS */
  g_print ("Running...\n");
  g_main_loop_run (loop);

  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);
  return 0;
}
