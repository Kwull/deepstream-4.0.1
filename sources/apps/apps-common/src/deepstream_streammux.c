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
#include "deepstream_streammux.h"


// Create bin, add queue and the element, link all elements and ghost pads,
// Set the element properties from the parsed config
gboolean
set_streammux_properties (NvDsStreammuxConfig *config, GstElement *element)
{
  gboolean ret = FALSE;

  g_object_set(G_OBJECT(element), "gpu-id",
               config->gpu_id, NULL);

  g_object_set (G_OBJECT (element), "nvbuf-memory-type",
        config->nvbuf_memory_type, NULL);

  g_object_set(G_OBJECT(element), "live-source",
               config->live_source, NULL);

  g_object_set(G_OBJECT(element),
               "batched-push-timeout", config->batched_push_timeout, NULL);

  if (config->batch_size){
      g_object_set(G_OBJECT(element), "batch-size",
          config->batch_size, NULL);
  }

  g_object_set(G_OBJECT(element), "enable-padding",
               config->enable_padding, NULL);

  if (config->pipeline_width && config->pipeline_height) {
    g_object_set(G_OBJECT(element), "width",
                 config->pipeline_width, NULL);
    g_object_set(G_OBJECT(element), "height",
                 config->pipeline_height, NULL);
  }
  return ret;
}
