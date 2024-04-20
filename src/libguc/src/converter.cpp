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

#include "converter.h"

#include <pxr/base/tf/envSetting.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/base/gf/camera.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdMtlx/reader.h>
#include <pxr/usd/usdMtlx/utils.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/kind/registry.h>

#include <MaterialXFormat/XmlIo.h>
#include <MaterialXFormat/Util.h>

#include "debugCodes.h"
#include "usdpreviewsurface.h"
#include "materialx.h"
#include "mesh.h"
#include "cgltf_util.h"
#include "image.h"
#include "naming.h"

namespace mx = MaterialX;
namespace fs = std::filesystem;

TF_DEFINE_PRIVATE_TOKENS(
  _tokens,
  (copyright)
  (generator)
  (version)
  (min_version)
  (bitangentSigns)
  (guc)
  (generated)
);

const static char* MTLX_GLTF_PBR_FILE_NAME = "gltf_pbr.mtlx";
const static char* DEFAULT_MATERIAL_NAME = "default";
const static cgltf_material DEFAULT_MATERIAL = {};

#ifndef NDEBUG
TF_DEFINE_ENV_SETTING(GUC_DISABLE_PREVIEW_MATERIAL_BINDINGS, false,
                      "Don't emit preview material bindings. This is used by the test suite.")
#endif

namespace detail
{
  using namespace guc;

  template<typename T>
  bool readVtArrayFromNonSparseAccessor(const cgltf_accessor* accessor, VtArray<T>& array)
  {
    cgltf_size elementSize = cgltf_calc_size(accessor->type, accessor->component_type);

    array.resize(accessor->count);

    for (size_t i = 0; i < accessor->count; i++)
    {
      T& item = array[i];

      if constexpr (std::is_same<T, int>())
      {
        unsigned int tmpUint = 0;
        if (!cgltf_accessor_read_uint(accessor, i, &tmpUint, elementSize))
        {
          TF_RUNTIME_ERROR("unable to read accessor data");
          return false;
        }
        item = (int) tmpUint;
      }
      else if constexpr (std::is_same<T, GfVec2f>() ||
                         std::is_same<T, GfVec3f>() ||
                         std::is_same<T, GfVec4f>())
      {
        if (!cgltf_accessor_read_float(accessor, i, item.data(), elementSize))
        {
          TF_RUNTIME_ERROR("unable to read accessor data");
          return false;
        }
      }
      else
      {
        TF_CODING_ERROR("unhandled accessor component type");
        return false;
      }
    }
    return true;
  }

  template<typename T>
  bool readVtArrayFromAccessor(const cgltf_accessor* accessor, VtArray<T>& array)
  {
    if (accessor->is_sparse)
    {
      array.resize(accessor->count);

      cgltf_size numFloats = cgltf_accessor_unpack_floats(accessor, nullptr, 0);

      std::vector<float> floats;
      floats.resize(numFloats);
      if (cgltf_accessor_unpack_floats(accessor, floats.data(), numFloats) < numFloats)
      {
        TF_RUNTIME_ERROR("unable to unpack sparse accessor");
        return false;
      }

      for (size_t i = 0; i < accessor->count; i++)
      {
        if constexpr (std::is_same<T, GfVec2f>())
        {
          array[i] = GfVec2f(floats[i * 2 + 0], floats[i * 2 + 1]);
        }
        else if constexpr (std::is_same<T, GfVec3f>())
        {
          array[i] = GfVec3f(floats[i * 3 + 0], floats[i * 3 + 1], floats[i * 3 + 2]);
        }
        else if constexpr (std::is_same<T, GfVec4f>())
        {
          array[i] = GfVec4f(floats[i * 4 + 0], floats[i * 4 + 1], floats[i * 4 + 2], floats[i * 4 + 3]);
        }
        else if constexpr (std::is_same<T, int>())
        {
          array[i] = floats[i];
        }
        else
        {
          TF_CODING_ERROR("unhandled accessor component type");
          return false;
        }
      }
    }
    else if (accessor->buffer_view)
    {
      if (!readVtArrayFromNonSparseAccessor(accessor, array))
      {
        return false;
      }
    }
    else
    {
      TF_DEBUG(GUC).Msg("empty accessor; defined by unsupported extension?\n");
      return false;
    }
    return true;
  }

  template<typename T>
  void deindexVtArray(VtIntArray indices, VtArray<T>& arr)
  {
    if (arr.empty())
    {
      return;
    }

    int newVertexCount = indices.size();

    VtArray<T> newArr;
    newArr.resize(newVertexCount);

    for (int i = 0; i < newVertexCount; i++)
    {
      newArr[i] = arr[indices[i]];
    }

    arr = newArr;
  }

  bool findMtlxGltfPbrFilePath(fs::path& path)
  {
    const NdrStringVec& searchPaths = UsdMtlxSearchPaths();

    for (const auto& searchDir : searchPaths)
    {
      for (const auto& fileEntry : fs::recursive_directory_iterator(searchDir))
      {
        const auto& filePath = fileEntry.path();
        const auto& fileName = filePath.filename();

        if (fileName != MTLX_GLTF_PBR_FILE_NAME)
        {
          continue;
        }

        TF_DEBUG(GUC).Msg("found MaterialX glTF PBR at %s\n", filePath.string().c_str());

        path = fs::absolute(filePath);
        return true;
      }
    }

    return false;
  }

  void markAttributeAsGenerated(UsdAttribute attr)
  {
    VtDictionary customData;
    customData[_tokens->generated] = true;
    attr.SetCustomDataByKey(_tokens->guc, VtValue(customData));
  }
}

namespace guc
{
  Converter::Converter(const cgltf_data* data, UsdStageRefPtr stage, const Params& params)
    : m_data(data)
    , m_stage(stage)
    , m_params(params)
    , m_mtlxDoc(mx::createDocument())
    , m_mtlxConverter(m_mtlxDoc, m_imgMetadata,
        params.gltfPbrImpl == GltfPbrImpl::Flattened,
        params.explicitColorspaceTransforms, params.hdStormCompat)
    , m_usdPreviewSurfaceConverter(m_stage, m_imgMetadata)
  {
  }

  void Converter::convert(FileExports& fileExports)
  {
    // Step 1: set up stage & root prim
    auto rootXForm = UsdGeomXform::Define(m_stage, getEntryPath(EntryPathType::Root));
    UsdModelAPI(rootXForm).SetKind(KindTokens->component);

    auto defaultPrim = rootXForm.GetPrim();
    m_stage->SetDefaultPrim(defaultPrim);
    m_stage->SetMetadata(SdfFieldKeys->Documentation, TfStringPrintf("Converted from glTF with guc %s", GUC_VERSION_STRING));

    UsdGeomSetStageUpAxis(m_stage, UsdGeomTokens->y);
    UsdGeomSetStageMetersPerUnit(m_stage, 1.0);

    // FIXME: use SetAssetInfoByKey for some of these values
    const cgltf_asset& asset = m_data->asset;
    if (asset.copyright) {
      defaultPrim.SetCustomDataByKey(_tokens->copyright, VtValue(std::string(asset.copyright)));
    }
    if (asset.generator) {
      defaultPrim.SetCustomDataByKey(_tokens->generator, VtValue(std::string(asset.generator)));
    }
    if (asset.version) {
      defaultPrim.SetCustomDataByKey(_tokens->version, VtValue(std::string(asset.version)));
    }
    if (asset.min_version) {
      defaultPrim.SetCustomDataByKey(_tokens->min_version, VtValue(std::string(asset.min_version)));
    }

    if (m_data->variants_count > 0)
    {
      UsdVariantSets variantSets = defaultPrim.GetVariantSets();
      UsdVariantSet set = variantSets.AddVariantSet(getMaterialVariantSetName());

      for (size_t i = 0; i < m_data->variants_count; i++)
      {
        const cgltf_material_variant* variant = &m_data->variants[i];
        TF_VERIFY(set.AddVariant(normalizeVariantName(variant->name)));
      }
    }

    // Step 2: process images
    processImages(m_data->images, m_data->images_count, m_params.srcDir,
      m_params.dstDir, m_params.copyExistingFiles, m_params.genRelativePaths, m_imgMetadata);

    fileExports.reserve(m_imgMetadata.size());
    for (const auto& imgMetadataPair : m_imgMetadata)
    {
      const ImageMetadata& metadata = imgMetadataPair.second;
      fileExports.push_back({ metadata.filePath, metadata.refPath });
    }

    // Step 3: create materials
    bool hasMaterials = (m_data->materials_count > 0);
    bool createDefaultMaterial = false;

    for (size_t i = 0; i < m_data->meshes_count && !createDefaultMaterial; i++)
    {
      const cgltf_mesh* gmesh = &m_data->meshes[i];

      for (size_t j = 0; j < gmesh->primitives_count && !createDefaultMaterial; j++)
      {
        const cgltf_primitive* gprim = &gmesh->primitives[j];

        createDefaultMaterial |= !gprim->material;
      }
    }

    if (hasMaterials || createDefaultMaterial)
    {
      UsdGeomScope::Define(m_stage, getEntryPath(EntryPathType::Materials));

      createMaterials(fileExports, createDefaultMaterial);
    }

    // Step 4: create scene graph (nodes, meshes, lights, cameras, ...)
    auto createNode = [this](const cgltf_node* nodeData, SdfPath path)
    {
      std::string baseName(nodeData->name ? nodeData->name : "node");
      SdfPath nodePath = makeUniqueStageSubpath(m_stage, path, baseName);

      createNodesRecursively(nodeData, nodePath);
    };

    for (size_t i = 0; i < m_data->scenes_count; i++)
    {
      const SdfPath& scenesPath = getEntryPath(EntryPathType::Scenes);
      if (i == 0)
      {
        UsdGeomXform::Define(m_stage, scenesPath);
      }

      const cgltf_scene* sceneData = &m_data->scenes[i];
      std::string name(sceneData->name ? sceneData->name : "scene");
      SdfPath scenePath = makeUniqueStageSubpath(m_stage, scenesPath, name);

      auto xform = UsdGeomXform::Define(m_stage, scenePath);
      if (m_data->scenes_count > 1)
      {
        UsdModelAPI(xform).SetKind(KindTokens->subcomponent);

        if (m_data->scene != sceneData)
        {
          xform.MakeInvisible();
        }
      }

      for (size_t i = 0; i < sceneData->nodes_count; i++)
      {
        const cgltf_node* nodeData = sceneData->nodes[i];

        createNode(nodeData, scenePath);
      }
    }

    // Assign default material variant
    if (m_data->variants_count > 0)
    {
      int variantIndex = m_params.defaultMaterialVariant;

      if (variantIndex < 0 || variantIndex >= int(m_data->variants_count))
      {
        TF_RUNTIME_ERROR("default material variant index %d out of range [0, %d); using 0",
          variantIndex, int(m_data->variants_count));

        variantIndex = 0;
      }

      UsdVariantSets variantSets = defaultPrim.GetVariantSets();
      UsdVariantSet set = variantSets.GetVariantSet(getMaterialVariantSetName());

      std::string defaultVariantName = normalizeVariantName(m_data->variants[variantIndex].name);
      TF_VERIFY(set.SetVariantSelection(defaultVariantName));
    }

    // According to glTF spec sec. 3.5.1., "glTF assets that do not contain any
    // [scenes] should be treated as a library of individual entities [...]". In
    // this case, we put all nodes under an invisible "Nodes" root prim.
    if (m_data->scenes_count == 0)
    {
      const SdfPath& nodesPath = getEntryPath(EntryPathType::Nodes);

      auto scope = UsdGeomScope::Define(m_stage, nodesPath);
      scope.MakeInvisible();

      for (size_t i = 0; i < m_data->nodes_count; i++)
      {
        const cgltf_node* nodeData = &m_data->nodes[i];

        createNode(nodeData, nodesPath);
      }
    }
  }

  void Converter::createMaterials(FileExports& fileExports, bool createDefaultMaterial)
  {
    // We import the MaterialX bxdf/pbrlib/stdlib documents mainly for validation, but
    // because UsdMtlx tries to output them, we only do so when not exporting UsdShade.
    if (m_params.emitMtlx && !m_params.mtlxAsUsdShade)
    {
      mx::FilePathVec libFolders = { "libraries" };
      mx::FileSearchPath searchPath;

      // Starting from MaterialX 1.38.4 at PR 877, we must remove the "libraries" part:
      for (const std::string& stdLibPath : UsdMtlxStandardLibraryPaths())
      {
        mx::FilePath newPath(stdLibPath);
        if (newPath.getBaseName() == "libraries") {
          newPath = newPath.getParentPath();
        }
        searchPath.append(newPath);

        auto newPathString = newPath.asString();
        TF_DEBUG(GUC).Msg("adding UsdMtlx search path %s\n", newPathString.c_str());
      }

      try
      {
        mx::loadLibraries(libFolders, searchPath, m_mtlxDoc);
      }
      catch (const mx::Exception& ex)
      {
        TF_RUNTIME_ERROR("failed to load MaterialX libraries: %s", ex.what());
      }
    }

    std::unordered_set<std::string> materialNameSet;

    // Create a default material if needed (glTF spec. 3.7.2.1)
    if (createDefaultMaterial)
    {
      TF_DEBUG(GUC).Msg("creating default material\n");

      SdfPath previewPath = makeUsdPreviewSurfaceMaterialPath(DEFAULT_MATERIAL_NAME);
      m_usdPreviewSurfaceConverter.convert(&DEFAULT_MATERIAL, previewPath);

      if (m_params.emitMtlx)
      {
        m_mtlxConverter.convert(&DEFAULT_MATERIAL, DEFAULT_MATERIAL_NAME);
      }
      materialNameSet.insert(DEFAULT_MATERIAL_NAME);
    }

    m_materialNames.resize(m_data->materials_count);

    // Create UsdPreviewSurface prims and MaterialX document nodes for glTF materials
    for (size_t i = 0; i < m_data->materials_count; i++)
    {
      const cgltf_material* gmat = &m_data->materials[i];

      std::string& materialName = m_materialNames[i];
      {
        materialName = gmat->name ? std::string(gmat->name) : "";
        materialName = makeUniqueMaterialName(materialName, materialNameSet);
        materialNameSet.insert(materialName);
      }

      SdfPath previewPath = makeUsdPreviewSurfaceMaterialPath(m_materialNames[i]);
      m_usdPreviewSurfaceConverter.convert(gmat, previewPath);

      if (m_params.emitMtlx)
      {
        m_mtlxConverter.convert(gmat, materialName);
      }
    }

    if (!m_params.emitMtlx)
    {
      return;
    }

    // Export MaterialX glTF PBR file if wanted
    if (m_params.gltfPbrImpl == GltfPbrImpl::File)
    {
      fs::path implFilePath;
      if (!detail::findMtlxGltfPbrFilePath(implFilePath))
      {
        TF_RUNTIME_ERROR("can't find %s - portable node impl not possible", MTLX_GLTF_PBR_FILE_NAME);
      }
      else if (!m_params.copyExistingFiles)
      {
        const std::string& refPath = m_params.genRelativePaths ? MTLX_GLTF_PBR_FILE_NAME : implFilePath.string();
        fileExports.push_back({ implFilePath.string(), refPath });
      }
      else
      {
        fs::path dstFilePath = m_params.dstDir / MTLX_GLTF_PBR_FILE_NAME;

        TF_DEBUG(GUC).Msg("copying glTF PBR mtlx file from %s to %s\n",
          implFilePath.string().c_str(), dstFilePath.string().c_str());

        if (!fs::copy_file(implFilePath, dstFilePath, fs::copy_options::overwrite_existing))
        {
          TF_RUNTIME_ERROR("can't copy %s to destination path - portable node impl not possible", MTLX_GLTF_PBR_FILE_NAME);
        }
        else
        {
          mx::prependXInclude(m_mtlxDoc, mx::FilePath(MTLX_GLTF_PBR_FILE_NAME));
          fileExports.push_back({ dstFilePath.string(), MTLX_GLTF_PBR_FILE_NAME });
        }
      }
    }

    std::string validationErrMsg;
    if (!m_mtlxDoc->validate(&validationErrMsg))
    {
      TF_CODING_ERROR("invalid MaterialX document: %s", validationErrMsg.c_str());
    }

    // Let UsdMtlx convert the document to UsdShade
    if (m_params.mtlxAsUsdShade)
    {
      UsdMtlxRead(m_mtlxDoc, m_stage, getEntryPath(EntryPathType::MaterialXMaterials));
    }
    else
    {
      // Otherwise, write the document as XML to a separate file
      mx::XmlWriteOptions writeOptions;
      writeOptions.elementPredicate = [](mx::ConstElementPtr elem) {
        // Prevent imported libraries (pbrlib etc.) from being emitted as XML includes
        return !elem->hasSourceUri() || elem->getSourceUri() == MTLX_GLTF_PBR_FILE_NAME;
      };

      auto mtlxFileName = m_params.mtlxFileName;
      auto mtlxFilePath = m_params.dstDir / mtlxFileName;
      TF_DEBUG(GUC).Msg("writing mtlx file %s\n", mtlxFilePath.string().c_str());
      mx::writeToXmlFile(m_mtlxDoc, mx::FilePath(mtlxFilePath.string()), &writeOptions);

      // And create a reference to it
      auto over = m_stage->OverridePrim(getEntryPath(EntryPathType::MaterialXMaterials));
      auto references = over.GetReferences();
      TF_VERIFY(references.AddReference(mtlxFileName.string(), SdfPath("/MaterialX")));

      fileExports.push_back({ mtlxFilePath.string(), mtlxFileName.string() });
    }
  }

  void Converter::createNodesRecursively(const cgltf_node* nodeData, SdfPath path)
  {
    auto xform = UsdGeomXform::Define(m_stage, path);

    if (nodeData->has_matrix)
    {
      const float* m = nodeData->matrix;

      auto transform = GfMatrix4d(
        m[ 0], m[ 1], m[ 2], m[ 3],
        m[ 4], m[ 5], m[ 6], m[ 7],
        m[ 8], m[ 9], m[10], m[11],
        m[12], m[13], m[14], m[15]
      );

      auto op = xform.AddTransformOp(UsdGeomXformOp::PrecisionDouble);
      op.Set(transform);
    }
    else
    {
      if (nodeData->has_translation)
      {
        auto op = xform.AddTranslateOp(UsdGeomXformOp::PrecisionFloat);
        op.Set(GfVec3f(nodeData->translation));
      }
      if (nodeData->has_rotation)
      {
        // Express rotation using axis-angle
        auto op = xform.AddOrientOp(UsdGeomXformOp::PrecisionFloat);
        GfQuatf rot(nodeData->rotation[3], GfVec3f(nodeData->rotation[0], nodeData->rotation[1], nodeData->rotation[2]));
        op.Set(rot);
      }
      if (nodeData->has_scale)
      {
        auto op = xform.AddScaleOp(UsdGeomXformOp::PrecisionFloat);
        op.Set(GfVec3f(nodeData->scale));
      }
    }

    if (nodeData->mesh)
    {
      std::string meshName = nodeData->mesh->name ? std::string(nodeData->mesh->name) : "mesh";
      auto meshPath = makeUniqueStageSubpath(m_stage, path, meshName);

      createOrOverMesh(nodeData->mesh, meshPath);
    }

    if (nodeData->camera)
    {
      std::string camName = nodeData->camera->name ? std::string(nodeData->camera->name) : "cam";
      auto camPath = makeUniqueStageSubpath(m_stage, path, camName);

      createOrOverCamera(nodeData->camera, camPath);
    }

    if (nodeData->light)
    {
      std::string lightName = nodeData->light->name ? std::string(nodeData->light->name) : "light";
      auto lightPath = makeUniqueStageSubpath(m_stage, path, lightName);

      createOrOverLight(nodeData->light, lightPath);
    }

    for (size_t i = 0; i < nodeData->children_count; i++)
    {
      const cgltf_node* childNodeData = nodeData->children[i];

      std::string childName(childNodeData->name ? childNodeData->name : "node");
      SdfPath childNodePath = makeUniqueStageSubpath(m_stage, path, childName);

      createNodesRecursively(childNodeData, childNodePath);
    }
  }

  void Converter::createOrOverCamera(const cgltf_camera* cameraData, SdfPath path)
  {
    UsdPrim prim;
    if (overridePrimInPathMap((void*) cameraData, path, prim))
    {
      return;
    }

    GfCamera gfCamera;
    if (cameraData->type == cgltf_camera_type_perspective)
    {
      gfCamera.SetProjection(GfCamera::Projection::Perspective);
      gfCamera.SetPerspectiveFromAspectRatioAndFieldOfView(
        cameraData->data.perspective.aspect_ratio,
        GfRadiansToDegrees(cameraData->data.perspective.yfov),
        GfCamera::FOVDirection::FOVVertical
      );
      if (cameraData->data.perspective.has_zfar)
      {
        gfCamera.SetClippingRange(GfRange1f(cameraData->data.perspective.znear, cameraData->data.perspective.zfar));
      }
    }
    else if (cameraData->type == cgltf_camera_type_orthographic)
    {
      gfCamera.SetProjection(GfCamera::Projection::Orthographic);
      float aspect_ratio = cameraData->data.orthographic.xmag / cameraData->data.orthographic.ymag;
      gfCamera.SetOrthographicFromAspectRatioAndSize(
        aspect_ratio,
        cameraData->data.orthographic.ymag * 2.0f, // ymag is half the orthographic height
        GfCamera::FOVDirection::FOVVertical
      );
      gfCamera.SetClippingRange(GfRange1f(cameraData->data.orthographic.znear, cameraData->data.orthographic.zfar));
    }
    else
    {
      TF_VERIFY(cameraData->type == cgltf_camera_type_invalid);
      TF_RUNTIME_ERROR("invalid camera type; skipping");
      return;
    }

    auto camera = UsdGeomCamera::Define(m_stage, path);
    camera.SetFromCamera(gfCamera, UsdTimeCode::Default());

    // SetFromCamera adds a transform xformop which we need to remove
    prim = camera.GetPrim();
    prim.RemoveProperty(TfToken("xformOp:transform"));
    prim.RemoveProperty(TfToken("xformOpOrder"));

    m_uniquePaths[(void*) cameraData] = path;
  }

  void Converter::createOrOverLight(const cgltf_light* lightData, SdfPath path)
  {
    UsdPrim prim;
    if (overridePrimInPathMap((void*) lightData, path, prim))
    {
      return;
    }

    if (lightData->type == cgltf_light_type_directional)
    {
      // We rotate the light via an Xform instead of setting the angle
      auto light = UsdLuxDistantLight::Define(m_stage, path);
      light.CreateIntensityAttr(VtValue(lightData->intensity));
      light.CreateColorAttr(VtValue(GfVec3f(lightData->color)));
    }
    else if (lightData->type == cgltf_light_type_point ||
             lightData->type == cgltf_light_type_spot)
    {
      auto light = UsdLuxSphereLight::Define(m_stage, path);
      light.CreateIntensityAttr(VtValue(lightData->intensity));
      light.CreateColorAttr(VtValue(GfVec3f(lightData->color)));
      // Point lights are not natively supported, we can only hint at them:
      // https://graphics.pixar.com/usd/dev/api/usd_lux_page_front.html#usdLux_Geometry
      light.CreateTreatAsPointAttr(VtValue(true));

      if (lightData->range > 0.0f)
      {
        light.CreateRadiusAttr(VtValue(lightData->range));
      }

      if (lightData->type == cgltf_light_type_spot)
      {
        prim = light.GetPrim();
        auto shapingApi = UsdLuxShapingAPI::Apply(prim);
        // FIXME: translate spot_inner_cone_angle and spot_outer_cone_angle to either ConeFocusAttr or ConeSoftnessAttr
        shapingApi.CreateShapingConeAngleAttr(VtValue(lightData->spot_outer_cone_angle));
      }
    }
    else
    {
      TF_VERIFY(lightData->type == cgltf_light_type_invalid);
      TF_RUNTIME_ERROR("invalid light type; skipping");
      return;
    }

    // Set extent information
    if (auto boundable = UsdGeomBoundable(prim))
    {
        VtArray<GfVec3f> extent;
        UsdGeomBoundable::ComputeExtentFromPlugins(boundable, UsdTimeCode::Default(), &extent);
        boundable.CreateExtentAttr(VtValue(extent));
    }

    m_uniquePaths[(void*) lightData] = path;
  }

  void Converter::createOrOverMesh(const cgltf_mesh* meshData, SdfPath path)
  {
    auto xform = UsdGeomXform::Define(m_stage, path);

    for (size_t i = 0; i < meshData->primitives_count; i++)
    {
      const cgltf_primitive* primitiveData = &meshData->primitives[i];

      std::string submeshName = (meshData->primitives_count == 1) ? "submesh" : ("submesh_" + std::to_string(i));
      auto submeshPath = makeUniqueStageSubpath(m_stage, path, submeshName);

      UsdPrim submesh;
      if (!overridePrimInPathMap((void*) primitiveData, submeshPath, submesh))
      {
        if (!createPrimitive(primitiveData, submeshPath, submesh))
        {
          TF_RUNTIME_ERROR("unable to create primitive; skipping");
          continue;
        }

        m_uniquePaths[(void*) primitiveData] = submeshPath;
      }

      // Assign material (explicit, fallback, variants)
      std::string materialName = DEFAULT_MATERIAL_NAME;

      const auto getMaterialName = [&](const cgltf_material* material) {
        int materialIndex = cgltf_material_index(m_data, material);
        TF_VERIFY(materialIndex >= 0);
        return m_materialNames[materialIndex].c_str();
      };

      if (primitiveData->material)
      {
        materialName = getMaterialName(primitiveData->material);
      }

      if (primitiveData->mappings_count > 0)
      {
        UsdPrim defaultPrim = m_stage->GetDefaultPrim();
        UsdVariantSets variantSets = defaultPrim.GetVariantSets();
        UsdVariantSet set = variantSets.GetVariantSet(getMaterialVariantSetName());

        for (size_t j = 0; j < primitiveData->mappings_count; j++)
        {
          const cgltf_material_mapping* mapping = &primitiveData->mappings[j];
          std::string variantName = normalizeVariantName(m_data->variants[mapping->variant].name);

          TF_VERIFY(set.SetVariantSelection(variantName));

          UsdEditContext editContext(set.GetVariantEditContext());

          materialName = getMaterialName(mapping->material);
          createMaterialBinding(submesh, materialName);
        }

        TF_VERIFY(set.ClearVariantSelection());
      }
      else
      {
        createMaterialBinding(submesh, materialName);
      }
    }
  }

  void Converter::createMaterialBinding(UsdPrim& prim, const std::string& materialName)
  {
#ifndef NDEBUG
    if (!TfGetEnvSetting(GUC_DISABLE_PREVIEW_MATERIAL_BINDINGS))
#endif
    {
      UsdShadeMaterialBindingAPI::Apply(prim).Bind(
        UsdShadeMaterial::Get(m_stage, makeUsdPreviewSurfaceMaterialPath(materialName)),
        UsdShadeTokens->fallbackStrength,
        UsdShadeTokens->preview
      );
    }

    if (m_params.emitMtlx)
    {
      UsdShadeMaterialBindingAPI::Apply(prim).Bind(
        UsdShadeMaterial::Get(m_stage, makeMtlxMaterialPath(materialName)),
        UsdShadeTokens->fallbackStrength,
        UsdShadeTokens->allPurpose
      );
    }
  }

  bool Converter::createPrimitive(const cgltf_primitive* primitiveData, SdfPath path, UsdPrim& prim)
  {
    const cgltf_material* material = primitiveData->material;

    // "If material is undefined, then a default material MUST be used." (glTF spec. 3.7.2.1)
    if (!material)
    {
      material = &DEFAULT_MATERIAL;
    }

    // Indices
    VtIntArray indices;
    {
      const cgltf_accessor* accessor = primitiveData->indices;
      if (accessor)
      {
        if (!detail::readVtArrayFromAccessor(accessor, indices))
        {
          TF_RUNTIME_ERROR("unable to read primitive indices");
          return false;
        }
      }
    }

    // Points
    VtVec3fArray points;
    VtIntArray faceVertexCounts;
    {
      const cgltf_accessor* accessor = cgltf_find_accessor(primitiveData, "POSITION");

      if (!detail::readVtArrayFromAccessor(accessor, points) || accessor->count == 0)
      {
        TF_RUNTIME_ERROR("invalid POSITION accessor");
        return false;
      }

      if (indices.empty())
      {
        for (size_t i = 0; i < accessor->count; i++)
        {
          indices.push_back(i);
        }
      }

      VtIntArray newIndices;
      if (!createGeometryRepresentation(primitiveData, indices, newIndices, faceVertexCounts))
      {
        TF_RUNTIME_ERROR("unable to create geometric representation");
        return false;
      }
      indices = newIndices;
    }

    // Colors
    std::vector<VtVec3fArray> colorSets;
    std::vector<VtFloatArray> opacitySets;
    while (true)
    {
      // The glTF PBR shading model which we implement using MaterialX requires us to
      // multiply the material's base color with the individual vertex colors.
      //
      // We don't use the standardized 'displayColor' primvar for this purpose because of
      // two reasons:
      //  1) we can generate the display color from the material's base color or from
      //     the base color image, and multiplying with this generated value in our
      //     shading network would be incorrect.
      //  2) there is only one 'displayColor' - it's not supposed to be indexed.
      //
      // I've therefore settled on using a separate primvar.
      //
      // There's also the question of having a single color4 primvar (combined color and
      // opacity) vs. having separate ones. I've decided for the latter in order to be
      // consistent with the displayColor and displayOpacity primvars.

      TF_VERIFY(colorSets.size() == opacitySets.size());
      std::string name = "COLOR_" + std::to_string(colorSets.size());

      const cgltf_accessor* accessor = cgltf_find_accessor(primitiveData, name.c_str());
      if (!accessor)
      {
        break;
      }

      VtVec3fArray colors;
      VtFloatArray opacities;

      if (accessor->type == cgltf_type_vec3)
      {
        if (!detail::readVtArrayFromAccessor(accessor, colors))
        {
          TF_RUNTIME_ERROR("can't read %s attribute; ignoring", name.c_str());
          continue;
        }
      }
      else if (accessor->type == cgltf_type_vec4)
      {
        VtVec4fArray rgbaColors;
        if (!detail::readVtArrayFromAccessor(accessor, rgbaColors))
        {
          TF_RUNTIME_ERROR("can't read %s attribute; ignoring", name.c_str());
          continue;
        }

        size_t rgbaColorCount = rgbaColors.size();

        colors.resize(rgbaColorCount);
        for (size_t k = 0; k < rgbaColorCount; k++)
        {
          colors[k] = GfVec3f(rgbaColors[k].data());
        }

        // Optimization: if material is opaque, we don't read the opacities anyway
        if (material->alpha_mode != cgltf_alpha_mode_opaque)
        {
          opacities.resize(rgbaColorCount);
          for (size_t k = 0; k < rgbaColorCount; k++)
          {
            opacities[k] = rgbaColors[k][3];
          }
        }
      }
      else
      {
        TF_RUNTIME_ERROR("invalid COLOR attribute type; ignoring");
        continue;
      }

      colorSets.push_back(colors);
      opacitySets.push_back(opacities);
    }

    // Display colors and opacities
    VtVec3fArray displayColors;
    VtFloatArray displayOpacities;
    bool generatedDisplayColors = false;

    if (!colorSets.empty())
    {
      displayColors = colorSets[0];

      // The alpha mode 'overrides' vertex opacity, for instance for the default material.
      if (material->alpha_mode != cgltf_alpha_mode_opaque)
      {
        displayOpacities = opacitySets[0];
      }
    }

    if (material->has_pbr_metallic_roughness)
    {
      if (displayColors.empty())
      {
        displayColors = { GfVec3f(1.0f) };
        generatedDisplayColors = true;
      }
      if (displayOpacities.empty() && material->alpha_mode != cgltf_alpha_mode_opaque)
      {
        displayOpacities = { 1.0f };
      }

      const cgltf_pbr_metallic_roughness* pbr_metallic_roughness = &material->pbr_metallic_roughness;

      for (GfVec3f& c : displayColors)
      {
        c = GfCompMult(c, GfVec3f(pbr_metallic_roughness->base_color_factor));
      }
      for (float& o : displayOpacities)
      {
        o *= pbr_metallic_roughness->base_color_factor[3];
      }
    }

    // TexCoord sets
    std::vector<VtVec2fArray> texCoordSets;
    while (true)
    {
      std::string name = "TEXCOORD_" + std::to_string(texCoordSets.size());

      const cgltf_accessor* accessor = cgltf_find_accessor(primitiveData, name.c_str());
      if (!accessor)
      {
        break;
      }

      VtVec2fArray texCoords;
      if (!detail::readVtArrayFromAccessor(accessor, texCoords))
      {
        continue;
      }

      // Y values need to be flipped
      for (GfVec2f& texCoord : texCoords)
      {
        texCoord[1] = 1.0f - texCoord[1];
      }

      texCoordSets.push_back(texCoords);
    }

    // Normals and Tangents
    VtVec3fArray normals;

    const auto deindexPrimvarsExceptTangents = [&]()
    {
      detail::deindexVtArray(indices, points);
      detail::deindexVtArray(indices, normals);

      for (VtVec2fArray& texCoords : texCoordSets)
      {
        detail::deindexVtArray(indices, texCoords);
      }
      for (VtVec3fArray& colors : colorSets)
      {
        detail::deindexVtArray(indices, colors);
      }
      for (VtFloatArray& opacities : opacitySets)
      {
        detail::deindexVtArray(indices, opacities);
      }
      if (!generatedDisplayColors) // constant interpolation
      {
        detail::deindexVtArray(indices, displayColors);
      }
      if (!generatedDisplayColors) // constant interpolation
      {
        detail::deindexVtArray(indices, displayOpacities);
      }

      for (size_t i = 0; i < indices.size(); i++)
      {
        indices[i] = i;
      }
    };

    bool hasTriangleTopology = primitiveData->type == cgltf_primitive_type_triangles ||
                               primitiveData->type == cgltf_primitive_type_triangle_strip ||
                               primitiveData->type == cgltf_primitive_type_triangle_fan;

    bool generatedNormals = false;
    {
      const cgltf_accessor* accessor = cgltf_find_accessor(primitiveData, "NORMAL");

      if (!accessor || !detail::readVtArrayFromAccessor(accessor, normals))
      {
        if (hasTriangleTopology) // generate fallback normals (spec sec. 3.7.2.1)
        {
          TF_DEBUG(GUC).Msg("normals do not exist; calculating flat normals\n");

          // For flat normals, vertex normals can not be shared among triangles.
          deindexPrimvarsExceptTangents();

          createFlatNormals(indices, points, normals);

          generatedNormals = true;
        }
      }
    }

    VtVec3fArray tangents;
    VtFloatArray bitangentSigns;
    bool generatedTangents = false;
    {
      const cgltf_accessor* accessor = cgltf_find_accessor(primitiveData, "TANGENT");
      if (!generatedNormals && accessor) // according to glTF spec 3.7.2.1, tangents must be ignored if normals are missing
      {
        VtVec4fArray tangentsWithW;
        if (detail::readVtArrayFromAccessor(accessor, tangentsWithW))
        {
          tangents.resize(tangentsWithW.size());
          bitangentSigns.resize(tangentsWithW.size());

          for (size_t i = 0; i < tangentsWithW.size(); i++)
          {
            tangents[i] = GfVec3f(tangentsWithW[i].data());
            bitangentSigns[i] = tangentsWithW[i][3];
          }
        }
      }
      else if (hasTriangleTopology)
      {
        const cgltf_texture_view& textureView = material->normal_texture;

        if (isValidTexture(textureView))
        {
          int texCoordSetCount = int(texCoordSets.size());

          if (textureView.texcoord < texCoordSetCount)
          {
            TF_DEBUG(GUC).Msg("generating tangents\n");

            const VtVec2fArray& texCoords = texCoordSets[textureView.texcoord];
            createTangents(indices, points, normals, texCoords, bitangentSigns, tangents);

            // The generated tangents are unindexed, which means that we
            // have to deindex all other primvars and reindex the mesh.
            if (!generatedNormals)
            {
              deindexPrimvarsExceptTangents();
            }

            generatedTangents = true;
          }
          else
          {
            TF_RUNTIME_ERROR("invalid UV index for normalmap; can't calculate tangents");
          }
        }
      }
    }

    // Create GPrim and assign values
    auto mesh = UsdGeomMesh::Define(m_stage, path);
    auto primvarsApi = UsdGeomPrimvarsAPI(mesh);

    mesh.CreateSubdivisionSchemeAttr(VtValue(UsdGeomTokens->none));

    if (material->double_sided)
    {
      mesh.CreateDoubleSidedAttr(VtValue(true));
    }

    if (!indices.empty())
    {
      auto attr = mesh.CreateFaceVertexIndicesAttr(VtValue(indices));

      // If we generated normals or tangents, we have re-indexed the mesh. This means
      // that we have de-indexed all other primvars; but unlike the indices, their data
      // still exists and is just encoded in a different way. This is why we only add
      // the "generated" custom data to the indices.
      if (generatedNormals || generatedTangents)
      {
        detail::markAttributeAsGenerated(attr);
      }
    }
    mesh.CreatePointsAttr(VtValue(points));
    mesh.CreateFaceVertexCountsAttr(VtValue(faceVertexCounts));

    if (!normals.empty())
    {
      auto attr = mesh.CreateNormalsAttr(VtValue(normals));
      mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);

      if (generatedNormals)
      {
        detail::markAttributeAsGenerated(attr);
      }
    }

    VtVec3fArray extent;
    if (UsdGeomPointBased::ComputeExtent(points, &extent))
    {
      mesh.CreateExtentAttr(VtValue(extent));
    }
    else
    {
      TF_WARN("unable to compute extent for mesh");
    }

    // There is no formal schema for tangents and tangent signs/bitangents, so we define our own primvars
    if (!tangents.empty())
    {
      auto primvar = primvarsApi.CreatePrimvar(UsdGeomTokens->tangents, SdfValueTypeNames->Float3Array, UsdGeomTokens->vertex);
      primvar.Set(tangents);

      if (generatedTangents)
      {
        detail::markAttributeAsGenerated(primvar);
      }
    }
    if (!bitangentSigns.empty())
    {
      auto primvar = primvarsApi.CreatePrimvar(_tokens->bitangentSigns, SdfValueTypeNames->FloatArray, UsdGeomTokens->vertex);
      primvar.Set(bitangentSigns);

      if (generatedTangents)
      {
        detail::markAttributeAsGenerated(primvar);
      }
    }

    for (size_t i = 0; i < texCoordSets.size(); i++)
    {
      const VtVec2fArray& texCoords = texCoordSets[i];
      if (texCoords.empty())
      {
        continue;
      }
      auto primvarId = TfToken(makeStSetName(i));
      auto primvar = primvarsApi.CreatePrimvar(primvarId, SdfValueTypeNames->TexCoord2fArray, UsdGeomTokens->vertex);
      primvar.Set(texCoords);
    }

    for (size_t i = 0; i < colorSets.size(); i++)
    {
      const VtVec3fArray& colors = colorSets[i];
      if (colors.empty())
      {
        continue;
      }
      auto colorPrimvarId = TfToken(makeColorSetName(i));
      auto colorPrimvar = primvarsApi.CreatePrimvar(colorPrimvarId, SdfValueTypeNames->Float3Array, UsdGeomTokens->vertex);
      colorPrimvar.Set(colors);

      // We do an emptyness check here instead of in the retrieval routine above
      // in order to keep the color-opacity primvar index correspondence, e.g.:
      //  color1, opacity1
      //  color2, (missing)
      //  color3, opacity3
      const VtFloatArray& opacities = opacitySets[i];
      if (opacities.empty())
      {
        continue;
      }
      auto opacityPrimvarId = TfToken(makeOpacitySetName(i));
      auto opacityPrimvar = primvarsApi.CreatePrimvar(opacityPrimvarId, SdfValueTypeNames->FloatArray, UsdGeomTokens->vertex);
      opacityPrimvar.Set(opacities);
    }

    TfToken displayPrimvarInterpolation = generatedDisplayColors ? UsdGeomTokens->constant : UsdGeomTokens->vertex;
    if (!displayColors.empty())
    {
      auto primvar = mesh.CreateDisplayColorPrimvar(displayPrimvarInterpolation);
      primvar.Set(displayColors);

      if (generatedDisplayColors)
      {
        detail::markAttributeAsGenerated(primvar);
      }
    }
    if (!displayOpacities.empty())
    {
      auto primvar = mesh.CreateDisplayOpacityPrimvar(displayPrimvarInterpolation);
      primvar.Set(displayOpacities);

      if (generatedDisplayColors)
      {
        detail::markAttributeAsGenerated(primvar);
      }
    }

    prim = mesh.GetPrim();
    return true;
  }

  bool Converter::overridePrimInPathMap(void* dataPtr, const SdfPath& path, UsdPrim& prim)
  {
    auto iter = m_uniquePaths.find((void*) dataPtr);
    if (iter == m_uniquePaths.end())
    {
      return false;
    }

    prim = m_stage->OverridePrim(path);
    auto references = prim.GetReferences();
    references.AddReference("", iter->second);
    return true;
  }

  bool Converter::isValidTexture(const cgltf_texture_view& textureView)
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

    auto iter = m_imgMetadata.find(image);
    if (iter == m_imgMetadata.end())
    {
      return false;
    }

    return true;
  }
}
