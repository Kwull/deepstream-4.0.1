/**
 * Copyright (c) 2018-2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 */

#include <gst/gst.h>
#include <string.h>

#include "gstnvinfer_property_parser.h"
#include "gstnvinfer.h"

#define CHECK_ERROR(error) \
    if (error) { \
        g_printerr ("Error while parsing config file: %s\n", error->message); \
        goto done; \
    }

extern const int DEFAULT_REINFER_INTERVAL;

/* Get the absolute path of a file mentioned in the config given a
 * file path absolute/relative to the config file. */
static gboolean
get_absolute_file_path (gchar * cfg_file_path, gchar * file_path,
    char *abs_path_str)
{
  gchar abs_cfg_path[PATH_MAX + 1];
  gchar abs_real_file_path[PATH_MAX + 1];
  gchar *abs_file_path;
  gchar *delim;

  /* Absolute path. No need to resolve further. */
  if (file_path[0] == '/') {
    /* Check if the file exists, return error if not. */
    if (!realpath (file_path, abs_real_file_path)) {
      return FALSE;
    }
    g_strlcpy (abs_path_str, abs_real_file_path, _PATH_MAX);
    return TRUE;
  }

  /* Get the absolute path of the config file. */
  if (!realpath (cfg_file_path, abs_cfg_path)) {
    return FALSE;
  }

  /* Remove the file name from the absolute path to get the directory of the
   * config file. */
  delim = g_strrstr (abs_cfg_path, "/");
  *(delim + 1) = '\0';

  /* Get the absolute file path from the config file's directory path and
   * relative file path. */
  abs_file_path = g_strconcat (abs_cfg_path, file_path, nullptr);

  /* Resolve the path.*/
  if (realpath (abs_file_path, abs_real_file_path) == nullptr) {
    /* Ignore error if file does not exist and use the unresolved path. */
    if (errno == ENOENT)
      g_strlcpy (abs_real_file_path, abs_file_path, _PATH_MAX);
    else
      return FALSE;
  }

  g_free (abs_file_path);

  g_strlcpy (abs_path_str, abs_real_file_path, _PATH_MAX);
  return TRUE;
}

/* Parse per-class detection parameters. Returns FALSE in case of an error. */
static gboolean
gst_nvinfer_parse_class_attrs (GKeyFile * key_file, gchar * group,
    NvDsInferDetectionParams & detection_params,
    GstNvInferDetectionFilterParams & detection_filter_params,
    GstNvInferColorParams & color_params)
{
  gboolean ret = FALSE;
  gchar **keys = nullptr;
  gchar **key = nullptr;
  GError *error = nullptr;

  keys = g_key_file_get_keys (key_file, group, nullptr, &error);
  CHECK_ERROR (error);

  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_CLASS_ATTRS_THRESHOLD)) {
      detection_params.threshold =
          g_key_file_get_double (key_file, group,
          CONFIG_GROUP_INFER_CLASS_ATTRS_THRESHOLD, &error);
      CHECK_ERROR (error);
      if (detection_params.threshold < 0) {
        g_printerr ("Error: Negative threshold (%.2f) specified for group %s\n",
            detection_params.threshold, group);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_CLASS_ATTRS_EPS)) {
      detection_params.eps =
          g_key_file_get_double (key_file, group,
          CONFIG_GROUP_INFER_CLASS_ATTRS_EPS, &error);
      CHECK_ERROR (error);
      if (detection_params.eps < 0) {
        g_printerr ("Error: Negative eps (%.2f) specified for group %s\n",
            detection_params.eps, group);
        goto done;
      }
    } else if (!g_strcmp0 (*key,
            CONFIG_GROUP_INFER_CLASS_ATTRS_GROUP_THRESHOLD)) {
      detection_params.groupThreshold =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_INFER_CLASS_ATTRS_GROUP_THRESHOLD, &error);
      CHECK_ERROR (error);
      if (detection_params.groupThreshold < 0) {
        g_printerr
            ("Error: Negative group-threshold (%d) specified for group %s\n",
            detection_params.groupThreshold, group);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_CLASS_ATTRS_MIN_BOXES)) {
      detection_params.minBoxes =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_INFER_CLASS_ATTRS_MIN_BOXES, &error);
      CHECK_ERROR (error);
      if (detection_params.minBoxes < 0) {
        g_printerr
            ("Error: Negative minBoxes (%d) specified for group %s\n",
            detection_params.minBoxes, group);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_CLASS_ATTRS_ROI_TOP_OFFSET)) {
      detection_filter_params.roiTopOffset =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_INFER_CLASS_ATTRS_ROI_TOP_OFFSET, &error);
      CHECK_ERROR (error);
      if ((gint) detection_filter_params.roiTopOffset < 0) {
        g_printerr
            ("Error: Negative roiTopOffset (%d) specified for group %s\n",
            detection_filter_params.roiTopOffset, group);
        goto done;
      }
    } else if (!g_strcmp0 (*key,
            CONFIG_GROUP_INFER_CLASS_ATTRS_ROI_BOTTOM_OFFSET)) {
      detection_filter_params.roiBottomOffset =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_INFER_CLASS_ATTRS_ROI_BOTTOM_OFFSET, &error);
      CHECK_ERROR (error);
      if ((gint) detection_filter_params.roiBottomOffset < 0) {
        g_printerr
            ("Error: Negative roiBottomOffset (%d) specified for group %s\n",
            detection_filter_params.roiBottomOffset, group);
        goto done;
      }
    } else if (!g_strcmp0 (*key,
            CONFIG_GROUP_INFER_CLASS_ATTRS_DETECTED_MIN_WIDTH)) {
      detection_filter_params.detectionMinWidth =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_INFER_CLASS_ATTRS_DETECTED_MIN_WIDTH, &error);
      CHECK_ERROR (error);
      if ((gint) detection_filter_params.detectionMinWidth < 0) {
        g_printerr
            ("Error: Negative detectionMinWidth (%d) specified for group %s\n",
            detection_filter_params.detectionMinWidth, group);
        goto done;
      }
    } else if (!g_strcmp0 (*key,
            CONFIG_GROUP_INFER_CLASS_ATTRS_DETECTED_MIN_HEIGHT)) {
      detection_filter_params.detectionMinHeight =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_INFER_CLASS_ATTRS_DETECTED_MIN_HEIGHT, &error);
      CHECK_ERROR (error);
      if ((gint) detection_filter_params.detectionMinHeight < 0) {
        g_printerr
            ("Error: Negative detectionMinHeight (%d) specified for group %s\n",
            detection_filter_params.detectionMinHeight, group);
        goto done;
      }
    } else if (!g_strcmp0 (*key,
            CONFIG_GROUP_INFER_CLASS_ATTRS_DETECTED_MAX_WIDTH)) {
      detection_filter_params.detectionMaxWidth =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_INFER_CLASS_ATTRS_DETECTED_MAX_WIDTH, &error);
      CHECK_ERROR (error);
      if ((gint) detection_filter_params.detectionMaxWidth < 0) {
        g_printerr
            ("Error: Negative detectionMaxWidth (%d) specified for group %s\n",
            detection_filter_params.detectionMaxWidth, group);
        goto done;
      }
    } else if (!g_strcmp0 (*key,
            CONFIG_GROUP_INFER_CLASS_ATTRS_DETECTED_MAX_HEIGHT)) {
      detection_filter_params.detectionMaxHeight =
          g_key_file_get_integer (key_file, group,
          CONFIG_GROUP_INFER_CLASS_ATTRS_DETECTED_MAX_HEIGHT, &error);
      if ((gint) detection_filter_params.detectionMaxHeight < 0) {
        g_printerr
            ("Error: Negative detectionMaxHeight (%d) specified for group %s\n",
            detection_filter_params.detectionMaxHeight, group);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_CLASS_ATTRS_BORDER_COLOR)) {
      gsize length;
      gdouble *list = g_key_file_get_double_list (key_file, group,
          CONFIG_GROUP_INFER_CLASS_ATTRS_BORDER_COLOR, &length, &error);
      CHECK_ERROR (error);
      if (length != 4) {
        g_printerr
            ("Error: Group %s, Number of Color params should be exactly 4 "
            "floats {r, g, b, a} between 0 and 1", group);
        goto done;
      }
      color_params.border_color.red = list[0];
      color_params.border_color.green = list[1];
      color_params.border_color.blue = list[2];
      color_params.border_color.alpha = list[3];

      g_free (list);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_CLASS_ATTRS_BG_COLOR)) {
      gsize length;
      gdouble *list = g_key_file_get_double_list (key_file, group,
          CONFIG_GROUP_INFER_CLASS_ATTRS_BG_COLOR, &length, &error);
      CHECK_ERROR (error);
      if (length != 4) {
        g_printerr
            ("Error: Group %s, Number of Color params should be exactly 4 "
            "floats {r, g, b, a} between 0 and 1", group);
        goto done;
      }
      color_params.bg_color.red = list[0];
      color_params.bg_color.green = list[1];
      color_params.bg_color.blue = list[2];
      color_params.bg_color.alpha = list[3];
      color_params.have_bg_color = TRUE;

      g_free (list);
    } else {
      g_printerr ("Unknown key '%s' for group [%s]\n", *key,
          CONFIG_GROUP_PROPERTY);
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
  return ret;
}

/* Parse 'property' group. Returns FALSE in case of an error. If any of the
 * properties are set through the GObject set method this function does not
 * parse those properties i.e. values set through g_object_set override the
 * corresponding properties in the config file. */
static gboolean
gst_nvinfer_parse_props (GstNvInfer * nvinfer,
    GKeyFile * key_file, gchar * cfg_file_path)
{
  gboolean ret = FALSE;
  gchar **keys = nullptr;
  gchar **key = nullptr;
  GError *error = nullptr;

  /* Handle legacy key names. */
  if (g_key_file_has_key (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_UFF_INPUT_DIMENSIONS_LEGACY, nullptr)
      && !g_key_file_has_key (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_UFF_INPUT_DIMENSIONS, nullptr)) {
    /* Do not parse here, set the key-value pair with the new key. */
    gchar *value = g_key_file_get_value (key_file, CONFIG_GROUP_PROPERTY,
        CONFIG_GROUP_INFER_UFF_INPUT_DIMENSIONS_LEGACY, &error);
    CHECK_ERROR (error);
    g_key_file_set_value (key_file, CONFIG_GROUP_PROPERTY,
        CONFIG_GROUP_INFER_UFF_INPUT_DIMENSIONS, value);
    g_free (value);
    g_key_file_remove_key (key_file, CONFIG_GROUP_PROPERTY,
        CONFIG_GROUP_INFER_UFF_INPUT_DIMENSIONS_LEGACY, nullptr);
  }

  if (g_key_file_has_key (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_IS_CLASSIFIER_LEGACY, nullptr)
      && !g_key_file_has_key (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_NETWORK_TYPE, nullptr)) {
    /* Do not parse here, set the key-value pair with the new key. */
    gboolean value = g_key_file_get_boolean (key_file, CONFIG_GROUP_PROPERTY,
        CONFIG_GROUP_INFER_IS_CLASSIFIER_LEGACY, &error);
    CHECK_ERROR (error);
    guint new_value = (value) ? NvDsInferNetworkType_Classifier : NvDsInferNetworkType_Detector;
    g_key_file_set_integer (key_file, CONFIG_GROUP_PROPERTY,
        CONFIG_GROUP_INFER_NETWORK_TYPE, new_value);
    g_key_file_remove_key (key_file, CONFIG_GROUP_PROPERTY,
        CONFIG_GROUP_INFER_IS_CLASSIFIER_LEGACY, nullptr);
  }

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_PROPERTY, nullptr, &error);
  CHECK_ERROR (error);

  nvinfer->secondary_reinfer_interval = DEFAULT_REINFER_INTERVAL;
  nvinfer->init_params->networkInputFormat = NvDsInferFormat_RGB;

  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_UNIQUE_ID)) {
      if ((*nvinfer->is_prop_set)[PROP_UNIQUE_ID])
        continue;
      nvinfer->init_params->uniqueID = nvinfer->unique_id =
          g_key_file_get_integer (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_UNIQUE_ID, &error);
      CHECK_ERROR (error);

      if (nvinfer->unique_id <= 0) {
        g_printerr ("Error: %s (%d) should be > 0\n",
            CONFIG_GROUP_INFER_UNIQUE_ID, nvinfer->unique_id);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_PROCESS_MODE)) {
      if ((*nvinfer->is_prop_set)[PROP_PROCESS_MODE])
        continue;
      guint val = g_key_file_get_integer (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_PROCESS_MODE, &error);
      CHECK_ERROR (error);

      switch (val) {
        case 1:
          nvinfer->process_full_frame = TRUE;
          break;
        case 2:
          nvinfer->process_full_frame = FALSE;
          break;
        default:
          g_printerr ("Error: Invalid value for %s (%d)\n",
              CONFIG_GROUP_INFER_PROCESS_MODE, val);
          goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_INTERVAL)) {
      if ((*nvinfer->is_prop_set)[PROP_INTERVAL])
        continue;
      nvinfer->interval =
          g_key_file_get_integer (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_INTERVAL, &error);
      CHECK_ERROR (error);
      if ((gint) nvinfer->interval < 0) {
        g_printerr ("Error: Negative value (%d) specified for interval\n",
            nvinfer->interval);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_LABEL)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_LABEL, &error);
      CHECK_ERROR (error);

      if (!get_absolute_file_path (cfg_file_path, str,
              nvinfer->init_params->labelsFilePath)) {
        g_printerr ("Error: Could not parse labels file path\n");
        g_free (str);
        goto done;
      }
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_GPU_ID)) {
      if ((*nvinfer->is_prop_set)[PROP_GPU_DEVICE_ID])
        continue;
      gint devices;

      nvinfer->gpu_id = nvinfer->init_params->gpuID =
          g_key_file_get_integer (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_GPU_ID, &error);
      CHECK_ERROR (error);

      if (cudaGetDeviceCount (&devices) != cudaSuccess) {
        g_printerr ("Error: Could not get cuda device count (%s)\n",
            cudaGetErrorName (cudaGetLastError ()));
        goto done;
      }
      if (nvinfer->init_params->gpuID >= (guint) devices && 0) {
        g_printerr
            ("Error: Invalid gpu device ID (%d). CUDA device count (%d)\n",
            nvinfer->init_params->gpuID, devices);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_SECONDARY_REINFER_INTERVAL)) {
      nvinfer->secondary_reinfer_interval =
          g_key_file_get_integer (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_SECONDARY_REINFER_INTERVAL, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_OUTPUT_TENSOR_META)) {
      if (g_key_file_get_boolean (key_file, CONFIG_GROUP_PROPERTY,
              CONFIG_GROUP_INFER_OUTPUT_TENSOR_META, &error))
        nvinfer->output_tensor_meta = TRUE;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_ENABLE_DLA)) {
      if (g_key_file_get_boolean (key_file, CONFIG_GROUP_PROPERTY,
              CONFIG_GROUP_INFER_ENABLE_DLA, &error))
        nvinfer->init_params->useDLA = TRUE;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_USE_DLA_CORE)) {
      nvinfer->init_params->dlaCore =
          g_key_file_get_integer (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_USE_DLA_CORE, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_BATCH_SIZE)) {
      if ((*nvinfer->is_prop_set)[PROP_BATCH_SIZE])
        continue;
      nvinfer->max_batch_size = nvinfer->init_params->maxBatchSize =
          g_key_file_get_integer (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_BATCH_SIZE, &error);
      CHECK_ERROR (error);

      if (nvinfer->max_batch_size <= 0
          || nvinfer->max_batch_size > NVDSINFER_MAX_BATCH_SIZE) {
        g_printerr ("Error: %s(%d) should be in the range [%d,%d]\n",
            CONFIG_GROUP_INFER_BATCH_SIZE, nvinfer->max_batch_size, 1,
            NVDSINFER_MAX_BATCH_SIZE);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_NETWORK_MODE)) {
      guint val = g_key_file_get_integer (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_NETWORK_MODE, &error);
      CHECK_ERROR (error);

      switch (val) {
        case NvDsInferNetworkMode_FP32:
        case NvDsInferNetworkMode_FP16:
        case NvDsInferNetworkMode_INT8:
          break;
        default:
          g_printerr ("Error. Invalid value for '%s':'%d'\n",
              CONFIG_GROUP_INFER_NETWORK_MODE, val);
          goto done;
          break;
      }
      nvinfer->init_params->networkMode = (NvDsInferNetworkMode) val;
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_MODEL_ENGINE)) {
      if ((*nvinfer->is_prop_set)[PROP_MODEL_ENGINEFILE])
        continue;
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_MODEL_ENGINE, &error);
      CHECK_ERROR (error);

      if (!get_absolute_file_path (cfg_file_path, str,
              nvinfer->init_params->modelEngineFilePath)) {
        g_printerr ("Error: Could not parse model engine file path\n");
        g_free (str);
        goto done;
      }
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_INT8_CALIBRATION_FILE)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_INT8_CALIBRATION_FILE, &error);
      CHECK_ERROR (error);

      if (!get_absolute_file_path (cfg_file_path, str,
              nvinfer->init_params->int8CalibrationFilePath)) {
        g_printerr ("Error: Could not parse INT8 calibration file path\n");
        g_free (str);
        goto done;
      }
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_OUTPUT_BLOB_NAMES)) {
      gsize length;
      nvinfer->init_params->outputLayerNames =
          g_key_file_get_string_list (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_OUTPUT_BLOB_NAMES, &length, &error);
      nvinfer->init_params->numOutputLayers = length;

      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_NETWORK_TYPE)) {
      guint val = g_key_file_get_integer (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_NETWORK_TYPE, &error);
      CHECK_ERROR (error);

      switch ((NvDsInferNetworkType) val) {
        case NvDsInferNetworkType_Detector:
        case NvDsInferNetworkType_Classifier:
        case NvDsInferNetworkType_Segmentation:
        case NvDsInferNetworkType_Other:
          nvinfer->init_params->networkType = (NvDsInferNetworkType) val;
          break;
        default:
          g_printerr ("Error. Invalid value for '%s':'%d'\n",
              CONFIG_GROUP_INFER_NETWORK_TYPE, val);
          goto done;
          break;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_MODEL_COLOR_FORMAT)) {
      guint val = g_key_file_get_integer (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_MODEL_COLOR_FORMAT, &error);
      CHECK_ERROR (error);
      switch (val) {
        case 0:
          nvinfer->init_params->networkInputFormat = NvDsInferFormat_RGB;
          break;
        case 1:
          nvinfer->init_params->networkInputFormat = NvDsInferFormat_BGR;
          break;
        case 2:
          nvinfer->init_params->networkInputFormat = NvDsInferFormat_GRAY;
          break;
        default:
          g_printerr ("Error. Invalid value for '%s':'%d'\n",
              CONFIG_GROUP_INFER_MODEL_COLOR_FORMAT, val);
          goto done;
          break;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_SCALE_FACTOR)) {
      nvinfer->init_params->networkScaleFactor =
          g_key_file_get_double (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_SCALE_FACTOR, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_OFFSETS)) {
      gsize length, i;
      gdouble *dbl_list =
          g_key_file_get_double_list (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_OFFSETS, &length, &error);
      CHECK_ERROR (error);

      if (length > _MAX_CHANNELS) {
        g_printerr ("Error. Maximum  length of %d is allowed for '%s'\n",
            _MAX_CHANNELS, CONFIG_GROUP_INFER_OFFSETS);
        g_free (dbl_list);
        goto done;
      }

      for (i = 0; i < length; i++)
        nvinfer->init_params->offsets[i] = dbl_list[i];
      nvinfer->init_params->numOffsets = length;
      g_free (dbl_list);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_MEANFILE)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_MEANFILE, &error);
      CHECK_ERROR (error);

      if (!get_absolute_file_path (cfg_file_path, str,
              nvinfer->init_params->meanImageFilePath)) {
        g_printerr ("Error: Could not parse mean image file path\n");
        g_free (str);
        goto done;
      }
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_MAINTAIN_ASPECT_RATIO)) {
      if (g_key_file_get_boolean (key_file, CONFIG_GROUP_PROPERTY,
              CONFIG_GROUP_INFER_MAINTAIN_ASPECT_RATIO, &error))
        nvinfer->maintain_aspect_ratio = TRUE;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_CUSTOM_LIB_PATH)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_CUSTOM_LIB_PATH, &error);
      CHECK_ERROR (error);

      if (!get_absolute_file_path (cfg_file_path, str,
              nvinfer->init_params->customLibPath)) {
        g_printerr ("Error: Could not parse custom library path\n");
        g_free (str);
        goto done;
      }
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_CUSTOM_PARSE_BBOX_FUNC)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_CUSTOM_PARSE_BBOX_FUNC, &error);
      CHECK_ERROR (error);
      g_strlcpy (nvinfer->init_params->customBBoxParseFuncName, str,
          _MAX_STR_LENGTH);
      g_free (str);
    } else if (!g_strcmp0 (*key,
            CONFIG_GROUP_INFER_CUSTOM_PARSE_CLASSIFIER_FUNC)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_CUSTOM_PARSE_CLASSIFIER_FUNC, &error);
      CHECK_ERROR (error);
      g_strlcpy (nvinfer->init_params->customClassifierParseFuncName, str,
          _MAX_STR_LENGTH);
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_CUSTOM_NETWORK_CONFIG)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_CUSTOM_NETWORK_CONFIG, &error);
      CHECK_ERROR (error);
      g_strlcpy (nvinfer->init_params->customNetworkConfigFilePath, str,
          _MAX_STR_LENGTH);
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_MODEL)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_MODEL, &error);
      CHECK_ERROR (error);

      if (!get_absolute_file_path (cfg_file_path, str,
              nvinfer->init_params->modelFilePath)) {
        g_printerr ("Error: Could not parse model file path\n");
        g_free (str);
        goto done;
      }
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_PROTO)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_PROTO, &error);
      CHECK_ERROR (error);

      if (!get_absolute_file_path (cfg_file_path, str,
              nvinfer->init_params->protoFilePath)) {
        g_printerr ("Error: Could not parse prototxt file path\n");
        g_free (str);
        goto done;
      }
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_UFF)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_UFF, &error);
      CHECK_ERROR (error);

      if (!get_absolute_file_path (cfg_file_path, str,
              nvinfer->init_params->uffFilePath)) {
        g_printerr ("Error: Could not parse UFF file path\n");
        g_free (str);
        goto done;
      }
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_UFF_INPUT_DIMENSIONS)) {
      gsize length;
      gint *int_list = g_key_file_get_integer_list (key_file,
          CONFIG_GROUP_PROPERTY, CONFIG_GROUP_INFER_UFF_INPUT_DIMENSIONS,
          &length, &error);
      CHECK_ERROR (error);

      if (length != 4) {
        g_printerr ("Error. '%s' array length is %lu. Should be 4.\n",
            CONFIG_GROUP_INFER_UFF_INPUT_DIMENSIONS, length);
        goto done;
      }
      switch (int_list[3]) {
        case 0:
          nvinfer->init_params->uffInputOrder = NvDsInferUffInputOrder_kNCHW;
          break;
        case 1:
          nvinfer->init_params->uffInputOrder = NvDsInferUffInputOrder_kNHWC;
          break;
        default:
          g_printerr ("Error. Invalid value for '%s', UFF input order :'%d'\n",
              CONFIG_GROUP_INFER_UFF_INPUT_DIMENSIONS, int_list[3]);
          goto done;
          break;
      }
      nvinfer->init_params->uffDimsCHW = NvDsInferDimsCHW {
      (unsigned int) int_list[0],
            (unsigned int) int_list[1], (unsigned int) int_list[2]};
      g_free (int_list);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_UFF_INPUT_BLOB_NAME)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_UFF_INPUT_BLOB_NAME, &error);
      CHECK_ERROR (error);

      g_strlcpy (nvinfer->init_params->uffInputBlobName, str, _MAX_STR_LENGTH);
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_TLT_ENCODED_MODEL)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_TLT_ENCODED_MODEL, &error);
      CHECK_ERROR (error);

      if (!get_absolute_file_path (cfg_file_path, str,
              nvinfer->init_params->tltEncodedModelFilePath)) {
        g_printerr ("Error: Could not parse TLT encoded model file path\n");
        g_free (str);
        goto done;
      }
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_TLT_MODEL_KEY)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_TLT_MODEL_KEY, &error);
      CHECK_ERROR (error);

      g_strlcpy (nvinfer->init_params->tltModelKey, str, _MAX_STR_LENGTH);
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_ONNX)) {
      gchar *str = g_key_file_get_string (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_ONNX, &error);
      CHECK_ERROR (error);

      if (!get_absolute_file_path (cfg_file_path, str,
              nvinfer->init_params->onnxFilePath)) {
        g_printerr ("Error: Could not parse ONNX file path\n");
        g_free (str);
        goto done;
      }
      g_free (str);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_NUM_DETECTED_CLASSES)) {
      nvinfer->init_params->numDetectedClasses =
          g_key_file_get_integer (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_NUM_DETECTED_CLASSES, &error);
      CHECK_ERROR (error);

      if (nvinfer->init_params->numDetectedClasses < 0) {
        g_printerr ("Error: Negative value specified for %s(%d)\n",
            CONFIG_GROUP_INFER_NUM_DETECTED_CLASSES,
            nvinfer->init_params->numDetectedClasses);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_ENABLE_DBSCAN)) {
      if (g_key_file_get_boolean (key_file, CONFIG_GROUP_PROPERTY,
              CONFIG_GROUP_INFER_ENABLE_DBSCAN, &error))
        nvinfer->init_params->useDBScan = TRUE;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_CLASSIFIER_THRESHOLD)) {
      nvinfer->init_params->classifierThreshold =
          g_key_file_get_double (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_CLASSIFIER_THRESHOLD, &error);
      CHECK_ERROR (error);

      if (nvinfer->init_params->classifierThreshold < 0) {
        g_printerr ("Error: Negative value specified for %s(%.2f)\n",
            CONFIG_GROUP_INFER_CLASSIFIER_THRESHOLD,
            nvinfer->init_params->classifierThreshold);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_CLASSIFIER_ASYNC_MODE)) {
      if (g_key_file_get_boolean (key_file, CONFIG_GROUP_PROPERTY,
              CONFIG_GROUP_INFER_CLASSIFIER_ASYNC_MODE, &error))
        nvinfer->classifier_async_mode = TRUE;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_SEGMENTATION_THRESHOLD)) {
      nvinfer->init_params->segmentationThreshold =
          g_key_file_get_double (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_SEGMENTATION_THRESHOLD, &error);
      CHECK_ERROR (error);

      if (nvinfer->init_params->segmentationThreshold < 0) {
        g_printerr ("Error: Negative value specified for %s(%.2f)\n",
            CONFIG_GROUP_INFER_SEGMENTATION_THRESHOLD,
            nvinfer->init_params->segmentationThreshold);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_INPUT_OBJECT_MIN_WIDTH)) {
      nvinfer->min_input_object_width = g_key_file_get_integer (key_file,
          CONFIG_GROUP_PROPERTY, CONFIG_GROUP_INFER_INPUT_OBJECT_MIN_WIDTH,
          &error);
      CHECK_ERROR (error);
      if ((gint) nvinfer->min_input_object_width < 0) {
        g_printerr ("Error: Negative value specified for %s(%d)\n",
            CONFIG_GROUP_INFER_INPUT_OBJECT_MIN_WIDTH,
            nvinfer->min_input_object_width);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_INPUT_OBJECT_MIN_HEIGHT)) {
      nvinfer->min_input_object_height = g_key_file_get_integer (key_file,
          CONFIG_GROUP_PROPERTY, CONFIG_GROUP_INFER_INPUT_OBJECT_MIN_HEIGHT,
          &error);
      CHECK_ERROR (error);
      if ((gint) nvinfer->min_input_object_height < 0) {
        g_printerr ("Error: Negative value specified for %s(%d)\n",
            CONFIG_GROUP_INFER_INPUT_OBJECT_MIN_HEIGHT,
            nvinfer->min_input_object_height);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_INPUT_OBJECT_MAX_WIDTH)) {
      nvinfer->max_input_object_width = g_key_file_get_integer (key_file,
          CONFIG_GROUP_PROPERTY, CONFIG_GROUP_INFER_INPUT_OBJECT_MAX_WIDTH,
          &error);
      CHECK_ERROR (error);
      if ((gint) nvinfer->max_input_object_width < 0) {
        g_printerr ("Error: Negative value specified for %s(%d)\n",
            CONFIG_GROUP_INFER_INPUT_OBJECT_MAX_WIDTH,
            nvinfer->max_input_object_width);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_INPUT_OBJECT_MAX_HEIGHT)) {
      nvinfer->max_input_object_height = g_key_file_get_integer (key_file,
          CONFIG_GROUP_PROPERTY, CONFIG_GROUP_INFER_INPUT_OBJECT_MAX_HEIGHT,
          &error);
      CHECK_ERROR (error);
      if ((gint) nvinfer->max_input_object_height < 0) {
        g_printerr ("Error: Negative value specified for %s(%d)\n",
            CONFIG_GROUP_INFER_INPUT_OBJECT_MAX_HEIGHT,
            nvinfer->max_input_object_height);
        goto done;
      }
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_GIE_ID_FOR_OPERATION)) {
      if ((*nvinfer->is_prop_set)[PROP_OPERATE_ON_GIE_ID] ||
          (*nvinfer->is_prop_set)[PROP_OPERATE_ON_CLASS_IDS])
        continue;
      nvinfer->operate_on_gie_id = g_key_file_get_integer (key_file,
          CONFIG_GROUP_PROPERTY, CONFIG_GROUP_INFER_GIE_ID_FOR_OPERATION,
          &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_INFER_CLASS_IDS_FOR_OPERATION)) {
      if ((*nvinfer->is_prop_set)[PROP_OPERATE_ON_GIE_ID] ||
          (*nvinfer->is_prop_set)[PROP_OPERATE_ON_CLASS_IDS])
        continue;
      gsize length, i;
      gint max_class_id = -1;
      gint *int_list =
          g_key_file_get_integer_list (key_file, CONFIG_GROUP_PROPERTY,
          CONFIG_GROUP_INFER_CLASS_IDS_FOR_OPERATION, &length, &error);

      for (i = 0; i < length; i++) {
        if (int_list[i] > max_class_id)
          max_class_id = int_list[i];
      }
      nvinfer->operate_on_class_ids->assign (max_class_id + 1, FALSE);
      for (i = 0; i < length; i++) {
        nvinfer->operate_on_class_ids->at (int_list[i]) = TRUE;
      }
      g_free (int_list);
    } else {
      g_printerr ("Unknown key '%s' for group [%s]\n", *key,
          CONFIG_GROUP_PROPERTY);
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
  return ret;
}

/* Parse the nvinfer config file. Returns FALSE in case of an error. */
gboolean
gst_nvinfer_parse_config_file (GstNvInfer * nvinfer, gchar * cfg_file_path)
{
  GError *error = nullptr;
  gboolean ret = FALSE;
  gchar **groups = nullptr;
  gchar **group;
  GKeyFile *cfg_file = g_key_file_new ();

  if (!g_key_file_load_from_file (cfg_file, cfg_file_path, G_KEY_FILE_NONE,
          &error)) {
    g_printerr ("Failed to load config file: %s\n", error->message);
    goto done;
  }

  /* 'property' group is mandatory. */
  if (!g_key_file_has_group (cfg_file, CONFIG_GROUP_PROPERTY)) {
    g_printerr ("Could not find group %s\n", CONFIG_GROUP_PROPERTY);
    goto done;
  }

  if (!gst_nvinfer_parse_props (nvinfer, cfg_file, cfg_file_path)) {
    g_printerr ("Failed to parse group %s\n", CONFIG_GROUP_PROPERTY);
    goto done;
  }
  g_key_file_remove_group (cfg_file, CONFIG_GROUP_PROPERTY, nullptr);

  /* If the nvinfer instance is to be configured as a detector, parse the
   * per-class detection parameters. */
  if (nvinfer->init_params->networkType == NvDsInferNetworkType_Detector) {
    /* Set the default detection parameters. */
    NvDsInferDetectionParams detection_params{DEFAULT_THRESHOLD, DEFAULT_EPS,
        DEFAULT_GROUP_THRESHOLD, DEFAULT_MIN_BOXES};
    GstNvInferDetectionFilterParams detection_filter_params{0, 0, 0, 0, 0, 0};
    GstNvInferColorParams color_params;
    color_params.have_border_color = TRUE;
    color_params.border_color = (NvOSD_ColorParams) {1, 0, 0, 1};
    color_params.have_bg_color = FALSE;

    /* Parse the parameters for "all" classes if the group has been specified. */
    if (g_key_file_has_group (cfg_file,
            CONFIG_GROUP_INFER_CLASS_ATTRS_PREFIX "all")) {
      if (!gst_nvinfer_parse_class_attrs (cfg_file,
              (gchar *) CONFIG_GROUP_INFER_CLASS_ATTRS_PREFIX "all",
              detection_params, detection_filter_params, color_params)) {
        g_printerr ("Error while parsing group %s\n",
            CONFIG_GROUP_INFER_CLASS_ATTRS_PREFIX "all");
        goto done;
      }
      g_key_file_remove_group (cfg_file,
          CONFIG_GROUP_INFER_CLASS_ATTRS_PREFIX "all", nullptr);
    }

    /* Initialize the per-class vector with the same default/parsed values for
     * all classes. */
    nvinfer->init_params->perClassDetectionParams =
        new NvDsInferDetectionParams[nvinfer->init_params->numDetectedClasses];
    for (unsigned int i = 0; i < nvinfer->init_params->numDetectedClasses; i++)
      nvinfer->init_params->perClassDetectionParams[i] = detection_params;
    nvinfer->perClassDetectionFilterParams =
        new std::vector < GstNvInferDetectionFilterParams >
        (nvinfer->init_params->numDetectedClasses, detection_filter_params);
    nvinfer->perClassColorParams =
        new std::vector < GstNvInferColorParams >
        (nvinfer->init_params->numDetectedClasses, color_params);

    /* Parse values for specified classes. */
    groups = g_key_file_get_groups (cfg_file, nullptr);
    for (group = groups; *group; group++) {
      if (!strncmp (*group, CONFIG_GROUP_INFER_CLASS_ATTRS_PREFIX,
              sizeof (CONFIG_GROUP_INFER_CLASS_ATTRS_PREFIX) - 1)) {
        gchar *key1 =
            *group + sizeof (CONFIG_GROUP_INFER_CLASS_ATTRS_PREFIX) - 1;
        gchar *endptr;
        guint64 class_index = g_ascii_strtoull (key1, &endptr, 10);

        /* Check that class_index has been parsed successfully and that it lies
         * within the valid range of class_ids [0, numDetectedClasses - 1]. */
        if (class_index == 0 && endptr == key1) {
          g_printerr
              ("Invalid group [%s]. Class attributes should be specified using group name '"
              CONFIG_GROUP_INFER_CLASS_ATTRS_PREFIX "<class-id>'\n", *group);
          goto done;
        }
        if ((gint) class_index < 0) {
          g_printerr ("Invalid group [%s]. class-id should be >= 0\n", *group);
          goto done;
        }
        if (class_index >= nvinfer->init_params->numDetectedClasses) {
          g_printerr
              ("Attributes specified for class %lu while element has been "
              "configured with num-detected-classes=%d\n",
              class_index, nvinfer->init_params->numDetectedClasses);
          goto done;
        }

        /* Parse the group. */
        if (!gst_nvinfer_parse_class_attrs (cfg_file, *group,
                nvinfer->init_params->perClassDetectionParams[class_index],
                (*nvinfer->perClassDetectionFilterParams)[class_index],
                (*nvinfer->perClassColorParams)[class_index])) {
          g_printerr ("Error while parsing group %s\n", *group);
          goto done;
        }
      }
    }
  }
  ret = TRUE;

done:
  if (cfg_file) {
    g_key_file_free (cfg_file);
  }

  if (groups) {
    g_strfreev (groups);
  }

  if (error) {
    g_error_free (error);
  }
  if (!ret) {
    g_printerr ("** ERROR: <%s:%d>: failed\n", __func__, __LINE__);
  }
  return ret;
}
