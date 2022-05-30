//
// Copyright 2022 Pablo Delgado Krämer
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once

#include <cgltf.h>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdShade/shader.h>

#include "image.h"

using namespace PXR_NS;

namespace guc
{
  class UsdPreviewSurfaceMaterialConverter
  {
  public:
    UsdPreviewSurfaceMaterialConverter(UsdStageRefPtr stage,
                                       const ImageMetadataMap& imageMetadataMap);

    void convert(const cgltf_material* material, const SdfPath& path);

  private:
    UsdStageRefPtr m_stage;
    const ImageMetadataMap& m_imageMetadataMap;

  private:
    void setNormalTextureInput(const SdfPath& basePath,
                               UsdShadeInput& shaderInput,
                               const cgltf_texture_view& textureView);

    void setOcclusionTextureInput(const SdfPath& basePath,
                                  UsdShadeInput& shaderInput,
                                  const cgltf_texture_view& textureView);

    void setSrgbTextureInput(const SdfPath& basePath,
                             UsdShadeInput& shaderInput,
                             const cgltf_texture_view& textureView,
                             const GfVec4f& factor,
                             const GfVec4f* fallback = nullptr);

    void setFloatTextureInput(const SdfPath& basePath,
                              UsdShadeInput& shaderInput,
                              const cgltf_texture_view& textureView,
                              const TfToken& channel,
                              const GfVec4f& factor,
                              const GfVec4f* fallback = nullptr);

  private:
    void setTextureInput(const SdfPath& basePath,
                         UsdShadeInput& shaderInput,
                         const cgltf_texture_view& textureView,
                         const TfToken& channels,
                         const TfToken& colorSpace,
                         const GfVec4f* scale,
                         const GfVec4f* bias,
                         const GfVec4f* fallback);

    bool addTextureNode(const SdfPath& basePath,
                        const cgltf_texture_view& textureView,
                        const TfToken& colorSpace,
                        const GfVec4f* scale,
                        const GfVec4f* bias,
                        const GfVec4f* fallback,
                        UsdShadeShader& node);

    void setStPrimvarInput(UsdShadeInput& input, const SdfPath& nodeBasePath, int stIndex);

  private:
    bool getTextureMetadata(const cgltf_texture_view& textureView, ImageMetadata& metadata) const;
    bool getTextureFileName(const cgltf_texture_view& textureView, std::string& fileName) const;
    int getTextureChannelCount(const cgltf_texture_view& textureView) const;
  };
}
