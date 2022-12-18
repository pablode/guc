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

#include <MaterialXCore/Document.h>
#include <MaterialXCore/Value.h>
#include <MaterialXFormat/XmlIo.h>
#include <MaterialXFormat/Util.h>

#include <filesystem>
#include <cassert>

#include "naming.h"
#include "debugCodes.h"

// By setting this define, the exported .mtlx file can be imported into MaterialX's viewer.
// The individual materials must be manually mapped to the meshes of the original glTF file.
// The look may not be 100% correct because vertex color multiplications are omitted.
//#define MATERIALXVIEW_COMPAT

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

namespace detail
{
  using namespace guc;

  mx::Color3 makeMxColor3(const float* ptr)
  {
    return mx::Color3(ptr[0], ptr[1], ptr[2]);
  }

  // Same logic as: https://github.com/PixarAnimationStudios/USD/blob/3b097e3ba8fabf1777a1256e241ea15df83f3065/pxr/imaging/hdSt/textureUtils.cpp#L74-L94
  float convertLinearFloatToSrgb(float in)
  {
    float out;
    if (in <= 0.0031308f) {
      out = 12.92f * in;
    }
    else {
      out = 1.055f * pow(in, 1.0f / 2.4f) - 0.055f;
    }
    return GfClamp(out, 0.0f, 1.0f);
  }

  mx::ValuePtr convertFloat3ValueToSrgb(mx::ValuePtr value)
  {
    if (value->isA<mx::Color3>())
    {
      auto color = value->asA<mx::Color3>();
      return mx::Value::createValue(mx::Color3(convertLinearFloatToSrgb(color[0]),
                                               convertLinearFloatToSrgb(color[1]),
                                               convertLinearFloatToSrgb(color[2])));
    }
    else
    {
      auto vector = value->asA<mx::Vector3>();
      return mx::Value::createValue(mx::Vector3(convertLinearFloatToSrgb(vector[0]),
                                                convertLinearFloatToSrgb(vector[1]),
                                                convertLinearFloatToSrgb(vector[2])));
    }
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
        // weird scenario: greyscale+alpha texture that RGB is read from. have to choose one component from vec3 as default.
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

  // These two functions implement the following code with MaterialX nodes:
  // https://github.com/PixarAnimationStudios/USD/blob/3b097e3ba8fabf1777a1256e241ea15df83f3065/pxr/imaging/hdSt/textureUtils.cpp#L74-L94
  mx::NodePtr makeSrgbToLinearConversionNodes(mx::NodeGraphPtr nodeGraph, mx::NodePtr srcNode)
  {
    TF_VERIFY(srcNode->getType() == MTLX_TYPE_FLOAT);

    mx::NodePtr leftBranch = nodeGraph->addNode("divide", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      auto in1Input = leftBranch->addInput("in1", MTLX_TYPE_FLOAT);
      in1Input->setNodeName(srcNode->getName());

      auto in2Input = leftBranch->addInput("in2", MTLX_TYPE_FLOAT);
      in2Input->setValue(12.92f);
    }

    mx::NodePtr rightBranch = nodeGraph->addNode("power", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      mx::NodePtr addNode = nodeGraph->addNode("add", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
      {
        auto in1Input = addNode->addInput("in1", MTLX_TYPE_FLOAT);
        in1Input->setNodeName(srcNode->getName());

        auto in2Input = addNode->addInput("in2", MTLX_TYPE_FLOAT);
        in2Input->setValue(0.055f);
      }

      mx::NodePtr divideNode = nodeGraph->addNode("divide", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
      {
        auto in1Input = divideNode->addInput("in1", MTLX_TYPE_FLOAT);
        in1Input->setNodeName(addNode->getName());

        auto in2Input = divideNode->addInput("in2", MTLX_TYPE_FLOAT);
        in2Input->setValue(1.055f);
      }

      auto in1Input = rightBranch->addInput("in1", MTLX_TYPE_FLOAT);
      in1Input->setNodeName(divideNode->getName());

      auto in2Input = rightBranch->addInput("in2", MTLX_TYPE_FLOAT);
      in2Input->setValue(2.4f);
    }

    mx::NodePtr ifGrEqNode = nodeGraph->addNode("ifgreatereq", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      auto value1Input = ifGrEqNode->addInput("value1", MTLX_TYPE_FLOAT);
      value1Input->setValue(0.04045f);

      auto value2Input = ifGrEqNode->addInput("value2", MTLX_TYPE_FLOAT);
      value2Input->setNodeName(srcNode->getName());

      auto in1Input = ifGrEqNode->addInput("in1", MTLX_TYPE_FLOAT);
      in1Input->setNodeName(leftBranch->getName());

      auto in2Input = ifGrEqNode->addInput("in2", MTLX_TYPE_FLOAT);
      in2Input->setNodeName(rightBranch->getName());
    }

    return makeClampNode(nodeGraph, ifGrEqNode);
  }

  mx::NodePtr makeLinearToSrgbConversionNodes(mx::NodeGraphPtr nodeGraph, mx::NodePtr srcNode)
  {
    TF_VERIFY(srcNode->getType() == MTLX_TYPE_FLOAT);

    mx::NodePtr leftBranch = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      auto in1Input = leftBranch->addInput("in1", MTLX_TYPE_FLOAT);
      in1Input->setNodeName(srcNode->getName());

      auto in2Input = leftBranch->addInput("in2", MTLX_TYPE_FLOAT);
      in2Input->setValue(12.92f);
    }

    mx::NodePtr rightBranch = nodeGraph->addNode("subtract", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      mx::NodePtr powerNode = nodeGraph->addNode("power", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
      {
        auto in1Input = powerNode->addInput("in1", MTLX_TYPE_FLOAT);
        in1Input->setNodeName(srcNode->getName());

        auto in2Input = powerNode->addInput("in2", MTLX_TYPE_FLOAT);
        in2Input->setValue(1.0f / 2.4f);
      }

      mx::NodePtr multiplyNode = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
      {
        auto in1Input = multiplyNode->addInput("in1", MTLX_TYPE_FLOAT);
        in1Input->setNodeName(powerNode->getName());

        auto in2Input = multiplyNode->addInput("in2", MTLX_TYPE_FLOAT);
        in2Input->setValue(1.055f);
      }

      auto in1Input = rightBranch->addInput("in1", MTLX_TYPE_FLOAT);
      in1Input->setNodeName(multiplyNode->getName());

      auto in2Input = rightBranch->addInput("in2", MTLX_TYPE_FLOAT);
      in2Input->setValue(0.055f);
    }

    mx::NodePtr ifGrEqNode = nodeGraph->addNode("ifgreatereq", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      auto value1Input = ifGrEqNode->addInput("value1", MTLX_TYPE_FLOAT);
      value1Input->setValue(0.0031308f);

      auto value2Input = ifGrEqNode->addInput("value2", MTLX_TYPE_FLOAT);
      value2Input->setNodeName(srcNode->getName());

      auto in1Input = ifGrEqNode->addInput("in1", MTLX_TYPE_FLOAT);
      in1Input->setNodeName(leftBranch->getName());

      auto in2Input = ifGrEqNode->addInput("in2", MTLX_TYPE_FLOAT);
      in2Input->setNodeName(rightBranch->getName());
    }

    return makeClampNode(nodeGraph, ifGrEqNode);
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
                                                         const ImageMetadataMap& imageMetadataMap,
                                                         bool flatten_nodes,
                                                         bool explicit_colorspace_transforms,
                                                         bool hdstorm_compat)
    : m_doc(doc)
    , m_imageMetadataMap(imageMetadataMap)
    , m_defaultColorSetName(makeColorSetName(0))
    , m_defaultOpacitySetName(makeOpacitySetName(0))
    , m_flattenNodes(flatten_nodes)
#ifndef MATERIALXVIEW_COMPAT
    , m_explicitColorSpaceTransforms(explicit_colorspace_transforms || hdstorm_compat)
    , m_hdstormCompat(hdstorm_compat)
#else
    , m_explicitColorSpaceTransforms(explicit_colorspace_transforms)
    , m_hdstormCompat(false)
#endif
  {
    if (!m_explicitColorSpaceTransforms)
    {
      // see MaterialX spec "Color Spaces and Color Management Systems"
      m_doc->setAttribute("colorspace", MTLX_COLORSPACE_LINEAR);
    }
  }

  void MaterialXMaterialConverter::convert(const cgltf_material* material, const std::string& materialName)
  {
    // By default, the scientific notation is emitted for small values, causing the document to be invalid
    mx::ScopedFloatFormatting floatFormat(mx::Value::FloatFormatFixed);

    std::string nodegraphName = "NG_" + materialName;
    std::string shaderName = "SR_" + materialName;

    mx::NodeGraphPtr nodeGraph = m_doc->addNodeGraph(nodegraphName);
    mx::GraphElementPtr shaderNodeRoot = m_flattenNodes ? std::static_pointer_cast<mx::GraphElement>(nodeGraph) : std::static_pointer_cast<mx::GraphElement>(m_doc);
    mx::NodePtr shaderNode = shaderNodeRoot->addNode("gltf_pbr", shaderName, MTLX_TYPE_SURFACESHADER);

    // Fill nodegraph with helper nodes (e.g. textures) and set glTF PBR node params.
    setGltfPbrInputs(material, nodeGraph, shaderNode);

    if (m_flattenNodes)
    {
      // Expand glTF PBR node to implementation nodes.
      nodeGraph->flattenSubgraphs();

      // According to https://github.com/PixarAnimationStudios/USD/issues/1502, to be compatible
      // with UsdMtlx, we need to have all nodes except the surface node inside a nodegraph. For
      // that, we extract the surface node to the nodegraph outside after flattening.

      // 1. Find surface shader in nodegraph.
      auto surfaceNodes = nodeGraph->getNodesOfType(mx::SURFACE_SHADER_TYPE_STRING);
      assert(surfaceNodes.size() == 1);
      mx::NodePtr surfaceNode = surfaceNodes[0];

      // 2. Create new surface node.
      mx::NodePtr newSurfaceNode = m_doc->addNode("surface", shaderName, MTLX_TYPE_SURFACESHADER);
      for (mx::InputPtr surfaceInput : surfaceNode->getInputs())
      {
        std::string nodegraphOutputName = "out_" + surfaceInput->getName();

        mx::OutputPtr nodegraphOutput = nodeGraph->addOutput(nodegraphOutputName, surfaceInput->getType());
        nodegraphOutput->setNodeName(surfaceInput->getNodeName());

        mx::InputPtr newSurfaceInput = newSurfaceNode->addInput(surfaceInput->getName(), surfaceInput->getType());
        newSurfaceInput->setNodeGraphString(nodegraphName);
        newSurfaceInput->setOutputString(nodegraphOutputName);
      }

      // 3. Remove old surface from nodegraph.
      nodeGraph->removeNode(surfaceNode->getName());
    }

    // Create material and connect surface to it
    mx::NodePtr materialNode = m_doc->addNode("surfacematerial", materialName, MTLX_TYPE_MATERIAL);
    mx::InputPtr materialSurfaceInput = materialNode->addInput("surfaceshader", MTLX_TYPE_SURFACESHADER);
    materialSurfaceInput->setNodeName(shaderNode->getName());
  }

  void MaterialXMaterialConverter::setGltfPbrInputs(const cgltf_material* material,
                                                    mx::NodeGraphPtr nodeGraph,
                                                    mx::NodePtr shaderNode)
  {
    mx::InputPtr baseColorInput = shaderNode->addInput("base_color", MTLX_TYPE_COLOR3);
    mx::InputPtr alphaInput = shaderNode->addInput("alpha", MTLX_TYPE_FLOAT);
    mx::InputPtr occlusionInput = shaderNode->addInput("occlusion", MTLX_TYPE_FLOAT);
    mx::InputPtr metallicInput = shaderNode->addInput("metallic", MTLX_TYPE_FLOAT);
    mx::InputPtr roughnessInput = shaderNode->addInput("roughness", MTLX_TYPE_FLOAT);

    // FIXME: overwrite default values for the following inputs, as they are incorrect in
    //        MaterialX 1.38.4. Remove this in later versions (see MaterialX PR #971).
    auto baseColorDefault = mx::Color3(1.0f);
    auto alphaDefault = 1.0f;

    baseColorInput->setValue(baseColorDefault);
    alphaInput->setValue(alphaDefault);
    occlusionInput->setValue(1.0f);
    metallicInput->setValue(1.0f);
    roughnessInput->setValue(1.0f);

    mx::InputPtr emissiveInput = shaderNode->addInput("emissive", MTLX_TYPE_COLOR3);
    mx::Color3 emissiveFactor = detail::makeMxColor3(material->emissive_factor);
    auto emissiveDefault = mx::Color3(1.0f, 1.0f, 1.0f); // spec sec. 5.19.7
    setSrgbTextureInput(nodeGraph, emissiveInput, material->emissive_texture, emissiveFactor, emissiveDefault);

    mx::InputPtr normalInput = shaderNode->addInput("normal", MTLX_TYPE_VECTOR3);
    if (!setNormalTextureInput(nodeGraph, normalInput, material->normal_texture))
    {
      // in case no texture has been found, fall back to the implicit declaration (defaultgeomprop="Nworld")
      shaderNode->removeInput("normal");
    }

    setOcclusionTextureInput(nodeGraph, occlusionInput, material->occlusion_texture);

    mx::InputPtr alphaModeInput = shaderNode->addInput("alpha_mode", MTLX_TYPE_INTEGER);
    alphaModeInput->setValue(int(material->alpha_mode));

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
        setAlphaTextureInput(nodeGraph, alphaInput, &pbrMetallicRoughness->base_color_texture, pbrMetallicRoughness->base_color_factor[3]);
      }

      setDiffuseTextureInput(nodeGraph, baseColorInput, &pbrMetallicRoughness->base_color_texture, detail::makeMxColor3(pbrMetallicRoughness->base_color_factor));

      auto metallicDefault = 1.0f; // spec sec. 5.22.5
      setFloatTextureInput(nodeGraph, metallicInput, pbrMetallicRoughness->metallic_roughness_texture, 2, pbrMetallicRoughness->metallic_factor, metallicDefault);

      auto roughnessDefault = 1.0f; // spec sec. 5.22.5
      setFloatTextureInput(nodeGraph, roughnessInput, pbrMetallicRoughness->metallic_roughness_texture, 1, pbrMetallicRoughness->roughness_factor, roughnessDefault);
    }
    // Regardless of the existence of base color and texture, we still need to multiply by vertex color / opacity
    else
    {
      setDiffuseTextureInput(nodeGraph, baseColorInput, nullptr, baseColorDefault);

      if (material->alpha_mode != cgltf_alpha_mode_opaque)
      {
        setAlphaTextureInput(nodeGraph, alphaInput, nullptr, alphaDefault);
      }
    }

    if (material->has_clearcoat)
    {
      const cgltf_clearcoat* clearcoat = &material->clearcoat;

      mx::InputPtr clearcoatInput = shaderNode->addInput("clearcoat", MTLX_TYPE_FLOAT);
      auto clearcoatDefault = 1.0f; // according to spec
      setFloatTextureInput(nodeGraph, clearcoatInput, clearcoat->clearcoat_texture, 0, clearcoat->clearcoat_factor, clearcoatDefault);

      mx::InputPtr clearcoatRoughnessInput = shaderNode->addInput("clearcoat_roughness", MTLX_TYPE_FLOAT);
      auto clearcodeRoughnessDefault = 1.0f; // according to spec
      setFloatTextureInput(nodeGraph, clearcoatRoughnessInput, clearcoat->clearcoat_roughness_texture, 1, clearcoat->clearcoat_roughness_factor, clearcodeRoughnessDefault);

      mx::InputPtr clearcoatNormalInput = shaderNode->addInput("clearcoat_normal", MTLX_TYPE_VECTOR3);
      if (!setNormalTextureInput(nodeGraph, clearcoatNormalInput, clearcoat->clearcoat_normal_texture))
      {
        // in case no texture has been found, fall back to the implicit declaration (defaultgeomprop="Nworld")
        shaderNode->removeInput("clearcoat_normal");
      }
    }

    if (material->has_transmission)
    {
      const cgltf_transmission* transmission = &material->transmission;

      mx::InputPtr transmissionInput = shaderNode->addInput("transmission", MTLX_TYPE_FLOAT);
      auto transmissionDefault = 0.0f; // not given by spec
      setFloatTextureInput(nodeGraph, transmissionInput, transmission->transmission_texture, 0, transmission->transmission_factor, transmissionDefault);
    }

    if (material->has_volume)
    {
      const cgltf_volume* volume = &material->volume;

      mx::InputPtr thicknessInput = shaderNode->addInput("thickness", MTLX_TYPE_FLOAT);
      auto thicknessDefault = 0.0f; // not given by spec
      setFloatTextureInput(nodeGraph, thicknessInput, volume->thickness_texture, 1, volume->thickness_factor, thicknessDefault);

      mx::InputPtr attenuationDistanceInput = shaderNode->addInput("attenuation_distance", MTLX_TYPE_FLOAT);
      attenuationDistanceInput->setValue(volume->attenuation_distance);

      mx::InputPtr attenuationColorInput = shaderNode->addInput("attenuation_color", MTLX_TYPE_COLOR3);
      attenuationColorInput->setValue(detail::makeMxColor3(volume->attenuation_color));
    }

    if (material->has_ior)
    {
      const cgltf_ior* ior = &material->ior;

      mx::InputPtr iorInput = shaderNode->addInput("ior", MTLX_TYPE_FLOAT);
      iorInput->setValue(ior->ior);
    }

    if (material->has_specular)
    {
      const cgltf_specular* specular = &material->specular;

      mx::InputPtr specularInput = shaderNode->addInput("specular", MTLX_TYPE_FLOAT);
      auto specularDefault = 1.0f; // not given by spec
      setFloatTextureInput(nodeGraph, specularInput, specular->specular_texture, 3, specular->specular_factor, specularDefault);

      mx::InputPtr specularColorInput = shaderNode->addInput("specular_color", MTLX_TYPE_COLOR3);
      auto specularColorDefault = mx::Color3(1.0f); // not given by spec
      setSrgbTextureInput(nodeGraph, specularColorInput, specular->specular_color_texture, detail::makeMxColor3(specular->specular_color_factor), specularColorDefault);
    }

    if (material->has_sheen)
    {
      const cgltf_sheen* sheen = &material->sheen;

      mx::InputPtr sheenColorInput = shaderNode->addInput("sheen_color", MTLX_TYPE_COLOR3);
      auto sheenColorDefault = mx::Color3(0.0f); // not given by spec
      setSrgbTextureInput(nodeGraph, sheenColorInput, sheen->sheen_color_texture, detail::makeMxColor3(sheen->sheen_color_factor), sheenColorDefault);

      mx::InputPtr sheenRoughnessInput = shaderNode->addInput("sheen_roughness", MTLX_TYPE_FLOAT);
      auto sheenRoughnessDefault = 0.0f; // not given by spec
      setFloatTextureInput(nodeGraph, sheenRoughnessInput, sheen->sheen_roughness_texture, 3, sheen->sheen_roughness_factor, sheenRoughnessDefault);
    }

    // Unfortunately, hdStorm blending is messed up because the material is not flagged as 'translucent':
    // https://github.com/PixarAnimationStudios/USD/blob/db8e3266dcaa24aa26b7201bc20ff4d8e81448d6/pxr/imaging/hdSt/materialXFilter.cpp#L441-L507
    // For alpha materials, set a non-zero transmission input to make the renderer believe that we are a translucent Standard Surface.
    // We don't seem to need this if we flatten the glTF PBR node.
    if (material->alpha_mode != cgltf_alpha_mode_opaque && m_hdstormCompat && !m_flattenNodes)
    {
      mx::InputPtr transmissionInput = material->has_transmission ? shaderNode->getInput("transmission") : shaderNode->addInput("transmission", MTLX_TYPE_FLOAT);

      if (!transmissionInput->hasValue() || (transmissionInput->getValue()->isA<float>() && transmissionInput->getValue()->asA<float>() == 0.0f))
      {
        float valueCloseToZero = 0.00001f;
        transmissionInput->setValue(valueCloseToZero);
      }
    }
  }

  void MaterialXMaterialConverter::setDiffuseTextureInput(mx::NodeGraphPtr nodeGraph,
                                                          mx::InputPtr shaderInput,
                                                          const cgltf_texture_view* textureView,
                                                          const mx::Color3& factor)
  {
    mx::NodePtr multiplyNode1 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_COLOR3);
    {
      mx::InputPtr input1 = multiplyNode1->addInput("in1", MTLX_TYPE_COLOR3);
      auto defaultValue = mx::Value::createValue(mx::Vector3(1.0f, 1.0f, 1.0f));

      // COLOR_0 according to spec sec. 3.9.2
      auto geompropNode = makeGeompropValueNode(nodeGraph, m_defaultColorSetName, MTLX_TYPE_COLOR3, defaultValue);
      input1->setNodeName(geompropNode->getName());

      mx::InputPtr input2 = multiplyNode1->addInput("in2", MTLX_TYPE_COLOR3);
      input2->setValue(factor);
    }

    std::string filePath;
    if (!textureView || !getTextureFilePath(*textureView, filePath))
    {
      connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, multiplyNode1);
      return;
    }

    auto defaultValue = mx::Value::createValue(mx::Color3(1.0f, 1.0f, 1.0f)); // spec sec. 5.22.2
    mx::NodePtr textureNode = addFloat3TextureNodes(nodeGraph, *textureView, filePath, true, defaultValue);

    mx::NodePtr multiplyNode2 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_COLOR3);
    {
      auto input1 = multiplyNode2->addInput("in1", MTLX_TYPE_COLOR3);
      input1->setNodeName(multiplyNode1->getName());

      auto input2 = multiplyNode2->addInput("in2", MTLX_TYPE_COLOR3);
      input2->setNodeName(textureNode->getName());
    }

    connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, multiplyNode2);
  }

  void MaterialXMaterialConverter::setAlphaTextureInput(mx::NodeGraphPtr nodeGraph,
                                                        mx::InputPtr shaderInput,
                                                        const cgltf_texture_view* textureView,
                                                        float factor)
  {
    mx::NodePtr multiplyNode1 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      mx::InputPtr input1 = multiplyNode1->addInput("in1", MTLX_TYPE_FLOAT);
      auto defaultOpacity = mx::Value::createValue(1.0f);

      std::string opacityPrimvarName = makeOpacitySetName(0); // COLOR_0 according to spec sec. 3.9.2
      auto geompropNode = makeGeompropValueNode(nodeGraph, opacityPrimvarName, MTLX_TYPE_FLOAT, defaultOpacity);
      input1->setNodeName(geompropNode->getName());

      mx::InputPtr input2 = multiplyNode1->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setValue(factor);
    }

    std::string filePath;
    if (!textureView || !getTextureFilePath(*textureView, filePath))
    {
      connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, multiplyNode1);
      return;
    }

    auto defaultValue = 1.0f; // spec sec. 5.22.2
    mx::NodePtr valueNode = addFloatTextureNodes(nodeGraph, *textureView, filePath, 3, defaultValue);

    mx::NodePtr multiplyNode2 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      auto input1 = multiplyNode2->addInput("in1", MTLX_TYPE_FLOAT);
      input1->setNodeName(multiplyNode1->getName());

      auto input2 = multiplyNode2->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setNodeName(valueNode->getName());
    }

    connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, multiplyNode2);
  }

  bool MaterialXMaterialConverter::setNormalTextureInput(mx::NodeGraphPtr nodeGraph,
                                                         mx::InputPtr shaderInput,
                                                         const cgltf_texture_view& textureView)
  {
    std::string filePath;
    if (!getTextureFilePath(textureView, filePath))
    {
      return false;
    }

    // here, we basically implement the normalmap node, but with variable handedness by using the bitangent
    // https://github.com/AcademySoftwareFoundation/MaterialX/blob/main/libraries/stdlib/genglsl/mx_normalmap.glsl

    mx::ValuePtr defaultValue = mx::Value::createValue(mx::Vector3(0.5f, 0.5f, 1.0f));
    mx::NodePtr textureNode = addFloat3TextureNodes(nodeGraph, textureView, filePath, false, defaultValue);

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
    mx::NodePtr multiplyNode2 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      mx::InputPtr input1 = multiplyNode2->addInput("in1", MTLX_TYPE_VECTOR3);
      input1->setNodeName(subtractNode->getName());

      mx::InputPtr input2 = multiplyNode2->addInput("in2", MTLX_TYPE_VECTOR3);
      input2->setValue(mx::Vector3(textureView.scale, textureView.scale, 1.0f));
    }

    // not done in the MaterialX normalmap impl, but required according to glTF spec sec. 3.9.3
    mx::NodePtr normalizeNode1 = detail::makeNormalizeNode(nodeGraph, multiplyNode2);

    // let's avoid separate3 due to multi-output support concerns
    mx::NodePtr outx = detail::makeExtractChannelNode(nodeGraph, normalizeNode1, 0);
    mx::NodePtr outy = detail::makeExtractChannelNode(nodeGraph, normalizeNode1, 1);
    mx::NodePtr outz = detail::makeExtractChannelNode(nodeGraph, normalizeNode1, 2);

    auto normalNode = nodeGraph->addNode("normal", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      auto spaceInput = normalNode->addInput("space", MTLX_TYPE_STRING);
      spaceInput->setValue("world");
    }
    auto tangentNode = nodeGraph->addNode("tangent", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      auto spaceInput = tangentNode->addInput("space", MTLX_TYPE_STRING);
      spaceInput->setValue("world");
    }

#ifndef MATERIALXVIEW_COMPAT
    auto crossproductNode = nodeGraph->addNode("crossproduct", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
     auto input1 = crossproductNode->addInput("in1", MTLX_TYPE_VECTOR3);
     input1->setNodeName(normalNode->getName());

     auto input2 = crossproductNode->addInput("in2", MTLX_TYPE_VECTOR3);
     input2->setNodeName(tangentNode->getName());
    }

    auto tangentSignNode = makeGeompropValueNode(nodeGraph, "tangentSigns", MTLX_TYPE_FLOAT);

    auto bitangentNode = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      auto input1 = bitangentNode->addInput("in1", MTLX_TYPE_VECTOR3);
      input1->setNodeName(crossproductNode->getName());

      auto input2 = bitangentNode->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setNodeName(tangentSignNode->getName());
    }
#else
    auto bitangentNode = nodeGraph->addNode("bitangent", mx::EMPTY_STRING, MTLX_TYPE_VECTOR3);
    {
      auto spaceInput = bitangentNode->addInput("space", MTLX_TYPE_STRING);
      spaceInput->setValue("world");
    }
#endif

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

    return true;
  }

  void MaterialXMaterialConverter::setOcclusionTextureInput(mx::NodeGraphPtr nodeGraph,
                                                            mx::InputPtr shaderInput,
                                                            const cgltf_texture_view& textureView)
  {
    std::string filePath;
    if (!getTextureFilePath(textureView, filePath))
    {
      return;
    }

    // glTF spec 2.0 3.9.3.
    // if 'strength' attribute is present, it affects occlusion as follows:
    //     1.0 + strength * (occlusionTexture - 1.0)

    auto defaultValue = 1.0f; // fall back to unoccluded area if texture is not found
    mx::NodePtr valueNode = addFloatTextureNodes(nodeGraph, textureView, filePath, 0, defaultValue);

    mx::NodePtr substractNode = nodeGraph->addNode("subtract", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      auto input1 = substractNode->addInput("in1", MTLX_TYPE_FLOAT);
      input1->setNodeName(valueNode->getName());

      auto input2 = substractNode->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setValue(1.0f);
    }

    mx::NodePtr multiplyNode = nodeGraph->addNode("multiply", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      auto input1 = multiplyNode->addInput("in1", MTLX_TYPE_FLOAT);
      input1->setValue(textureView.scale);

      auto input2 = multiplyNode->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setNodeName(substractNode->getName());
    }

    mx::NodePtr addNode = nodeGraph->addNode("add", mx::EMPTY_STRING, MTLX_TYPE_FLOAT);
    {
      auto input1 = addNode->addInput("in1", MTLX_TYPE_FLOAT);
      input1->setValue(1.0f);

      auto input2 = addNode->addInput("in2", MTLX_TYPE_FLOAT);
      input2->setNodeName(multiplyNode->getName());
    }

    connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, addNode);
  }

  void MaterialXMaterialConverter::setSrgbTextureInput(mx::NodeGraphPtr nodeGraph,
                                                       mx::InputPtr input,
                                                       const cgltf_texture_view& textureView,
                                                       mx::Color3 factorValue,
                                                       mx::Color3 defaultValue)
  {
    std::string valueString = mx::Value::createValue(factorValue)->getValueString();

    std::string filePath;
    if (getTextureFilePath(textureView, filePath))
    {
      auto defaultValuePtr = mx::Value::createValue(defaultValue);
      mx::NodePtr valueNode = addFloat3TextureNodes(nodeGraph, textureView, filePath, true, defaultValuePtr);

      mx::NodePtr multiplyNode = nodeGraph->addNode("multiply", mx::EMPTY_STRING, valueNode->getType());
      {
        auto input1 = multiplyNode->addInput("in1", valueNode->getType());
        input1->setValueString(valueString);

        auto input2 = multiplyNode->addInput("in2", valueNode->getType());
        input2->setNodeName(valueNode->getName());
      }

      connectNodeGraphNodeToShaderInput(nodeGraph, input, multiplyNode);
    }
    else if (!valueString.empty())
    {
      input->setValueString(valueString);
    }
  }

  void MaterialXMaterialConverter::setFloatTextureInput(mx::NodeGraphPtr nodeGraph,
                                                        mx::InputPtr input,
                                                        const cgltf_texture_view& textureView,
                                                        int channelIndex,
                                                        float factorValue,
                                                        float defaultValue)
  {
    std::string valueString = mx::Value::createValue(factorValue)->getValueString();

    std::string filePath;
    if (getTextureFilePath(textureView, filePath))
    {
      mx::NodePtr valueNode = addFloatTextureNodes(nodeGraph, textureView, filePath, channelIndex, defaultValue);

      mx::NodePtr multiplyNode = nodeGraph->addNode("multiply", mx::EMPTY_STRING, valueNode->getType());
      {
        auto input1 = multiplyNode->addInput("in1", valueNode->getType());
        input1->setValueString(valueString);

        auto input2 = multiplyNode->addInput("in2", valueNode->getType());
        input2->setNodeName(valueNode->getName());
      }

      connectNodeGraphNodeToShaderInput(nodeGraph, input, multiplyNode);
    }
    else if (!valueString.empty())
    {
      input->setValueString(valueString);
    }
  }

  mx::NodePtr MaterialXMaterialConverter::addFloatTextureNodes(mx::NodeGraphPtr nodeGraph,
                                                               const cgltf_texture_view& textureView,
                                                               std::string& filePath,
                                                               int channelIndex,
                                                               float defaultValue)
  {
    std::string texValueType = getTextureValueType(textureView, false);

    // USD may incorrectly detect the texture as sRGB and perform a colorspace conversion on the RGB components.
    bool isSrgbInUsd = m_hdstormCompat && isTextureSrgbInUsd(textureView) && (channelIndex != 3);

    if (isSrgbInUsd)
    {
      // The default value must be in the same colorspace as the image itself.
      defaultValue = detail::convertLinearFloatToSrgb(defaultValue);
    }
    auto defaultValuePtr = mx::Value::createValue(defaultValue);

    mx::NodePtr valueNode = addTextureNode(nodeGraph, filePath, texValueType, false, textureView, defaultValuePtr);

    if (texValueType != MTLX_TYPE_FLOAT)
    {
      bool remapChannelToAlpha = false;

      // USD probably handles greyscale+alpha textures like it does for the UsdPreviewSurface spec:
      // "If a two-channel texture is fed into a UsdUVTexture, the r, g, and b components of the rgb output will
      // repeat the first channel's value, while the single a output will be set to the second channel's value."
      if (m_hdstormCompat)
      {
        int channelCount = getTextureChannelCount(textureView);
        remapChannelToAlpha = (channelCount == 2 && channelIndex == 1);
      }

      valueNode = detail::makeExtractChannelNode(nodeGraph, valueNode, remapChannelToAlpha ? 3 : channelIndex);
    }

    if (isSrgbInUsd)
    {
      // Undo USD's incorrect sRGB->linear colorspace conversion.
      valueNode = detail::makeLinearToSrgbConversionNodes(nodeGraph, valueNode);
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

    bool isSrgbInUsd = m_hdstormCompat && isTextureSrgbInUsd(textureView);
    bool convertToSrgb = color && !isSrgbInUsd;
    bool vec3IncorrectlyLinearized = !color && isSrgbInUsd;

    // Bring the default value into the texture colorspace before performing colorspace transformation
    if (m_explicitColorSpaceTransforms && vec3IncorrectlyLinearized)
    {
      defaultValue = detail::convertFloat3ValueToSrgb(defaultValue);
    }

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

    if (m_explicitColorSpaceTransforms && (convertToSrgb || vec3IncorrectlyLinearized))
    {
      auto channel1Node = detail::makeExtractChannelNode(nodeGraph, valueNode, 0);
      channel1Node = vec3IncorrectlyLinearized ? detail::makeLinearToSrgbConversionNodes(nodeGraph, channel1Node) : detail::makeSrgbToLinearConversionNodes(nodeGraph, channel1Node);

      auto channel2Node = detail::makeExtractChannelNode(nodeGraph, valueNode, 1);
      channel2Node = vec3IncorrectlyLinearized ? detail::makeLinearToSrgbConversionNodes(nodeGraph, channel2Node) : detail::makeSrgbToLinearConversionNodes(nodeGraph, channel2Node);

      auto channel3Node = detail::makeExtractChannelNode(nodeGraph, valueNode, 2);
      channel3Node = vec3IncorrectlyLinearized ? detail::makeLinearToSrgbConversionNodes(nodeGraph, channel3Node) : detail::makeSrgbToLinearConversionNodes(nodeGraph, channel3Node);

      auto combineNode = nodeGraph->addNode("combine3", mx::EMPTY_STRING, desiredValueType);
      {
        auto input1 = combineNode->addInput("in1", channel1Node->getType());
        input1->setNodeName(channel1Node->getName());

        auto input2 = combineNode->addInput("in2", channel2Node->getType());
        input2->setNodeName(channel2Node->getName());

        auto input3 = combineNode->addInput("in3", channel3Node->getType());
        input3->setNodeName(channel3Node->getName());
      }

      valueNode = combineNode;
    }

    return valueNode;
  }

  mx::NodePtr MaterialXMaterialConverter::addTextureNode(mx::NodeGraphPtr nodeGraph,
                                                         const std::string& filePath,
                                                         const std::string& textureType,
                                                         bool isSrgb,
                                                         const cgltf_texture_view& textureView,
                                                         mx::ValuePtr defaultValue)
  {
    mx::NodePtr node = nodeGraph->addNode("image", mx::EMPTY_STRING, textureType);

    mx::InputPtr uvInput = node->addInput("texcoord", MTLX_TYPE_VECTOR2);
    int stIndex = textureView.texcoord;

#ifdef MATERIALXVIEW_COMPAT
    auto texcoordNode = nodeGraph->addNode("texcoord", mx::EMPTY_STRING, MTLX_TYPE_VECTOR2);
    {
      auto indexInput = texcoordNode->addInput("index");
      indexInput->setValue(stIndex);

      uvInput->setNodeName(texcoordNode->getName());
    }
#else
    auto geompropNode = makeGeompropValueNode(nodeGraph, makeStSetName(stIndex), MTLX_TYPE_VECTOR2);
    uvInput->setNodeName(geompropNode->getName());
#endif

    mx::InputPtr fileInput = node->addInput("file", MTLX_TYPE_FILENAME);
    fileInput->setValue(filePath, MTLX_TYPE_FILENAME);
    if (!m_explicitColorSpaceTransforms)
    {
      fileInput->setAttribute("colorspace", isSrgb ? MTLX_COLORSPACE_SRGB : MTLX_COLORSPACE_LINEAR);
    }

    if (defaultValue)
    {
      mx::InputPtr defaultInput = node->addInput("default", textureType);
      if (!m_explicitColorSpaceTransforms)
      {
        defaultInput->setAttribute("colorspace", MTLX_COLORSPACE_LINEAR);
      }

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
#ifdef MATERIALXVIEW_COMPAT
    node = nodeGraph->addNode("constant", mx::EMPTY_STRING, geompropValueTypeName);

    // Workaround for MaterialXView not supporting geompropvalue node fallback values:
    // https://github.com/AcademySoftwareFoundation/MaterialX/issues/941
    mx::ValuePtr valuePtr = defaultValue;
    if (!defaultValue)
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
#else
    node = nodeGraph->addNode("geompropvalue", mx::EMPTY_STRING, geompropValueTypeName);

    auto geompropInput = node->addInput("geomprop", MTLX_TYPE_STRING);
    geompropInput->setValue(geompropName);

    if (defaultValue)
    {
      auto defaultInput = node->addInput("default", geompropValueTypeName);
      defaultInput->setValueString(defaultValue->getValueString());
    }
#endif

    if (!m_explicitColorSpaceTransforms && geompropName == m_defaultColorSetName)
    {
      node->setAttribute("colorspace", MTLX_COLORSPACE_LINEAR);
    }

    return node;
  }

  void MaterialXMaterialConverter::connectNodeGraphNodeToShaderInput(mx::NodeGraphPtr nodeGraph, mx::InputPtr input, mx::NodePtr node)
  {
    const std::string& nodeName = node->getName();

    if (m_flattenNodes)
    {
      input->setNodeName(nodeName);
    }
    else
    {
      std::string outName = "out_" + nodeName;

      mx::OutputPtr output = nodeGraph->addOutput(outName, node->getType());
      output->setNodeName(nodeName);

      input->setOutputString(outName);
      input->setNodeGraphString(nodeGraph->getName());
    }

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

  bool MaterialXMaterialConverter::isTextureSrgbInUsd(const cgltf_texture_view& textureView) const
  {
    ImageMetadata metadata;
    TF_VERIFY(getTextureMetadata(textureView, metadata));
    return metadata.isSrgbInUSD;
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

    if (channelCount == 3 || (m_hdstormCompat && channelCount == 1))
    {
      // USD promotes single-channel textures to RGB
      return color ? MTLX_TYPE_COLOR3 : MTLX_TYPE_VECTOR3;
    }
    else if (channelCount == 4 || (m_hdstormCompat && channelCount == 2))
    {
      // And for greyscale-alpha textures, to RGBA (with vec2[1] being alpha)
      return color ? MTLX_TYPE_COLOR4 : MTLX_TYPE_VECTOR4;
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
