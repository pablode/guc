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

#include <pxr/base/gf/matrix4f.h>
#include <pxr/usd/usd/stage.h>
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
  (bitangents)
);

namespace detail
{
  using namespace guc;

  template<typename T>
  bool readVtArrayFromNonSparseAccessor(const cgltf_accessor* accessor, VtArray<T>& array)
  {
    cgltf_size elementSize = cgltf_calc_size(accessor->type, accessor->component_type);

    array.resize(accessor->count);

    for (int i = 0; i < accessor->count; i++)
    {
      T& item = array[i];

      if constexpr (std::is_same<T, int>())
      {
        unsigned int tmpUint = 0;
        if (!cgltf_accessor_read_uint(accessor, i, &tmpUint, elementSize))
        {
          TF_RUNTIME_ERROR("unable to read accessor data\n");
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
          TF_RUNTIME_ERROR("unable to read accessor data\n");
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

      for (int i = 0; i < accessor->count; i++)
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
    int newVertexCount = indices.size();

    VtArray<T> newArr;
    newArr.resize(newVertexCount);

    for (int i = 0; i < newVertexCount; i++)
    {
      newArr[i] = arr[indices[i]];
    }

    arr = newArr;
  }
}

namespace guc
{
  Converter::Converter(const cgltf_data* data,
                       UsdStageRefPtr stage,
                       const fs::path& srcDir,
                       const fs::path& dstDir,
                       const fs::path& mtlxFileName,
                       bool copyImageFiles,
                       const guc_params& params)
    : m_data(data)
    , m_stage(stage)
    , m_srcDir(srcDir)
    , m_dstDir(dstDir)
    , m_mtlxFileName(mtlxFileName)
    , m_copyImageFiles(copyImageFiles)
    , m_params(params)
    , m_mtlxDoc(mx::createDocument())
    , m_mtlxConverter(m_mtlxDoc, m_imgMetadata, params.flatten_nodes, params.explicit_colorspace_transforms, params.hdstorm_compat)
    , m_usdPreviewSurfaceConverter(m_stage, m_imgMetadata)
  {
  }

  bool Converter::convert()
  {
    auto rootXForm = UsdGeomXform::Define(m_stage, SdfPath("/Geom"));

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

    // Step 1: export images
    exportImages(m_data->images, m_data->images_count, m_srcDir, m_dstDir, m_copyImageFiles, m_imgMetadata);

    // Step 2: create materials
    if (m_data->materials_count > 0)
    {
      UsdGeomScope::Define(m_stage, SdfPath("/Materials"));

      if (!createMaterials())
      {
        return false;
      }
    }

    // Step 3: create scene graph (nodes, meshes, lights, cameras, ...)
    for (int i = 0; i < m_data->scenes_count; i++)
    {
      const cgltf_scene* sceneData = &m_data->scenes[i];

      SdfPath scenePath("/Geom");
      if (m_data->scenes_count > 1)
      {
        std::string name(sceneData->name ? sceneData->name : "scene");
        scenePath = makeUniqueStageSubpath(m_stage, "/Geom", name);
      }

      auto prim = m_stage->DefinePrim(scenePath);
      for (int i = 0; i < sceneData->nodes_count; i++)
      {
        const cgltf_node* nodeData = sceneData->nodes[i];

        std::string baseName(nodeData->name ? nodeData->name : "node");
        SdfPath nodePath = makeUniqueStageSubpath(m_stage, scenePath.GetAsString(), baseName);

        createNodesRecursively(nodeData, nodePath);
      }
    }

    return true;
  }

  bool Converter::createMaterials()
  {
    bool exportMtlxDoc = m_params.emit_mtlx && m_data->materials_count > 0;

    if (exportMtlxDoc && !m_params.mtlx_as_usdshade)
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

      mx::loadLibraries(libFolders, searchPath, m_mtlxDoc);
    }

    std::unordered_set<std::string> materialNameSet;
    m_materialNames.resize(m_data->materials_count);

    for (int i = 0; i < m_data->materials_count; i++)
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

      if (m_params.emit_mtlx)
      {
        m_mtlxConverter.convert(gmat, materialName);
      }
    }

    if (exportMtlxDoc)
    {
      std::string validationErrMsg;
      if (!m_mtlxDoc->validate(&validationErrMsg))
      {
        TF_CODING_ERROR("MaterialX document is invalid: %s", validationErrMsg.c_str());
      }

      // Let UsdMtlx convert the document to UsdShade
      if (m_params.mtlx_as_usdshade)
      {
        UsdMtlxRead(m_mtlxDoc, m_stage, SdfPath("/Materials/MaterialX"));
      }
      else
      {
        // Otherwise, write the document as XML to a separate file
        mx::XmlWriteOptions writeOptions;
        writeOptions.elementPredicate = [](mx::ConstElementPtr elem) {
          return !elem->hasSourceUri();
        };
        auto mtlxFilePath = m_dstDir / m_mtlxFileName;
        TF_DEBUG(GUC).Msg("writing mtlx file %s\n", mtlxFilePath.string().c_str());
        mx::writeToXmlFile(m_mtlxDoc, mx::FilePath(mtlxFilePath.string()), &writeOptions);

        // And create a reference to it
        auto over = m_stage->OverridePrim(SdfPath("/Materials/MaterialX"));
        auto references = over.GetPrim().GetReferences();
        TF_VERIFY(references.AddReference(m_mtlxFileName.string(), SdfPath("/MaterialX")));
      }
    }

    return true;
  }

  bool Converter::createNodesRecursively(const cgltf_node* nodeData, SdfPath path)
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
      auto meshPath = makeUniqueStageSubpath(m_stage, path.GetAsString(), meshName);

      if (!createOrOverMesh(nodeData->mesh, meshPath))
      {
        return false;
      }
    }

    if (nodeData->camera)
    {
      std::string camName = nodeData->camera->name ? std::string(nodeData->camera->name) : "cam";
      auto camPath = makeUniqueStageSubpath(m_stage, path.GetAsString(), camName);

      if (!createOrOverCamera(nodeData->camera, camPath))
      {
        return false;
      }
    }

    if (nodeData->light)
    {
      std::string lightName = nodeData->light->name ? std::string(nodeData->light->name) : "light";
      auto lightPath = makeUniqueStageSubpath(m_stage, path.GetAsString(), lightName);

      if (!createOrOverLight(nodeData->light, lightPath))
      {
        return false;
      }
    }

    for (int i = 0; i < nodeData->children_count; i++)
    {
      const cgltf_node* childNodeData = nodeData->children[i];

      std::string childName(childNodeData->name ? childNodeData->name : "node");
      SdfPath childNodePath = makeUniqueStageSubpath(m_stage, path.GetAsString(), childName);

      createNodesRecursively(childNodeData, childNodePath);
    }

    return true;
  }

  bool Converter::createOrOverCamera(const cgltf_camera* cameraData, SdfPath path)
  {
    UsdPrim prim;
    if (overridePrimInPathMap((void*) cameraData, path, prim))
    {
      return true;
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
      TF_RUNTIME_ERROR("invalid camera type; skipping");
      return false;
    }

    auto camera = UsdGeomCamera::Define(m_stage, path);
    camera.SetFromCamera(gfCamera, UsdTimeCode::Default());

    // SetFromCamera adds a transform xformop which we need to remove
    prim = camera.GetPrim();
    prim.RemoveProperty(TfToken("xformOp:transform"));
    prim.RemoveProperty(TfToken("xformOpOrder"));

    m_uniquePaths[(void*) cameraData] = path;
    return true;
  }

  bool Converter::createOrOverLight(const cgltf_light* lightData, SdfPath path)
  {
    UsdPrim prim;
    if (overridePrimInPathMap((void*) lightData, path, prim))
    {
      return true;
    }

    if (lightData->type == cgltf_light_type_directional)
    {
      // We rotate the light via an Xform instead of setting the angle
      auto light = UsdLuxDistantLight::Define(m_stage, path);
      light.CreateIntensityAttr(VtValue(lightData->intensity));
      light.CreateColorAttr(VtValue(GfVec3f(lightData->color)));
    }
    else
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
        auto shapingApi = UsdLuxShapingAPI(prim);
        // FIXME: translate spot_inner_cone_angle and spot_outer_cone_angle to either ConeFocusAttr or ConeSoftnessAttr
        shapingApi.CreateShapingConeAngleAttr(VtValue(lightData->spot_outer_cone_angle));
      }
    }

    m_uniquePaths[(void*) lightData] = path;
    return true;
  }

  bool Converter::createOrOverMesh(const cgltf_mesh* meshData, SdfPath path)
  {
    auto xform = UsdGeomXform::Define(m_stage, path);

    for (int i = 0; i < meshData->primitives_count; i++)
    {
      const cgltf_primitive* primitiveData = &meshData->primitives[i];

      std::string submeshName = (meshData->primitives_count == 1) ? "submesh" : ("submesh_" + std::to_string(i));
      auto submeshPath = makeUniqueStageSubpath(m_stage, path.GetAsString(), submeshName);

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

      if (!primitiveData->material)
      {
        continue;
      }

      int materialIndex = (primitiveData->material - &m_data->materials[0]);
      TF_VERIFY(materialIndex >= 0);

      const std::string& materialName = m_materialNames[materialIndex];

      UsdShadeMaterialBindingAPI::Apply(submesh);

      UsdShadeMaterialBindingAPI(submesh).Bind(
        UsdShadeMaterial::Get(m_stage, makeUsdPreviewSurfaceMaterialPath(materialName)),
        UsdShadeTokens->fallbackStrength,
        UsdShadeTokens->preview
      );

      if (m_params.emit_mtlx)
      {
        UsdShadeMaterialBindingAPI(submesh).Bind(
          UsdShadeMaterial::Get(m_stage, makeMtlxMaterialPath(materialName)),
          UsdShadeTokens->fallbackStrength,
          UsdShadeTokens->allPurpose
        );
      }
    }

    return true;
  }

  bool Converter::createPrimitive(const cgltf_primitive* primitiveData, SdfPath path, UsdPrim& prim)
  {
    const cgltf_material* material = primitiveData->material;

    // Indices
    VtIntArray indices;
    {
      const cgltf_accessor* accessor = primitiveData->indices;
      if (accessor)
      {
        detail::readVtArrayFromAccessor(accessor, indices);
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
        for (int i = 0; i < accessor->count; i++)
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

    // Normals
    bool createNormals = true;
    VtVec3fArray normals;
    {
      const cgltf_accessor* accessor = cgltf_find_accessor(primitiveData, "NORMAL");
      if (accessor)
      {
        createNormals = !detail::readVtArrayFromAccessor(accessor, normals);
      }
    }
    if (createNormals) // spec sec. 3.7.2.1
    {
      TF_DEBUG(GUC).Msg("normals do not exist; calculating flat normals\n");

      createFlatNormals(indices, points, normals);
    }

    // Colors
    VtFloatArray displayOpacities;
    VtVec3fArray displayColors;
    {
      // In the MaterialX material shading network, we multiply by the vertex color.
      // This is implemented by using the displayColor primvar. Hence, we need to
      // ensure that it exists - by filling it with fallback data, if necessary.
      bool createOpacities = m_params.emit_mtlx;
      bool createColors = m_params.emit_mtlx;

      const cgltf_accessor* accessor = cgltf_find_accessor(primitiveData, "COLOR_0");
      if (accessor)
      {
        if (accessor->type == cgltf_type_vec3)
        {
          if (detail::readVtArrayFromAccessor(accessor, displayColors))
          {
            createColors = false;
          }
        }
        else if (accessor->type == cgltf_type_vec4)
        {
          VtVec4fArray rgbaDisplayColors;
          if (detail::readVtArrayFromAccessor(accessor, rgbaDisplayColors))
          {
            displayColors.resize(rgbaDisplayColors.size());
            displayOpacities.resize(rgbaDisplayColors.size());
            for (int k = 0; k < rgbaDisplayColors.size(); k++)
            {
              displayColors[k] = GfVec3f(rgbaDisplayColors[k].data());
              displayOpacities[k] = rgbaDisplayColors[k][3];
            }
            createColors = false;
            createOpacities = false;
          }
        }
      }

      if (createColors)
      {
        displayColors.resize(points.size());
        for (GfVec3f& rgb : displayColors)
        {
          rgb = GfVec3f(1.0f, 1.0f, 1.0f);
        }
      }
      if (createOpacities)
      {
        displayOpacities.resize(points.size());
        for (float& a : displayOpacities)
        {
          a = 1.0f;
        }
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

    // Tangents and Bitangents
    VtVec3fArray tangents;
    VtVec3fArray bitangents;
    if (!createNormals) // according to glTF spec 3.7.2.1, tangents must be ignored if normals are missing
    {
      VtFloatArray signs;

      const cgltf_accessor* accessor = cgltf_find_accessor(primitiveData, "TANGENT");
      if (accessor)
      {
        VtVec4fArray tangentsWithW;
        detail::readVtArrayFromAccessor(accessor, tangentsWithW);

        tangents.resize(tangentsWithW.size());
        signs.resize(tangentsWithW.size());

        for (int i = 0; i < tangentsWithW.size(); i++)
        {
          tangents[i] = GfVec3f(tangentsWithW[i].data());
          signs[i] = tangentsWithW[i][3];
        }
      }
      else if (material)
      {
        const cgltf_texture_view& textureView = material->normal_texture;

        if (isValidTexture(textureView))
        {
          if (textureView.texcoord < texCoordSets.size())
          {
            TF_DEBUG(GUC).Msg("generating tangents\n");

            const VtVec2fArray& texCoords = texCoordSets[textureView.texcoord];
            createTangents(indices, points, normals, texCoords, signs, tangents);

            // The generated tangents are unindexed, which means that we
            // have to deindex all other primvars and reindex the mesh.
            detail::deindexVtArray(indices, points);
            detail::deindexVtArray(indices, normals);
            for (VtVec2fArray& texCoords : texCoordSets)
            {
              detail::deindexVtArray(indices, texCoords);
            }
            if (!displayColors.empty())
            {
              detail::deindexVtArray(indices, displayColors);
            }
            if (!displayOpacities.empty())
            {
              detail::deindexVtArray(indices, displayOpacities);
            }

            for (int i = 0; i < indices.size(); i++)
            {
              indices[i] = i;
            }
          }
          else
          {
            TF_RUNTIME_ERROR("invalid UV index for normalmap; can't calculate normals");
          }
        }
      }

      // We bake the w component (sign) into the bitangents for correct handedness while having vec3 tangents
      if (!tangents.empty())
      {
        createBitangents(normals, tangents, signs, bitangents);
      }
    }

    // Create GPrim and assign values
    auto mesh = UsdGeomMesh::Define(m_stage, path);

    mesh.CreateSubdivisionSchemeAttr(VtValue(UsdGeomTokens->none));

    if (material && material->double_sided)
    {
      mesh.CreateDoubleSidedAttr(VtValue(true));
    }

    if (!indices.empty())
    {
      mesh.CreateFaceVertexIndicesAttr(VtValue(indices));
    }
    mesh.CreatePointsAttr(VtValue(points));
    mesh.CreateFaceVertexCountsAttr(VtValue(faceVertexCounts));

    if (!normals.empty())
    {
      mesh.CreateNormalsAttr(VtValue(normals));
      mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);
    }

    VtVec3fArray extent;
    if (UsdGeomPointBased::ComputeExtent(points, &extent))
    {
      mesh.CreateExtentAttr(VtValue(extent));
    }
    else
    {
      TF_WARN("Unable to compute extent for mesh");
    }

    // There is no formal schema for tangents and bitangents, so we just define primvars
    if (!tangents.empty())
    {
      auto primvarsApi = UsdGeomPrimvarsAPI(mesh);
      auto primvar = primvarsApi.CreatePrimvar(UsdGeomTokens->tangents, SdfValueTypeNames->Float3Array, UsdGeomTokens->vertex);
      primvar.Set(tangents);
    }
    if (!bitangents.empty())
    {
      auto primvarsApi = UsdGeomPrimvarsAPI(mesh);
      auto primvar = primvarsApi.CreatePrimvar(_tokens->bitangents, SdfValueTypeNames->Float3Array, UsdGeomTokens->vertex);
      primvar.Set(bitangents);
    }

    for (int i = 0; i < texCoordSets.size(); i++)
    {
      auto primvarsApi = UsdGeomPrimvarsAPI(mesh);
      auto primvarId = TfToken(makeStSetName(i));
      auto primvar = primvarsApi.CreatePrimvar(primvarId, SdfValueTypeNames->TexCoord2fArray, UsdGeomTokens->vertex);
      primvar.Set(texCoordSets[i]);
    }

    if (!displayColors.empty())
    {
      auto primvar = mesh.CreateDisplayColorPrimvar(UsdGeomTokens->vertex);
      primvar.Set(displayColors);
    }
    if (!displayOpacities.empty())
    {
      auto primvar = mesh.CreateDisplayOpacityPrimvar(UsdGeomTokens->vertex);
      primvar.Set(displayOpacities);
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
