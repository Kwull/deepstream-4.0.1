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

#include "NvCaffeParser.h"
#include "NvInferPlugin.h"
#include <memory>
#include <cassert>
#include <cstring>
#include "nvdssample_fasterRCNN_common.h"

using namespace nvinfer1;
using namespace nvcaffeparser1;
using namespace plugin;

// integration for serialization
class FRCNNPluginFactoryLegacy : public nvinfer1::IPluginFactory, public nvcaffeparser1::IPluginFactory
{
public:
  // deserialization plugin implementation
  virtual nvinfer1::IPlugin* createPlugin(const char* layerName,
      const nvinfer1::Weights* weights, int nbWeights) override
  {
    assert(isPlugin(layerName));
    if (!strcmp(layerName, "RPROIFused"))
    {
      assert(mPluginRPROI == nullptr);
      assert(nbWeights == 0 && weights == nullptr);
      mPluginRPROI = std::unique_ptr<INvPlugin, decltype(nvPluginDeleter)>(
          createFasterRCNNPlugin(featureStride, preNmsTop, nmsMaxOut,
              iouThreshold, minBoxSize, spatialScale, DimsHW(poolingH, poolingW),
              Weights{ nvinfer1::DataType::kFLOAT, anchorsRatios, anchorsRatioCount },
              Weights{ nvinfer1::DataType::kFLOAT, anchorsScales, anchorsScaleCount }),
          nvPluginDeleter);
      return mPluginRPROI.get();
    }
    else
    {
      assert(0);
      return nullptr;
    }
  }

  IPlugin* createPlugin(const char* layerName, const void* serialData,
      size_t serialLength) override
  {
    assert(isPlugin(layerName));
    if (!strcmp(layerName, "RPROIFused"))
    {
      //assert(mPluginRPROI == nullptr);
      mPluginRPROI = std::unique_ptr<INvPlugin, decltype(nvPluginDeleter)>(
          createFasterRCNNPlugin(serialData, serialLength), nvPluginDeleter);
      return mPluginRPROI.get();
    }
    else
    {
      assert(0);
      return nullptr;
    }
  }

  // caffe parser plugin implementation
  bool isPlugin(const char* name) override { return !strcmp(name, "RPROIFused"); }

  // User application destroys plugin when it is safe to do so.
  // Should be done after consumers of plugin (like ICudaEngine) are destroyed.
  void destroyPlugin()
  {
    mPluginRPROI.reset();
  }

  void(*nvPluginDeleter)(INvPlugin*) { [](INvPlugin* ptr) {ptr->destroy(); } };
  std::unique_ptr<INvPlugin, decltype(nvPluginDeleter)> mPluginRPROI{ nullptr, nvPluginDeleter };

  virtual ~FRCNNPluginFactoryLegacy()
  {
  }
};
