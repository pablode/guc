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
           strcmp(name, "KHR_materials_clearcoat") == 0 ||
           strcmp(name, "KHR_materials_ior") == 0 ||
           strcmp(name, "KHR_materials_sheen") == 0 ||
           strcmp(name, "KHR_materials_specular") == 0 ||
           strcmp(name, "KHR_materials_transmission") == 0 ||
           strcmp(name, "KHR_materials_volume") == 0;
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

  // NOTE: the following three functions are copied from the cgltf library and
  // are therefore licensed under its accompanying MIT license.
  cgltf_size cgltf_calc_size(cgltf_type type, cgltf_component_type component_type)
  {
    cgltf_size component_size = cgltf_component_size(component_type);
    if (type == cgltf_type_mat2 && component_size == 1)
    {
      return 8 * component_size;
    }
    else if (type == cgltf_type_mat3 && (component_size == 1 || component_size == 2))
    {
      return 12 * component_size;
    }
    return component_size * cgltf_num_components(type);
  }

  int cgltf_unhex(char ch)
  {
    return
      (unsigned)(ch - '0') < 10 ? (ch - '0') :
      (unsigned)(ch - 'A') < 6 ? (ch - 'A') + 10 :
      (unsigned)(ch - 'a') < 6 ? (ch - 'a') + 10 :
      -1;
  }

  cgltf_size cgltf_decode_uri(char* uri)
  {
    char* write = uri;
    char* i = uri;

    while (*i)
    {
      if (*i == '%')
      {
        int ch1 = cgltf_unhex(i[1]);

        if (ch1 >= 0)
        {
          int ch2 = cgltf_unhex(i[2]);

          if (ch2 >= 0)
          {
            *write++ = (char)(ch1 * 16 + ch2);
            i += 3;
            continue;
          }
        }
      }

      *write++ = *i++;
    }

    *write = 0;
    return write - uri;
  }
}
