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

#include <MaterialXCore/Document.h>

#include "image.h"

namespace mx = MaterialX;

namespace guc
{
  class MaterialXMaterialConverter
  {
  public:
    MaterialXMaterialConverter(mx::DocumentPtr doc,
                               const ImageMetadataMap& imageMetadataMap,
                               bool flattenNodes,
                               bool explicitColorspaceTransforms,
                               bool hdstormCompat);

    void convert(const cgltf_material* material, const std::string& materialName);

  private:
    MaterialX::DocumentPtr m_doc;
    const ImageMetadataMap& m_imageMetadataMap;
    std::string m_defaultColorSetName;
    std::string m_defaultOpacitySetName;

    bool m_flattenNodes;
    bool m_explicitColorSpaceTransforms;
    bool m_hdstormCompat;

  private:
    void createGltfPbrNodes(const cgltf_material* material,
                             const std::string& materialName);

    void setGltfPbrInputs(const cgltf_material* material,
                          mx::NodeGraphPtr nodeGraph,
                          mx::NodePtr shaderNode);

  private:
    void setDiffuseTextureInput(mx::NodeGraphPtr nodeGraph,
                                mx::InputPtr shaderInput,
                                const cgltf_texture_view* textureView,
                                const mx::Color3& factor);

    void setAlphaTextureInput(mx::NodeGraphPtr nodeGraph,
                              mx::InputPtr shaderInput,
                              const cgltf_texture_view* textureView,
                              float factor);

    bool setNormalTextureInput(mx::NodeGraphPtr nodeGraph,
                               mx::InputPtr shaderInput,
                               const cgltf_texture_view& textureView);

    void setOcclusionTextureInput(mx::NodeGraphPtr nodeGraph,
                                  mx::InputPtr shaderInput,
                                  const cgltf_texture_view& textureView);

    void setIridescenceThicknessInput(mx::NodeGraphPtr nodeGraph,
                                      mx::InputPtr shaderInput,
                                      const cgltf_iridescence* iridescence);

  private:
    void setSrgbTextureInput(mx::NodeGraphPtr nodeGraph,
                             mx::InputPtr input,
                             const cgltf_texture_view& textureView,
                             mx::Color3 factor,
                             mx::Color3 fallback);

    void setFloatTextureInput(mx::NodeGraphPtr nodeGraph,
                              mx::InputPtr input,
                              const cgltf_texture_view& textureView,
                              int channelIndex,
                              float factor,
                              float fallback);

  private:
    // These two functions not only set up the image nodes with the correct value
    // types and sampling properties, but also resolve mismatches between the desired and
    // given component types. Resolution is handled according to this table:
    //
    //             texture type
    //              (#channels)
    //           +---------------+---------------+--------------------+
    //  desired  |               |               | color3             |
    //   type    |               | float         | (/vector3)         |
    //           +---------------+---------------+--------------------+
    //           |               |               | img +              |
    //           | greyscale (1) | img           | convert_color3     |
    //           +---------------+---------------+--------------------+
    //           |               |               | img +              |
    //           | greyscale +   | img +         | extract_float(0) + |
    //           | alpha (2)     | extract_float | convert_color3     |
    //           +---------------+---------------+--------------------+
    //           |               | img +         |                    |
    //           | RGB (3)       | extract_float | img                |
    //           +---------------+---------------+--------------------+
    //           |               | img +         | img +              |
    //           | RGBA (4)      | extract_float | convert_color3     |
    //           +---------------+---------------+--------------------+
    //
    mx::NodePtr addFloatTextureNodes(mx::NodeGraphPtr nodeGraph,
                                     const cgltf_texture_view& textureView,
                                     std::string& filePath,
                                     int channelIndex,
                                     float defaultValue);

    mx::NodePtr addFloat3TextureNodes(mx::NodeGraphPtr nodeGraph,
                                      const cgltf_texture_view& textureView,
                                      std::string& filePath,
                                      bool color,
                                      mx::ValuePtr defaultValue);

    mx::NodePtr addTextureTransformNode(mx::NodeGraphPtr nodeGraph,
                                        mx::NodePtr texcoordNode,
                                        const cgltf_texture_transform& transform);

    mx::NodePtr addTextureNode(mx::NodeGraphPtr nodeGraph,
                               const std::string& filePath,
                               const std::string& textureType,
                               bool isSrgb,
                               const cgltf_texture_view& textureView,
                               mx::ValuePtr defaultValue);

    mx::NodePtr makeGeompropValueNode(mx::NodeGraphPtr nodeGraph,
                                      const std::string& geompropName,
                                      const std::string& geompropValueTypeName,
                                      mx::ValuePtr defaultValue = nullptr);

    void connectNodeGraphNodeToShaderInput(mx::NodeGraphPtr nodeGraph, mx::InputPtr input, mx::NodePtr node);

  private:
    bool getTextureMetadata(const cgltf_texture_view& textureView, ImageMetadata& metadata) const;
    bool getTextureFilePath(const cgltf_texture_view& textureView, std::string& filePath) const;
    bool isTextureSrgbInUsd(const cgltf_texture_view& textureView) const;
    int getTextureChannelCount(const cgltf_texture_view& textureView) const;

    std::string getTextureValueType(const cgltf_texture_view& textureView, bool color) const;
  };
}
