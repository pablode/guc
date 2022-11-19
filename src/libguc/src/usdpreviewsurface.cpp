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

#include "usdpreviewsurface.h"

#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdMtlx/reader.h>

#include <filesystem>

#include "naming.h"
#include "debugCodes.h"

namespace fs = std::filesystem;

TF_DEFINE_PRIVATE_TOKENS(
  _tokens,
  // Shading node IDs
  (UsdPreviewSurface)
  (UsdUVTexture)
  (UsdPrimvarReader_float2)
  // UsdPreviewSurface inputs
  (emissiveColor)
  (occlusion)
  (normal)
  (opacityThreshold)
  (diffuseColor)
  (opacity)
  (metallic)
  (roughness)
  (clearcoat)
  (clearcoatRoughness)
  (ior)
  (specularColor)
  (useSpecularWorkflow)
  // UsdUVTexture inputs
  (st)
  (file)
  (scale)
  (bias)
  (fallback)
  (wrapS)
  (wrapT)
  (sourceColorSpace)
  // UsdUVTexture outputs
  (r)
  (g)
  (b)
  (a)
  (rgb)
  // UsdUVTexture wrap modes
  (clamp)
  (mirror)
  (repeat)
  // UsdUVTexture color spaces
  (raw)
  (sRGB)
  // UsdPrimvarReader_float2 input and output
  (varname)
  (result)
);

namespace detail
{
  void setChannelInputValues(const UsdShadeInput& input, GfVec4f value, const TfToken& channels)
  {
    if (channels == _tokens->rgb) {
      input.Set(GfVec3f(value[0], value[1], value[2]));
    }
    else if (channels == _tokens->r) {
      input.Set(value[0]);
    }
    else if (channels == _tokens->g) {
      input.Set(value[1]);
    }
    else if (channels == _tokens->b) {
      input.Set(value[2]);
    }
    else if (channels == _tokens->a) {
      input.Set(value[3]);
    }
    else {
      TF_CODING_ERROR("unhandled input channel");
    }
  }

  TfToken convertWrapMode(int wrapMode)
  {
    switch (wrapMode)
    {
    case 33071 /* CLAMP_TO_EDGE */:
      return _tokens->clamp;
    case 33648 /* MIRRORED_REPEAT */:
      return _tokens->mirror;
    default:
      TF_CODING_ERROR("invalid wrap mode");
    case 0:
      // use glTF default
    case 10497 /* REPEAT */:
      return _tokens->repeat;
    }
  }

  void connectTextureInputOutput(UsdShadeInput& input, UsdShadeShader& node, const TfToken& channels)
  {
    auto valueType = channels == _tokens->rgb ? SdfValueTypeNames->Float3 : SdfValueTypeNames->Float;
    auto output = node.CreateOutput(channels, valueType);
    input.ConnectToSource(output);
  }
}

namespace guc
{
  UsdPreviewSurfaceMaterialConverter::UsdPreviewSurfaceMaterialConverter(UsdStageRefPtr stage,
                                                                         const ImageMetadataMap& imageMetadataMap)
    : m_stage(stage)
    , m_imageMetadataMap(imageMetadataMap)
  {
  }

  void UsdPreviewSurfaceMaterialConverter::convert(const cgltf_material* material, const SdfPath& path)
  {
    auto shadeMaterial = UsdShadeMaterial::Define(m_stage, path);
    auto surfaceOutput = shadeMaterial.CreateSurfaceOutput(UsdShadeTokens->universalRenderContext);

    // FIXME: the first node will be called 'node' while MaterialX's first node is 'node1'
    const char* nodeNameNumberDelimiter = ""; // mimic MaterialX nodename generation with no delimiter between "node" and number
    auto shaderPath = makeUniqueStageSubpath(m_stage, path.GetAsString(), "node", nodeNameNumberDelimiter);
    auto shader = UsdShadeShader::Define(m_stage, shaderPath);
    shader.CreateIdAttr(VtValue(_tokens->UsdPreviewSurface));
    auto shaderOutput = shader.CreateOutput(UsdShadeTokens->surface, SdfValueTypeNames->Token);
    surfaceOutput.ConnectToSource(shaderOutput);

    auto emissiveColorInput = shader.CreateInput(_tokens->emissiveColor, SdfValueTypeNames->Float3);
    setSrgbTextureInput(path, emissiveColorInput, material->emissive_texture, GfVec4f(material->emissive_factor));

    auto occlusionInput = shader.CreateInput(_tokens->occlusion, SdfValueTypeNames->Float);
    setOcclusionTextureInput(path, occlusionInput, material->occlusion_texture);

    auto normalInput = shader.CreateInput(_tokens->normal, SdfValueTypeNames->Float3);
    setNormalTextureInput(path, normalInput, material->normal_texture);

    // We need to set these values regardless of whether pbrMetallicRoughness is present or not, because UsdPreviewSurface's
    // default values differ (and we want to come as close as possible to the MaterialX look, although the shading model differs).
    auto diffuseColorInput = shader.CreateInput(_tokens->diffuseColor, SdfValueTypeNames->Float3);
    auto opacityInput = shader.CreateInput(_tokens->opacity, SdfValueTypeNames->Float);
    auto metallicInput = shader.CreateInput(_tokens->metallic, SdfValueTypeNames->Float);
    auto roughnessInput = shader.CreateInput(_tokens->roughness, SdfValueTypeNames->Float);

    if (material->has_pbr_metallic_roughness)
    {
      const cgltf_pbr_metallic_roughness* pbrMetallicRoughness = &material->pbr_metallic_roughness;

      GfVec4f diffuseColorFallback(1.0f); // same as glTF spec sec. 5.22.2: "When undefined, the texture MUST be sampled as having 1.0 in all components."
      setSrgbTextureInput(path, diffuseColorInput, pbrMetallicRoughness->base_color_texture, GfVec4f(pbrMetallicRoughness->base_color_factor), &diffuseColorFallback);
      GfVec4f metallicRoughnessFallback(1.0f); // same as glTF spec sec. 5.22.5: "When undefined, the texture MUST be sampled as having 1.0 in G and B components."
      setFloatTextureInput(path, metallicInput, pbrMetallicRoughness->metallic_roughness_texture, _tokens->b, GfVec4f(pbrMetallicRoughness->metallic_factor), &metallicRoughnessFallback);
      setFloatTextureInput(path, roughnessInput, pbrMetallicRoughness->metallic_roughness_texture, _tokens->g, GfVec4f(pbrMetallicRoughness->roughness_factor), &metallicRoughnessFallback);

      if (material->alpha_mode != cgltf_alpha_mode_opaque)
      {
        GfVec4f opacityFallback(1.0f); // image fallback value is 0.0, but opacity default should be 1.0
        setFloatTextureInput(path, opacityInput, pbrMetallicRoughness->base_color_texture, _tokens->a, GfVec4f(pbrMetallicRoughness->base_color_factor[3]), &opacityFallback);

        if (material->alpha_mode == cgltf_alpha_mode_mask)
        {
          auto opacityThresholdInput = shader.CreateInput(_tokens->opacityThreshold, SdfValueTypeNames->Float);
          opacityThresholdInput.Set(material->alpha_cutoff);
        }
      }
    }
    else
    {
      diffuseColorInput.Set(GfVec3f(1.0f)); // 0.18 in UsdPreviewSurface spec
      opacityInput.Set(1.0f);
      metallicInput.Set(1.0f); // 0.0 in UsdPreviewSurface spec
      roughnessInput.Set(1.0f); // 0.5 in UsdPreviewSurface spec
    }

    if (material->has_clearcoat)
    {
      const cgltf_clearcoat* clearcoat = &material->clearcoat;

      // see glTF clearcoat extension spec: "If the clearcoatTexture or clearcoatRoughnessTexture is not given, respective texture components are assumed to have a value of 1.0."
      GfVec4f clearcoatFallback(1.0f);
      GfVec4f clearcoatRoughnessFallback(1.0f);

      auto clearcoatInput = shader.CreateInput(_tokens->clearcoat, SdfValueTypeNames->Float);
      setFloatTextureInput(path, clearcoatInput, clearcoat->clearcoat_texture, _tokens->r, GfVec4f(clearcoat->clearcoat_factor), &clearcoatFallback);

      auto clearcoatRoughnessInput = shader.CreateInput(_tokens->clearcoatRoughness, SdfValueTypeNames->Float);
      setFloatTextureInput(path, clearcoatRoughnessInput, clearcoat->clearcoat_roughness_texture, _tokens->g, GfVec4f(clearcoat->clearcoat_roughness_factor), &clearcoatRoughnessFallback);
    }

    if (material->has_ior)
    {
      auto iorInput = shader.CreateInput(_tokens->ior, SdfValueTypeNames->Float);

      const cgltf_ior* ior = &material->ior;
      iorInput.Set(ior->ior);
    }

    if (material->has_specular)
    {
      const cgltf_specular* specular = &material->specular;

      GfVec4f specularColorFallback(1.0f); // use default from glTF specular ext spec

      auto specularColorInput = shader.CreateInput(_tokens->specularColor, SdfValueTypeNames->Float3);
      setSrgbTextureInput(path, specularColorInput, specular->specular_color_texture, GfVec4f(specular->specular_color_factor), &specularColorFallback);

      auto useSpecularWorkflowInput = shader.CreateInput(_tokens->useSpecularWorkflow, SdfValueTypeNames->Int);
      useSpecularWorkflowInput.Set(1);
    }
  }

  void UsdPreviewSurfaceMaterialConverter::setNormalTextureInput(const SdfPath& basePath,
                                                                 UsdShadeInput& shaderInput,
                                                                 const cgltf_texture_view& textureView)
  {
    // glTF spec 2.0 3.9.3: transform [0, 1] value range to [-1, 1].
    // We also scale the normal although this does not guarantee that the resulting vector is normalized.
    float xyScale = 2.0f * textureView.scale;
    float xyBias = -1.0f * textureView.scale;
    GfVec4f scale(xyScale, xyScale, 2.0f, 0.0f);
    GfVec4f bias(xyBias, xyBias, -1.0f, 0.0f);
    GfVec4f fallback(0.5f, 0.5f, 1.0f, 0.0f); // glTF fallback normal

    UsdShadeShader textureNode;
    if (!addTextureNode(basePath, textureView, _tokens->raw, &scale, &bias, &fallback, textureNode))
    {
      return;
    }

    auto stInput = textureNode.CreateInput(_tokens->st, SdfValueTypeNames->Float2);
    setStPrimvarInput(stInput, basePath, textureView.texcoord);

    detail::connectTextureInputOutput(shaderInput, textureNode, _tokens->rgb);
  }

  void UsdPreviewSurfaceMaterialConverter::setOcclusionTextureInput(const SdfPath& basePath,
                                                                    UsdShadeInput& shaderInput,
                                                                    const cgltf_texture_view& textureView)
  {
    // glTF spec 2.0 3.9.3.
    // if 'strength' attribute is present, it affects occlusion as follows:
    //     1.0 + strength * (occlusionTexture - 1.0)
    //
    // we multiply that out since we only have scale and bias (value * scale + bias) to
    //     occlusionTexture * strength + (1.0 - strength)
    //     ----------------   ++++++++   ~~~~~~~~~~~~~~~~
    //         (value)      *   scale  +        bias
    GfVec4f scale(textureView.scale);
    GfVec4f bias(1.0f - textureView.scale);
    GfVec4f fallback(1.0f); // image fallback value is 0.0, but default occlusion value should be 1.0

    UsdShadeShader textureNode;
    if (!addTextureNode(basePath, textureView, _tokens->raw, &scale, &bias, &fallback, textureNode))
    {
      return;
    }

    auto stInput = textureNode.CreateInput(_tokens->st, SdfValueTypeNames->Float2);
    setStPrimvarInput(stInput, basePath, textureView.texcoord);

    detail::connectTextureInputOutput(shaderInput, textureNode, _tokens->r);
  }

  void UsdPreviewSurfaceMaterialConverter::setSrgbTextureInput(const SdfPath& basePath,
                                                               UsdShadeInput& shaderInput,
                                                               const cgltf_texture_view& textureView,
                                                               const GfVec4f& factor,
                                                               const GfVec4f* fallback)
  {
    setTextureInput(basePath, shaderInput, textureView, _tokens->rgb, _tokens->sRGB, &factor, nullptr, fallback);
  }

  void UsdPreviewSurfaceMaterialConverter::setFloatTextureInput(const SdfPath& basePath,
                                                                UsdShadeInput& shaderInput,
                                                                const cgltf_texture_view& textureView,
                                                                const TfToken& channel,
                                                                const GfVec4f& factor,
                                                                const GfVec4f* fallback)
  {
    setTextureInput(basePath, shaderInput, textureView, channel, _tokens->raw, &factor, nullptr, fallback);
  }

  void UsdPreviewSurfaceMaterialConverter::setTextureInput(const SdfPath& basePath,
                                                           UsdShadeInput& shaderInput,
                                                           const cgltf_texture_view& textureView,
                                                           const TfToken& channels,
                                                           const TfToken& colorSpace,
                                                           const GfVec4f* scale,
                                                           const GfVec4f* bias,
                                                           const GfVec4f* fallback)
  {
    UsdShadeShader textureNode;
    if (addTextureNode(basePath, textureView, colorSpace, scale, bias, fallback, textureNode))
    {
      // "If a two-channel texture is fed into a UsdUVTexture, the r, g, and b components of the rgb output will
      // repeat the first channel's value, while the single a output will be set to the second channel's value."
      int channelCount = getTextureChannelCount(textureView);
      bool remapChannelToAlpha = (channelCount == 2 && channels == _tokens->g);

      auto stInput = textureNode.CreateInput(_tokens->st, SdfValueTypeNames->Float2);
      setStPrimvarInput(stInput, basePath, textureView.texcoord);

      detail::connectTextureInputOutput(shaderInput, textureNode, remapChannelToAlpha ? _tokens->a : channels);
    }
    else if (scale)
    {
      detail::setChannelInputValues(shaderInput, *scale, channels);
    }
  }

  bool UsdPreviewSurfaceMaterialConverter::addTextureNode(const SdfPath& basePath,
                                                          const cgltf_texture_view& textureView,
                                                          const TfToken& colorSpace,
                                                          const GfVec4f* scale,
                                                          const GfVec4f* bias,
                                                          const GfVec4f* fallback,
                                                          UsdShadeShader& node)
  {
    std::string filePath;
    if (!getTextureFilePath(textureView, filePath))
    {
      return false;
    }

    auto nodePath = makeUniqueStageSubpath(m_stage, basePath.GetAsString(), "node", "");
    node = UsdShadeShader::Define(m_stage, nodePath);
    node.CreateIdAttr(VtValue(_tokens->UsdUVTexture));

    auto fileInput = node.CreateInput(_tokens->file, SdfValueTypeNames->Asset);
    fileInput.Set(SdfAssetPath(filePath));

    if (scale)
    {
      auto scaleInput = node.CreateInput(_tokens->scale, SdfValueTypeNames->Float4);
      scaleInput.Set(*scale);
    }

    if (bias)
    {
      auto biasInput = node.CreateInput(_tokens->bias, SdfValueTypeNames->Float4);
      biasInput.Set(*bias);
    }

    if (fallback)
    {
      auto fallbackInput = node.CreateInput(_tokens->fallback, SdfValueTypeNames->Float4);
      fallbackInput.Set(*fallback);
    }

    auto sourceColorSpaceInput = node.CreateInput(_tokens->sourceColorSpace, SdfValueTypeNames->Token);
    sourceColorSpaceInput.Set(colorSpace);

    const cgltf_sampler* sampler = textureView.texture->sampler;

    // glTF spec sec. 5.29.1. texture sampler: "When undefined, a sampler with repeat wrapping [..] SHOULD be used."
    auto wrapSInput = node.CreateInput(_tokens->wrapS, SdfValueTypeNames->Token);
    wrapSInput.Set(sampler ? detail::convertWrapMode(sampler->wrap_s) : _tokens->repeat);

    auto wrapTInput = node.CreateInput(_tokens->wrapT, SdfValueTypeNames->Token);
    wrapTInput.Set(sampler ? detail::convertWrapMode(sampler->wrap_t) : _tokens->repeat);

    return true;
  }

  void UsdPreviewSurfaceMaterialConverter::setStPrimvarInput(UsdShadeInput& input,
                                                             const SdfPath& nodeBasePath,
                                                             int stIndex)
  {
    auto nodePath = makeUniqueStageSubpath(m_stage, nodeBasePath.GetAsString(), "node", "");
    auto node = UsdShadeShader::Define(m_stage, nodePath);
    node.CreateIdAttr(VtValue(_tokens->UsdPrimvarReader_float2));

    auto varnameInput = node.CreateInput(_tokens->varname, SdfValueTypeNames->String);
    varnameInput.Set(makeStSetName(stIndex));

    auto output = node.CreateOutput(_tokens->result, SdfValueTypeNames->Float2);
    input.ConnectToSource(output);
  }

  bool UsdPreviewSurfaceMaterialConverter::getTextureMetadata(const cgltf_texture_view& textureView, ImageMetadata& metadata) const
  {
    const cgltf_texture* texture = textureView.texture;
    if (!texture)
    {
      return false;
    }

    const cgltf_image* image = texture->image;
    if (!image)
    {
      return false;
    }

    auto iter = m_imageMetadataMap.find(image);
    if (iter == m_imageMetadataMap.end())
    {
      return false;
    }

    metadata = iter->second;
    return true;
  }

  bool UsdPreviewSurfaceMaterialConverter::getTextureFilePath(const cgltf_texture_view& textureView, std::string& fileName) const
  {
    ImageMetadata metadata;
    if (!getTextureMetadata(textureView, metadata))
    {
      return false;
    }
    fileName = metadata.exportedFilePath;
    return true;
  }

  int UsdPreviewSurfaceMaterialConverter::getTextureChannelCount(const cgltf_texture_view& textureView) const
  {
    ImageMetadata metadata;
    TF_VERIFY(getTextureMetadata(textureView, metadata));
    return metadata.channelCount;
  }
}
