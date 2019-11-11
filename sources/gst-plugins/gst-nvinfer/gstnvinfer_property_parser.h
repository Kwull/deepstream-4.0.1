/**
 * Copyright (c) 2018 2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 */

#ifndef __GST_NVINFER_PROPERTY_PARSER_H__
#define __GST_NVINFER_PROPERTY_PARSER_H__

#include <glib.h>

#include "nvdsinfer_context.h"
#include "gstnvinfer.h"

#define DEFAULT_THRESHOLD 0.2
#define DEFAULT_EPS 0.0
#define DEFAULT_GROUP_THRESHOLD 0
#define DEFAULT_MIN_BOXES 0

#define CONFIG_GROUP_PROPERTY "property"

#define CONFIG_GROUP_INFER_PARSE_FUNC "parse-func"

/** Gstreamer element configuration. */
#define CONFIG_GROUP_INFER_UNIQUE_ID "gie-unique-id"
#define CONFIG_GROUP_INFER_PROCESS_MODE "process-mode"
#define CONFIG_GROUP_INFER_INTERVAL "interval"
#define CONFIG_GROUP_INFER_LABEL "labelfile-path"
#define CONFIG_GROUP_INFER_GPU_ID "gpu-id"
#define CONFIG_GROUP_INFER_SECONDARY_REINFER_INTERVAL "secondary-reinfer-interval"
#define CONFIG_GROUP_INFER_OUTPUT_TENSOR_META "output-tensor-meta"


#define CONFIG_GROUP_INFER_ENABLE_DLA "enable-dla"
#define CONFIG_GROUP_INFER_USE_DLA_CORE "use-dla-core"

/** Runtime engine parameters. */
#define CONFIG_GROUP_INFER_BATCH_SIZE "batch-size"
#define CONFIG_GROUP_INFER_NETWORK_MODE "network-mode"
#define CONFIG_GROUP_INFER_MODEL_ENGINE "model-engine-file"
#define CONFIG_GROUP_INFER_INT8_CALIBRATION_FILE "int8-calib-file"

/** Generic model parameters. */
#define CONFIG_GROUP_INFER_OUTPUT_BLOB_NAMES "output-blob-names"
#define CONFIG_GROUP_INFER_IS_CLASSIFIER_LEGACY "is-classifier"
#define CONFIG_GROUP_INFER_NETWORK_TYPE "network-type"

/** Preprocessing parameters. */
#define CONFIG_GROUP_INFER_MODEL_COLOR_FORMAT "model-color-format"
#define CONFIG_GROUP_INFER_SCALE_FACTOR "net-scale-factor"
#define CONFIG_GROUP_INFER_OFFSETS "offsets"
#define CONFIG_GROUP_INFER_MEANFILE "mean-file"
#define CONFIG_GROUP_INFER_MAINTAIN_ASPECT_RATIO "maintain-aspect-ratio"

/** Custom implementation required to support a network. */
#define CONFIG_GROUP_INFER_CUSTOM_LIB_PATH "custom-lib-path"
#define CONFIG_GROUP_INFER_CUSTOM_PARSE_BBOX_FUNC "parse-bbox-func-name"
#define CONFIG_GROUP_INFER_CUSTOM_PARSE_CLASSIFIER_FUNC "parse-classifier-func-name"
#define CONFIG_GROUP_INFER_CUSTOM_NETWORK_CONFIG "custom-network-config"

/** Caffe model specific parameters. */
#define CONFIG_GROUP_INFER_MODEL "model-file"
#define CONFIG_GROUP_INFER_PROTO "proto-file"

/** UFF model specific parameters. */
#define CONFIG_GROUP_INFER_UFF "uff-file"
#define CONFIG_GROUP_INFER_UFF_INPUT_DIMENSIONS "uff-input-dims"
#define CONFIG_GROUP_INFER_UFF_INPUT_DIMENSIONS_LEGACY "input-dims"
#define CONFIG_GROUP_INFER_UFF_INPUT_BLOB_NAME "uff-input-blob-name"

/** TLT model parameters. */
#define CONFIG_GROUP_INFER_TLT_ENCODED_MODEL "tlt-encoded-model"
#define CONFIG_GROUP_INFER_TLT_MODEL_KEY "tlt-model-key"

/** ONNX model specific parameters. */
#define CONFIG_GROUP_INFER_ONNX "onnx-file"

/** Detector specific parameters. */
#define CONFIG_GROUP_INFER_NUM_DETECTED_CLASSES "num-detected-classes"
#define CONFIG_GROUP_INFER_ENABLE_DBSCAN "enable-dbscan"

/** Classifier specific parameters. */
#define CONFIG_GROUP_INFER_CLASSIFIER_THRESHOLD "classifier-threshold"
#define CONFIG_GROUP_INFER_CLASSIFIER_ASYNC_MODE "classifier-async-mode"

/** Segmentaion specific parameters. */
#define CONFIG_GROUP_INFER_SEGMENTATION_THRESHOLD "segmentation-threshold"

/** Parameters for filtering objects based min/max size threshold when
    operating in secondary mode. */
#define CONFIG_GROUP_INFER_INPUT_OBJECT_MIN_WIDTH "input-object-min-width"
#define CONFIG_GROUP_INFER_INPUT_OBJECT_MIN_HEIGHT "input-object-min-height"
#define CONFIG_GROUP_INFER_INPUT_OBJECT_MAX_WIDTH "input-object-max-width"
#define CONFIG_GROUP_INFER_INPUT_OBJECT_MAX_HEIGHT "input-object-max-height"

/** Parameters for filtering objects based on class-id and unique id of the
    detector when operating in secondary mode. */
#define CONFIG_GROUP_INFER_GIE_ID_FOR_OPERATION "operate-on-gie-id"
#define CONFIG_GROUP_INFER_CLASS_IDS_FOR_OPERATION "operate-on-class-ids"

/** Per-class detection/filtering parameters. */
#define CONFIG_GROUP_INFER_CLASS_ATTRS_PREFIX "class-attrs-"
#define CONFIG_GROUP_INFER_CLASS_ATTRS_THRESHOLD "threshold"
#define CONFIG_GROUP_INFER_CLASS_ATTRS_EPS "eps"
#define CONFIG_GROUP_INFER_CLASS_ATTRS_GROUP_THRESHOLD "group-threshold"
#define CONFIG_GROUP_INFER_CLASS_ATTRS_MIN_BOXES "minBoxes"
#define CONFIG_GROUP_INFER_CLASS_ATTRS_ROI_TOP_OFFSET "roi-top-offset"
#define CONFIG_GROUP_INFER_CLASS_ATTRS_ROI_BOTTOM_OFFSET "roi-bottom-offset"
#define CONFIG_GROUP_INFER_CLASS_ATTRS_DETECTED_MIN_WIDTH "detected-min-w"
#define CONFIG_GROUP_INFER_CLASS_ATTRS_DETECTED_MIN_HEIGHT "detected-min-h"
#define CONFIG_GROUP_INFER_CLASS_ATTRS_DETECTED_MAX_WIDTH "detected-max-w"
#define CONFIG_GROUP_INFER_CLASS_ATTRS_DETECTED_MAX_HEIGHT "detected-max-h"
#define CONFIG_GROUP_INFER_CLASS_ATTRS_BORDER_COLOR "border-color"
#define CONFIG_GROUP_INFER_CLASS_ATTRS_BG_COLOR "bg-color"

gboolean gst_nvinfer_parse_config_file (GstNvInfer *nvinfer,
        gchar * cfg_file_path);

#endif /*__GST_NVINFER_PROPERTY_PARSER_H__*/
