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

const char* COLORSPACE_SRGB = "srgb_texture";
const char* COLORSPACE_LINEAR = "lin_rec709";

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

    if ((defaultValue->isA<float>() && textureType == "float") ||
        (defaultValue->isA<mx::Color3>() && textureType == "color3") ||
        (defaultValue->isA<mx::Vector3>() && textureType == "vector3"))
    {
      valuePtr = defaultValue;
    }
    else if (defaultValue->isA<mx::Color3>())
    {
      auto colorValue = defaultValue->asA<mx::Color3>();
      if (textureType == "color4")
      {
        valuePtr = mx::Value::createValue(mx::Color4(colorValue[0], colorValue[1], colorValue[2], 1.0f));
      }
      else if (textureType == "vector2")
      {
        // weird scenario: greyscale+alpha texture that RGB is read from. have to choose one component from vec3 as default.
        valuePtr = mx::Value::createValue(mx::Vector2(colorValue[0]));
      }
      else if (textureType == "float")
      {
        // greyscale
        valuePtr = mx::Value::createValue(colorValue[0]);
      }
    }
    else if (defaultValue->isA<mx::Vector3>())
    {
      auto vectorValue = defaultValue->asA<mx::Vector3>();
      if (textureType == "vector4")
      {
        valuePtr = mx::Value::createValue(mx::Vector4(vectorValue[0], vectorValue[1], vectorValue[2], 1.0f));
      }
      else if (textureType == "vector2")
      {
        valuePtr = mx::Value::createValue(mx::Vector2(vectorValue[0]));
      }
      else if (textureType == "float")
      {
        valuePtr = mx::Value::createValue(vectorValue[0]);
      }
    }
    else if (defaultValue->isA<float>())
    {
      float floatValue = defaultValue->asA<float>();
      if (textureType == "vector2") {
        valuePtr = mx::Value::createValue(mx::Vector2(floatValue));
      }
      else if (textureType == "color3") {
        valuePtr = mx::Value::createValue(mx::Color3(floatValue));
      }
      else if (textureType == "vector3") {
        valuePtr = mx::Value::createValue(mx::Vector3(floatValue));
      }
      else if (textureType == "color4") {
        valuePtr = mx::Value::createValue(mx::Color4(floatValue));
      }
      else if (textureType == "vector4") {
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
    mx::NodePtr remapNode = nodeGraph->addNode("range", mx::EMPTY_STRING, srcNode->getType());
    remapNode->addInputsFromNodeDef();

    auto inInput = remapNode->getInput("in");
    inInput->setNodeName(srcNode->getName());

    // clamps to [0, 1] because of outlow, outhigh defaults
    auto doclampInput = remapNode->getInput("doclamp");
    doclampInput->setValue(true);

    return remapNode;
  }

  // These two functions implement the following code with MaterialX nodes:
  // https://github.com/PixarAnimationStudios/USD/blob/3b097e3ba8fabf1777a1256e241ea15df83f3065/pxr/imaging/hdSt/textureUtils.cpp#L74-L94
  mx::NodePtr makeSrgbToLinearConversionNodes(mx::NodeGraphPtr nodeGraph, mx::NodePtr srcNode)
  {
    TF_VERIFY(srcNode->getType() == "float");

    mx::NodePtr leftBranch = nodeGraph->addNode("divide", mx::EMPTY_STRING, "float");
    {
      leftBranch->addInputsFromNodeDef();

      auto in1Input = leftBranch->getInput("in1");
      in1Input->setNodeName(srcNode->getName());

      auto in2Input = leftBranch->getInput("in2");
      in2Input->setValue(12.92f);
    }

    mx::NodePtr rightBranch = nodeGraph->addNode("power", mx::EMPTY_STRING, "float");
    {
      mx::NodePtr addNode = nodeGraph->addNode("add", mx::EMPTY_STRING, "float");
      {
        addNode->addInputsFromNodeDef();

        auto in1Input = addNode->getInput("in1");
        in1Input->setNodeName(srcNode->getName());

        auto in2Input = addNode->getInput("in2");
        in2Input->setValue(0.055f);
      }

      mx::NodePtr divideNode = nodeGraph->addNode("divide", mx::EMPTY_STRING, "float");
      {
        divideNode->addInputsFromNodeDef();

        auto in1Input = divideNode->getInput("in1");
        in1Input->setNodeName(addNode->getName());

        auto in2Input = divideNode->getInput("in2");
        in2Input->setValue(1.055f);
      }

      rightBranch->addInputsFromNodeDef();

      auto in1Input = rightBranch->getInput("in1");
      in1Input->setNodeName(divideNode->getName());

      auto in2Input = rightBranch->getInput("in2");
      in2Input->setValue(2.4f);
    }

    mx::NodePtr ifGrEqNode = nodeGraph->addNode("ifgreatereq", mx::EMPTY_STRING, "float");
    {
      ifGrEqNode->addInputsFromNodeDef();

      auto value1Input = ifGrEqNode->getInput("value1");
      value1Input->setValue(0.04045f);

      auto value2Input = ifGrEqNode->getInput("value2");
      value2Input->setNodeName(srcNode->getName());

      auto in1Input = ifGrEqNode->getInput("in1");
      in1Input->setNodeName(leftBranch->getName());

      auto in2Input = ifGrEqNode->getInput("in2");
      in2Input->setNodeName(rightBranch->getName());
    }

    return makeClampNode(nodeGraph, ifGrEqNode);
  }

  mx::NodePtr makeLinearToSrgbConversionNodes(mx::NodeGraphPtr nodeGraph, mx::NodePtr srcNode)
  {
    TF_VERIFY(srcNode->getType() == "float");

    mx::NodePtr leftBranch = nodeGraph->addNode("multiply", mx::EMPTY_STRING, "float");
    {
      leftBranch->addInputsFromNodeDef();

      auto in1Input = leftBranch->getInput("in1");
      in1Input->setNodeName(srcNode->getName());

      auto in2Input = leftBranch->getInput("in2");
      in2Input->setValue(12.92f);
    }

    mx::NodePtr rightBranch = nodeGraph->addNode("subtract", mx::EMPTY_STRING, "float");
    {
      mx::NodePtr powerNode = nodeGraph->addNode("power", mx::EMPTY_STRING, "float");
      {
        powerNode->addInputsFromNodeDef();

        auto in1Input = powerNode->getInput("in1");
        in1Input->setNodeName(srcNode->getName());

        auto in2Input = powerNode->getInput("in2");
        in2Input->setValue(1.0f / 2.4f);
      }

      mx::NodePtr multiplyNode = nodeGraph->addNode("multiply", mx::EMPTY_STRING, "float");
      {
        multiplyNode->addInputsFromNodeDef();

        auto in1Input = multiplyNode->getInput("in1");
        in1Input->setNodeName(powerNode->getName());

        auto in2Input = multiplyNode->getInput("in2");
        in2Input->setValue(1.055f);
      }

      rightBranch->addInputsFromNodeDef();

      auto in1Input = rightBranch->getInput("in1");
      in1Input->setNodeName(multiplyNode->getName());

      auto in2Input = rightBranch->getInput("in2");
      in2Input->setValue(0.055f);
    }

    mx::NodePtr ifGrEqNode = nodeGraph->addNode("ifgreatereq", mx::EMPTY_STRING, "float");
    {
      ifGrEqNode->addInputsFromNodeDef();

      auto value1Input = ifGrEqNode->getInput("value1");
      value1Input->setValue(0.0031308f);

      auto value2Input = ifGrEqNode->getInput("value2");
      value2Input->setNodeName(srcNode->getName());

      auto in1Input = ifGrEqNode->getInput("in1");
      in1Input->setNodeName(leftBranch->getName());

      auto in2Input = ifGrEqNode->getInput("in2");
      in2Input->setNodeName(rightBranch->getName());
    }

    return makeClampNode(nodeGraph, ifGrEqNode);
  }

  mx::NodePtr makeExtractChannelNode(mx::NodeGraphPtr nodeGraph, mx::NodePtr srcNode, int index)
  {
    mx::NodePtr node = nodeGraph->addNode("extract", mx::EMPTY_STRING, "float");
    node->setAttribute("nodedef", "ND_extract_" + srcNode->getType());
    node->addInputsFromNodeDef();

    auto input = node->getInput("in");
    input->setNodeName(srcNode->getName());
    input->setType(srcNode->getType());

    auto indexInput = node->getInput("index");
    indexInput->setValue(index);

    return node;
  }

  mx::NodePtr makeConversionNode(mx::NodeGraphPtr nodeGraph, mx::NodePtr srcNode, const std::string& destType)
  {
    mx::NodePtr node = nodeGraph->addNode("convert", mx::EMPTY_STRING, destType);
    node->setAttribute("nodedef", "ND_convert_" + srcNode->getType() + "_" + destType);
    node->addInputsFromNodeDef();

    auto input = node->getInput("in");
    input->setNodeName(srcNode->getName());
    input->setType(srcNode->getType());

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
    , m_flattenNodes(flatten_nodes)
    , m_explicitColorSpaceTransforms(explicit_colorspace_transforms || hdstorm_compat)
    , m_hdstormCompat(hdstorm_compat)
  {
    if (!m_explicitColorSpaceTransforms)
    {
      // see MaterialX spec "Color Spaces and Color Management Systems"
      m_doc->setAttribute("colorspace", COLORSPACE_LINEAR);
    }
  }

  void MaterialXMaterialConverter::convert(const cgltf_material* material, const std::string& materialName)
  {
    std::string nodegraphName = "NG_" + materialName;
    std::string shaderName = "SR_" + materialName;

    mx::NodeGraphPtr nodeGraph = m_doc->addNodeGraph(nodegraphName);
    mx::GraphElementPtr shaderNodeRoot = m_flattenNodes ? std::static_pointer_cast<mx::GraphElement>(nodeGraph) : std::static_pointer_cast<mx::GraphElement>(m_doc);
    mx::NodePtr shaderNode = shaderNodeRoot->addNode("gltf_pbr", shaderName, "surfaceshader");

    shaderNode->addInputsFromNodeDef();

    // Fill nodegraph with helper nodes (e.g. textures) and set glTF PBR node params.
    setGltfPbrProperties(material, nodeGraph, shaderNode);

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
      mx::NodePtr newSurfaceNode = m_doc->addNode("surface", shaderName, "surfaceshader");
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
    mx::NodePtr materialNode = m_doc->addNode("surfacematerial", materialName, "material");
    mx::InputPtr materialSurfaceInput = materialNode->addInput("surfaceshader", "surfaceshader");
    materialSurfaceInput->setNodeName(shaderNode->getName());
  }

  void MaterialXMaterialConverter::setGltfPbrProperties(const cgltf_material* material,
                                                        mx::NodeGraphPtr nodeGraph,
                                                        mx::NodePtr shaderNode)
  {
    auto emissiveInput = shaderNode->getInput("emissive");
    mx::Color3 emissiveFactor = detail::makeMxColor3(material->emissive_factor);
    auto emissiveDefault = mx::Color3(1.0f, 1.0f, 1.0f); // spec sec. 5.19.7
    setSrgbTextureInput(nodeGraph, emissiveInput, material->emissive_texture, emissiveFactor, emissiveDefault);

    auto normalInput = shaderNode->getInput("normal");
    setNormalTextureInput(nodeGraph, normalInput, material->normal_texture);

    auto occlusionInput = shaderNode->getInput("occlusion");
    setOcclusionTextureInput(nodeGraph, occlusionInput, material->occlusion_texture);

    mx::InputPtr alphaModeInput = shaderNode->getInput("alpha_mode");
    alphaModeInput->setValue(int(material->alpha_mode));

    if (material->alpha_mode == cgltf_alpha_mode_mask)
    {
      mx::InputPtr alphaCutoffInput = shaderNode->getInput("alpha_cutoff");
      alphaCutoffInput->setValue(material->alpha_cutoff);
    }

    if (material->has_pbr_metallic_roughness)
    {
      const cgltf_pbr_metallic_roughness* pbrMetallicRoughness = &material->pbr_metallic_roughness;

      if (material->alpha_mode != cgltf_alpha_mode_opaque)
      {
        mx::InputPtr alphaInput = shaderNode->getInput("alpha");
        setAlphaTextureInput(nodeGraph, alphaInput, pbrMetallicRoughness->base_color_texture, pbrMetallicRoughness->base_color_factor[3]);
      }

      mx::InputPtr baseColorInput = shaderNode->getInput("base_color");
      setDiffuseTextureInput(nodeGraph, baseColorInput, pbrMetallicRoughness->base_color_texture, detail::makeMxColor3(pbrMetallicRoughness->base_color_factor));

      mx::InputPtr metallicInput = shaderNode->getInput("metallic");
      auto metallicDefault = 1.0f; // spec sec. 5.22.5
      setFloatTextureInput(nodeGraph, metallicInput, pbrMetallicRoughness->metallic_roughness_texture, 2, pbrMetallicRoughness->metallic_factor, metallicDefault);

      mx::InputPtr roughnessInput = shaderNode->getInput("roughness");
      auto roughnessDefault = 1.0f; // spec sec. 5.22.5
      setFloatTextureInput(nodeGraph, roughnessInput, pbrMetallicRoughness->metallic_roughness_texture, 1, pbrMetallicRoughness->roughness_factor, roughnessDefault);
    }

    if (material->has_clearcoat)
    {
      const cgltf_clearcoat* clearcoat = &material->clearcoat;

      mx::InputPtr clearcoatInput = shaderNode->getInput("clearcoat");
      auto clearcoatDefault = 1.0f; // according to spec
      setFloatTextureInput(nodeGraph, clearcoatInput, clearcoat->clearcoat_texture, 0, clearcoat->clearcoat_factor, clearcoatDefault);

      mx::InputPtr clearcoatRoughnessInput = shaderNode->getInput("clearcoat_roughness");
      auto clearcodeRoughnessDefault = 1.0f; // according to spec
      setFloatTextureInput(nodeGraph, clearcoatRoughnessInput, clearcoat->clearcoat_roughness_texture, 1, clearcoat->clearcoat_roughness_factor, clearcodeRoughnessDefault);

      mx::InputPtr clearcoatNormalInput = shaderNode->getInput("clearcoat_normal");
      setNormalTextureInput(nodeGraph, clearcoatNormalInput, clearcoat->clearcoat_normal_texture);
    }

    if (material->has_transmission)
    {
      const cgltf_transmission* transmission = &material->transmission;

      mx::InputPtr transmissionInput = shaderNode->getInput("transmission");
      auto transmissionDefault = 0.0f; // not given by spec
      setFloatTextureInput(nodeGraph, transmissionInput, transmission->transmission_texture, 0, transmission->transmission_factor, transmissionDefault);
    }

    if (material->has_volume)
    {
      const cgltf_volume* volume = &material->volume;

      mx::InputPtr thicknessInput = shaderNode->getInput("thickness");
      auto thicknessDefault = 0.0f; // not given by spec
      setFloatTextureInput(nodeGraph, thicknessInput, volume->thickness_texture, 1, volume->thickness_factor, thicknessDefault);

      mx::InputPtr attenuationDistanceInput = shaderNode->getInput("attenuation_distance");
      attenuationDistanceInput->setValue(volume->attenuation_distance);

      mx::InputPtr attenuationColorInput = shaderNode->getInput("attenuation_color");
      attenuationColorInput->setValue(detail::makeMxColor3(volume->attenuation_color));
    }

    if (material->has_ior)
    {
      const cgltf_ior* ior = &material->ior;

      mx::InputPtr iorInput = shaderNode->getInput("ior");
      iorInput->setValue(ior->ior);
    }

    if (material->has_specular)
    {
      const cgltf_specular* specular = &material->specular;

      mx::InputPtr specularInput = shaderNode->getInput("specular");
      auto specularDefault = 1.0f; // not given by spec
      setFloatTextureInput(nodeGraph, specularInput, specular->specular_texture, 3, specular->specular_factor, specularDefault);

      mx::InputPtr specularColorInput = shaderNode->getInput("specular_color");
      auto specularColorDefault = mx::Color3(1.0f); // not given by spec
      setSrgbTextureInput(nodeGraph, specularColorInput, specular->specular_color_texture, detail::makeMxColor3(specular->specular_color_factor), specularColorDefault);
    }

    if (material->has_sheen)
    {
      const cgltf_sheen* sheen = &material->sheen;

      mx::InputPtr sheenColorInput = shaderNode->getInput("sheen_color");
      auto sheenColorDefault = mx::Color3(0.0f); // not given by spec
      setSrgbTextureInput(nodeGraph, sheenColorInput, sheen->sheen_color_texture, detail::makeMxColor3(sheen->sheen_color_factor), sheenColorDefault);

      mx::InputPtr sheenRoughnessInput = shaderNode->getInput("sheen_roughness");
      auto sheenRoughnessDefault = 0.0f; // not given by spec
      setFloatTextureInput(nodeGraph, sheenRoughnessInput, sheen->sheen_roughness_texture, 3, sheen->sheen_roughness_factor, sheenRoughnessDefault);
    }

    // Unfortunately, hdStorm blending is messed up because the material is not flagged as 'translucent':
    // https://github.com/PixarAnimationStudios/USD/blob/db8e3266dcaa24aa26b7201bc20ff4d8e81448d6/pxr/imaging/hdSt/materialXFilter.cpp#L441-L507
    // For alpha materials, set a non-zero transmission input to make the renderer believe that we are a translucent Standard Surface.
    // We don't seem to need this if we flatten the glTF PBR node.
    if (material->alpha_mode != cgltf_alpha_mode_opaque && m_hdstormCompat && !m_flattenNodes)
    {
      mx::InputPtr transmissionInput = shaderNode->getInput("transmission");
      if (!transmissionInput->hasValue() || (transmissionInput->getValue()->isA<float>() && transmissionInput->getValue()->asA<float>() == 0.0f))
      {
        float valueCloseToZero = std::nextafter(0.0f, 1.0f);
        transmissionInput->setValue(valueCloseToZero);
      }
    }
  }

  void MaterialXMaterialConverter::setDiffuseTextureInput(mx::NodeGraphPtr nodeGraph,
                                                          mx::InputPtr shaderInput,
                                                          const cgltf_texture_view& textureView,
                                                          const mx::Color3& factor)
  {
    mx::NodePtr multiplyNode1 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, "color3");
    {
      multiplyNode1->addInputsFromNodeDef();

      mx::InputPtr input1 = multiplyNode1->getInput("in1");
      auto defaultValue = mx::Value::createValue(mx::Vector3(1.0f, 1.0f, 1.0f));
      addAndConnectGeompropValueNode(nodeGraph, input1, "displayColor", "color3", defaultValue);

      mx::InputPtr input2 = multiplyNode1->getInput("in2");
      input2->setValue(factor);
    }

    std::string uri;
    if (!getTextureImageFileName(textureView, uri))
    {
      connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, multiplyNode1);
      return;
    }

    auto defaultValue = mx::Value::createValue(mx::Color3(1.0f, 1.0f, 1.0f)); // spec sec. 5.22.2
    mx::NodePtr textureNode = addFloat3TextureNodes(nodeGraph, textureView, uri, true, defaultValue);

    mx::NodePtr multiplyNode2 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, "color3");
    {
      multiplyNode2->addInputsFromNodeDef();

      auto input1 = multiplyNode2->getInput("in1");
      input1->setNodeName(multiplyNode1->getName());

      auto input2 = multiplyNode2->getInput("in2");
      input2->setNodeName(textureNode->getName());
    }

    connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, multiplyNode2);
  }

  void MaterialXMaterialConverter::setAlphaTextureInput(mx::NodeGraphPtr nodeGraph,
                                                        mx::InputPtr shaderInput,
                                                        const cgltf_texture_view& textureView,
                                                        float factor)
  {
    mx::NodePtr multiplyNode1 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, "float");
    {
      multiplyNode1->addInputsFromNodeDef();

      mx::InputPtr input1 = multiplyNode1->getInput("in1");
      auto defaultOpacity = mx::Value::createValue(1.0f);
      addAndConnectGeompropValueNode(nodeGraph, input1, "displayOpacity", "float", defaultOpacity);

      mx::InputPtr input2 = multiplyNode1->getInput("in2");
      input2->setValue(factor);
    }

    std::string uri;
    if (!getTextureImageFileName(textureView, uri))
    {
      connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, multiplyNode1);
      return;
    }

    auto defaultValue = 1.0f; // spec sec. 5.22.2
    mx::NodePtr valueNode = addFloatTextureNodes(nodeGraph, textureView, uri, 3, defaultValue);

    mx::NodePtr multiplyNode2 = nodeGraph->addNode("multiply", mx::EMPTY_STRING, "float");
    {
      multiplyNode2->addInputsFromNodeDef();

      auto input1 = multiplyNode2->getInput("in1");
      input1->setNodeName(multiplyNode1->getName());

      auto input2 = multiplyNode2->getInput("in2");
      input2->setNodeName(valueNode->getName());
    }

    connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, multiplyNode2);
  }

  void MaterialXMaterialConverter::setNormalTextureInput(mx::NodeGraphPtr nodeGraph,
                                                         mx::InputPtr shaderInput,
                                                         const cgltf_texture_view& textureView)
  {
    std::string uri;
    if (!getTextureImageFileName(textureView, uri))
    {
      return;
    }

    mx::ValuePtr defaultValue = mx::Value::createValue(mx::Vector3(0.5f, 0.5f, 1.0f)); // default according to spec
    mx::NodePtr textureNode = addFloat3TextureNodes(nodeGraph, textureView, uri, false, defaultValue);

    // FIXME: it seems like this is not required; investigate
#if 0
    // we need to remap the texture [0, 1] values to [-1, 1] vectors
    mx::NodePtr multiplyNode = nodeGraph->addNode("multiply", mx::EMPTY_STRING, "vector3");
    {
      multiplyNode->addInputsFromNodeDef();

      mx::InputPtr input1 = multiplyNode->getInput("in1");
      input1->setNodeName(textureNode->getName());

      mx::InputPtr input2 = multiplyNode->getInput("in2");
      input2->setValue(mx::Vector3(2.0f));
    }

    mx::NodePtr substractNode = nodeGraph->addNode("subtract", mx::EMPTY_STRING, "vector3");
    {
      substractNode->addInputsFromNodeDef();

      auto input1 = substractNode->getInput("in1");
      input1->setNodeName(multiplyNode->getName());

      auto input2 = substractNode->getInput("in2");
      input2->setValue(mx::Vector3(1.0f));
    }

    mx::NodePtr normalizeNode = nodeGraph->addNode("normalize", mx::EMPTY_STRING, "vector3");
    {
      normalizeNode->addInputsFromNodeDef();

      auto input = normalizeNode->getInput("in");
      input->setNodeName(substractNode->getName());
    }
#endif

    mx::NodePtr normalMapNode = nodeGraph->addNode("normalmap", mx::EMPTY_STRING, "vector3");
    {
      normalMapNode->addInputsFromNodeDef();

      auto imageInput = normalMapNode->getInput("in");
      imageInput->setNodeName(textureNode->getName());

      // multiply with scale according to glTF spec 2.0 3.9.3.
      auto scaleInput = normalMapNode->getInput("scale");
      scaleInput->setValue(textureView.scale);
    }

    connectNodeGraphNodeToShaderInput(nodeGraph, shaderInput, normalMapNode);
  }

  void MaterialXMaterialConverter::setOcclusionTextureInput(mx::NodeGraphPtr nodeGraph,
                                                            mx::InputPtr shaderInput,
                                                            const cgltf_texture_view& textureView)
  {
    std::string uri;
    if (!getTextureImageFileName(textureView, uri))
    {
      return;
    }

    // glTF spec 2.0 3.9.3.
    // if 'strength' attribute is present, it affects occlusion as follows:
    //     1.0 + strength * (occlusionTexture - 1.0)

    auto defaultValue = 1.0f; // fall back to unoccluded area if texture is not found
    mx::NodePtr valueNode = addFloatTextureNodes(nodeGraph, textureView, uri, 0, defaultValue);

    mx::NodePtr substractNode = nodeGraph->addNode("subtract", mx::EMPTY_STRING, "float");
    {
      substractNode->addInputsFromNodeDef();

      auto input1 = substractNode->getInput("in1");
      input1->setNodeName(valueNode->getName());

      auto input2 = substractNode->getInput("in2");
      input2->setValue(1.0f);
    }

    mx::NodePtr multiplyNode = nodeGraph->addNode("multiply", mx::EMPTY_STRING, "float");
    {
      multiplyNode->addInputsFromNodeDef();

      auto input1 = multiplyNode->getInput("in1");
      input1->setValue(textureView.scale);

      auto input2 = multiplyNode->getInput("in2");
      input2->setNodeName(substractNode->getName());
    }

    mx::NodePtr addNode = nodeGraph->addNode("add", mx::EMPTY_STRING, "float");
    {
      addNode->addInputsFromNodeDef();

      auto input1 = addNode->getInput("in1");
      input1->setValue(1.0f);

      auto input2 = addNode->getInput("in2");
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

    std::string uri;
    if (getTextureImageFileName(textureView, uri))
    {
      auto defaultValuePtr = mx::Value::createValue(defaultValue);
      mx::NodePtr valueNode = addFloat3TextureNodes(nodeGraph, textureView, uri, true, defaultValuePtr);

      mx::NodePtr multiplyNode = nodeGraph->addNode("multiply", mx::EMPTY_STRING, valueNode->getType());
      {
        multiplyNode->addInputsFromNodeDef();

        auto input1 = multiplyNode->getInput("in1");
        input1->setValueString(valueString);

        auto input2 = multiplyNode->getInput("in2");
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

    std::string uri;
    if (getTextureImageFileName(textureView, uri))
    {
      mx::NodePtr valueNode = addFloatTextureNodes(nodeGraph, textureView, uri, channelIndex, defaultValue);

      mx::NodePtr multiplyNode = nodeGraph->addNode("multiply", mx::EMPTY_STRING, valueNode->getType());
      {
        multiplyNode->addInputsFromNodeDef();

        auto input1 = multiplyNode->getInput("in1");
        input1->setValueString(valueString);

        auto input2 = multiplyNode->getInput("in2");
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
                                                               std::string& uri,
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

    mx::NodePtr valueNode = addTextureNode(nodeGraph, uri, texValueType, false, textureView, defaultValuePtr);

    if (texValueType != "float")
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
                                                                std::string& uri,
                                                                bool color,
                                                                mx::ValuePtr defaultValue)
  {
    std::string desiredValueType = color ? "color3" : "vector3";
    std::string texValueType = getTextureValueType(textureView, color);

    bool isSrgbInUsd = m_hdstormCompat && isTextureSrgbInUsd(textureView);
    bool linearizeColor = color && !isSrgbInUsd;
    bool vec3IncorrectlyLinearized = !color && isSrgbInUsd;

    // Bring the default value into the texture colorspace before performing colorspace transformation
    if (m_explicitColorSpaceTransforms && linearizeColor)
    {
      defaultValue = detail::convertFloat3ValueToSrgb(defaultValue);
    }

    mx::NodePtr valueNode = addTextureNode(nodeGraph, uri, texValueType, color, textureView, defaultValue);

    // In case of RGBA, we need to drop one channel.
    if (texValueType == "color4" || texValueType == "vector4")
    {
      valueNode = detail::makeConversionNode(nodeGraph, valueNode, desiredValueType);
    }
    else
    {
      // In case of a greyscale images, we want to convert channel 0 (float) to color3.
      // For greyscale images with an alpha channel, we additionally need an extraction node.
      if (texValueType == "vector2")
      {
        valueNode = detail::makeExtractChannelNode(nodeGraph, valueNode, 0);
      }
      if (texValueType == "float" || texValueType == "vector2")
      {
        valueNode = detail::makeConversionNode(nodeGraph, valueNode, desiredValueType);
      }
    }

    if (m_explicitColorSpaceTransforms && (linearizeColor || vec3IncorrectlyLinearized))
    {
      auto channel1Node = detail::makeExtractChannelNode(nodeGraph, valueNode, 0);
      channel1Node = vec3IncorrectlyLinearized ? detail::makeLinearToSrgbConversionNodes(nodeGraph, channel1Node) : detail::makeSrgbToLinearConversionNodes(nodeGraph, channel1Node);

      auto channel2Node = detail::makeExtractChannelNode(nodeGraph, valueNode, 1);
      channel2Node = vec3IncorrectlyLinearized ? detail::makeLinearToSrgbConversionNodes(nodeGraph, channel2Node) : detail::makeSrgbToLinearConversionNodes(nodeGraph, channel2Node);

      auto channel3Node = detail::makeExtractChannelNode(nodeGraph, valueNode, 2);
      channel3Node = vec3IncorrectlyLinearized ? detail::makeLinearToSrgbConversionNodes(nodeGraph, channel3Node) : detail::makeSrgbToLinearConversionNodes(nodeGraph, channel3Node);

      auto combineNode = nodeGraph->addNode("combine3", mx::EMPTY_STRING, desiredValueType);
      {
        combineNode->addInputsFromNodeDef();

        auto input1 = combineNode->getInput("in1");
        input1->setNodeName(channel1Node->getName());

        auto input2 = combineNode->getInput("in2");
        input2->setNodeName(channel2Node->getName());

        auto input3 = combineNode->getInput("in3");
        input3->setNodeName(channel3Node->getName());
      }

      valueNode = combineNode;
    }

    return valueNode;
  }

  mx::NodePtr MaterialXMaterialConverter::addTextureNode(mx::NodeGraphPtr nodeGraph,
                                                         const std::string& uri,
                                                         const std::string& textureType,
                                                         bool isSrgb,
                                                         const cgltf_texture_view& textureView,
                                                         mx::ValuePtr defaultValue)
  {
    mx::NodePtr node = nodeGraph->addNode("image", mx::EMPTY_STRING, textureType);
    node->addInputsFromNodeDef();

    mx::InputPtr uvInput = node->getInput("texcoord");
    int stIndex = textureView.texcoord;

#ifdef MATERIALXVIEW_COMPAT
    auto texcoordNode = nodeGraph->addNode("texcoord", mx::EMPTY_STRING, "vector2");
    {
      texcoordNode->addInputsFromNodeDef();

      auto indexInput = texcoordNode->getInput("index");
      indexInput->setValue(stIndex);

      uvInput->setNodeName(texcoordNode->getName());
    }
#else
    addAndConnectGeompropValueNode(nodeGraph, uvInput, makeStSetName(stIndex), "vector2");
#endif

    mx::InputPtr fileInput = node->getInput("file");
    fileInput->setValue(uri, "filename");
    if (!m_explicitColorSpaceTransforms)
    {
      fileInput->setAttribute("colorspace", isSrgb ? COLORSPACE_SRGB : COLORSPACE_LINEAR);
    }

    if (defaultValue)
    {
      mx::InputPtr defaultInput = node->getInput("default");
      if (!m_explicitColorSpaceTransforms)
      {
        defaultInput->setAttribute("colorspace", COLORSPACE_LINEAR);
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
        auto filterInput = node->getInput("filtertype");
        filterInput->setValue(filtertype);
      }
    }

    auto uaddressModeInput = node->getInput("uaddressmode");
    uaddressModeInput->setValue(sampler ? detail::getMtlxAddressMode(sampler->wrap_s) : "periodic");

    auto vaddressModeInput = node->getInput("vaddressmode");
    vaddressModeInput->setValue(sampler ? detail::getMtlxAddressMode(sampler->wrap_t) : "periodic");

    return node;
  }

  void MaterialXMaterialConverter::addAndConnectGeompropValueNode(mx::NodeGraphPtr nodeGraph,
                                                                  mx::InputPtr input,
                                                                  const std::string& geompropName,
                                                                  const std::string& geompropValueTypeName,
                                                                  mx::ValuePtr defaultValue)
  {
    mx::NodePtr node;
#ifdef MATERIALXVIEW_COMPAT
    node = nodeGraph->addNode("constant", mx::EMPTY_STRING, geompropValueTypeName);
    node->addInputsFromNodeDef();

    mx::ValuePtr valuePtr = defaultValue;
    if (!defaultValue)
    {
      if (geompropName == "displayColor")
      {
        // FIXME: find out how handle different <geomcolor> value types
        valuePtr = mx::Value::createValue(mx::Color3(1.0f));
      }
      else if (geompropName == "displayOpacity")
      {
        // FIXME: extract from <geomcolor> when possible
        valuePtr = mx::Value::createValue(1.0f);
      }
      else
      {
        TF_VERIFY(false);
      }
    }

    auto valueInput = node->getInput("value");
    valueInput->setValueString(valuePtr->getValueString());
#else
    node = nodeGraph->addNode("geompropvalue", mx::EMPTY_STRING, geompropValueTypeName);
    node->addInputsFromNodeDef();

    auto geompropInput = node->getInput("geomprop");
    geompropInput->setValue(geompropName);

    if (defaultValue)
    {
      auto defaultInput = node->getInput("default");
      defaultInput->setValueString(defaultValue->getValueString());
    }
#endif

    if (!m_explicitColorSpaceTransforms && geompropName == "displayColor")
    {
      node->setAttribute("colorspace", COLORSPACE_LINEAR);
    }

    input->setNodeName(node->getName());
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

  bool MaterialXMaterialConverter::getTextureImageFileName(const cgltf_texture_view& textureView, std::string& fileName) const
  {
    ImageMetadata metadata;
    if (!getTextureMetadata(textureView, metadata))
    {
      return false;
    }
    fileName = metadata.exportedFileName;
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

    switch (metadata.channelCount)
    {
    case 1:
      return "float";
    case 2:
      return "vector2";
    case 3:
      return color ? "color3" : "vector3";
    case 4:
      return color ? "color4" : "vector4";
    }

    TF_VERIFY(false);
    return "";
  }
}
