/**
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 */

/**
 * @file nvbufsurftransform.h
 * <b>NvBufSurfTransform Interface </b>
 *
 * This file specifies the NvBufSurfTransform image transformation APIs.
 *
 * NvBufSurfTransform APIs provides methods to set / get session parameters
 * and transform / composite APIs.
 */
#ifndef NVBUFSURFTRANSFORM_H_
#define NVBUFSURFTRANSFORM_H_
#include <stdio.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <npp.h>
#include "nvbufsurface.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @name NvBufSurfTransform types and functions.
 * This section describes types and functions of NvBufSurfTransform application
 * programming interface.
 */

/** @{ */
/**
 *  Defines compute devices used by NvBufSurfTransform.
 */
typedef enum
{
  /** Use VIC as compute device for Jetson or GPU for x86_64 system */
  NvBufSurfTransformCompute_Default,
  /** Use GPU as compute device */
  NvBufSurfTransformCompute_GPU,
  /** Use VIC as compute device, only applicable for Jetson */
  NvBufSurfTransformCompute_VIC
} NvBufSurfTransform_Compute;


/**
 * Defines video flip methods. Only Supported for Jetson
 */
typedef enum
{
  /** Video flip none. */
  NvBufSurfTransform_None,
  /** Video flip rotate 90 degree clockwise. */
  NvBufSurfTransform_Rotate90,
  /** Video flip rotate 180 degree clockwise. */
  NvBufSurfTransform_Rotate180,
  /** Video flip rotate 270 degree clockwise. */
  NvBufSurfTransform_Rotate270,
  /** Video flip with respect to X-axis. */
  NvBufSurfTransform_FlipX,
  /** Video flip with respect to Y-axis. */
  NvBufSurfTransform_FlipY,
  /** Video flip transpose. */
  NvBufSurfTransform_Transpose,
  /** Video flip inverse transpose. */
  NvBufSurfTransform_InvTranspose,
} NvBufSurfTransform_Flip;


/**
 * Defines video interpolation methods.
 */
typedef enum
{
  /** Nearest Interpolation Method */
  NvBufSurfTransformInter_Nearest = 0,
  /** Bilinear Interpolation Method */
  NvBufSurfTransformInter_Bilinear,
  /** GPU-Cubic, VIC-5 Tap  */
  NvBufSurfTransformInter_Algo1,
  /** GPU-Super, VIC-10 Tap  */
  NvBufSurfTransformInter_Algo2,
  /** GPU-Lanzos, VIC-Smart*/
  NvBufSurfTransformInter_Algo3,
  /** GPU-Ignored, VIC-Nicest*/
  NvBufSurfTransformInter_Algo4,
  /** GPU-Nearest, VIC-Nearest */
  NvBufSurfTransformInter_Default
} NvBufSurfTransform_Inter;

/**
 * Defines Error codes returned by NvBufSurfTransform APIs.
 */
typedef enum
{
  /** Error in source or destination ROI */
  NvBufSurfTransformError_ROI_Error = -4,
  /** Invalid input parameters */
  NvBufSurfTransformError_Invalid_Params = -3,
  /** Runtime execution Error */
  NvBufSurfTransformError_Execution_Error = -2,
  /** Unsupported Feature/Format */
  NvBufSurfTransformError_Unsupported = -1,
  /** Operation successful */
  NvBufSurfTransformError_Success = 0
} NvBufSurfTransform_Error;

/**
 * Defines flags to indicate for valid transform.
 */
typedef enum {
  /** transform flag to crop source rectangle. */
  NVBUFSURF_TRANSFORM_CROP_SRC   = 1,
  /** transform flag to crop destination rectangle. */
  NVBUFSURF_TRANSFORM_CROP_DST   = 1 << 1,
  /** transform flag to set filter type. */
  NVBUFSURF_TRANSFORM_FILTER     = 1 << 2,
  /** transform flag to set flip method. */
  NVBUFSURF_TRANSFORM_FLIP       = 1 << 3,
} NvBufSurfTransform_Transform_Flag;

/**
 * Defines flags that specify valid composition operations.
 */
typedef enum {
  /** flag to set for composition. */
  NVBUFSURF_TRANSFORM_COMPOSITE  = 1,
} NvBufSurfTransform_Composite_Flag;

/**
 * Holds coordinates for a rectangle.
 */
typedef struct
{
  /** rectangle top. */
  uint32_t top;
  /** rectangle left. */
  uint32_t left;
  /** rectangle width. */
  uint32_t width;
  /** rectangle height. */
  uint32_t height;
}NvBufSurfTransformRect;

/**
 * Holds configuration parameters for Transform/Composite Session.
 */
typedef struct _NvBufSurfTransformConfigParams
{
  /** Mode of operation, VIC (Jetson) or GPU (iGPU + dGPU) if VIC configured,
   * gpu_id will be ignored */
  NvBufSurfTransform_Compute compute_mode;

  /** GPU ID to be used for processing */
  int32_t gpu_id;

  /** User configure stream to be used, if NULL
   * default stream will be used
   * ignored if VIC is used */
  cudaStream_t cuda_stream;

} NvBufSurfTransformConfigParams;

/**
 * Holds Transform parameters for Transform Call.
 */
typedef struct _NvBufSurfaceTransformParams
{
  /** flag to indicate which of the transform
   *parameters are valid. */
  uint32_t transform_flag;
  /** flip method. */
  NvBufSurfTransform_Flip transform_flip;
  /** transform filter. */
  NvBufSurfTransform_Inter transform_filter;
  /** list of source rectangle coordinates
   * for crop operation */
  NvBufSurfTransformRect *src_rect;
  /** list of destination rectangle
   * coordinates for crop operation. */
  NvBufSurfTransformRect *dst_rect;
}NvBufSurfTransformParams;

/**
 * Holds Composite parameters for Composite Call.
 */
typedef struct _NvBufSurfTransformCompositeParams
{
  /** flag to indicate which of the composition parameters are valid. */
  uint32_t composite_flag;
  /** number of the input buffers to be composited. */
  uint32_t input_buf_count;
 /** source rectangle coordinates of input buffers for composition. */
  NvBufSurfTransformRect *src_comp_rect;
  /** destination rectangle coordinates of input buffers for composition. */
  NvBufSurfTransformRect *dst_comp_rect;
}NvBufSurfTransformCompositeParams;

/**
 * Set user defined session parameters to be used, if default session is not
 * to be used by NvBufSurfTransform
 *
 * @param[in] config_params pointer, populated with session params to be used.
 *
 * @return NvBufSurfTransform_Error notifying succes or failure.
 */
NvBufSurfTransform_Error NvBufSurfTransformSetSessionParams
(NvBufSurfTransformConfigParams *config_params);

/**
 * Get current session parameters used by NvBufSurfTransform
 *
 * @param[out] config_params pointer(caller allocated), populated with session params used.
 *
 * @return NvBufSurfTransform_Error notifying succes or failure.
 */

NvBufSurfTransform_Error NvBufSurfTransformGetSessionParams
(NvBufSurfTransformConfigParams *config_params);

/**
 * Performs Transformation on batched input images
 *
 * Transforms batched input pointed by src pointer. Transformation includes
 * scaling / format conversion croping for both source and destination, and
 * all of the above in combination, flip / rotation is supported on VIC, dst
 * pointer is user allocated. Type of Transformation to be done is set in transform_params
 * In case of destination cropping the memory other than crop location is not
 * touched and may have stale information,  its callers
 * responsibility to memset it if required before sending for transformation
 *
 * Use NvBufSurfTransformSetSessionParams before each call, if user defined
 * session parameters are to be used.
 *
 * @param[in] src pointer to input batched buffers to be transformed.
 * @param[out] dst pointer to where transformed output would be stored.
 * @param[in] transform_params pointer to NvBufSurfTransformParams structure.
 *
 * @return NvBufSurfTransform_Error indicating success or failure.
 */
NvBufSurfTransform_Error NvBufSurfTransform (NvBufSurface *src, NvBufSurface *dst,
    NvBufSurfTransformParams *transform_params);

/**
 * Performs Composition on batched input images
 *
 * Composites batched input pointed by src pointer. Compositer scales and stitches
 * batched buffers pointed by src into single dst buffer, the parameters for
 * location to be composited is provided by composite_params
 * Use NvBufSurfTransformSetSessionParams before each call, if user defined
 * session parameters are to be used.
 *
 * @param[in] src pointer to input batched buffers to be transformed.
 * @param[out] dst pointer (single buffer) where composited output would be stored.
 * @param[in] composite_params pointer to NvBufSurfTransformCompositeParams structure.
 *
 * @return NvBufSurfTransform_Error indicating success or failure.
 */
NvBufSurfTransform_Error NvBufSurfTransformComposite (NvBufSurface *src,
    NvBufSurface *dst, NvBufSurfTransformCompositeParams *composite_params);

#ifdef __cplusplus
}
#endif
#endif
