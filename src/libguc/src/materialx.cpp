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

#include "materialx.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/envSetting.h>

#include <MaterialXCore/Document.h>
#include <MaterialXCore/Value.h>
#include <MaterialXFormat/XmlIo.h>
#include <MaterialXFormat/Util.h>

#include <filesystem>

#include "naming.h"
#include "debugCodes.h"
#include "cgltf_util.h"

namespace mx = MaterialX;
namespace fs = std::filesystem;

const char* MTLX_COLORSPACE_SRGB = "srgb_texture";
const char* MTLX_COLORSPACE_LINEAR = "lin_rec709";
const char* MTLX_TYPE_INTEGER = "integer";
const char* MTLX_TYPE_FLOAT = "float";
const char* MTLX_TYPE_VECTOR2 = "vector2";
const char* MTLX_TYPE_VECTOR3 = "vector3";
const char* MTLX_TYPE_VECTOR4 = "vector4";
const char* MTLX_TYPE_COLOR3 = "color3";
const char* MTLX_TYPE_COLOR4 = "color4";
const char* MTLX_TYPE_STRING = "string";
const char* MTLX_TYPE_FILENAME = "filename";
const char* MTLX_TYPE_MATERIAL = "material";
const char* MTLX_TYPE_SURFACESHADER = "surfaceshader";

#ifndef NDEBUG
TF_DEFINE_ENV_SETTING(GUC_ENABLE_MTLX_GLTF_PBR_TANGENT, false,
                      "Set the 'tangent' input of the MaterialX glTF PBR (required for anisotropy).")
TF_DEFINE_ENV_SETTING(GUC_ENABLE_MTLX_VIEWER_COMPAT, false,
                      "Emit geometric nodes and constant vertex colors for compatibility with MaterialXView.")
#endif

namespace detail
{
  using namespace guc;

  mx::Color3 makeMxColor3(const float* ptr)
  {
    return mx::Color3(ptr[0], ptr[1], ptr[2]);
  }

  std::string getMtlxFilterType(int filter)
  {
    switch (filter)
    {
      // see spec sec. 3.8.4.2 for mapping table
    case 9728 /* NEAREST */:
    case 9984 /* NEAREST_MIPMAP_NEAREST */:
    case 9986 /* NEAREST_MIPMAP_LINEAR */:
      return "closest";
    case 9729 /* LINEAR */:
    case 9985 /* LINEAR_MIPMAP_NEAREST */:
    case 9987 /* LINEAR_MIPMAP_LINEAR */:
      return "linear";
    case 0:
      // spec sec. 3.8.4.1: "Client implementations SHOULD follow specified filtering modes.
      // When the latter are undefined, client implementations MAY set their own default texture filtering settings."
      // Implementation-defined according to MaterialX spec, so just let the application set it.
      return "";
    default:
      TF_RUNTIME_ERROR("invalid texture filter");
      return "";
    }
  }

  std::string getMtlxAddressMode(int addressMode)
  {
    switch (addressMode)
    {
    case 33071 /* CLAMP_TO_EDGE */:
      return "clamp";
    case 33648 /* MIRRORED_REPEAT */:
      return "mirror";
    default:
      TF_RUNTIME_ERROR("invalid wrap mode");
    case 0:
      // default according to spec sec. 5.26
    case 10497 /* REPEAT */:
      return "periodic";
    }
  }

  // When we retrieve a value from an image, we often have to extract it in a certain way. The
  // fallback value which is returned when the image can not be loaded must match the image value type,
  // rather than the extracted value. This means, as an example, that when we define a default value
  // for roughness, the float needs to be extrapolated to the R, B, and possibly A component.
  std::string getTextureTypeAdjustedDefaultValueString(mx::ValuePtr defaultValue, const std::string& textureType)
  {
    mx::ValuePtr valuePtr = nullptr;

    if ((defaultValue->isA<float>() && textureType == MTLX_TYPE_FLOAT) ||
        (defaultValue->isA<mx::Color3>() && textureType == MTLX_TYPE_COLOR3) ||
        (defaultValue->isA<mx::Vector3>() && textureType == MTLX_TYPE_VECTOR3))
    {
      valuePtr = defaultValue;
    }
    else if (defaultValue->isA<mx::Color3>())
    {
      auto colorValue = defaultValue->asA<mx::Color3>();
      if (textureType == MTLX_TYPE_COLOR4)
      {
        valuePtr = mx::Value::createValue(mx::Color4(colorValue[0], colorValue[1], colorValue[2], 1.0f));
      }
      else if (textureType == MTLX_TYPE_VECTOR2)
      {
        // greyscale+alpha texture that RGB is read from - ignore the alpha channel
        valuePtr = mx::Value::createValue(mx::Vector2(colorValue[0]));
      }
      else if (textureType == MTLX_TYPE_FLOAT)
      {
        // greyscale
        valuePtr = mx::Value::createValue(colorValue[0]);
      }
    }
    else if (defaultValue->isA<mx::Vector3>())
    {
      auto vectorValue = defaultValue->asA<mx::Vector3>();
      if (textureType == MTLX_TYPE_VECTOR4)
      {
        valuePtr = mx::Value::createValue(mx::Vector4(vectorValue[0], vectorValue[1], vectorValue[2], 1.0f));
      }
      else if (textureType == MTLX_TYPE_VECTOR2)
      {
        valuePtr = mx::Value::createValue(mx::Vector2(vectorValue[0]));
      }
      else if (textureType == MTLX_TYPE_FLOAT)
      {
        valuePtr = mx::Value::createValue(vectorValue[0]);
      }
    }
    else if (defaultValue->isA<float>())
    {
      float floatValue = defaultValue->asA<float>();
      if (textureType == MTLX_TYPE_VECTOR2) {
        valuePtr = mx::Value::createValue(mx::Vector2(floatValue));
      }
      else if (textureType == MTLX_TYPE_COLOR3) {
        valuePtr = mx::Value::createValue(mx::Color3(floatValue));
      }
      else if (textureType == MTLX_TYPE_VECTOR3) {
        valuePtr = mx::Value::createValue(mx::Vector3(floatValue));
      }
      else if (textureType == MTLX_TYPE_COLOR4) {
        valuePtr = mx::Value::createValue(mx::Color4(floatValue));
      }
      else if (textureType == MTLX_TYPE_VECTOR4) {
        valuePtr = mx::Value::createValue(mx::Vector4(floatValue));
      }
    }

    if (valuePtr)
    {
      return valuePtr->getValueString();
    }

    TF_CODING_ERROR("unhandled default texture value type");
    return "";
  }

  mx::NodePtr makeClampNode(mx::NodeGraphPtr nodeGraph, mx::NodePtr srcNode)
  {
    mx::NodePtr node = nodeGraph->addNode("clamp", mx::EMPTY_STRING, srcNode->getType());

    auto inInput = node->addInput("in", srcNode->getType());
    inInput->setNodeName(srcNode->getName());

    return node;
  }

  mx::NodePtr makeMultiplyFactorNodeIfNecessary(mx::NodeGraphPtr nodeGraph, mx::NodePtr srcNode, mx::ValuePtr factor)
  {
    // Skip multiplication if possible.
    if ((factor->isA<float>() && factor->asA<float>() == 1.0f) ||
        (factor->isA<mx::Vector3>() && factor->asA<mx::Vector3>() == mx::Vector3(1.0)) ||
        (factor->isA<mx::Color3>() && factor->asA<mx::Color3>() == mx::Color3(1.0)))
    {
      return srcNode;
    }

    mx::NodePtr multiplyNode = nodeGraph->addNode("multiply", mx::EMPTY_STRING, factor->getTypeString());
    {
      mx::InputPtr input1 = multiplyNode->addInput("in1", srcNode->getType());
      input1->setNodeName(srcNode->getName());

      mx::InputPtr input2 = multiplyNode->addInput("in2", factor->getTypeString());
      input2->setValueString(factor->getValueString());
    }

    return multiplyNode;
  }

  mx::NodePtr makeExtractChannelNode(mx::NodeGraphPtr nodeGraph, mx::NodePtr srcNode, int index)
  {
    mx::NodePtr node = nodeGraph->addNode("extract", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);

    auto input = node->addInput("in", srcNode->getType());
    input->setNodeName(srcNode->getName());

    auto indexInput = node->addInput("index", MTLX_TYPE_INTEGER);
    indexInput->setValue(index);

    return node;
  }

  mx::NodePtr makeConversionNode(mx::NodeGraphPtr nodeGraph, mx::NodePtr srcNode, const std::string& destType)
  {
    mx::NodePtr node = nodeGraph->addNode("convert", mx::EMPTY_STRING, destType);

    auto input = node->addInput("in", srcNode->getType());
    input->setNodeName(srcNode->getName());

    return node;
  }

  mx::NodePtr makeVectorToWorldSpaceNode(mx::NodeGraphPtr nodeGraph, mx::NodePtr srcNode)
  {
    mx::NodePtr node = nodeGraph->addNode("transformvector", mx::EMPTY_STRING, srcNode->getType());

    auto input = node->addInput("in", srcNode->getType());
    input->setNodeName(srcNode->getName());

    auto fromspaceInput = node->addInput("fromspace", MTLX_TYPE_STRING);
    fromspaceInput->setValueString("object");

    auto tospaceInput = node->addInput("tospace", MTLX_TYPE_STRING);
    tospaceInput->setValueString("world");

    return node;
  }

  mx::NodePtr makeNormalizeNode(mx::NodeGraphPtr nodeGraph, mx::NodePtr srcNode)
  {
    mx::NodePtr node = nodeGraph->addNode("normalize", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);

    auto input = node->addInput("in", MTLX_TYPE_VECTOR3);
    input->setNodeName(srcNode->getName());

    return node;
  }
}

namespace guc
{
  MaterialXMaterialConverter::MaterialXMaterialConverter(mx::DocumentPtr doc,
                                                         const ImageMetadataMap& imageMetadataMap)
    : m_doc(doc)
    , m_imageMetadataMap(imageMetadataMap)
    , m_defaultColorSetName(makeColorSetName(0))
    , m_defaultOpacitySetName(makeOpacitySetName(0))
  {
  }

  void MaterialXMaterialConverter::convert(const cgltf_material* material, const std::string& materialName)
  {
    // By default, the scientific notation is emitted for small values, causing the document to be invalid
    mx::ScopedFloatFormatting floatFormat(mx::Value::FloatFormatFixed);

    try
    {
      if (material->unlit)
      {
        createUnlitSurfaceNodes(material, materialName);
      }
      else
      {
        createGltfPbrNodes(material, materialName);
      }
    }
    catch (const mx::Exception& ex)
    {
      TF_RUNTIME_ERROR("Failed to create glTF PBR nodes for material '%s': %s", materialName.c_str(), ex.what());
    }
  }

  void MaterialXMaterialConverter::createUnlitSurfaceNodes(const cgltf_material* material, const std::string& materialName)
  {
    createMaterialNodes(material, materialName, "surface_unlit", [this](const cgltf_material* material,
                                                                        mx::NodeGraphPtr nodeGraph,
                                                                        mx::NodePtr shaderNode) {
      if (material->has_pbr_metallic_roughness)
      {
        const cgltf_pbr_metallic_roughness* pbrMetallicRoughness = &material->pbr_metallic_roughness;

        if (material->alpha_mode != cgltf_alpha_mode_opaque)
        {
          addAlphaTextureInput(nodeGraph, shaderNode, "opacity", &pbrMetallicRoughness->base_color_texture, pbrMetallicRoughness->base_color_factor[3]);
        }

        addDiffuseTextureInput(nodeGraph, shaderNode, "emission_color", &pbrMetallicRoughness->base_color_texture, detail::makeMxColor3(pbrMetallicRoughness->base_color_factor));
      }
    });
  }

  void MaterialXMaterialConverter::createGltfPbrNodes(const cgltf_material* material, const std::string& materialName)
  {
    createMaterialNodes(material, materialName, "gltf_pbr", [this](const cgltf_material* material,
                                                                   mx::NodeGraphPtr nodeGraph,
                                                                   mx::NodePtr shaderNode) {
      addGltfPbrInputs(material, nodeGraph, shaderNode);
    });
  }

  void MaterialXMaterialConverter::createMaterialNodes(const cgltf_material* material,
                                                       const std::string& materialName,
                                                       const std::string& shaderNodeType,
                                                       ShaderNodeCreationCallback callback)

  {
    std::string nodegraphName = "NG_" + materialName;
    std::string shaderName = "SR_" + materialName;

    mx::NodeGraphPtr nodeGraph = m_doc->addNodeGraph(nodegraphName);
    mx::NodePtr shaderNode = m_doc->addNode(shaderNodeType, shaderName, MTLX_TYPE_SURFACESHADER);

    // Fill nodegraph with helper nodes (e.g. textures) and set shadernode params.
    callback(material, nodeGraph, shaderNode);

    // Create material and connect surface to it
    mx::NodePtr materialNode = m_doc->addNode("surfacematerial", materialName, MTLX_TYPE_MATERIAL);
    mx::InputPtr materialSurfaceInput = materialNode->addInput("surfaceshader", MTLX_TYPE_SURFACESHADER);
    materialSurfaceInput->setNodeName(shaderNode->getName());
  }

  void MaterialXMaterialConverter::addGltfPbrInputs(const cgltf_material* material,
                                                    mx::NodeGraphPtr nodeGraph,
                                                    mx::NodePtr shaderNode)
  {
    mx::Color3 emissiveFactor = detail::makeMxColor3(material->emissive_factor);
    auto emissiveFactorDefault = mx::Color3(0.0f); // spec sec. 5.19.8
    addSrgbTextureInput(nodeGraph, shaderNode, "emissive", material->emissive_texture, emissiveFactor, emissiveFactorDefault);

    addNormalTextureInput(nodeGraph, shaderNode, "normal", material->normal_texture);

    addOcclusionTextureInput(nodeGraph, shaderNode, material->occlusion_texture);

    if (material->alpha_mode != cgltf_alpha_mode_opaque)
    {
      mx::InputPtr alphaModeInput = shaderNode->addInput("alpha_mode", MTLX_TYPE_INTEGER);
      alphaModeInput->setValue(int(material->alpha_mode));
    }

    if (material->alpha_mode == cgltf_alpha_mode_mask)
    {
      mx::InputPtr alphaCutoffInput = shaderNode->addInput("alpha_cutoff", MTLX_TYPE_FLOAT);
      alphaCutoffInput->setValue(material->alpha_cutoff);
    }

    if (material->has_pbr_metallic_roughness)
    {
      const cgltf_pbr_metallic_roughness* pbrMetallicRoughness = &material->pbr_metallic_roughness;

      if (material->alpha_mode != cgltf_alpha_mode_opaque)
      {
        addAlphaTextureInput(nodeGraph, shaderNode, "alpha", &pbrMetallicRoughness->base_color_texture, pbrMetallicRoughness->base_color_factor[3]);
      }

      addDiffuseTextureInput(nodeGraph, shaderNode, "base_color", &pbrMetallicRoughness->base_color_texture, detail::makeMxColor3(pbrMetallicRoughness->base_color_factor));

      float metallicFactorDefault = 1.0f; // spec sec. 5.22.3
      addFloatTextureInput(nodeGraph, shaderNode, "metallic", pbrMetallicRoughness->metallic_roughness_texture, 2, pbrMetallicRoughness->metallic_factor, metallicFactorDefault);

      float roughnessFactorDefault = 1.0f; // spec sec. 5.22.4
      addFloatTextureInput(nodeGraph, shaderNode, "roughness", pbrMetallicRoughness->metallic_roughness_texture, 1, pbrMetallicRoughness->roughness_factor, roughnessFactorDefault);
    }
    // Regardless of the existence of base color and texture, we still need to multiply by vertex color / opacity
    else
    {
      auto baseColorDefault = mx::Color3(1.0f);
      addDiffuseTextureInput(nodeGraph, shaderNode, "base_color", nullptr, baseColorDefault);

      if (material->alpha_mode != cgltf_alpha_mode_opaque)
      {
        float alphaDefault = 1.0f;
        addAlphaTextureInput(nodeGraph, shaderNode, "alpha", nullptr, alphaDefault);
      }
    }

    if (material->has_emissive_strength)
    {
      const cgltf_emissive_strength* emissiveStrength = &material->emissive_strength;

      mx::InputPtr emissiveStrengthInput = shaderNode->addInput("emissive_strength", MTLX_TYPE_FLOAT);
      emissiveStrengthInput->setValue(emissiveStrength->emissive_strength);
    }

    if (material->has_clearcoat)
    {
      const cgltf_clearcoat* clearcoat = &material->clearcoat;

      auto clearcoatFactorDefault = 0.0f; // according to spec
      addFloatTextureInput(nodeGraph, shaderNode, "clearcoat", clearcoat->clearcoat_texture, 0, clearcoat->clearcoat_factor, clearcoatFactorDefault);

      auto clearcoatRoughnessDefault = 0.0f; // according to spec
      addFloatTextureInput(nodeGraph, shaderNode, "clearcoat_roughness", clearcoat->clearcoat_roughness_texture, 1, clearcoat->clearcoat_roughness_factor, clearcoatRoughnessDefault);

      addNormalTextureInput(nodeGraph, shaderNode, "clearcoat_normal", clearcoat->clearcoat_normal_texture);
    }

    if (material->has_transmission)
    {
      const cgltf_transmission* transmission = &material->transmission;

      auto transmissionFactorDefault = 0.0f; // according to spec
      addFloatTextureInput(nodeGraph, shaderNode, "transmission", transmission->transmission_texture, 0, transmission->transmission_factor, transmissionFactorDefault);
    }

    if (material->has_volume)
    {
      const cgltf_volume* volume = &material->volume;

      auto thicknessFactorDefault = 0.0f; // according to spec
      addFloatTextureInput(nodeGraph, shaderNode, "thickness", volume->thickness_texture, 1, volume->thickness_factor, thicknessFactorDefault);

      mx::InputPtr attenuationDistanceInput = shaderNode->addInput("attenuation_distance", MTLX_TYPE_FLOAT);
      attenuationDistanceInput->setValue(volume->attenuation_distance);

      mx::InputPtr attenuationColorInput = shaderNode->addInput("attenuation_color", MTLX_TYPE_COLOR3);
      attenuationColorInput->setValue(detail::makeMxColor3(volume->attenuation_color));
    }

    if (material->has_ior)
    {
      const cgltf_ior* ior = &material->ior;

      if (ior->ior != 1.5f) // default given by spec
      {
        mx::InputPtr iorInput = shaderNode->addInput("ior", MTLX_TYPE_FLOAT);
        iorInput->setValue(ior->ior);
      }
    }

    if (material->has_iridescence)
    {
      const cgltf_iridescence* iridescence = &material->iridescence;

      float iridescenceFactorDefault = 0.0f; // acording to spec
      addFloatTextureInput(nodeGraph, shaderNode, "iridescence", iridescence->iridescence_texture, 0, iridescence->iridescence_factor, iridescenceFactorDefault);

      mx::InputPtr iridescenceIorInput = shaderNode->addInput("iridescence_ior", MTLX_TYPE_FLOAT);
      iridescenceIorInput->setValue(iridescence->iridescence_ior);

      addIridescenceThicknessInput(nodeGraph, shaderNode, iridescence);
    }

    if (material->has_specular)
    {
      const cgltf_specular* specular = &material->specular;

      auto specularFactorDefault = 1.0f; // according to spec
      addFloatTextureInput(nodeGraph, shaderNode, "specular", specular->specular_texture, 3, specular->specular_factor, specularFactorDefault);

      auto specularColorDefault = mx::Color3(1.0f); // according to spec
      addSrgbTextureInput(nodeGraph, shaderNode, "specular_color", specular->specular_color_texture, detail::makeMxColor3(specular->specular_color_factor), specularColorDefault);
    }

    if (material->has_sheen)
    {
      const cgltf_sheen* sheen = &material->sheen;

      auto sheenFactorDefault = mx::Color3(0.0f); // according to spec
      addSrgbTextureInput(nodeGraph, shaderNode, "sheen_color", sheen->sheen_color_texture, detail::makeMxColor3(sheen->sheen_color_factor), sheenFactorDefault);

      auto sheenRoughnessFactorDefault = 0.0f; // according to spec
      addFloatTextureInput(nodeGraph, shaderNode, "sheen_roughness", sheen->sheen_roughness_texture, 3, sheen->sheen_roughness_factor, sheenRoughnessFactorDefault);
    }

#ifndef NDEBUG
    if (TfGetEnvSetting(GUC_ENABLE_MTLX_GLTF_PBR_TANGENT))
    {
      mx::NodePtr tangentNode;

      if (TfGetEnvSetting(GUC_ENABLE_MTLX_VIEWER_COMPAT))
      {
        tangentNode = nodeGraph->addNode("tangent", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);

        auto spaceInput = tangentNode->addInput("space", MTLX_TYPE_STRING);
        spaceInput->setValue("world");
      }
      else
      {
        tangentNode = makeGeompropValueNode(nodeGraph, "tangents", MTLX_TYPE_VECTOR3);
        tangentNode = detail::makeVectorToWorldSpaceNode(nodeGraph, tangentNode);
        tangentNode = detail::makeNormalizeNode(nodeGraph, tangentNode);
      }

      mx::InputPtr tangentInput = shaderNode->addInput("tangent", MTLX_TYPE_VECTOR3);
      connectNodeGraphNodeToShaderInput(nodeGraph, tangentInput, tangentNode);
    }
#endif
  }

  void MaterialXMaterialConverter::addDiffuseTextureInput(mx::NodeGraphPtr nodeGraph,
                                                          mx::NodePtr shaderNode,
                                                          const std::string& inputName,
                                                          const cgltf_texture_view* textureView,
                                                          const mx::Color3& factor)
  {
    // TODO: right now, we assume that vertex colors exist - hence, an input is always needed
    mx::InputPtr shaderInput = shaderNode->addInput(inputName, MTLX_TYPE_COLOR3);

    auto defaultVertexValue = mx::Value::createValue(mx::Vector3(1.0f, 1.0f, 1.0f));
    mx::NodePtr geompropNode = makeGeompropValueNode(nodeGraph, m_defaultColorSetName, MTLX_TYPE_COLOR3, defaultVertexValue);

    mx::NodePtr multiplyNode1 = detail::makeMultiplyFactorNodeIfNecessary(nodeGraph, geompropNode, mx::Value::createValue(factor));

    std::string filePath;
    if (!textureView || !getTextureFilePath(*textureView, filePath))
    {
      connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, multiplyNode1);
      return;
    }

    auto defaultTextureValue = mx::Value::createValue(mx::Color3(1.0f, 1.0f, 1.0f)); // spec sec. 5.22.2
    mx::NodePtr textureNode = addFloat3TextureNodes(nodeGraph, *textureView, filePath, true, defaultTextureValue);

    mx::NodePtr multiplyNode2 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_COLOR3);
    {
      auto input1 = multiplyNode2->addInput("in1", MTLX_TYPE_COLOR3);
      input1->setNodeName(multiplyNode1->getName());

      auto input2 = multiplyNode2->addInput("in2", MTLX_TYPE_COLOR3);
      input2->setNodeName(textureNode->getName());
    }

    connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, multiplyNode2);
  }

  void MaterialXMaterialConverter::addAlphaTextureInput(mx::NodeGraphPtr nodeGraph,
                                                        mx::NodePtr shaderNode,
                                                        const std::string& inputName,
                                                        const cgltf_texture_view* textureView,
                                                        float factor)
  {
    // TODO: right now, we assume that vertex colors/opacities exist - hence, an input is always needed
    mx::InputPtr shaderInput = shaderNode->addInput(inputName, MTLX_TYPE_FLOAT);

    auto defaultOpacityValue = mx::Value::createValue(1.0f);
    mx::NodePtr geompropNode = makeGeompropValueNode(nodeGraph, m_defaultOpacitySetName, MTLX_TYPE_FLOAT, defaultOpacityValue);

    mx::NodePtr multiplyNode1 = detail::makeMultiplyFactorNodeIfNecessary(nodeGraph, geompropNode, mx::Value::createValue(factor));

    ImageMetadata metadata;
    if (!textureView || !getTextureMetadata(*textureView, metadata))
    {
      connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, multiplyNode1);
      return;
    }

    std::string filePath = metadata.filePath;

    int channelIndex = (metadata.channelCount == 4) ? 3 : 1;

    if (metadata.channelCount == 1 || metadata.channelCount == 3)
    {
      // The spec does not address greyscale textures, but this is clarified here: https://github.com/KhronosGroup/glTF/issues/2298
      TF_WARN("glTF spec violation: alpha must be encoded in the 4th channel of an RGBA texture (§5.22.2). %s only has %d channel(s).", filePath.c_str(), metadata.channelCount);
      // Fall back to the first channel, as the author probably intended
      channelIndex = 0;
    }

    mx::NodePtr valueNode = addFloatTextureNodes(nodeGraph, *textureView, filePath, channelIndex);

    mx::NodePtr multiplyNode2 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      auto input1 = multiplyNode2->addInput("in1", MTLX_TYPE_FLOAT);
      input1->setNodeName(multiplyNode1->getName());

      auto input2 = multiplyNode2->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setNodeName(valueNode->getName());
    }

    connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, multiplyNode2);
  }

  void MaterialXMaterialConverter::addNormalTextureInput(mx::NodeGraphPtr nodeGraph,
                                                         mx::NodePtr shaderNode,
                                                         const std::string& inputName,
                                                         const cgltf_texture_view& textureView)
  {
    std::string filePath;
    if (!getTextureFilePath(textureView, filePath))
    {
      return;
    }

    mx::InputPtr shaderInput = shaderNode->addInput(inputName, MTLX_TYPE_VECTOR3);

    mx::ValuePtr defaultValue = mx::Value::createValue(mx::Vector3(0.5f, 0.5f, 1.0f));
    mx::NodePtr textureNode = addFloat3TextureNodes(nodeGraph, textureView, filePath, false, defaultValue);

    auto normalNode = nodeGraph->addNode("normal", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      auto spaceInput = normalNode->addInput("space", MTLX_TYPE_STRING);
      spaceInput->setValue("world");
    }

    mx::NodePtr tangentNode;
#ifndef NDEBUG
    if (TfGetEnvSetting(GUC_ENABLE_MTLX_VIEWER_COMPAT))
    {
      tangentNode = nodeGraph->addNode("tangent", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
      auto spaceInput = tangentNode->addInput("space", MTLX_TYPE_STRING);
      spaceInput->setValue("world");
    }
    else
#endif
    {
      tangentNode = makeGeompropValueNode(nodeGraph, "tangents", MTLX_TYPE_VECTOR3);
      tangentNode = detail::makeVectorToWorldSpaceNode(nodeGraph, tangentNode);
      tangentNode = detail::makeNormalizeNode(nodeGraph, tangentNode);
    }

    mx::NodePtr bitangentNode;
#ifndef NDEBUG
    if (TfGetEnvSetting(GUC_ENABLE_MTLX_VIEWER_COMPAT))
    {
      bitangentNode = nodeGraph->addNode("bitangent", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
      auto spaceInput = bitangentNode->addInput("space", MTLX_TYPE_STRING);
      spaceInput->setValue("world");
    }
    else
#endif
    {
      bitangentNode = makeGeompropValueNode(nodeGraph, "bitangents", MTLX_TYPE_VECTOR3);
      bitangentNode = detail::makeVectorToWorldSpaceNode(nodeGraph, bitangentNode);
      bitangentNode = detail::makeNormalizeNode(nodeGraph, bitangentNode);
    }

#if MATERIALX_MAJOR_VERSION > 1 || (MATERIALX_MAJOR_VERSION == 1 && MATERIALX_MINOR_VERSION > 38)
    // MaterialX 1.39 introduces a 'bitangent' input to the <normalmap> node
    auto normalmapNode = nodeGraph->addNode("normalmap", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      auto inInput = normalmapNode->addInput("in", MTLX_TYPE_VECTOR3);
      inInput->setNodeName(textureNode->getName());

      if (textureView.scale != 1.0f)
      {
        auto scaleInput = normalmapNode->addInput("scale", MTLX_TYPE_FLOAT);
        scaleInput->setValue(textureView.scale);
      }

      auto normalInput = normalmapNode->addInput("normal", MTLX_TYPE_VECTOR3);
      normalInput->setNodeName(normalNode->getName());

      auto tangentInput = normalmapNode->addInput("tangent", MTLX_TYPE_VECTOR3);
      tangentInput->setNodeName(tangentNode->getName());

      auto bitangentInput = normalmapNode->addInput("bitangent", MTLX_TYPE_VECTOR3);
      bitangentInput->setNodeName(bitangentNode->getName());
    }

    connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, normalmapNode);
#else
    // For older versions, we basically implement the normalmap node with variable handedness by using the bitangent
    // https://github.com/AcademySoftwareFoundation/MaterialX/blob/main/libraries/stdlib/genglsl/mx_normalmap.glsl

    // we need to remap the texture [0, 1] values to [-1, 1] values
    mx::NodePtr multiplyNode1 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      mx::InputPtr input1 = multiplyNode1->addInput("in1", MTLX_TYPE_VECTOR3);
      input1->setNodeName(textureNode->getName());

      mx::InputPtr input2 = multiplyNode1->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setValue(2.0f);
    }

    mx::NodePtr subtractNode = nodeGraph->addNode("subtract", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      auto input1 = subtractNode->addInput("in1", MTLX_TYPE_VECTOR3);
      input1->setNodeName(multiplyNode1->getName());

      auto input2 = subtractNode->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setValue(1.0f);
    }

    // multiply with scale according to glTF spec sec. 3.9.3
    auto scale = mx::Value::createValue(mx::Vector3(textureView.scale, textureView.scale, 1.0f));
    mx::NodePtr multiplyNode2 = detail::makeMultiplyFactorNodeIfNecessary(nodeGraph, subtractNode, scale);

    // not done in the MaterialX normalmap impl, but required according to glTF spec sec. 3.9.3
    mx::NodePtr normalizeNode1 = detail::makeNormalizeNode(nodeGraph, multiplyNode2);

    // let's avoid separate3 due to multi-output support concerns
    mx::NodePtr outx = detail::makeExtractChannelNode(nodeGraph, normalizeNode1, 0);
    mx::NodePtr outy = detail::makeExtractChannelNode(nodeGraph, normalizeNode1, 1);
    mx::NodePtr outz = detail::makeExtractChannelNode(nodeGraph, normalizeNode1, 2);

    // the next nodes implement multiplication with the TBN matrix
    mx::NodePtr multiplyNode3 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      mx::InputPtr input1 = multiplyNode3->addInput("in1", MTLX_TYPE_VECTOR3);
      input1->setNodeName(tangentNode->getName());

      mx::InputPtr input2 = multiplyNode3->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setNodeName(outx->getName());
    }

    mx::NodePtr multiplyNode4 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      mx::InputPtr input1 = multiplyNode4->addInput("in1", MTLX_TYPE_VECTOR3);
      input1->setNodeName(bitangentNode->getName());

      mx::InputPtr input2 = multiplyNode4->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setNodeName(outy->getName());
    }

    mx::NodePtr multiplyNode5 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      mx::InputPtr input1 = multiplyNode5->addInput("in1", MTLX_TYPE_VECTOR3);
      input1->setNodeName(normalNode->getName());

      mx::InputPtr input2 = multiplyNode5->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setNodeName(outz->getName());
    }

    mx::NodePtr addNode1 = nodeGraph->addNode("add", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      mx::InputPtr input1 = addNode1->addInput("in1", MTLX_TYPE_VECTOR3);
      input1->setNodeName(multiplyNode3->getName());

      mx::InputPtr input2 = addNode1->addInput("in2", MTLX_TYPE_VECTOR3);
      input2->setNodeName(multiplyNode4->getName());
    }

    mx::NodePtr addNode2 = nodeGraph->addNode("add", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      mx::InputPtr input1 = addNode2->addInput("in1", MTLX_TYPE_VECTOR3);
      input1->setNodeName(addNode1->getName());

      mx::InputPtr input2 = addNode2->addInput("in2", MTLX_TYPE_VECTOR3);
      input2->setNodeName(multiplyNode5->getName());
    }

    mx::NodePtr normalizeNode2 = detail::makeNormalizeNode(nodeGraph, addNode2);

    connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, normalizeNode2);
#endif
  }

  void MaterialXMaterialConverter::addOcclusionTextureInput(mx::NodeGraphPtr nodeGraph,
                                                            mx::NodePtr shaderNode,
                                                            const cgltf_texture_view& textureView)
  {
    std::string filePath;
    if (!getTextureFilePath(textureView, filePath))
    {
      return;
    }

    mx::InputPtr shaderInput = shaderNode->addInput("occlusion", MTLX_TYPE_FLOAT);

    // glTF spec 2.0 3.9.3.
    // if 'strength' attribute is present, it affects occlusion as follows:
    //     1.0 + strength * (occlusionTexture - 1.0)

    mx::NodePtr valueNode = addFloatTextureNodes(nodeGraph, textureView, filePath, 0);

    mx::NodePtr substractNode = nodeGraph->addNode("subtract", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      auto input1 = substractNode->addInput("in1", MTLX_TYPE_FLOAT);
      input1->setNodeName(valueNode->getName());

      auto input2 = substractNode->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setValue(1.0f);
    }

    auto scale = mx::Value::createValue(textureView.scale);
    mx::NodePtr multiplyNode = detail::makeMultiplyFactorNodeIfNecessary(nodeGraph, substractNode, scale);

    mx::NodePtr addNode = nodeGraph->addNode("add", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      auto input1 = addNode->addInput("in1", MTLX_TYPE_FLOAT);
      input1->setValue(1.0f);

      auto input2 = addNode->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setNodeName(multiplyNode->getName());
    }

    connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, addNode);
  }

  void MaterialXMaterialConverter::addIridescenceThicknessInput(mx::NodeGraphPtr nodeGraph,
                                                                mx::NodePtr shaderNode,
                                                                const cgltf_iridescence* iridescence)
  {
    std::string filePath;
    bool validTexture = getTextureFilePath(iridescence->iridescence_thickness_texture, filePath);

    mx::InputPtr shaderInput;
    bool isThicknessMaxNonDefault = iridescence->iridescence_thickness_max != 100.0f;
    if (validTexture || (!validTexture && isThicknessMaxNonDefault))
    {
      shaderInput = shaderNode->addInput("iridescence_thickness", MTLX_TYPE_FLOAT);
    }

    if (!validTexture)
    {
      if (isThicknessMaxNonDefault)
      {
        // "The thickness of the thin-film is set to iridescenceThicknessMaximum if iridescenceThicknessTexture is not given."
        shaderInput->setValue(iridescence->iridescence_thickness_max);
      }
      return;
    }

    // Otherwise, we insert a mix(min, max, texture.g) node and connect it to the input, as noted here:
    // https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_iridescence#properties
    mx::NodePtr mixNode = nodeGraph->addNode("mix", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      mx::InputPtr inputBg = mixNode->addInput("bg", MTLX_TYPE_FLOAT);
      inputBg->setValue(iridescence->iridescence_thickness_min);

      mx::InputPtr inputFg = mixNode->addInput("fg", MTLX_TYPE_FLOAT);
      inputFg->setValue(iridescence->iridescence_thickness_max);

      mx::NodePtr thicknessTexNode = addFloatTextureNodes(nodeGraph, iridescence->iridescence_thickness_texture, filePath, 1);

      mx::InputPtr inputMix = mixNode->addInput("mix", MTLX_TYPE_FLOAT);
      inputMix->setNodeName(thicknessTexNode->getName());
    }

    connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, mixNode);
  }

  void MaterialXMaterialConverter::addSrgbTextureInput(mx::NodeGraphPtr nodeGraph,
                                                       mx::NodePtr shaderNode,
                                                       const std::string& inputName,
                                                       const cgltf_texture_view& textureView,
                                                       mx::Color3 factor,
                                                       mx::Color3 factorDefault)
  {
    std::string filePath;
    bool validTexture = getTextureFilePath(textureView, filePath);

    mx::InputPtr input;
    if (validTexture || (!validTexture && factor != factorDefault))
    {
      input = shaderNode->addInput(inputName, MTLX_TYPE_COLOR3);
    }

    mx::ValuePtr factorValue = mx::Value::createValue(factor);
    if (validTexture)
    {
      auto defaultValuePtr = mx::Value::createValue(mx::Color3(1.0));
      mx::NodePtr valueNode = addFloat3TextureNodes(nodeGraph, textureView, filePath, true, defaultValuePtr);

      mx::NodePtr multiplyNode = detail::makeMultiplyFactorNodeIfNecessary(nodeGraph, valueNode, factorValue);

      connectNodeGraphNodeToShaderInput(nodeGraph, input, multiplyNode);
    }
    else if (factor != factorDefault)
    {
      input->setValueString(factorValue->getValueString());
    }
  }

  void MaterialXMaterialConverter::addFloatTextureInput(mx::NodeGraphPtr nodeGraph,
                                                        mx::NodePtr shaderNode,
                                                        const std::string& inputName,
                                                        const cgltf_texture_view& textureView,
                                                        int channelIndex,
                                                        float factor,
                                                        float factorDefault)
  {
    std::string filePath;
    bool validTexture = getTextureFilePath(textureView, filePath);

    mx::InputPtr input;
    if (validTexture || (!validTexture && factor != factorDefault))
    {
      input = shaderNode->addInput(inputName, MTLX_TYPE_FLOAT);
    }

    mx::ValuePtr factorValue = mx::Value::createValue(factor);
    if (validTexture)
    {
      mx::NodePtr valueNode = addFloatTextureNodes(nodeGraph, textureView, filePath, channelIndex);

      mx::NodePtr multiplyNode = detail::makeMultiplyFactorNodeIfNecessary(nodeGraph, valueNode, factorValue);

      connectNodeGraphNodeToShaderInput(nodeGraph, input, multiplyNode);
    }
    else if (factor != factorDefault)
    {
      input->setValueString(factorValue->getValueString());
    }
  }

  mx::NodePtr MaterialXMaterialConverter::addFloatTextureNodes(mx::NodeGraphPtr nodeGraph,
                                                               const cgltf_texture_view& textureView,
                                                               std::string& filePath,
                                                               int channelIndex)
  {
    std::string texValueType = getTextureValueType(textureView, false);

    mx::NodePtr valueNode = addTextureNode(nodeGraph, filePath, texValueType, false, textureView, mx::Value::createValue(1.0f));

    if (texValueType != MTLX_TYPE_FLOAT)
    {
      valueNode = detail::makeExtractChannelNode(nodeGraph, valueNode, channelIndex);
    }

    return valueNode;
  }

  mx::NodePtr MaterialXMaterialConverter::addFloat3TextureNodes(mx::NodeGraphPtr nodeGraph,
                                                                const cgltf_texture_view& textureView,
                                                                std::string& filePath,
                                                                bool color,
                                                                mx::ValuePtr defaultValue)
  {
    std::string desiredValueType = color ? MTLX_TYPE_COLOR3 : MTLX_TYPE_VECTOR3;
    std::string texValueType = getTextureValueType(textureView, color);

    mx::NodePtr valueNode = addTextureNode(nodeGraph, filePath, texValueType, color, textureView, defaultValue);

    // In case of RGBA, we need to drop one channel.
    if (texValueType == MTLX_TYPE_COLOR4 || texValueType == MTLX_TYPE_VECTOR4)
    {
      valueNode = detail::makeConversionNode(nodeGraph, valueNode, desiredValueType);
    }
    else
    {
      // In case of a greyscale images, we want to convert channel 0 (float) to color3.
      // For greyscale images with an alpha channel, we additionally need an extraction node.
      if (texValueType == MTLX_TYPE_VECTOR2)
      {
        valueNode = detail::makeExtractChannelNode(nodeGraph, valueNode, 0);
      }
      if (texValueType == MTLX_TYPE_FLOAT || texValueType == MTLX_TYPE_VECTOR2)
      {
        valueNode = detail::makeConversionNode(nodeGraph, valueNode, desiredValueType);
      }
    }

    return valueNode;
  }

  mx::NodePtr MaterialXMaterialConverter::addTextureTransformNode(mx::NodeGraphPtr nodeGraph,
                                                                  mx::NodePtr texcoordNode,
                                                                  const cgltf_texture_transform& transform)
  {
    mx::NodePtr node = nodeGraph->addNode("place2d", mx::EMPTY_STRING, MTLX_TYPE_VECTOR2);

    mx::InputPtr texcoordInput = node->addInput("texcoord", MTLX_TYPE_VECTOR2);
    texcoordInput->setNodeName(texcoordNode->getName());

    mx::InputPtr offsetInput = node->addInput("offset", MTLX_TYPE_VECTOR2);
    offsetInput->setValue(mx::Vector2(-transform.offset[0], transform.offset[1]));

    mx::InputPtr rotationInput = node->addInput("rotate", MTLX_TYPE_FLOAT);
    rotationInput->setValue(float(GfRadiansToDegrees(-transform.rotation)));

    float scaleX = transform.scale[0] == 0.0f ? 0.0f : (1.0f / transform.scale[0]);
    float scaleY = transform.scale[1] == 0.0f ? 0.0f : (1.0f / transform.scale[1]);

    mx::InputPtr scaleInput = node->addInput("scale", MTLX_TYPE_VECTOR2);
    scaleInput->setValue(mx::Vector2(scaleX, scaleY));

    mx::InputPtr pivotInput = node->addInput("pivot", MTLX_TYPE_VECTOR2);
    pivotInput->setValue(mx::Vector2(0.0f, 1.0f));

    return node;
  }

  mx::NodePtr MaterialXMaterialConverter::addTextureNode(mx::NodeGraphPtr nodeGraph,
                                                         const std::string& filePath,
                                                         const std::string& textureType,
                                                         bool isSrgb,
                                                         const cgltf_texture_view& textureView,
                                                         mx::ValuePtr defaultValue)
  {
    mx::NodePtr node = nodeGraph->addNode("image", mx::EMPTY_STRING, textureType);

    const cgltf_texture_transform& transform = textureView.transform;
    int stIndex = (textureView.has_transform && transform.has_texcoord) ? transform.texcoord : textureView.texcoord;

    mx::NodePtr texcoordNode;
#ifndef NDEBUG
    if (TfGetEnvSetting(GUC_ENABLE_MTLX_VIEWER_COMPAT))
    {
      texcoordNode = nodeGraph->addNode("texcoord", mx::EMPTY_STRING, MTLX_TYPE_VECTOR2);

      mx::InputPtr indexInput = texcoordNode->addInput("index");
      indexInput->setValue(stIndex);
    }
    else
#endif
    {
      texcoordNode = makeGeompropValueNode(nodeGraph, makeStSetName(stIndex), MTLX_TYPE_VECTOR2);
    }

    if (textureView.has_transform && cgltf_transform_required(transform))
    {
      texcoordNode = addTextureTransformNode(nodeGraph, texcoordNode, transform);
    }

    mx::InputPtr uvInput = node->addInput("texcoord", MTLX_TYPE_VECTOR2);
    uvInput->setNodeName(texcoordNode->getName());

    mx::InputPtr fileInput = node->addInput("file", MTLX_TYPE_FILENAME);
    fileInput->setValue(filePath, MTLX_TYPE_FILENAME);
    fileInput->setAttribute("colorspace", isSrgb ? MTLX_COLORSPACE_SRGB : MTLX_COLORSPACE_LINEAR);

    if (defaultValue)
    {
      mx::InputPtr defaultInput = node->addInput("default", textureType);
      defaultInput->setAttribute("colorspace", MTLX_COLORSPACE_LINEAR);

      auto defaultValueString = detail::getTextureTypeAdjustedDefaultValueString(defaultValue, textureType);
      defaultInput->setValueString(defaultValueString);
    }

    const cgltf_sampler* sampler = textureView.texture->sampler;

    // spec sec. 5.29.1. texture sampler: "When undefined, a sampler with repeat wrapping and auto filtering SHOULD be used."
    if (sampler && (sampler->min_filter != 0 || sampler->mag_filter != 0))
    {
      std::string filtertype;
      if (sampler->min_filter == 0 && sampler->mag_filter != 0)
      {
        filtertype = detail::getMtlxFilterType(sampler->mag_filter);
      }
      else if (sampler->mag_filter == 0 && sampler->min_filter != 0)
      {
        filtertype = detail::getMtlxFilterType(sampler->min_filter);
      }
      else if (sampler->min_filter != sampler->mag_filter)
      {
        TF_DEBUG(GUC).Msg("texture min filter does not match mag filter; ignoring min filter\n");
        filtertype = detail::getMtlxFilterType(sampler->mag_filter);
      }

      if (!filtertype.empty())
      {
        auto filterInput = node->addInput("filtertype", MTLX_TYPE_STRING);
        filterInput->setValue(filtertype);
      }
    }

    auto uaddressModeInput = node->addInput("uaddressmode", MTLX_TYPE_STRING);
    uaddressModeInput->setValue(sampler ? detail::getMtlxAddressMode(sampler->wrap_s) : "periodic");

    auto vaddressModeInput = node->addInput("vaddressmode", MTLX_TYPE_STRING);
    vaddressModeInput->setValue(sampler ? detail::getMtlxAddressMode(sampler->wrap_t) : "periodic");

    return node;
  }

  mx::NodePtr MaterialXMaterialConverter::makeGeompropValueNode(mx::NodeGraphPtr nodeGraph,
                                                                const std::string& geompropName,
                                                                const std::string& geompropValueTypeName,
                                                                mx::ValuePtr defaultValue)
  {
    mx::NodePtr node;
#ifndef NDEBUG
    if (TfGetEnvSetting(GUC_ENABLE_MTLX_VIEWER_COMPAT))
    {
      node = nodeGraph->addNode("constant", mx::EMPTY_STRING, geompropValueTypeName);

      // Workaround for MaterialXView not supporting geompropvalue node fallback values:
      // https://github.com/AcademySoftwareFoundation/MaterialX/issues/941
      mx::ValuePtr valuePtr = defaultValue;
      if (!valuePtr)
      {
        if (geompropName == m_defaultColorSetName)
        {
          valuePtr = mx::Value::createValue(mx::Color3(1.0f));
        }
        else if (geompropName == m_defaultOpacitySetName)
        {
          valuePtr = mx::Value::createValue(1.0f);
        }
        else
        {
          TF_VERIFY(false);
        }
      }

      auto valueInput = node->addInput("value", geompropValueTypeName);
      valueInput->setValueString(valuePtr->getValueString());
    }
    else
#endif
    {
      node = nodeGraph->addNode("geompropvalue", mx::EMPTY_STRING, geompropValueTypeName);

      auto geompropInput = node->addInput("geomprop", MTLX_TYPE_STRING);
      geompropInput->setValue(geompropName);

      if (defaultValue)
      {
        auto defaultInput = node->addInput("default", geompropValueTypeName);
        defaultInput->setValueString(defaultValue->getValueString());
      }
    }

    if (geompropName == m_defaultColorSetName)
    {
      node->setAttribute("colorspace", MTLX_COLORSPACE_LINEAR);
    }

    return node;
  }

  void MaterialXMaterialConverter::connectNodeGraphNodeToShaderInput(mx::NodeGraphPtr nodeGraph, mx::InputPtr input, mx::NodePtr node)
  {
    const std::string& nodeName = node->getName();

    std::string outName = "out_" + nodeName;

    mx::OutputPtr output = nodeGraph->addOutput(outName, node->getType());
    output->setNodeName(nodeName);

    input->setOutputString(outName);
    input->setNodeGraphString(nodeGraph->getName());
    input->removeAttribute("value");
  }

  bool MaterialXMaterialConverter::getTextureMetadata(const cgltf_texture_view& textureView, ImageMetadata& metadata) const
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

  bool MaterialXMaterialConverter::getTextureFilePath(const cgltf_texture_view& textureView, std::string& filePath) const
  {
    ImageMetadata metadata;
    if (!getTextureMetadata(textureView, metadata))
    {
      return false;
    }
    filePath = metadata.refPath;
    return true;
  }

  int MaterialXMaterialConverter::getTextureChannelCount(const cgltf_texture_view& textureView) const
  {
    ImageMetadata metadata;
    TF_VERIFY(getTextureMetadata(textureView, metadata));
    return metadata.channelCount;
  }

  std::string MaterialXMaterialConverter::getTextureValueType(const cgltf_texture_view& textureView, bool color) const
  {
    ImageMetadata metadata;
    if (!getTextureMetadata(textureView, metadata))
    {
      TF_VERIFY(false);
      return "";
    }

    int channelCount = metadata.channelCount;

    if (channelCount == 4)
    {
      return color ? MTLX_TYPE_COLOR4 : MTLX_TYPE_VECTOR4;
    }
    else if (channelCount == 3)
    {
      return color ? MTLX_TYPE_COLOR3 : MTLX_TYPE_VECTOR3;
    }
    else if (channelCount == 2)
    {
      return MTLX_TYPE_VECTOR2;
    }
    else if (channelCount == 1)
    {
      return MTLX_TYPE_FLOAT;
    }
    else
    {
      TF_VERIFY(false);
      return "";
    }
  }
}
