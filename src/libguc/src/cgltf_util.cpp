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

#include "cgltf_util.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/gf/math.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolvedPath.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <assert.h>
#include <string.h>
#include <unordered_map>

#include "debugCodes.h"

using namespace PXR_NS;

namespace detail
{
  bool extensionSupported(const char* name)
  {
    return strcmp(name, "KHR_materials_pbrSpecularGlossiness") == 0 ||
           strcmp(name, "KHR_lights_punctual") == 0 ||
           strcmp(name, "KHR_materials_emissive_strength") == 0 ||
           strcmp(name, "KHR_materials_clearcoat") == 0 ||
           strcmp(name, "KHR_materials_ior") == 0 ||
           strcmp(name, "KHR_materials_iridescence") == 0 ||
           strcmp(name, "KHR_materials_sheen") == 0 ||
           strcmp(name, "KHR_materials_specular") == 0 ||
           strcmp(name, "KHR_materials_transmission") == 0 ||
           strcmp(name, "KHR_materials_unlit") == 0 ||
           strcmp(name, "KHR_materials_variants") == 0 ||
           strcmp(name, "KHR_materials_volume") == 0 ||
           strcmp(name, "KHR_mesh_quantization") == 0 ||
           strcmp(name, "KHR_texture_transform") == 0;
  }

  struct BufferHolder
  {
    std::unordered_map<const char*, std::shared_ptr<const char>> map;
  };

  cgltf_result readFile(const cgltf_memory_options* memory_options,
                        const cgltf_file_options* file_options,
                        const char* path,
                        cgltf_size* size,
                        void** data)
  {
    TF_DEBUG(GUC).Msg("reading file %s\n", path);

    ArResolver& resolver = ArGetResolver();
    std::string identifier = resolver.CreateIdentifier(path);
    TF_DEBUG(GUC).Msg("normalized path to %s\n", identifier.c_str());

    ArResolvedPath resolvedPath = resolver.Resolve(identifier);
    if (!resolvedPath)
    {
      TF_RUNTIME_ERROR("unable to resolve %s", path);
      return cgltf_result_file_not_found;
    }

    std::string resolvedPathStr = resolvedPath.GetPathString();
    TF_DEBUG(GUC).Msg("resolved path to %s\n", resolvedPathStr.c_str());

    std::shared_ptr<ArAsset> asset = resolver.OpenAsset(ArResolvedPath(path));
    if (!asset)
    {
      TF_RUNTIME_ERROR("unable to open asset %s", resolvedPathStr.c_str());
      return cgltf_result_file_not_found;
    }

    std::shared_ptr<const char> buffer = asset->GetBuffer();
    if (!buffer)
    {
      TF_RUNTIME_ERROR("unable to open buffer for %s", resolvedPathStr.c_str());
      return cgltf_result_io_error;
    }

    const char* bufferPtr = buffer.get();
    (*size) = asset->GetSize();
    (*data) = (void*) bufferPtr;

    auto bufferHolder = (BufferHolder*) file_options->user_data;
    bufferHolder->map[bufferPtr] = buffer;

    return cgltf_result_success;
  }

  void releaseFile(const cgltf_memory_options* memory_options,
                   const cgltf_file_options* file_options,
                   void* data)
  {
    auto bufferPtr = (const char*) data;
    auto bufferHolder = (BufferHolder*) file_options->user_data;
    bufferHolder->map.erase(bufferPtr);
  }
}

namespace guc
{
  bool load_gltf(const char* gltfPath, cgltf_data** data)
  {
    detail::BufferHolder* bufferHolder = new detail::BufferHolder;

    cgltf_file_options fileOptions = {};
    fileOptions.read = detail::readFile;
    fileOptions.release = detail::releaseFile;
    fileOptions.user_data = bufferHolder;

    cgltf_options options = {};
    options.file = fileOptions;

    cgltf_result result = cgltf_parse_file(&options, gltfPath, data);
    if (result != cgltf_result_success)
    {
      TF_RUNTIME_ERROR("unable to parse glTF file: %s", cgltf_error_string(result));
      delete bufferHolder;
      return false;
    }

    result = cgltf_load_buffers(&options, *data, gltfPath);
    if (result != cgltf_result_success)
    {
      TF_RUNTIME_ERROR("unable to load glTF buffers: %s", cgltf_error_string(result));
      free_gltf(*data);
      return false;
    }

    result = cgltf_validate(*data);
    if (result != cgltf_result_success)
    {
      TF_RUNTIME_ERROR("unable to validate glTF: %s", cgltf_error_string(result));
      free_gltf(*data);
      return false;
    }

    for (size_t i = 0; i < (*data)->extensions_required_count; i++)
    {
      const char* ext = (*data)->extensions_required[i];
      TF_DEBUG(GUC).Msg("extension required: %s\n", ext);

      if (detail::extensionSupported(ext))
      {
        continue;
      }

      TF_RUNTIME_ERROR("extension %s not supported", ext);
      free_gltf(*data);
      return false;
    }

    for (size_t i = 0; i < (*data)->extensions_used_count; i++)
    {
      const char* ext = (*data)->extensions_used[i];
      TF_DEBUG(GUC).Msg("extension used: %s\n", ext);

      if (detail::extensionSupported(ext))
      {
        continue;
      }

      TF_WARN("optional extension %s not suppported", ext);
    }

    return true;
  }

  void free_gltf(cgltf_data* data)
  {
    auto bufferHolder = (detail::BufferHolder*) data->file.user_data;
    cgltf_free(data); // releases buffers in buffer holder
    delete bufferHolder;
  }

  const char* cgltf_error_string(cgltf_result result)
  {
    assert(result != cgltf_result_success);
    assert(result != cgltf_result_invalid_options);
    switch (result)
    {
    case cgltf_result_legacy_gltf:
      return "legacy glTF not supported";
    case cgltf_result_data_too_short:
    case cgltf_result_invalid_json:
    case cgltf_result_invalid_gltf:
      return "malformed glTF";
    case cgltf_result_unknown_format:
      return "unknown format";
    case cgltf_result_file_not_found:
      return "file not found";
    case cgltf_result_io_error:
      return "io error";
    case cgltf_result_out_of_memory:
      return "out of memory";
    default:
      return "unknown";
    }
  }

  const cgltf_accessor* cgltf_find_accessor(const cgltf_primitive* primitive,
                                            const char* name)
  {
    for (size_t j = 0; j < primitive->attributes_count; j++)
    {
      const cgltf_attribute* attribute = &primitive->attributes[j];

      if (strcmp(attribute->name, name) == 0)
      {
        return attribute->data;
      }
    }

    return nullptr;
  }

  bool cgltf_transform_required(const cgltf_texture_transform& transform)
  {
    return !GfIsClose(transform.offset[0], 0.0f, 1e-5f) ||
           !GfIsClose(transform.offset[1], 0.0f, 1e-5f) ||
           !GfIsClose(transform.rotation, 0.0f, 1e-5f) ||
           !GfIsClose(transform.scale[0], 1.0f, 1e-5f) ||
           !GfIsClose(transform.scale[1], 1.0f, 1e-5f);
  }
}
