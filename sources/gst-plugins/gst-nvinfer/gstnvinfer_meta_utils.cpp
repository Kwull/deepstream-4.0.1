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

#include <cstring>
#include "gstnvinfer_meta_utils.h"

static inline int
get_element_size (NvDsInferDataType data_type)
{
  switch (data_type) {
    case FLOAT:
      return 4;
    case HALF:
      return 2;
    case INT32:
      return 4;
    case INT8:
      return 1;
    default:
      return 0;
  }
}


/**
 * Attach metadata for the detector. We will be adding a new metadata.
 */
void
attach_metadata_detector (GstNvInfer * nvinfer, GstMiniObject * tensor_out_object,
    GstNvInferFrame & frame, NvDsInferDetectionOutput & detection_output)
{
  static gchar font_name[] = "Serif";
  NvDsObjectMeta *obj_meta = NULL;
  NvDsObjectMeta *parent_obj_meta = frame.obj_meta; /* This will be  NULL in case of primary detector */
  NvDsFrameMeta *frame_meta = frame.frame_meta;
  NvDsBatchMeta *batch_meta = frame_meta->base_meta.batch_meta;
  nvds_acquire_meta_lock (batch_meta);

  frame_meta->bInferDone = TRUE;
  /* Iterate through the inference output for one frame and attach the detected
   * bnounding boxes. */
  for (guint i = 0; i < detection_output.numObjects; i++) {
    NvDsInferObject & obj = detection_output.objects[i];
    GstNvInferDetectionFilterParams & filter_params =
        (*nvinfer->perClassDetectionFilterParams)[obj.classIndex];

    /* Scale the bounding boxes proportionally based on how the object/frame was
     * scaled during input. */
    obj.left /= frame.scale_ratio_x;
    obj.top /= frame.scale_ratio_y;
    obj.width /= frame.scale_ratio_x;
    obj.height /= frame.scale_ratio_y;

    /* Check if the scaled box co-ordinates meet the detection filter criteria.
     * Skip the box if it does not. */
    if (obj.width < filter_params.detectionMinWidth)
      continue;
    if (obj.height < filter_params.detectionMinHeight)
      continue;
    if (filter_params.detectionMaxWidth > 0 &&
        obj.width > filter_params.detectionMaxWidth)
      continue;
    if (filter_params.detectionMaxHeight > 0 &&
        obj.width > filter_params.detectionMaxHeight)
      continue;
    if (obj.top < filter_params.roiTopOffset)
      continue;
    if (obj.top + obj.height >
        (frame.input_surf_params->height - filter_params.roiBottomOffset))
      continue;

    obj_meta = nvds_acquire_obj_meta_from_pool (batch_meta);

    obj_meta->unique_component_id = nvinfer->unique_id;
    obj_meta->confidence = 0.0;

    /* This is an untracked object. Set tracking_id to -1. */
    obj_meta->object_id = UNTRACKED_OBJECT_ID;
    obj_meta->class_id = obj.classIndex;

    NvOSD_RectParams & rect_params = obj_meta->rect_params;
    NvOSD_TextParams & text_params = obj_meta->text_params;

    /* Assign bounding box coordinates. */
    rect_params.left = obj.left;
    rect_params.top = obj.top;
    rect_params.width = obj.width;
    rect_params.height = obj.height;

    if(!nvinfer->process_full_frame) {
      rect_params.left += parent_obj_meta->rect_params.left;
      rect_params.top += parent_obj_meta->rect_params.top;
    }

    /* Border of width 3. */
    rect_params.border_width = 3;
    if (obj.classIndex > (gint) nvinfer->perClassColorParams->size()) {
      rect_params.has_bg_color = 0;
      rect_params.border_color = (NvOSD_ColorParams) {1, 0, 0, 1};
    } else {
      GstNvInferColorParams &color_params =
          (*nvinfer->perClassColorParams)[obj.classIndex];
      rect_params.has_bg_color = color_params.have_bg_color;
      rect_params.bg_color = color_params.bg_color;
      rect_params.border_color = color_params.border_color;
    }

    if (obj.label)
      strncpy (obj_meta->obj_label, obj.label, MAX_LABEL_SIZE);
    /* display_text requires heap allocated memory. */
    text_params.display_text = g_strdup (obj.label);
    /* Display text above the left top corner of the object. */
    text_params.x_offset = rect_params.left;
    text_params.y_offset = rect_params.top - 10;
    /* Set black background for the text. */
    text_params.set_bg_clr = 1;
    text_params.text_bg_clr = (NvOSD_ColorParams) {
    0, 0, 0, 1};
    /* Font face, size and color. */
    text_params.font_params.font_name = font_name;
    text_params.font_params.font_size = 11;
    text_params.font_params.font_color = (NvOSD_ColorParams) {
    1, 1, 1, 1};
    nvds_add_obj_meta_to_frame (frame_meta, obj_meta, parent_obj_meta);
  }
  nvds_release_meta_lock (batch_meta);
}

/**
 * Update string label in an existing object metadata. If processing on full
 * frames, need to attach a new metadata. Assume only one label per object is generated.
 */
void
attach_metadata_classifier (GstNvInfer * nvinfer, GstMiniObject * tensor_out_object,
    GstNvInferFrame & frame, GstNvInferObjectInfo & object_info)
{
  NvDsObjectMeta *object_meta = frame.obj_meta;
  NvDsBatchMeta *batch_meta = (nvinfer->process_full_frame) ?
    frame.frame_meta->base_meta.batch_meta : object_meta->base_meta.batch_meta;

  if (object_info.attributes.size () == 0 ||
          object_info.label.length() == 0)
    return;

  nvds_acquire_meta_lock (batch_meta);
  if (nvinfer->process_full_frame) {

    /* Attach only one object in the meta since this is a full frame
     * classification. */
    object_meta = nvds_acquire_obj_meta_from_pool (batch_meta);

    /* Font to be used for label text. */
    static gchar font_name[] = "Serif";

    NvOSD_RectParams & rect_params = object_meta->rect_params;
    NvOSD_TextParams & text_params = object_meta->text_params;

    //frame.object_meta = object_meta;

    /* Assign bounding box coordinates. */
    rect_params.left = 0;
    rect_params.top = 0;
    rect_params.width = frame.input_surf_params->width;
    rect_params.height = frame.input_surf_params->height;

    /* Semi-transparent yellow background. */
    rect_params.has_bg_color = 0;
    rect_params.bg_color = (NvOSD_ColorParams) {
    1, 1, 0, 0.4};
    /* Red border of width 6. */
    rect_params.border_width = 6;
    rect_params.border_color = (NvOSD_ColorParams) {
    1, 0, 0, 1};

    object_meta->object_id = UNTRACKED_OBJECT_ID;
    object_meta->class_id = -1;

    /* display_text requires heap allocated memory. Actual string formation
     * is done later in the function. */
    text_params.display_text = g_strdup ("");
    /* Display text above the left top corner of the object. */
    text_params.x_offset = rect_params.left;
    text_params.y_offset = rect_params.top - 10;
    /* Set black background for the text. */
    text_params.set_bg_clr = 1;
    text_params.text_bg_clr = (NvOSD_ColorParams) {
    0, 0, 0, 1};
    /* Font face, size and color. */
    text_params.font_params.font_name = font_name;
    text_params.font_params.font_size = 11;
    text_params.font_params.font_color = (NvOSD_ColorParams) {
    1, 1, 1, 1};

    /* Attach the NvDsFrameMeta structure as NvDsMeta to the buffer. Pass the
     * function to be called when freeing the meta_data. */
    nvds_add_obj_meta_to_frame (frame.frame_meta, object_meta, NULL);
  }

  std::string string_label = object_info.label;

  /* Fill the attribute info structure for the object. */
  guint num_attrs = object_info.attributes.size ();

  NvDsClassifierMeta *classifier_meta =
      nvds_acquire_classifier_meta_from_pool (batch_meta);

  classifier_meta->unique_component_id = nvinfer->unique_id;

  for (unsigned int i = 0; i < num_attrs; i++) {
    NvDsLabelInfo *label_info =
        nvds_acquire_label_info_meta_from_pool (batch_meta);
    NvDsInferAttribute &attr = object_info.attributes[i];
    /* TODO: Check this for multilabel classifier <20-03-19, shaunakg> */
//    label_info->result_class_id =
    //    attributes[i].attributeIndex; /* multilabel classifier, clr, make, model */
    label_info->label_id = attr.attributeIndex;
    label_info->result_class_id = attr.attributeValue;
    label_info->result_prob = attr.attributeConfidence;
    if (attr.attributeLabel) {
      g_strlcpy (label_info->result_label, attr.attributeLabel, MAX_LABEL_SIZE);
      if (object_info.label.length() == 0)
        string_label.append (attr.attributeLabel).append(" ");
    }

    nvds_add_label_info_meta_to_classifier(classifier_meta, label_info);
  }

  if (string_label.length () > 0 && object_meta) {
    gchar *temp = object_meta->text_params.display_text;
    object_meta->text_params.display_text =
        g_strconcat (temp, " ", string_label.c_str (), nullptr);
    g_free (temp);
  }
  nvds_add_classifier_meta_to_object (object_meta, classifier_meta);
  nvds_release_meta_lock (batch_meta);
}


/**
 * Given an object's history, merge the new classification results with the
 * previous cached results. This can be used to improve the results of
 * classification when reinferencing over time. Currently, the function
 * just uses the latest results.
 */
void
merge_classification_output (GstNvInferObjectHistory & history,
    GstNvInferObjectInfo &new_result)
{
  history.cached_info.attributes.assign (new_result.attributes.begin (),
      new_result.attributes.end ());
  history.cached_info.label.assign (new_result.label);
}

static void
release_segmentation_meta (gpointer data, gpointer user_data)
{
  NvDsUserMeta *user_meta = (NvDsUserMeta *) data;
  NvDsInferSegmentationMeta *meta = (NvDsInferSegmentationMeta *) user_meta->user_meta_data;
  if (meta->priv_data) {
    gst_mini_object_unref (GST_MINI_OBJECT (meta->priv_data));
  } else {
    g_free (meta->class_map);
    g_free (meta->class_probabilities_map);
  }
  delete meta;
}

static gpointer
copy_segmentation_meta (gpointer data, gpointer user_data)
{
  NvDsUserMeta *src_user_meta = (NvDsUserMeta *) data;
  NvDsInferSegmentationMeta *src_meta = (NvDsInferSegmentationMeta *) src_user_meta->user_meta_data;
  NvDsInferSegmentationMeta *meta = (NvDsInferSegmentationMeta *) g_malloc (sizeof (NvDsInferSegmentationMeta));

  meta->classes = src_meta->classes;
  meta->width = src_meta->width;
  meta->height = src_meta->height;
  meta->class_map = (gint *) g_memdup(src_meta->class_map, meta->width * meta->height * sizeof (gint));
  meta->class_probabilities_map = (gfloat *) g_memdup(src_meta->class_probabilities_map, meta->classes * meta->width * meta->height * sizeof (gfloat));
  meta->priv_data = NULL;

  return meta;
}

void
attach_metadata_segmentation (GstNvInfer * nvinfer, GstMiniObject * tensor_out_object,
    GstNvInferFrame & frame, NvDsInferSegmentationOutput & segmentation_output)
{
  NvDsBatchMeta *batch_meta = (nvinfer->process_full_frame) ?
    frame.frame_meta->base_meta.batch_meta : frame.obj_meta->base_meta.batch_meta;

  NvDsUserMeta *user_meta = nvds_acquire_user_meta_from_pool (batch_meta);
  NvDsInferSegmentationMeta *meta = (NvDsInferSegmentationMeta *) g_malloc (sizeof (NvDsInferSegmentationMeta));

  meta->classes = segmentation_output.classes;
  meta->width = segmentation_output.width;
  meta->height = segmentation_output.height;
  meta->class_map = segmentation_output.class_map;
  meta->class_probabilities_map = segmentation_output.class_probability_map;
  meta->priv_data = gst_mini_object_ref (tensor_out_object);

  user_meta->user_meta_data = meta;
  user_meta->base_meta.meta_type = (NvDsMetaType) NVDSINFER_SEGMENTATION_META;
  user_meta->base_meta.release_func = release_segmentation_meta;
  user_meta->base_meta.copy_func = copy_segmentation_meta;

  if (nvinfer->process_full_frame) {
    nvds_add_user_meta_to_frame (frame.frame_meta, user_meta);
  } else {
    nvds_add_user_meta_to_obj (frame.obj_meta, user_meta);
  }
}

/* Called when NvDsUserMeta for each frame/object is released. Reduce the
 * refcount of the mini_object by 1 and free other memory. */
static void
release_tensor_output_meta (gpointer data, gpointer user_data)
{
  NvDsUserMeta *user_meta = (NvDsUserMeta *) data;
  NvDsInferTensorMeta *meta = (NvDsInferTensorMeta *) user_meta->user_meta_data;
  gst_mini_object_unref (GST_MINI_OBJECT (meta->priv_data));
  delete meta->out_buf_ptrs_dev;
  delete meta->out_buf_ptrs_host;
  delete meta;
}

/* Attaches the raw tensor output to the GstBuffer as metadata. */
void
attach_tensor_output_meta (GstNvInfer *nvinfer, GstMiniObject * tensor_out_object,
    GstNvInferBatch *batch, NvDsInferContextBatchOutput *batch_output)
{
  NvDsBatchMeta *batch_meta = (nvinfer->process_full_frame) ?
      batch->frames[0].frame_meta->base_meta.batch_meta :
      batch->frames[0].obj_meta->base_meta.batch_meta;

  /* Create and attach NvDsInferTensorMeta for each frame/object. Also
   * increment the refcount of GstNvInferTensorOutputObject. */
  for (size_t j = 0; j < batch->frames.size(); j++) {
    GstNvInferFrame &frame = batch->frames[j];
    NvDsInferTensorMeta *meta = new NvDsInferTensorMeta;
    meta->unique_id = nvinfer->unique_id;
    meta->num_output_layers = nvinfer->output_layers_info->size ();
    meta->output_layers_info = nvinfer->output_layers_info->data ();
    meta->out_buf_ptrs_host = new void *[meta->num_output_layers];
    meta->out_buf_ptrs_dev = new void *[meta->num_output_layers];
    meta->gpu_id = nvinfer->gpu_id;
    meta->priv_data = gst_mini_object_ref (tensor_out_object);

    for (unsigned int i = 0; i < meta->num_output_layers; i++) {
      NvDsInferLayerInfo & info = meta->output_layers_info[i];
      meta->out_buf_ptrs_dev[i] =
          (uint8_t *) batch_output->outputDeviceBuffers[i] +
          info.dims.numElements * get_element_size (info.dataType) * j;
      meta->out_buf_ptrs_host[i] =
          (uint8_t *) batch_output->hostBuffers[info.bindingIndex] +
          info.dims.numElements * get_element_size (info.dataType) * j;
    }

    NvDsUserMeta *user_meta = nvds_acquire_user_meta_from_pool (batch_meta);
    user_meta->user_meta_data = meta;
    user_meta->base_meta.meta_type =
        (NvDsMetaType) NVDSINFER_TENSOR_OUTPUT_META;
    user_meta->base_meta.release_func = release_tensor_output_meta;
    user_meta->base_meta.copy_func = nullptr;
    user_meta->base_meta.batch_meta = batch_meta;

    if (nvinfer->process_full_frame) {
      nvds_add_user_meta_to_frame (frame.frame_meta, user_meta);
    } else {
      nvds_add_user_meta_to_obj (frame.obj_meta, user_meta);
    }
  }
}
