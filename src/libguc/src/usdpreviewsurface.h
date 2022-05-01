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
