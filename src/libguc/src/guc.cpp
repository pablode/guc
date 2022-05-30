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

#include "guc.h"

#include <pxr/base/arch/fileSystem.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdUtils/dependencies.h>

#include <filesystem>

#include "debugCodes.h"
#include "cgltf_util.h"
#include "converter.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

using namespace guc;
namespace fs = std::filesystem;

bool loadGltf(const char* gltfPath, cgltf_data** data)
{
  cgltf_result result;
  cgltf_options options = {};

  result = cgltf_parse_file(&options, gltfPath, data);
  if (result != cgltf_result_success)
  {
    TF_RUNTIME_ERROR("unable to parse glTF: %s\n", cgltf_error_string(result));
    return false;
  }

  result = cgltf_load_buffers(&options, *data, gltfPath);
  if (result != cgltf_result_success)
  {
    cgltf_free(*data);
    TF_RUNTIME_ERROR("unable to load glTF buffers: %s\n", cgltf_error_string(result));
    return false;
  }

  result = cgltf_validate(*data);
  if (result != cgltf_result_success)
  {
    cgltf_free(*data);
    TF_RUNTIME_ERROR("unable to validate glTF: %s\n", cgltf_error_string(result));
    return false;
  }

  for (int i = 0; i < (*data)->extensions_required_count; i++)
  {
    const char* ext = (*data)->extensions_required[i];
    TF_DEBUG(GUC).Msg("extension required: %s\n", ext);

    if (strcmp(ext, "KHR_materials_pbrSpecularGlossiness") == 0 ||
        strcmp(ext, "KHR_lights_punctual") == 0 ||
        strcmp(ext, "KHR_materials_clearcoat") == 0 ||
        strcmp(ext, "KHR_materials_ior") == 0 ||
        strcmp(ext, "KHR_materials_sheen") == 0 ||
        strcmp(ext, "KHR_materials_specular") == 0 ||
        strcmp(ext, "KHR_materials_transmission") == 0 ||
        strcmp(ext, "KHR_materials_volume") == 0)
    {
      continue;
    }

    TF_RUNTIME_ERROR("extension %s not supported\n", ext);
    return false;
  }

  return true;
}

bool guc_convert(const char* gltf_path,
                 const char* usd_path,
                 const guc_params* params)
{
  TF_VERIFY(params);

  cgltf_data* data = nullptr;
  if (!loadGltf(gltf_path, &data))
  {
    TF_RUNTIME_ERROR("unable to load glTF at %s", gltf_path);
    return false;
  }

  UsdStageRefPtr stage = UsdStage::CreateNew(usd_path);
  if (!stage)
  {
    TF_RUNTIME_ERROR("unable to open stage at %s", usd_path);
    return false;
  }

  fs::path srcDir = fs::path(gltf_path).parent_path();
  fs::path usdFsPath(usd_path);
  fs::path dstDir = usdFsPath.parent_path();
  fs::path mtlxFileName = usdFsPath.filename();
  mtlxFileName.replace_extension(".mtlx");

  Converter converter(data, stage, srcDir, dstDir, mtlxFileName, *params);

  bool result = converter.convert();

  if (result)
  {
    stage->Save();
  }

  cgltf_free(data);

  return result;
}
