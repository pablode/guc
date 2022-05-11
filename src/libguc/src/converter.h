#pragma once

#include "guc.h"

#include <cgltf.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdShade/shader.h>
#include <MaterialXCore/Document.h>

#include <unordered_map>
#include <filesystem>
#include <string_view>

#include "materialx.h"
#include "usdpreviewsurface.h"

namespace fs = std::filesystem;
using namespace PXR_NS;

namespace guc
{
  class Converter
  {
  public:
    Converter(const cgltf_data* data,
              UsdStageRefPtr stage,
              const fs::path& srcDir,
              const fs::path& dstDir,
              const fs::path& mtlxFileName,
              const guc_params& params);

  public:
    bool convert();

  private:
    bool createMaterials();
    bool createNodesRecursively(const cgltf_node* nodeData, SdfPath path);
    bool createOrOverCamera(const cgltf_camera* cameraData, SdfPath path);
    bool createOrOverLight(const cgltf_light* lightData, SdfPath path);
    bool createOrOverMesh(const cgltf_mesh* meshData, SdfPath path);
    bool createPrimitive(const cgltf_primitive* primitiveData, SdfPath path, UsdPrim& prim);

  private:
    bool overridePrimInPathMap(void* dataPtr, const SdfPath& path, UsdPrim& prim);
    bool isValidTexture(const cgltf_texture_view& textureView);

  private:
    const cgltf_data* m_data;
    UsdStageRefPtr m_stage;
    const fs::path& m_srcDir;
    const fs::path& m_dstDir;
    const fs::path& m_mtlxFileName;
    const guc_params& m_params;

  private:
    ImageMetadataMap m_imgMetadata;
    MaterialX::DocumentPtr m_mtlxDoc;
    MaterialXMaterialConverter m_mtlxConverter;
    UsdPreviewSurfaceMaterialConverter m_usdPreviewSurfaceConverter;
    std::unordered_map<void*, SdfPath> m_uniquePaths;
    std::vector<std::string> m_materialNames;
  };
}
