#pragma once

#include <cgltf.h>

namespace guc
{
  const char* cgltf_error_string(cgltf_result result);

  const cgltf_accessor* cgltf_find_accessor(const cgltf_primitive* primitive,
                                            const char* name);

  cgltf_size cgltf_calc_size(cgltf_type type, cgltf_component_type component_type);

  cgltf_size cgltf_decode_uri(char* uri);
}
