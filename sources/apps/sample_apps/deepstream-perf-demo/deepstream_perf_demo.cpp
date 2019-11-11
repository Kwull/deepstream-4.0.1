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

#include <glib.h>
#include <gst/gst.h>
#include <gst/gstpipeline.h>

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <sys/time.h>

#define PGIE_CONFIG_FILE  "perf_demo_pgie_config.txt"
#define SGIE1_CONFIG_FILE "perf_demo_sgie1_config.txt"
#define SGIE2_CONFIG_FILE "perf_demo_sgie2_config.txt"
#define SGIE3_CONFIG_FILE "perf_demo_sgie3_config.txt"

#define GPU_ID 0

#define PGIE_CLASS_ID_VEHICLE 0
#define PGIE_CLASS_ID_PERSON 2
#define MAX_DISPLAY_LEN 64

/* The muxer output resolution must be set if the input streams will be of
 * different resolution. The muxer will scale all the input frames to this
 * resolution. */
#define MUXER_OUTPUT_WIDTH 1280
#define MUXER_OUTPUT_HEIGHT 720

/* Muxer batch formation timeout, for e.g. 40 millisec. Should ideally be set
 * based on the fastest source's framerate. */
#define MUXER_BATCH_TIMEOUT_USEC 4000000

guint sgie1_unique_id = 2;
guint sgie2_unique_id = 3;
guint sgie3_unique_id = 4;

static GMainLoop* loop = NULL;
std::vector<std::string> file_list;

#if defined(ENABLE_PROFILING)
static gint frame_number = 0;
static struct timeval g_start;
static struct timeval g_end;
static float g_accumulated_time_macro = 0;

static void profile_start() {
    gettimeofday(&g_start, 0);
}

static void profile_end() {
    gettimeofday(&g_end, 0);
}

static void profile_result() {
    g_accumulated_time_macro += 1000000 * (g_end.tv_sec - g_start.tv_sec)
                                + g_end.tv_usec - g_start.tv_usec;
    // Be careful 1000000 * g_accumulated_time_macro may be overflow.
    float fps = (float)((frame_number - 100) / (float)(g_accumulated_time_macro / 1000000));
    std::cout << "The average frame rate is " << fps
              << ", frame num " << frame_number - 100
              << ", time accumulated " << g_accumulated_time_macro/1000000
              << std::endl;
}
#endif

static char *getOneFileName(DIR *pDir, int &isFile) {
    struct dirent *ent;

    while (1) {
        ent = readdir(pDir);
        if (ent == NULL) {
            isFile = 0;
            return NULL;
        } else {
            if(ent->d_type & DT_REG) {
                isFile = 1;
                return ent->d_name;
            } else if (strcmp(ent->d_name, ".") == 0 ||
                       strcmp(ent->d_name, "..") == 0) {
                continue;
            } else {
                isFile = 0;
                return ent->d_name;
            }
        }
    }
}

static void get_file_list(char* inputDir) {
    if (inputDir == NULL) return;

    char *fn = NULL;
    int isFile = 1;
    std::string fnStd;
    std::string dirNameStd(inputDir);
    std::string fullName;

    DIR *dir = opendir(inputDir);

    while (1) {
        fn = getOneFileName(dir, isFile);

        if (isFile) {
            fnStd = fn;
            fullName = dirNameStd + "/" + fnStd;
            file_list.push_back(fullName);
        } else {
            break;
        }
    }
}

static gboolean source_switch_thread(gpointer* data) {
    static guint stream_num = 0;
    const char* location = file_list[stream_num % file_list.size()].c_str();

    GstElement* pipeline = (GstElement*) data;
    GstElement* source = gst_bin_get_by_name(GST_BIN(pipeline), "file-source");
    GstElement* h264parser = gst_bin_get_by_name(GST_BIN(pipeline), "h264-parser");
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "nvvideo-renderer");
    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
    ret = gst_element_set_state(source, GST_STATE_NULL);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_print("Unable to set state change for source element \n");
        g_main_loop_quit(loop);
    }

    g_object_set(G_OBJECT(source), "location", location, NULL);
    gst_pad_activate_mode(gst_element_get_static_pad(h264parser, "sink"), GST_PAD_MODE_PUSH, TRUE);
    gst_element_sync_state_with_parent(h264parser);
    gst_element_sync_state_with_parent(source);
    gst_element_sync_state_with_parent(sink);

#if 0 // Change rows/colums dynamically here
    guint rows = 0;
    guint columns = 0;
    g_object_get(G_OBJECT(sink), "rows", &rows, NULL);
    g_object_get(G_OBJECT(sink), "columns", &columns, NULL);

    if (stream_num % (rows * columns) == 0) {
        g_object_set (G_OBJECT(sink), "rows", rows * 2, NULL);
        g_object_set (G_OBJECT(sink), "columns", columns * 2, NULL);
    }
#endif
    stream_num++;

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    return FALSE;
}

static GstPadProbeReturn eos_probe_cb(GstPad* pad, GstPadProbeInfo* info, gpointer u_data) {
    gboolean ret = TRUE;
    GstEvent *event = GST_EVENT (info->data);

    static guint64 prev_accumulated_base = 0;
    static guint64 accumulated_base = 0;

    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
        GST_BUFFER_PTS(GST_BUFFER(info->data)) += prev_accumulated_base;
    }

    if ((info->type & GST_PAD_PROBE_TYPE_EVENT_BOTH)) {
        if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
            ret = gst_element_seek((GstElement*) u_data,
                                   1.0,
                                   GST_FORMAT_TIME,
                                   (GstSeekFlags)(GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_FLUSH),
                                   GST_SEEK_TYPE_SET,
                                   0,
                                   GST_SEEK_TYPE_NONE,
                                   GST_CLOCK_TIME_NONE);
            if (!ret) {
                g_print("###Error in seeking pipeline\n");
            }
            g_idle_add((GSourceFunc) source_switch_thread, u_data);
        }
    }

    if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
        GstSegment *segment;

        gst_event_parse_segment (event, (const GstSegment **) &segment);
        segment->base = accumulated_base;
        prev_accumulated_base = accumulated_base;
        accumulated_base += segment->stop;
    }

    switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    /* QOS events from downstream sink elements cause decoder to drop
     * frames after looping the file since the timestamps reset to 0.
     * We should drop the QOS events since we have custom logic for
     * looping individual sources. */
    case GST_EVENT_QOS:
    case GST_EVENT_SEGMENT:
        return GST_PAD_PROBE_DROP;
    default:
        break;
    }

    return GST_PAD_PROBE_OK;
}

static gboolean bus_call(GstBus* bus, GstMessage* msg, gpointer data) {
    GMainLoop* loop = (GMainLoop*) data;
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        g_print("End of stream\n");
        g_main_loop_quit(loop);
        break;
    case GST_MESSAGE_ERROR: {
        gchar* debug;
        GError* error;
        gst_message_parse_error(msg, &error, &debug);
        g_printerr("ERROR from element %s: %s\n", GST_OBJECT_NAME(msg->src), error->message);
        g_free(debug);
        g_printerr("Error: %s\n", error->message);
        g_error_free(error);
        g_main_loop_quit(loop);
        break;
    }
    default:
        break;
    }
    return TRUE;
}

int main(int argc, char* argv[]) {

    GstElement *pipeline = NULL, *source = NULL, *h264parser = NULL, *streammux = NULL,
                *decoder = NULL, *sink = NULL, *nvvidconv = NULL, *nvosd = NULL,
                 *pgie = NULL, *sgie1 = NULL, *sgie2 = NULL, *sgie3 = NULL;
#ifdef PLATFORM_TEGRA
    GstElement *transform = NULL;
#endif
    GstBus* bus = NULL;
    guint bus_watch_id;
    GstPad *dec_src_pad = NULL;

    /* Check input arguments */
    if (argc != 4) {
        g_printerr("Usage: %s <rows num> <columns num> <streams dir>\n", argv[0]);
        return -1;
    }

    get_file_list(argv[3]);

    /* Standard GStreamer initialization */
    gst_init(&argc, &argv);
    loop = g_main_loop_new(NULL, FALSE);

    /* Create gstreamer elements */
    /* Create Pipeline element that will form a connection of other elements */
    pipeline = gst_pipeline_new("perf-demo-pipeline");

    /* Source element for reading from the file */
    source = gst_element_factory_make("filesrc", "file-source");

    /* Since the data format in the input file is elementary h264 stream,
     * we need a h264parser */
    h264parser = gst_element_factory_make("h264parse", "h264-parser");

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

    /* We need three secondary gies so lets create 3 more instances of
       nvinfer */
    sgie1 = gst_element_factory_make ("nvinfer", "secondary1-nvinference-engine");
    sgie2 = gst_element_factory_make ("nvinfer", "secondary2-nvinference-engine");
    sgie3 = gst_element_factory_make ("nvinfer", "secondary3-nvinference-engine");

    /* Use convertor to convert from NV12 to RGBA as required by nvosd */
    nvvidconv = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter");

    /* Create OSD to draw on the converted RGBA buffer */
    nvosd = gst_element_factory_make ("nvdsosd", "nv-onscreendisplay");

    /* Finally render the osd output */
#ifdef PLATFORM_TEGRA
    transform = gst_element_factory_make ("nvegltransform", "nvegl-transform");
#endif
    sink = gst_element_factory_make ("nveglglessink", "nvvideo-renderer");

    /* caps filter for nvvidconv to convert NV12 to RGBA as nvosd expects input
     * in RGBA format */
    if (!source || !h264parser || !decoder || !nvvidconv || !pgie
            || !sgie1 || !sgie2 || !sgie3 || !nvosd || !sink) {
        g_printerr("One element could not be created. Exiting.\n");
        return -1;
    }

#ifdef PLATFORM_TEGRA
    if(!transform) {
        g_printerr ("One tegra element could not be created. Exiting.\n");
        return -1;
    }
#endif

    /* Set the input filename to the source element */
    g_object_set (G_OBJECT (source), "location", file_list[0].c_str(), NULL);

    g_object_set (G_OBJECT (streammux), "width", MUXER_OUTPUT_WIDTH, "height",
                  MUXER_OUTPUT_HEIGHT, "batch-size", 1,
                  "batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC, NULL);

    g_object_set (G_OBJECT(decoder), "gpu-id", GPU_ID, NULL);
    g_object_set (G_OBJECT(nvvidconv), "gpu-id", GPU_ID, NULL);
    g_object_set (G_OBJECT(nvosd), "gpu-id", GPU_ID, NULL);

    /* we set the osd properties here */
    g_object_set(G_OBJECT(nvosd), "font-size", 15, NULL);

    /* Set all the necessary properties of the nvinfer element,
     * the necessary ones are : */
    g_object_set (G_OBJECT (pgie), "config-file-path", PGIE_CONFIG_FILE, NULL);
    g_object_set (G_OBJECT (sgie1), "config-file-path", SGIE1_CONFIG_FILE, NULL);
    g_object_set (G_OBJECT (sgie2), "config-file-path", SGIE2_CONFIG_FILE, NULL);
    g_object_set (G_OBJECT (sgie3), "config-file-path", SGIE3_CONFIG_FILE, NULL);

    g_object_set (G_OBJECT(pgie), "gpu-id", GPU_ID, NULL);
    g_object_set (G_OBJECT(sgie1), "gpu-id", GPU_ID, NULL);
    g_object_set (G_OBJECT(sgie2), "gpu-id", GPU_ID, NULL);
    g_object_set (G_OBJECT(sgie3), "gpu-id", GPU_ID, NULL);

    g_object_set (G_OBJECT (sink), "sync", FALSE, "max-lateness", -1,
                  "async", FALSE, NULL);
    g_object_set (G_OBJECT(sink), "gpu-id", GPU_ID, NULL);
    g_object_set (G_OBJECT(sink), "rows", atoi(argv[1]), NULL);
    g_object_set (G_OBJECT(sink), "columns", atoi(argv[2]), NULL);

    /* we add a message handler */
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    /* Set up the pipeline */
    /* we add all elements into the pipeline */
#ifdef PLATFORM_TEGRA
    gst_bin_add_many (GST_BIN (pipeline),
                      source, h264parser, decoder, streammux, pgie, sgie1, sgie2, sgie3,
                      nvvidconv, nvosd, transform, sink, NULL);
#else
    gst_bin_add_many (GST_BIN (pipeline),
                      source, h264parser, decoder, streammux, pgie, sgie1, sgie2, sgie3,
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

    /* Link the elements together */
    if (!gst_element_link_many (source, h264parser, decoder, NULL)) {
        g_printerr ("Elements could not be linked: 1. Exiting.\n");
        return -1;
    }

#ifdef PLATFORM_TEGRA
    if (!gst_element_link_many (streammux, pgie, sgie1,
                                sgie2, sgie3, nvvidconv, nvosd, transform, sink, NULL)) {
        g_printerr ("Elements could not be linked. Exiting.\n");
        return -1;
    }
#else
    if (!gst_element_link_many (streammux, pgie, sgie1,
                                sgie2, sgie3, nvvidconv, nvosd, sink, NULL)) {
        g_printerr ("Elements could not be linked. Exiting.\n");
        return -1;
    }
#endif

    dec_src_pad = gst_element_get_static_pad(decoder, "sink");
    if (!dec_src_pad) {
        g_print("Unable to get h264parser src pad \n");
    } else {
        gst_pad_add_probe(dec_src_pad,
                          (GstPadProbeType) (GST_PAD_PROBE_TYPE_EVENT_BOTH |
                                             GST_PAD_PROBE_TYPE_EVENT_FLUSH |
                                             GST_PAD_PROBE_TYPE_BUFFER),
                          eos_probe_cb, pipeline, NULL);
        gst_object_unref(dec_src_pad);
    }
    /* Set the pipeline to "playing" state */
    g_print("Now playing: %s\n", argv[1]);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    /* Wait till pipeline encounters an error or EOS */
    g_print("Running...\n");
    g_main_loop_run(loop);

    /* Out of the main loop, clean up nicely */
    g_print("Returned, stopping playback\n");
    gst_element_set_state(pipeline, GST_STATE_NULL);
    g_print("Deleting pipeline\n");
    gst_object_unref(GST_OBJECT(pipeline));
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);
    return 0;
}
