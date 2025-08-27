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

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

struct guc_options
{
  // Generate and reference a MaterialX document containing an accurate translation
  // of the glTF materials. The document is serialized to a file if UsdShade inlining
  // is not active.
  bool emit_mtlx;

  // Parse the generated MaterialX document with UsdMtlx to a UsdShade representation
  // and inline it into the USD file. Note that information will be discarded as not
  // all MaterialX concepts can be encoded in UsdShade:
  // https://graphics.pixar.com/usd/release/api/usd_mtlx_page_front.html
  // Files generated without this option may be better supported by future USD
  // versions.
  bool mtlx_as_usdshade;

  // If the asset supports the KHR_materials_variants extension, select the material
  // variant at the given index by default.
  int default_material_variant;

  // By default, guc will write geometry and materials to separate layers, referenced by
  // a payload file. Setting this option to true merges all prims into a single layer.
  bool single_file;
};

bool guc_convert(const char* gltf_path,
                 const char* usd_path,
                 const struct guc_options* options);

#ifdef __cplusplus
}
#endif
