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
#include "nvdssample_fasterRCNN_common.h"

#include <cassert>
#include <cstring>
#include <memory>

using namespace nvinfer1;
using namespace nvcaffeparser1;
using namespace plugin;

class FRCNNPluginFactory : public nvcaffeparser1::IPluginFactoryV2
{
public:
  virtual nvinfer1::IPluginV2* createPlugin(const char* layerName,
      const nvinfer1::Weights* weights, int nbWeights,
      const char* libNamespace) override
  {
    assert(isPluginV2(layerName));
    if (!strcmp(layerName, "RPROIFused"))
    {
      assert(mPluginRPROI == nullptr);
      assert(nbWeights == 0 && weights == nullptr);
      mPluginRPROI = std::unique_ptr<IPluginV2, decltype(pluginDeleter)>(
          createRPNROIPlugin(featureStride, preNmsTop, nmsMaxOut, iouThreshold,
              minBoxSize, spatialScale, DimsHW(poolingH, poolingW),
              Weights{nvinfer1::DataType::kFLOAT, anchorsRatios, anchorsRatioCount},
              Weights{nvinfer1::DataType::kFLOAT, anchorsScales, anchorsScaleCount}),
          pluginDeleter);
      mPluginRPROI.get()->setPluginNamespace(libNamespace);
      return mPluginRPROI.get();
    }
    else
    {
      assert(0);
      return nullptr;
    }
  }

  // caffe parser plugin implementation
  bool isPluginV2(const char* name) override { return !strcmp(name, "RPROIFused"); }

  void destroyPlugin()
  {
    mPluginRPROI.reset();
  }

  void (*pluginDeleter)(IPluginV2*){[](IPluginV2* ptr) { ptr->destroy(); }};
  std::unique_ptr<IPluginV2, decltype(pluginDeleter)> mPluginRPROI{nullptr, pluginDeleter};

  virtual ~FRCNNPluginFactory()
  {
  }
};
