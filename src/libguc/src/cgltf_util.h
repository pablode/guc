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

#include <cgltf.h>

namespace guc
{
  constexpr static const char* GLTF_KHR_GAUSSIAN_SPLATTING_EXTENSION_NAME = "KHR_gaussian_splatting";

  bool load_gltf(const char* gltfPath, cgltf_data** data);

  void free_gltf(cgltf_data* data);

  const char* cgltf_error_string(cgltf_result result);

  const cgltf_accessor* cgltf_find_accessor(const cgltf_primitive* primitive,
                                            const char* name);

  bool cgltf_transform_required(const cgltf_texture_transform& transform);
}
