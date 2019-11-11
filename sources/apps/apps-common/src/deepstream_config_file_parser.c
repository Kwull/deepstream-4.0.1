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

#include "deepstream_common.h"
#include "deepstream_config_file_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

GST_DEBUG_CATEGORY (APP_CFG_PARSER_CAT);

#define CONFIG_GPU_ID "gpu-id"
#define CONFIG_NVBUF_MEMORY_TYPE "nvbuf-memory-type"
#define CONFIG_GROUP_ENABLE "enable"


#define CONFIG_GROUP_SOURCE_TYPE "type"
#define CONFIG_GROUP_SOURCE_CAMERA_WIDTH "camera-width"
#define CONFIG_GROUP_SOURCE_CAMERA_HEIGHT "camera-height"
#define CONFIG_GROUP_SOURCE_CAMERA_FPS_N "camera-fps-n"
#define CONFIG_GROUP_SOURCE_CAMERA_FPS_D "camera-fps-d"
#define CONFIG_GROUP_SOURCE_CAMERA_CSI_SID "camera-csi-sensor-id"
#define CONFIG_GROUP_SOURCE_CAMERA_V4L2_DEVNODE "camera-v4l2-dev-node"
#define CONFIG_GROUP_SOURCE_URI "uri"
#define CONFIG_GROUP_SOURCE_LATENCY "latency"
#define CONFIG_GROUP_SOURCE_NUM_SOURCES "num-sources"
#define CONFIG_GROUP_SOURCE_INTRA_DECODE "intra-decode-enable"
#define CONFIG_GROUP_SOURCE_NUM_DECODE_SURFACES "num-decode-surfaces"
#define CONFIG_GROUP_SOURCE_NUM_EXTRA_SURFACES "num-extra-surfaces"
#define CONFIG_GROUP_SOURCE_DROP_FRAME_INTERVAL "drop-frame-interval"
#define CONFIG_GROUP_SOURCE_CAMERA_ID "camera-id"

#define CONFIG_GROUP_STREAMMUX_ENABLE_PADDING "enable-padding"
#define CONFIG_GROUP_STREAMMUX_WIDTH "width"
#define CONFIG_GROUP_STREAMMUX_HEIGHT "height"
#define CONFIG_GROUP_STREAMMUX_BATCH_SIZE "batch-size"
#define CONFIG_GROUP_STREAMMUX_BATCHED_PUSH_TIMEOUT "batched-push-timeout"
#define CONFIG_GROUP_STREAMMUX_LIVE_SOURCE "live-source"

#define CONFIG_GROUP_OSD_MODE "process-mode"
#define CONFIG_GROUP_OSD_BORDER_WIDTH "border-width"
#define CONFIG_GROUP_OSD_BORDER_COLOR "border-color"
#define CONFIG_GROUP_OSD_TEXT_SIZE "text-size"
#define CONFIG_GROUP_OSD_TEXT_COLOR "text-color"
#define CONFIG_GROUP_OSD_TEXT_BG_COLOR "text-bg-color"
#define CONFIG_GROUP_OSD_FONT "font"
#define CONFIG_GROUP_OSD_CLOCK_ENABLE "show-clock"
#define CONFIG_GROUP_OSD_CLOCK_X_OFFSET "clock-x-offset"
#define CONFIG_GROUP_OSD_CLOCK_Y_OFFSET "clock-y-offset"
#define CONFIG_GROUP_OSD_CLOCK_TEXT_SIZE "clock-text-size"
#define CONFIG_GROUP_OSD_CLOCK_COLOR "clock-color"

#define CONFIG_GROUP_DEWARPER_CONFIG_FILE "config-file"

#define CONFIG_GROUP_GIE_BATCH_SIZE "batch-size"
#define CONFIG_GROUP_GIE_MODEL_ENGINE "model-engine-file"
#define CONFIG_GROUP_GIE_CONFIG_FILE "config-file"
#define CONFIG_GROUP_GIE_LABEL "labelfile-path"
#define CONFIG_GROUP_GIE_UNIQUE_ID "gie-unique-id"
#define CONFIG_GROUP_GIE_ID_FOR_OPERATION "operate-on-gie-id"
#define CONFIG_GROUP_GIE_BBOX_BORDER_COLOR "bbox-border-color"
#define CONFIG_GROUP_GIE_BBOX_BG_COLOR "bbox-bg-color"
#define CONFIG_GROUP_GIE_CLASS_IDS_FOR_OPERATION "operate-on-class-ids"
#define CONFIG_GROUP_GIE_INTERVAL "interval"
#define CONFIG_GROUP_GIE_RAW_OUTPUT_DIR "infer-raw-output-dir"

#define CONFIG_GROUP_TRACKER_WIDTH "tracker-width"
#define CONFIG_GROUP_TRACKER_HEIGHT "tracker-height"
#define CONFIG_GROUP_TRACKER_ALGORITHM "tracker-algorithm"
#define CONFIG_GROUP_TRACKER_IOU_THRESHOLD "iou-threshold"
#define CONFIG_GROUP_TRACKER_SURFACE_TYPE "tracker-surface-type"
#define CONFIG_GROUP_TRACKER_LL_CONFIG_FILE "ll-config-file"
#define CONFIG_GROUP_TRACKER_LL_LIB_FILE "ll-lib-file"
#define CONFIG_GROUP_TRACKER_ENABLE_BATCH_PROCESS "enable-batch-process"

#define CONFIG_GROUP_SINK_TYPE "type"
#define CONFIG_GROUP_SINK_WIDTH "width"
#define CONFIG_GROUP_SINK_HEIGHT "height"
#define CONFIG_GROUP_SINK_SYNC "sync"
#define CONFIG_GROUP_SINK_QOS "qos"
#define CONFIG_GROUP_SINK_CONTAINER "container"
#define CONFIG_GROUP_SINK_CODEC "codec"
#define CONFIG_GROUP_SINK_BITRATE "bitrate"
#define CONFIG_GROUP_SINK_IFRAMEINTERVAL "iframeinterval"
#define CONFIG_GROUP_SINK_OUTPUT_FILE "output-file"
#define CONFIG_GROUP_SINK_SOURCE_ID "source-id"
#define CONFIG_GROUP_SINK_RTSP_PORT "rtsp-port"
#define CONFIG_GROUP_SINK_UDP_PORT "udp-port"
#define CONFIG_GROUP_SINK_DISPLAY_ID "display-id"
#define CONFIG_GROUP_SINK_OVERLAY_ID "overlay-id"
#define CONFIG_GROUP_SINK_OFFSET_X "offset-x"
#define CONFIG_GROUP_SINK_OFFSET_Y "offset-y"

#define CONFIG_GROUP_SINK_MSG_CONV_CONFIG "msg-conv-config"
#define CONFIG_GROUP_SINK_MSG_CONV_PAYLOAD_TYPE "msg-conv-payload-type"
#define CONFIG_GROUP_SINK_MSG_CONV_MSG2P_LIB "msg-conv-msg2p-lib"
#define CONFIG_GROUP_SINK_MSG_CONV_COMP_ID "msg-conv-comp-id"

#define CONFIG_GROUP_SINK_MSG_BROKER_PROTO_LIB "msg-broker-proto-lib"
#define CONFIG_GROUP_SINK_MSG_BROKER_CONN_STR "msg-broker-conn-str"
#define CONFIG_GROUP_SINK_MSG_BROKER_TOPIC "topic"
#define CONFIG_GROUP_SINK_MSG_BROKER_CONFIG_FILE "msg-broker-config"
#define CONFIG_GROUP_SINK_MSG_BROKER_COMP_ID "msg-broker-comp-id"

#define CONFIG_GROUP_TILED_DISPLAY_ROWS "rows"
#define CONFIG_GROUP_TILED_DISPLAY_COLUMNS "columns"
#define CONFIG_GROUP_TILED_DISPLAY_WIDTH "width"
#define CONFIG_GROUP_TILED_DISPLAY_HEIGHT "height"

// To add configuration parsing for any element, you need to:
// 1. Define a group name and set of key strings for the config options
// 2. Create a function to parse these configs (refer parse_dsexample)
// 3. Call this function in

// Add group name for set of configs of dsexample element
#define CONFIG_GROUP_DSEXAMPLE "ds-example"
// Refer to gst-dsexample element source code for the meaning of these
// configs
#define CONFIG_GROUP_DSEXAMPLE_FULL_FRAME "full-frame"
#define CONFIG_GROUP_DSEXAMPLE_PROCESSING_WIDTH "processing-width"
#define CONFIG_GROUP_DSEXAMPLE_PROCESSING_HEIGHT "processing-height"
#define CONFIG_GROUP_DSEXAMPLE_UNIQUE_ID "unique-id"
#define CONFIG_GROUP_DSEXAMPLE_GPU_ID "gpu-id"

#define CHECK_ERROR(error) \
    if (error) { \
        GST_CAT_ERROR (APP_CFG_PARSER_CAT, "%s", error->message); \
        goto done; \
    }

#define N_DECODE_SURFACES 16
#define N_EXTRA_SURFACES 1
gchar *
get_absolute_file_path (gchar *cfg_file_path, gchar *file_path)
{
  gchar abs_cfg_path[PATH_MAX + 1];
  gchar *abs_file_path;
  gchar *delim;

  if (file_path && file_path[0] == '/') {
    return file_path;
  }

  if (!realpath (cfg_file_path, abs_cfg_path)) {
    g_free (file_path);
    return NULL;
  }

  // Return absolute path of config file if file_path is NULL.
  if (!file_path) {
    abs_file_path = g_strdup (abs_cfg_path);
    return abs_file_path;
  }

  delim = g_strrstr (abs_cfg_path, "/");
  *(delim + 1) = '\0';

  abs_file_path = g_strconcat (abs_cfg_path, file_path, NULL);
  g_free (file_path);

  return abs_file_path;
}

/**
 * Function to parse class label file. Parses the labels into a 2D-array of
 * strings. Refer the SDK documentation for format of the labels file.
 *
 * @param[in] config pointer to @ref NvDsGieConfig
 *
 * @return true if file parsed successfully else returns false.
 */
static gboolean
parse_labels_file (NvDsGieConfig *config)
{
  GList *labels_list = NULL;
  GList *label_outputs_length_list = NULL;
  GIOChannel *label_file = NULL;
  GError *err = NULL;
  gsize term_pos;
  GIOStatus status;
  gboolean ret = FALSE;
  GList *labels_iter;
  GList *label_outputs_length_iter;
  guint j;

  label_file =
    g_io_channel_new_file (GET_FILE_PATH (config->label_file_path), "r",
        &err);

  if (err) {
    NVGSTDS_ERR_MSG_V ("Failed to open label file '%s':%s",
        config->label_file_path, err->message);
    goto done;
  }

  /* Iterate over each line */
  do {
    gchar *temp;
    gchar *line_str = NULL;
    gchar *iter;
    GList *label_outputs_list = NULL;
    guint label_outputs_length = 0;
    GList *list_iter;
    guint i;
    gchar **label_outputs;

    /* Read the line into `line_str` char array */
    status =
      g_io_channel_read_line (label_file,
          (gchar **) &line_str, NULL, &term_pos, &err);

    if (line_str == NULL)
      continue;

    temp = line_str;

    temp[term_pos] = '\0';

    /* Parse ';' delimited strings and prepend to the `labels_output_list`.
     * Prepending to a list and reversing after adding all strings is faster
     * than appending every string.
     * https://developer.gnome.org/glib/stable/glib-Doubly-Linked-Lists.html#g-list-append
     */
    while ((iter = g_strstr_len (temp, -1, ";"))) {
      *iter = '\0';
      label_outputs_list = g_list_prepend (label_outputs_list, g_strdup (temp));
      label_outputs_length++;
      temp = iter + 1;
    }

    if (*temp != '\0') {
      label_outputs_list = g_list_prepend (label_outputs_list, g_strdup (temp));
      label_outputs_length++;
    }

    /* All labels in one line parsed and added. Now reverse the list. */
    label_outputs_list = g_list_reverse (label_outputs_list);
    list_iter = label_outputs_list;

    /* Convert the list to an array for faster access. */
    label_outputs = (gchar **) g_malloc0(sizeof(gchar *) * label_outputs_length);
    for (i = 0; i < label_outputs_length; i++) {
      label_outputs[i] = list_iter->data;
      list_iter = list_iter->next;
    }
    g_list_free (label_outputs_list);

    /* Prepend the pointer to array of labels in one line to `labels_list`.*/
    labels_list = g_list_prepend (labels_list, label_outputs);
    /* Prepend the corresponding array size to `label_outputs_length_list`.*/
    label_outputs_length_list =
      g_list_prepend (label_outputs_length_list,
          (gchar *) NULL + label_outputs_length);

    /* Maintain the number of labels(lines). */
    config->n_labels++;
    g_free (line_str);

  } while (status == G_IO_STATUS_NORMAL);
  g_io_channel_unref (label_file);
  ret = TRUE;

  /* Convert the `labels_list` and the `label_outputs_length_list` to arrays
   * for faster access. */
  config->n_label_outputs = g_malloc (config->n_labels * sizeof (guint));
  config->labels = g_malloc (config->n_labels * sizeof (gchar **));

  labels_list = g_list_reverse (labels_list);
  label_outputs_length_list = g_list_reverse (label_outputs_length_list);

  labels_iter = labels_list;
  label_outputs_length_iter = label_outputs_length_list;
  for (j = 0; j < config->n_labels; j++) {
    config->labels[j] = labels_iter->data;
    labels_iter = labels_iter->next;
    config->n_label_outputs[j] = (gchar *) label_outputs_length_iter->data - (gchar *) NULL;
    label_outputs_length_iter = label_outputs_length_iter->next;
  }

  g_list_free (labels_list);
  g_list_free (label_outputs_length_list);

done:
  return ret;
}

gboolean
parse_source (NvDsSourceConfig *config, GKeyFile *key_file, gchar *group, gchar *cfg_file_path)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;
  static GList *camera_id_list = NULL;

  gchar *source_id_start_ptr = group + strlen(CONFIG_GROUP_SOURCE);
  gchar *source_id_end_ptr = NULL;
  config->camera_id = g_ascii_strtoull (source_id_start_ptr, &source_id_end_ptr, 10);

  // Source group name should be of the form [source<%u>]. If
  // *source_id_end_ptr is not the string terminating character '\0' or if
  // the pointer has the same value as source_id_start_ptr, then the group
  // name does not conform to the specs.
  if (source_id_start_ptr == source_id_end_ptr || *source_id_end_ptr != '\0') {
    NVGSTDS_ERR_MSG_V ("Source group \"[%s]\" is not in the form \"[source<%%d>]\"",
        group);
    return FALSE;
  }

  // Check if a source with same source_id has already been parsed.
  if (g_list_find (camera_id_list, GUINT_TO_POINTER(config->camera_id)) != NULL) {
    NVGSTDS_ERR_MSG_V ("Did not parse source group \"[%s]\". Another source group"
        " with source-id %d already exists", group, config->camera_id);
    return FALSE;
  }
  camera_id_list = g_list_prepend (camera_id_list,
          GUINT_TO_POINTER(config->camera_id));

  keys = g_key_file_get_keys (key_file, group, NULL, &error);
  CHECK_ERROR (error);
  config->latency = 100;
  config->num_decode_surfaces = N_DECODE_SURFACES;
  config->num_extra_surfaces = N_EXTRA_SURFACES;
  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_TYPE)) {
      config->type =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_TYPE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_ENABLE)) {
      config->enable =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_ENABLE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_CAMERA_WIDTH)) {
      config->source_width =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_CAMERA_WIDTH, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_CAMERA_HEIGHT)) {
      config->source_height =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_CAMERA_HEIGHT, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_CAMERA_FPS_N)) {
      config->source_fps_n =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_CAMERA_FPS_N, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_CAMERA_FPS_D)) {
      config->source_fps_d =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_CAMERA_FPS_D, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_CAMERA_CSI_SID)) {
      config->camera_csi_sensor_id =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_CAMERA_CSI_SID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_CAMERA_V4L2_DEVNODE)) {
      config->camera_v4l2_dev_node =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_CAMERA_V4L2_DEVNODE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_URI)) {
      gchar *uri =
          g_key_file_get_string (key_file, group,
          CONFIG_GROUP_SOURCE_URI, &error);
      CHECK_ERROR (error);
      if (g_str_has_prefix (uri, "file://")) {
        config->uri = g_strdup (uri + 7);
        config->uri = g_strdup_printf ("file://%s", get_absolute_file_path (cfg_file_path, config->uri));
        g_free (uri);
      } else
        config->uri = uri;
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_LATENCY)) {
      config->latency =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_LATENCY, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_NUM_SOURCES)) {
      config->num_sources =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_NUM_SOURCES, &error);
      CHECK_ERROR (error);
      if (config->num_sources < 1) {
        config->num_sources = 1;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GPU_ID)) {
      config->gpu_id =
          g_key_file_get_integer (key_file, group,
          CONFIG_GPU_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_NUM_DECODE_SURFACES)) {
      config->num_decode_surfaces =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_NUM_DECODE_SURFACES, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_NUM_EXTRA_SURFACES)) {
      config->num_extra_surfaces =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_NUM_EXTRA_SURFACES, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_DROP_FRAME_INTERVAL)) {
      config->drop_frame_interval =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_DROP_FRAME_INTERVAL, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_CAMERA_ID)) {
      config->camera_id =
          g_key_file_get_integer (key_file, group,
              CONFIG_GROUP_SOURCE_CAMERA_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SOURCE_INTRA_DECODE)) {
      config->Intra_decode =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SOURCE_INTRA_DECODE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, "cudadec-memtype")) {
      config->cuda_memory_type =
          g_key_file_get_integer (key_file, group,
          "cudadec-memtype", &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_NVBUF_MEMORY_TYPE)) {
      config->nvbuf_memory_type =
          g_key_file_get_integer (key_file, group,
          CONFIG_NVBUF_MEMORY_TYPE, &error);
      CHECK_ERROR (error);
    }
    else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key,
                           group);
    }
  }

  ret = TRUE;
done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}


gboolean
parse_streammux (NvDsStreammuxConfig *config, GKeyFile *key_file)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_STREAMMUX, NULL, &error);
  CHECK_ERROR (error);

  config->batched_push_timeout = -1;
  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_STREAMMUX_WIDTH)) {
      config->pipeline_width =
        g_key_file_get_integer (key_file, CONFIG_GROUP_STREAMMUX,
            CONFIG_GROUP_STREAMMUX_WIDTH, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_STREAMMUX_HEIGHT)) {
      config->pipeline_height =
        g_key_file_get_integer (key_file, CONFIG_GROUP_STREAMMUX,
            CONFIG_GROUP_STREAMMUX_HEIGHT, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GPU_ID)) {
      config->gpu_id =
        g_key_file_get_integer (key_file, CONFIG_GROUP_STREAMMUX,
            CONFIG_GPU_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0(*key, CONFIG_GROUP_STREAMMUX_ENABLE_PADDING)) {
      config->enable_padding =
          g_key_file_get_integer(key_file, CONFIG_GROUP_STREAMMUX,
          CONFIG_GROUP_STREAMMUX_ENABLE_PADDING, &error);
      CHECK_ERROR(error);
    } else if (!g_strcmp0(*key, CONFIG_GROUP_STREAMMUX_BATCH_SIZE)) {
      config->batch_size =
          g_key_file_get_integer(key_file, CONFIG_GROUP_STREAMMUX,
          CONFIG_GROUP_STREAMMUX_BATCH_SIZE, &error);
      CHECK_ERROR(error);
    } else if (!g_strcmp0(*key, CONFIG_GROUP_STREAMMUX_LIVE_SOURCE)) {
      config->live_source =
          g_key_file_get_integer(key_file, CONFIG_GROUP_STREAMMUX,
          CONFIG_GROUP_STREAMMUX_LIVE_SOURCE, &error);
      CHECK_ERROR(error);
    } else if (!g_strcmp0(*key, CONFIG_GROUP_STREAMMUX_BATCHED_PUSH_TIMEOUT)) {
      config->batched_push_timeout =
          g_key_file_get_integer(key_file, CONFIG_GROUP_STREAMMUX,
          CONFIG_GROUP_STREAMMUX_BATCHED_PUSH_TIMEOUT, &error);
      CHECK_ERROR(error);
    } else if (!g_strcmp0 (*key, CONFIG_NVBUF_MEMORY_TYPE)) {
      config->nvbuf_memory_type =
          g_key_file_get_integer (key_file, CONFIG_GROUP_STREAMMUX,
          CONFIG_NVBUF_MEMORY_TYPE, &error);
      CHECK_ERROR (error);
    }else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key,
          CONFIG_GROUP_STREAMMUX);
    }
  }

  config->is_parsed = TRUE;

  ret = TRUE;
done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}


gboolean
parse_dsexample (NvDsDsExampleConfig *config, GKeyFile *key_file)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_DSEXAMPLE, NULL, &error);
  CHECK_ERROR (error);
  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_ENABLE)) {
      config->enable =
        g_key_file_get_integer (key_file, CONFIG_GROUP_DSEXAMPLE,
            CONFIG_GROUP_ENABLE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_DSEXAMPLE_FULL_FRAME)) {
      config->full_frame =
        g_key_file_get_integer (key_file, CONFIG_GROUP_DSEXAMPLE,
            CONFIG_GROUP_DSEXAMPLE_FULL_FRAME, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_DSEXAMPLE_PROCESSING_WIDTH)) {
      config->processing_width =
        g_key_file_get_integer (key_file, CONFIG_GROUP_DSEXAMPLE,
            CONFIG_GROUP_DSEXAMPLE_PROCESSING_WIDTH, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_DSEXAMPLE_PROCESSING_HEIGHT)) {
      config->processing_height =
        g_key_file_get_integer (key_file, CONFIG_GROUP_DSEXAMPLE,
            CONFIG_GROUP_DSEXAMPLE_PROCESSING_HEIGHT, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_DSEXAMPLE_UNIQUE_ID)) {
      config->unique_id =
        g_key_file_get_integer (key_file, CONFIG_GROUP_DSEXAMPLE,
            CONFIG_GROUP_DSEXAMPLE_UNIQUE_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_DSEXAMPLE_GPU_ID)) {
      config->gpu_id =
        g_key_file_get_integer (key_file, CONFIG_GROUP_DSEXAMPLE,
            CONFIG_GROUP_DSEXAMPLE_GPU_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_NVBUF_MEMORY_TYPE)) {
      config->nvbuf_memory_type =
          g_key_file_get_integer (key_file, CONFIG_GROUP_DSEXAMPLE,
          CONFIG_NVBUF_MEMORY_TYPE, &error);
      CHECK_ERROR (error);
    } else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key,
          CONFIG_GROUP_DSEXAMPLE);
    }
  }

  ret = TRUE;
done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

gboolean
parse_osd (NvDsOSDConfig *config, GKeyFile *key_file)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_OSD, NULL, &error);
  CHECK_ERROR (error);
  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_ENABLE)) {
      config->enable =
          g_key_file_get_integer (key_file, CONFIG_GROUP_OSD,
          CONFIG_GROUP_ENABLE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_OSD_MODE)) {
      config->mode =
          g_key_file_get_integer (key_file, CONFIG_GROUP_OSD,
          CONFIG_GROUP_OSD_MODE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_OSD_BORDER_WIDTH)) {
      config->border_width =
          g_key_file_get_integer (key_file, CONFIG_GROUP_OSD,
          CONFIG_GROUP_OSD_BORDER_WIDTH, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_OSD_TEXT_SIZE)) {
      config->text_size =
          g_key_file_get_integer (key_file, CONFIG_GROUP_OSD,
          CONFIG_GROUP_OSD_TEXT_SIZE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_OSD_TEXT_COLOR)) {
      gsize length;
      gdouble *list = g_key_file_get_double_list (key_file, CONFIG_GROUP_OSD,
          CONFIG_GROUP_OSD_TEXT_COLOR, &length, &error);
      CHECK_ERROR (error);
      if (length != 4) {
        NVGSTDS_ERR_MSG_V
            ("Color params should be exactly 4 floats {r, g, b, a} between 0 and 1");
        goto done;
      }
      config->text_color.red = list[0];
      config->text_color.green = list[1];
      config->text_color.blue = list[2];
      config->text_color.alpha = list[3];
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_OSD_TEXT_BG_COLOR)) {
      gsize length;
      gdouble *list = g_key_file_get_double_list (key_file, CONFIG_GROUP_OSD,
          CONFIG_GROUP_OSD_TEXT_BG_COLOR, &length, &error);
      CHECK_ERROR (error);
      if (length != 4) {
        NVGSTDS_ERR_MSG_V
            ("Color params should be exactly 4 floats {r, g, b, a} between 0 and 1");
        goto done;
      }
      config->text_bg_color.red = list[0];
      config->text_bg_color.green = list[1];
      config->text_bg_color.blue = list[2];
      config->text_bg_color.alpha = list[3];

      if (config->text_bg_color.red > 0 || config->text_bg_color.green > 0
          || config->text_bg_color.blue > 0 || config->text_bg_color.alpha > 0)
        config->text_has_bg = TRUE;
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_OSD_FONT)) {
      config->font =
          g_key_file_get_string (key_file, CONFIG_GROUP_OSD,
          CONFIG_GROUP_OSD_FONT, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_OSD_CLOCK_ENABLE)) {
      config->enable_clock =
          g_key_file_get_integer (key_file, CONFIG_GROUP_OSD,
          CONFIG_GROUP_OSD_CLOCK_ENABLE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_OSD_CLOCK_X_OFFSET)) {
      config->clock_x_offset =
          g_key_file_get_integer (key_file, CONFIG_GROUP_OSD,
          CONFIG_GROUP_OSD_CLOCK_X_OFFSET, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_OSD_CLOCK_Y_OFFSET)) {
      config->clock_y_offset =
          g_key_file_get_integer (key_file, CONFIG_GROUP_OSD,
          CONFIG_GROUP_OSD_CLOCK_Y_OFFSET, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_OSD_CLOCK_TEXT_SIZE)) {
      config->clock_text_size =
          g_key_file_get_integer (key_file, CONFIG_GROUP_OSD,
          CONFIG_GROUP_OSD_CLOCK_TEXT_SIZE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_NVBUF_MEMORY_TYPE)) {
      config->nvbuf_memory_type =
          g_key_file_get_integer (key_file, CONFIG_GROUP_OSD,
          CONFIG_NVBUF_MEMORY_TYPE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_OSD_CLOCK_COLOR)) {
      gsize length;
      gdouble *list = g_key_file_get_double_list (key_file, CONFIG_GROUP_OSD,
          CONFIG_GROUP_OSD_CLOCK_COLOR, &length, &error);
      CHECK_ERROR (error);
      if (length != 4) {
        NVGSTDS_ERR_MSG_V
            ("Color params should be exactly 4 floats {r, g, b, a} between 0 and 1");
        goto done;
      }
      config->clock_color.red = list[0];
      config->clock_color.green = list[1];
      config->clock_color.blue = list[2];
      config->clock_color.alpha = list[3];
    } else if (!g_strcmp0 (*key, CONFIG_GPU_ID)) {
      config->gpu_id =
          g_key_file_get_integer (key_file, CONFIG_GROUP_OSD,
          CONFIG_GPU_ID, &error);
      CHECK_ERROR (error);
    } else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key,
          CONFIG_GROUP_OSD);
    }
  }

  ret = TRUE;
done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

gboolean
parse_dewarper (NvDsDewarperConfig * config, GKeyFile * key_file, gchar *cfg_file_path)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_DEWARPER, NULL, &error);
  CHECK_ERROR (error);
  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_ENABLE)) {
      config->enable =
          g_key_file_get_integer (key_file, CONFIG_GROUP_DEWARPER,
          CONFIG_GROUP_ENABLE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GPU_ID)) {
      config->gpu_id =
          g_key_file_get_integer (key_file, CONFIG_GROUP_DEWARPER,
          CONFIG_GPU_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_DEWARPER_CONFIG_FILE)) {
      config->config_file = get_absolute_file_path (cfg_file_path,
                g_key_file_get_string (key_file,
                    CONFIG_GROUP_DEWARPER,
                    CONFIG_GROUP_DEWARPER_CONFIG_FILE, &error));
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_NVBUF_MEMORY_TYPE)) {
      config->nvbuf_memory_type =
          g_key_file_get_integer (key_file, CONFIG_GROUP_DEWARPER,
          CONFIG_NVBUF_MEMORY_TYPE, &error);
      CHECK_ERROR (error);
    }
    else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key,
          CONFIG_GROUP_DEWARPER);
    }
  }

  ret = TRUE;
done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

gboolean
parse_gie (NvDsGieConfig *config, GKeyFile *key_file, gchar *group, gchar *cfg_file_path)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;

  config->bbox_border_color_table = g_hash_table_new (NULL, NULL);
  config->bbox_bg_color_table = g_hash_table_new (NULL, NULL);
  config->bbox_border_color = (NvOSD_ColorParams) {1, 0, 0, 1};

  keys = g_key_file_get_keys (key_file, group, NULL, &error);
  CHECK_ERROR (error);
  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_ENABLE)) {
      config->enable =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_ENABLE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_GIE_CLASS_IDS_FOR_OPERATION)) {
      gsize length;
      config->list_operate_on_class_ids = g_key_file_get_integer_list (key_file, group,
          CONFIG_GROUP_GIE_CLASS_IDS_FOR_OPERATION, &length, &error);
      config->num_operate_on_class_ids = length;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_GIE_BATCH_SIZE)) {
      config->batch_size =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_GIE_BATCH_SIZE, &error);
      config->is_batch_size_set = TRUE;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_GIE_MODEL_ENGINE)) {
      config->model_engine_file_path = get_absolute_file_path (cfg_file_path,
          g_key_file_get_string (key_file, group,
          CONFIG_GROUP_GIE_MODEL_ENGINE, &error));
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_GIE_LABEL)) {
      config->label_file_path = get_absolute_file_path (cfg_file_path,
          g_key_file_get_string (key_file, group,
          CONFIG_GROUP_GIE_LABEL, &error));
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_GIE_CONFIG_FILE)) {
      config->config_file_path = get_absolute_file_path (cfg_file_path,
          g_key_file_get_string (key_file, group,
          CONFIG_GROUP_GIE_CONFIG_FILE, &error));
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_GIE_INTERVAL)) {
      config->interval =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_GIE_INTERVAL, &error);
      config->is_interval_set = TRUE;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_GIE_UNIQUE_ID)) {
      config->unique_id =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_GIE_UNIQUE_ID, &error);
      config->is_unique_id_set = TRUE;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_GIE_ID_FOR_OPERATION)) {
      config->operate_on_gie_id =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_GIE_ID_FOR_OPERATION, &error);
      config->is_operate_on_gie_id_set = TRUE;
      CHECK_ERROR (error);
    } else if (!strncmp (*key, CONFIG_GROUP_GIE_BBOX_BORDER_COLOR,
            sizeof (CONFIG_GROUP_GIE_BBOX_BORDER_COLOR) - 1)) {
      NvOSD_ColorParams *clr_params;
      gchar *key1 =
          *key + sizeof (CONFIG_GROUP_GIE_BBOX_BORDER_COLOR) - 1;
      gchar *endptr;
      gint64 class_index = -1;

      /* Check if the key is specified for a particular class or for all classes.
       * For generic key "bbox-border-color", strlen (key1) will return 0 and·
       * class_index will be -1.
       * For class-specific key "bbox-border-color<class-id>", strlen (key1)
       * will return a positive value and·class_index will have a value >= 0.
       */
      if (strlen (key1) > 0) {
        class_index = g_ascii_strtoll (key1, &endptr, 10);
        if (class_index == 0 && endptr == key1) {
          NVGSTDS_WARN_MSG_V ("BBOX colors should be specified with key '%s%%d'",
              CONFIG_GROUP_GIE_BBOX_BORDER_COLOR);
          continue;
        }
      }

      gsize length;
      gdouble *list = g_key_file_get_double_list (key_file, group,
          *key, &length, &error);
      CHECK_ERROR (error);
      if (length != 4) {
        NVGSTDS_ERR_MSG_V
            ("Number of Color params should be exactly 4 "
            "floats {r, g, b, a} between 0 and 1");
        goto done;
      }

      if (class_index == -1) {
        clr_params = &config->bbox_border_color;
      } else {
        clr_params = g_malloc (sizeof (NvOSD_ColorParams));
        g_hash_table_insert (config->bbox_border_color_table,
            class_index + (gchar *) NULL, clr_params);
      }

      clr_params->red = list[0];
      clr_params->green = list[1];
      clr_params->blue = list[2];
      clr_params->alpha = list[3];

    } else if (!strncmp (*key, CONFIG_GROUP_GIE_BBOX_BG_COLOR,
            sizeof (CONFIG_GROUP_GIE_BBOX_BG_COLOR) - 1)) {
      NvOSD_ColorParams *clr_params;
      gchar *key1 =
          *key + sizeof (CONFIG_GROUP_GIE_BBOX_BG_COLOR) - 1;
      gchar *endptr;
      gint64 class_index = -1;

      /* Check if the key is specified for a particular class or for all classes.
       * For generic key "bbox-bg-color", strlen (key1) will return 0 and·
       * class_index will be -1.
       * For class-specific key "bbox-bg-color<class-id>", strlen (key1)
       * will return a positive value and·class_index will have a value >= 0.
       */
      if (strlen (key1) > 0) {
        class_index = g_ascii_strtoll (key1, &endptr, 10);
        if (class_index == 0 && endptr == key1) {
          NVGSTDS_WARN_MSG_V ("BBOX background colors should be specified with key '%s%%d'",
              CONFIG_GROUP_GIE_BBOX_BG_COLOR);
          continue;
        }
      }

      gsize length;
      gdouble *list = g_key_file_get_double_list (key_file, group,
          *key, &length, &error);
      CHECK_ERROR (error);
      if (length != 4) {
        NVGSTDS_ERR_MSG_V
            ("Number of Color params should be exactly 4 "
            "floats {r, g, b, a} between 0 and 1");
        goto done;
      }

      if (class_index == -1) {
        clr_params = &config->bbox_bg_color;
        config->have_bg_color = TRUE;
      } else {
        clr_params = g_malloc (sizeof (NvOSD_ColorParams));
        g_hash_table_insert (config->bbox_bg_color_table, class_index + (gchar *) NULL,
            clr_params);
      }

      clr_params->red = list[0];
      clr_params->green = list[1];
      clr_params->blue = list[2];
      clr_params->alpha = list[3];
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_GIE_RAW_OUTPUT_DIR)) {
      config->raw_output_directory = get_absolute_file_path (cfg_file_path,
          g_key_file_get_string (key_file, group,
          CONFIG_GROUP_GIE_RAW_OUTPUT_DIR, &error));
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GPU_ID)) {
      config->gpu_id =
          g_key_file_get_integer (key_file, group,
          CONFIG_GPU_ID, &error);
      config->is_gpu_id_set = TRUE;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_NVBUF_MEMORY_TYPE)) {
      config->nvbuf_memory_type =
          g_key_file_get_integer (key_file, group,
          CONFIG_NVBUF_MEMORY_TYPE, &error);
      CHECK_ERROR (error);
    } else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key, group);
    }
  }
  if (config->enable && config->label_file_path && !parse_labels_file (config)) {
    NVGSTDS_ERR_MSG_V ("Failed while parsing label file '%s'", config->label_file_path);
    goto done;
  }
  if (!config->config_file_path) {
    NVGSTDS_ERR_MSG_V ("Config file not provided for group '%s'", group);
    goto done;
  }

  ret = TRUE;
done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

gboolean
parse_tracker (NvDsTrackerConfig *config, GKeyFile *key_file, gchar* cfg_file_path)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_TRACKER, NULL, &error);
  CHECK_ERROR (error);

  config->batch_config_set = FALSE;
  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_ENABLE)) {
      config->enable =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TRACKER,
          CONFIG_GROUP_ENABLE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TRACKER_WIDTH)) {
      config->width =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TRACKER,
          CONFIG_GROUP_TRACKER_WIDTH, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TRACKER_HEIGHT)) {
      config->height =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TRACKER,
          CONFIG_GROUP_TRACKER_HEIGHT, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GPU_ID)) {
      config->gpu_id =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TRACKER,
          CONFIG_GPU_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TRACKER_SURFACE_TYPE)) {
      config->tracking_surf_type =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TRACKER,
          CONFIG_GROUP_TRACKER_SURFACE_TYPE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TRACKER_LL_CONFIG_FILE)) {
      config->ll_config_file = get_absolute_file_path (cfg_file_path,
                g_key_file_get_string (key_file,
                    CONFIG_GROUP_TRACKER,
                    CONFIG_GROUP_TRACKER_LL_CONFIG_FILE, &error));
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TRACKER_LL_LIB_FILE)) {
      config->ll_lib_file = get_absolute_file_path (cfg_file_path,
                g_key_file_get_string (key_file,
                    CONFIG_GROUP_TRACKER,
                    CONFIG_GROUP_TRACKER_LL_LIB_FILE, &error));
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TRACKER_ENABLE_BATCH_PROCESS)) {
      config->enable_batch_process =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TRACKER,
          CONFIG_GROUP_TRACKER_ENABLE_BATCH_PROCESS, &error);
      CHECK_ERROR (error);
      config->batch_config_set = TRUE;
    } else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key,
          CONFIG_GROUP_TRACKER);
    }
  }

  ret = TRUE;
done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

gboolean
parse_sink (NvDsSinkSubBinConfig *config, GKeyFile *key_file, gchar *group, gchar * cfg_file_path)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;

  config->encoder_config.rtsp_port = 8554;
  config->encoder_config.udp_port = 5000;
  config->render_config.qos = TRUE;

  keys = g_key_file_get_keys (key_file, group, NULL, &error);
  CHECK_ERROR (error);
  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_ENABLE)) {
      config->enable =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_ENABLE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_TYPE)) {
      config->type =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_TYPE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_WIDTH)) {
      config->render_config.width =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_WIDTH, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_HEIGHT)) {
      config->render_config.height =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_HEIGHT, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_QOS)) {
      config->render_config.qos =
          g_key_file_get_boolean (key_file, group,
          CONFIG_GROUP_SINK_QOS, &error);
      config->render_config.qos_value_specified = TRUE;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_SYNC)) {
      config->render_config.sync =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_SYNC, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_NVBUF_MEMORY_TYPE)) {
      config->render_config.nvbuf_memory_type =
          g_key_file_get_integer (key_file, group,
          CONFIG_NVBUF_MEMORY_TYPE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_CONTAINER)) {
      config->encoder_config.container =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_CONTAINER, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_CODEC)) {
      config->encoder_config.codec =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_CODEC, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_BITRATE)) {
      config->encoder_config.bitrate =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_BITRATE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_IFRAMEINTERVAL)) {
      config->encoder_config.iframeinterval =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_IFRAMEINTERVAL, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_OUTPUT_FILE)) {
      config->encoder_config.output_file_path =
          g_key_file_get_string (key_file, group,
          CONFIG_GROUP_SINK_OUTPUT_FILE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_SOURCE_ID)) {
      config->source_id =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_SOURCE_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_RTSP_PORT)) {
      config->encoder_config.rtsp_port =
          g_key_file_get_integer (key_file, group,
              CONFIG_GROUP_SINK_RTSP_PORT, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_UDP_PORT)) {
      config->encoder_config.udp_port =
          g_key_file_get_integer (key_file, group,
              CONFIG_GROUP_SINK_UDP_PORT, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_OVERLAY_ID)) {
      config->render_config.overlay_id =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_OVERLAY_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_OFFSET_X)) {
      config->render_config.offset_x =
        g_key_file_get_integer (key_file, group,
            CONFIG_GROUP_SINK_OFFSET_X, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_OFFSET_Y)) {
      config->render_config.offset_y =
        g_key_file_get_integer (key_file, group,
            CONFIG_GROUP_SINK_OFFSET_Y, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_DISPLAY_ID)) {
      config->render_config.display_id =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_DISPLAY_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GPU_ID)) {
      config->encoder_config.gpu_id = config->render_config.gpu_id =
          g_key_file_get_integer (key_file, group,
          CONFIG_GPU_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_MSG_CONV_CONFIG)) {
      config->msg_conv_broker_config.config_file_path =
          get_absolute_file_path(cfg_file_path, g_key_file_get_string (key_file, group,
          CONFIG_GROUP_SINK_MSG_CONV_CONFIG, &error));
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_MSG_CONV_PAYLOAD_TYPE)) {
      config->msg_conv_broker_config.conv_payload_type =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_MSG_CONV_PAYLOAD_TYPE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_MSG_CONV_MSG2P_LIB)) {
      config->msg_conv_broker_config.conv_msg2p_lib =
          get_absolute_file_path(cfg_file_path, g_key_file_get_string (key_file, group,
          CONFIG_GROUP_SINK_MSG_CONV_MSG2P_LIB, &error));
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_MSG_CONV_COMP_ID)) {
      config->msg_conv_broker_config.conv_comp_id =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_MSG_CONV_COMP_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_MSG_BROKER_PROTO_LIB)) {
      config->msg_conv_broker_config.proto_lib =
          g_key_file_get_string (key_file, group,
          CONFIG_GROUP_SINK_MSG_BROKER_PROTO_LIB, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_MSG_BROKER_CONN_STR)) {
      config->msg_conv_broker_config.conn_str =
          g_key_file_get_string (key_file, group,
          CONFIG_GROUP_SINK_MSG_BROKER_CONN_STR, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_MSG_BROKER_TOPIC)) {
      config->msg_conv_broker_config.topic =
          g_key_file_get_string (key_file, group,
          CONFIG_GROUP_SINK_MSG_BROKER_TOPIC, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_MSG_BROKER_CONFIG_FILE)) {
      config->msg_conv_broker_config.broker_config_file_path =
          get_absolute_file_path(cfg_file_path, g_key_file_get_string (key_file, group,
          CONFIG_GROUP_SINK_MSG_BROKER_CONFIG_FILE, &error));
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SINK_MSG_BROKER_COMP_ID)) {
      config->msg_conv_broker_config.broker_comp_id =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_SINK_MSG_BROKER_COMP_ID, &error);
      CHECK_ERROR (error);
    } else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key, group);
    }
  }

  ret = TRUE;
done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

gboolean
parse_tiled_display (NvDsTiledDisplayConfig *config, GKeyFile *key_file)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_TILED_DISPLAY, NULL, &error);
  CHECK_ERROR (error);
  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_ENABLE)) {
      config->enable =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TILED_DISPLAY,
          CONFIG_GROUP_ENABLE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TILED_DISPLAY_ROWS)) {
      config->rows =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TILED_DISPLAY,
          CONFIG_GROUP_TILED_DISPLAY_ROWS, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TILED_DISPLAY_COLUMNS)) {
      config->columns =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TILED_DISPLAY,
          CONFIG_GROUP_TILED_DISPLAY_COLUMNS, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TILED_DISPLAY_WIDTH)) {
      config->width =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TILED_DISPLAY,
          CONFIG_GROUP_TILED_DISPLAY_WIDTH, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_TILED_DISPLAY_HEIGHT)) {
      config->height =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TILED_DISPLAY,
          CONFIG_GROUP_TILED_DISPLAY_HEIGHT, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GPU_ID)) {
      config->gpu_id =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TILED_DISPLAY,
          CONFIG_GPU_ID, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_NVBUF_MEMORY_TYPE)) {
      config->nvbuf_memory_type =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TILED_DISPLAY,
          CONFIG_NVBUF_MEMORY_TYPE, &error);
      CHECK_ERROR (error);
    } else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key,
          CONFIG_GROUP_TILED_DISPLAY);
    }
  }

  ret = TRUE;
done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}


