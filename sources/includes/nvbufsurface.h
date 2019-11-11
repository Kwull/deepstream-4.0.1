/*
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

/**
 * @file nvbufsurface.h
 * <b>NvBufSurface Interface </b>
 *
 * This file specifies the NvBufSurface management APIs.
 *
 * NvBufSurface APIs provides methods to allocate / deallocate, map / unmap
 * and copy batched buffers.
 */

#ifndef NVBUFSURFACE_H_
#define NVBUFSURFACE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** @name NvBufSurface types and functions.
 * This section describes types and functions of NvBufSurface application
 * programming interface.
 */

/** @{ */

/** Default padding for reserved fields of structures. */
#define STRUCTURE_PADDING  4

/** Maximum number of planes */
#define NVBUF_MAX_PLANES   4

/**
 *  Defines mapping types of NvBufSurface.
 */
typedef enum
{
  NVBUF_MAP_READ,
  NVBUF_MAP_WRITE,
  NVBUF_MAP_READ_WRITE,
} NvBufSurfaceMemMapFlags;

/**
 * Defines color formats for NvBufSurface.
 */
typedef enum
{
  /** Invalid color format. */
  NVBUF_COLOR_FORMAT_INVALID,
  /** 8 bit GRAY scale - single plane */
  NVBUF_COLOR_FORMAT_GRAY8,
  /** BT.601 colorspace - YUV420 multi-planar. */
  NVBUF_COLOR_FORMAT_YUV420,
  /** BT.601 colorspace - YUV420 multi-planar. */
  NVBUF_COLOR_FORMAT_YVU420,
  /** BT.601 colorspace - YUV420 ER multi-planar. */
  NVBUF_COLOR_FORMAT_YUV420_ER,
  /** BT.601 colorspace - YVU420 ER multi-planar. */
  NVBUF_COLOR_FORMAT_YVU420_ER,
  /** BT.601 colorspace - Y/CbCr 4:2:0 multi-planar. */
  NVBUF_COLOR_FORMAT_NV12,
  /** BT.601 colorspace - Y/CbCr ER 4:2:0 multi-planar. */
  NVBUF_COLOR_FORMAT_NV12_ER,
  /** BT.601 colorspace - Y/CbCr 4:2:0 multi-planar. */
  NVBUF_COLOR_FORMAT_NV21,
  /** BT.601 colorspace - Y/CbCr ER 4:2:0 multi-planar. */
  NVBUF_COLOR_FORMAT_NV21_ER,
  /** BT.601 colorspace - YUV 4:2:2 planar. */
  NVBUF_COLOR_FORMAT_UYVY,
  /** BT.601 colorspace - YUV ER 4:2:2 planar. */
  NVBUF_COLOR_FORMAT_UYVY_ER,
  /** BT.601 colorspace - YUV 4:2:2 planar. */
  NVBUF_COLOR_FORMAT_VYUY,
  /** BT.601 colorspace - YUV ER 4:2:2 planar. */
  NVBUF_COLOR_FORMAT_VYUY_ER,
  /** BT.601 colorspace - YUV 4:2:2 planar. */
  NVBUF_COLOR_FORMAT_YUYV,
  /** BT.601 colorspace - YUV ER 4:2:2 planar. */
  NVBUF_COLOR_FORMAT_YUYV_ER,
  /** BT.601 colorspace - YUV 4:2:2 planar. */
  NVBUF_COLOR_FORMAT_YVYU,
  /** BT.601 colorspace - YUV ER 4:2:2 planar. */
  NVBUF_COLOR_FORMAT_YVYU_ER,
  /** BT.601 colorspace - YUV444 multi-planar. */
  NVBUF_COLOR_FORMAT_YUV444,
  /** RGBA-8-8-8-8 single plane. */
  NVBUF_COLOR_FORMAT_RGBA,
  /** BGRA-8-8-8-8 single plane. */
  NVBUF_COLOR_FORMAT_BGRA,
  /** ARGB-8-8-8-8 single plane. */
  NVBUF_COLOR_FORMAT_ARGB,
  /** ABGR-8-8-8-8 single plane. */
  NVBUF_COLOR_FORMAT_ABGR,
  /** RGBx-8-8-8-8 single plane. */
  NVBUF_COLOR_FORMAT_RGBx,
  /** BGRx-8-8-8-8 single plane. */
  NVBUF_COLOR_FORMAT_BGRx,
  /** xRGB-8-8-8-8 single plane. */
  NVBUF_COLOR_FORMAT_xRGB,
  /** xBGR-8-8-8-8 single plane. */
  NVBUF_COLOR_FORMAT_xBGR,
  /** RGB-8-8-8 single plane. */
  NVBUF_COLOR_FORMAT_RGB,
  /** BGR-8-8-8 single plane. */
  NVBUF_COLOR_FORMAT_BGR,
  /** BT.601 colorspace - Y/CbCr 4:2:0 10-bit multi-planar. */
  NVBUF_COLOR_FORMAT_NV12_10LE,
  /** BT.601 colorspace - Y/CbCr 4:2:0 12-bit multi-planar. */
  NVBUF_COLOR_FORMAT_NV12_12LE,
  /** BT.709 colorspace - YUV420 multi-planar. */
  NVBUF_COLOR_FORMAT_YUV420_709,
  /** BT.709 colorspace - YUV420 ER multi-planar. */
  NVBUF_COLOR_FORMAT_YUV420_709_ER,
  /** BT.709 colorspace - Y/CbCr 4:2:0 multi-planar. */
  NVBUF_COLOR_FORMAT_NV12_709,
  /** BT.709 colorspace - Y/CbCr ER 4:2:0 multi-planar. */
  NVBUF_COLOR_FORMAT_NV12_709_ER,
  /** BT.2020 colorspace - YUV420 multi-planar. */
  NVBUF_COLOR_FORMAT_YUV420_2020,
  /** BT.2020 colorspace - Y/CbCr 4:2:0 multi-planar. */
  NVBUF_COLOR_FORMAT_NV12_2020,

  NVBUF_COLOR_FORMAT_LAST
} NvBufSurfaceColorFormat;

/**
 * Defines Layout formats for NvBufSurface video planes.
 */
typedef enum
{
  /** Pitch Layout. */
  NVBUF_LAYOUT_PITCH,
  /** Block Linear Layout. */
  NVBUF_LAYOUT_BLOCK_LINEAR,
} NvBufSurfaceLayout;

/**
 * Defines memory types of NvBufSurface.
 */
typedef enum
{
  /** NVBUF_MEM_CUDA_DEVICE type for dGpu and NVBUF_MEM_SURFACE_ARRAY type for Jetson. */
  NVBUF_MEM_DEFAULT,
  /** CUDA Host memory type */
  NVBUF_MEM_CUDA_PINNED,
  /** CUDA Device memory type */
  NVBUF_MEM_CUDA_DEVICE,
  /** CUDA Unified memory type */
  NVBUF_MEM_CUDA_UNIFIED,
  /** NVRM Surface Array type - valid only for Jetson */
  NVBUF_MEM_SURFACE_ARRAY,
  /** NVRM Handle type - valid only for Jetson */
  NVBUF_MEM_HANDLE,
  /** malloced memory */
  NVBUF_MEM_SYSTEM,
} NvBufSurfaceMemType;

/**
 * Holds plane wise parameters of a buffer.
 */
typedef struct NvBufSurfacePlaneParams
{
  /** Number of planes */
  uint32_t num_planes;
  /** width of planes */
  uint32_t width[NVBUF_MAX_PLANES];
  /** height of planes */
  uint32_t height[NVBUF_MAX_PLANES];
  /** pitch of planes in bytes */
  uint32_t pitch[NVBUF_MAX_PLANES];
  /** offsets of planes in bytes */
  uint32_t offset[NVBUF_MAX_PLANES];
  /** size of planes in bytes */
  uint32_t psize[NVBUF_MAX_PLANES];
  /** bytes taken for each pixel */
  uint32_t bytesPerPix[NVBUF_MAX_PLANES];

  void * _reserved[STRUCTURE_PADDING * NVBUF_MAX_PLANES];
} NvBufSurfacePlaneParams;

/**
 * Hold parameters required to allocate NvBufSurface.
 */
typedef struct NvBufSurfaceCreateParams {
  /** GPU id - valid for multi GPU system */
  uint32_t gpuId;
  /** width of buffer */
  uint32_t width;
  /** height of buffer */
  uint32_t height;
  /** optional but if set memory of that size will be allocated and
   *  all other parameters (width, height etc.) will be ignored */
  uint32_t size;
  /** optional but if set contiguous memory is allocated for batch.
   *  valid for CUDA memory types only.*/
  bool isContiguous;
  /** color format of buffer */
  NvBufSurfaceColorFormat colorFormat;
  /** BL or PL for Jetson, ONLY PL in case of dGPU */
  NvBufSurfaceLayout layout;
  /** type of memory to be allocated */
  NvBufSurfaceMemType memType;
} NvBufSurfaceCreateParams;

/**
 * Hold the pointers of mapped buffer.
 */
typedef struct NvBufSurfaceMappedAddr {
  /** plane wise pointers to CPU mapped buffer */
  void * addr[NVBUF_MAX_PLANES];
  /** pointer to mapped EGLImage */
  void *eglImage;

  void * _reserved[STRUCTURE_PADDING];
} NvBufSurfaceMappedAddr;

/**
 * Hold the information of single buffer in the batch.
 */
typedef struct NvBufSurfaceParams {
  /** width of buffer */
  uint32_t width;
  /** height of buffer */
  uint32_t height;
  /** pitch of buffer */
  uint32_t pitch;
  /** color format */
  NvBufSurfaceColorFormat colorFormat;
  /** BL or PL for Jetson, ONLY PL in case of dGPU */
  NvBufSurfaceLayout layout;
  /** dmabuf fd in case of NVBUF_MEM_SURFACE_ARRAY and NVBUF_MEM_HANDLE type memory.
   *  Invalid for other types.
   */
  uint64_t bufferDesc;
  /** size of allocated memory */
  uint32_t dataSize;
  /** pointer to allocated memory, Not valid for
   *  NVBUF_MEM_SURFACE_ARRAY and NVBUF_MEM_HANDLE
   */
  void * dataPtr;
  /** plane wise info (w, h, p, offset etc.) */
  NvBufSurfacePlaneParams planeParams;
  /** pointers of mapped buffers. Null Initialized values.*/
  NvBufSurfaceMappedAddr mappedAddr;

  void * _reserved[STRUCTURE_PADDING];
} NvBufSurfaceParams;

/**
 * Hold the information of batched buffers.
 */
typedef struct NvBufSurface {
  /** GPU id - valid for multiple GPU system */
  uint32_t gpuId;
  /** batch size */
  uint32_t batchSize;
  /** valid / filled buffers, Zero initialized */
  uint32_t numFilled;
  /** check if memory allocated for batch is contiguous. */
  bool isContiguous;
  /** type of memory of buffers in batch */
  NvBufSurfaceMemType memType;
  /** Pointer to array of batched buffers */
  NvBufSurfaceParams *surfaceList;

  void * _reserved[STRUCTURE_PADDING];
} NvBufSurface;

/**
 * Allocate batch of buffers.
 *
 * Allocates memory for batchSize buffers and returns in *surf a pointer to allocated NvBufSurface.
 * params structure should have allocation parameters of single buffer. If size field in
 * params is set, buffer of that size will be allocated and all other
 * parameters (w, h, color format etc.) will be ignored.
 *
 * Use NvBufSurfaceDestroy to free all the resources.
 *
 * @param[out] surf pointer to allocated batched buffers.
 * @param[in] batchSize batch size of buffers.
 * @param[in] params pointer to NvBufSurfaceCreateParams structure.
 *
 * @return 0 for success, -1 for failure.
 */
int NvBufSurfaceCreate (NvBufSurface **surf, uint32_t batchSize,
                        NvBufSurfaceCreateParams *params);

/**
 * Free the batched buffers previously allocated through NvBufSurfaceCreate.
 *
 * @param[in] surf pointer to NvBufSurface to free.
 *
 * @return 0 for success, -1 for failure.
 */
int NvBufSurfaceDestroy (NvBufSurface *surf);

/**
 * Map HW batched buffers to HOST / CPU address space.
 *
 * Valid for NVBUF_MEM_CUDA_UNIFIED type of memory for dGPU and
 * NVBUF_MEM_SURFACE_ARRAY and NVBUF_MEM_HANDLE type of memory for Jetson.
 *
 * This function will fill addr array of NvBufSurfaceMappedAddr field of NvBufSurfaceParams
 * with the CPU mapped memory pointers.
 *
 * The client must call NvBufSurfaceSyncForCpu() with the virtual address populated
 * by this function before accessing the mapped memory in CPU.
 *
 * After memory mapping is complete, mapped memory modification
 * must be coordinated between the CPU and hardware device as
 * follows:
 * - CPU: If the CPU modifies any mapped memory, the client must call
 *   NvBufSurfaceSyncForDevice() before any hardware device accesses the memory.
 * - Hardware device: If the mapped memory is modified by any hardware device,
 *   the client must call NvBufSurfaceSyncForCpu() before CPU accesses the memory.
 *
 * Use NvBufSurfaceUnMap() to unmap buffer(s) and release any resource.
 *
 * @param[in] surf pointer to NvBufSurface structure.
 * @param[in] index index of buffer in the batch. -1 for all buffers in batch.
 * @param[in] plane index of plane in buffer. -1 for all planes in buffer.
 * @param[in] type flag for mapping type.
 *
 * @return 0 for success, -1 for failure.
 */
int NvBufSurfaceMap (NvBufSurface *surf, int index, int plane, NvBufSurfaceMemMapFlags type);

/**
 * Unmap the previously mapped buffer(s).
 *
 * @param[in] surf pointer to NvBufSurface structure.
 * @param[in] index index of buffer in the batch. -1 for all buffers in batch.
 * @param[in] plane index of plane in buffer. -1 for all planes in buffer.
 *
 * @return 0 for success, -1 for failure.
 */
int NvBufSurfaceUnMap (NvBufSurface *surf, int index, int plane);

/**
 * Copy the memory content of source batched buffer(s) to memory of destination
 * batched buffer(s).
 *
 * This function can be used to copy source buffer(s) of one memory type
 * to destination buffer(s) of different memory type.
 * e.g. CUDA Host to CUDA Device or malloced memory to CUDA device etc.
 *
 * Both source and destination NvBufSurface must have same buffer and batch size.
 *
 * @param[in] srcSurf pointer to source NvBufSurface structure.
 * @param[in] dstSurf pointer to destination NvBufSurface structure.
 *
 * @return 0 for success, -1 for failure.
 */
int NvBufSurfaceCopy (NvBufSurface *srcSurf, NvBufSurface *dstSurf);

/**
 * Syncs the HW memory cache for the CPU.
 *
 * Valid only for NVBUF_MEM_SURFACE_ARRAY and NVBUF_MEM_HANDLE memory types.
 *
 * @param[in] surf pointer to NvBufSurface structure.
 * @param[in] index index of buffer in the batch. -1 for all buffers in batch.
 * @param[in] plane index of plane in buffer. -1 for all planes in buffer.
 *
 * @return 0 for success, -1 for failure.
 */
int NvBufSurfaceSyncForCpu (NvBufSurface *surf, int index, int plane);

/**
 * Syncs the HW memory cache for the device.
 *
 * Valid only for NVBUF_MEM_SURFACE_ARRAY and NVBUF_MEM_HANDLE memory types.
 *
 * @param[in] surf pointer to NvBufSurface structure.
 * @param[in] index index of buffer in the batch. -1 for all buffers in batch.
 * @param[in] plane index of plane in buffer. -1 for all planes in buffer.
 *
 * @return 0 for success, -1 for failure.
 */
int NvBufSurfaceSyncForDevice (NvBufSurface *surf, int index, int plane);

/**
 * Get the NvBufSurface from the dmabuf fd.
 *
 * @param[in] dmabuf_fd dmabuf fd of the buffer.
 * @param[out] buffer pointer to NvBufSurface.
 *
 * @return 0 for success, -1 for failure.
 */
int NvBufSurfaceFromFd (int dmabuf_fd, void **buffer);

/**
 * Fill each byte of buffer(s) in NvBufSurface with provided value.
 *
 * This function can also be used to reset the buffer(s) in the batch.
 *
 * @param[in] surf pointer to NvBufSurface structure.
 * @param[in] index index of buffer in the batch. -1 for all buffers in batch.
 * @param[in] plane index of plane in buffer. -1 for all planes in buffer.
 * @param[in] value value to be set.
 *
 * @return 0 for success, -1 for failure.
 */
int NvBufSurfaceMemSet (NvBufSurface *surf, int index, int plane, uint8_t value);

/**
 * Creates an EGLImage from memory of NvBufSurface buffer(s).
 *
 * Only memory type NVBUF_MEM_SURFACE_ARRAY is supported.
 * This function will set eglImage pointer of NvBufSurfaceMappedAddr field of NvBufSurfaceParams
 * with EGLImageKHR.
 *
 * This function can be used in scenarios where CUDA operation on Jetson HW
 * memory (NVBUF_MEM_SURFACE_ARRAY) is required. EGLImageKHR provided by this
 * function can then be register with CUDA for further CUDA operations.
 *
 * @param[in] surf pointer to NvBufSurface structure.
 * @param[in] index index of buffer in the batch. -1 for all buffers in batch.
 *
 * @return 0 for success, -1 for failure.
 */
int NvBufSurfaceMapEglImage (NvBufSurface *surf, int index);

/**
 * Destroy the previously created EGLImage(s).
 *
 * @param[in] surf pointer to NvBufSurface structure.
 * @param[in] index index of buffer in the batch. -1 for all buffers in batch.
 *
 * @return 0 for success, -1 for failure.
 */
int NvBufSurfaceUnMapEglImage (NvBufSurface *surf, int index);

/** @} */

#ifdef __cplusplus
}
#endif
#endif /* NVBUFSURFACE_H_ */
