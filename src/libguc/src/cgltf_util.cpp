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

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <assert.h>
#include <string.h>

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
           strcmp(name, "KHR_texture_transform") == 0;
  }
}

namespace guc
{
  bool load_gltf(const char* gltfPath, cgltf_data** data)
  {
    cgltf_result result;
    cgltf_options options = {};

    result = cgltf_parse_file(&options, gltfPath, data);
    if (result != cgltf_result_success)
    {
      TF_RUNTIME_ERROR("unable to parse glTF file: %s", cgltf_error_string(result));
      return false;
    }

    result = cgltf_load_buffers(&options, *data, gltfPath);
    if (result != cgltf_result_success)
    {
      cgltf_free(*data);
      TF_RUNTIME_ERROR("unable to load glTF buffers: %s", cgltf_error_string(result));
      return false;
    }

    result = cgltf_validate(*data);
    if (result != cgltf_result_success)
    {
      cgltf_free(*data);
      TF_RUNTIME_ERROR("unable to validate glTF: %s", cgltf_error_string(result));
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
