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

#include "guc_params.h"

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
              bool copyExistingFiles,
              bool genRelativePaths,
              const guc_params& params);

  public:
    struct FileExport
    {
      std::string filePath;
      std::string refPath;
    };
    using FileExports = std::vector<FileExport>;

    bool convert(FileExports& fileExports);

  private:
    bool createMaterials(FileExports& fileExports);
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
    const bool m_copyExistingFiles;
    const bool m_genRelativePaths;
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
