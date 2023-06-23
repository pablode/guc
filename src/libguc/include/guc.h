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

enum guc_gltf_pbr_impl
{
  GUC_GLTF_PBR_IMPL_RUNTIME,
  GUC_GLTF_PBR_IMPL_FILE,
  GUC_GLTF_PBR_IMPL_FLATTENED
};

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

  // MaterialX's 'colorspace' functionality may not be fully supported by an
  // application. We work around this by implementing colorspace transformations using
  // native MaterialX math nodes. MaterialX image nodes are assumed to return raw,
  // untransformed values, since the default document colorspace is 'linear'.
  bool explicit_colorspace_transforms;

  // Determines where the MaterialX glTF PBR implementation is assumed to live.
  // RUNTIME:   the node definition and implementation is provided by the target
  //            MaterialX runtime (default)
  // FILE:      a separate .mtlx file is exported that contains the glTF PBR
  // FLATTENED: the shading network is flattened to stdlib and pbrlib nodes. This
  //            option may negatively affect document parsing and compilation times.
  enum guc_gltf_pbr_impl gltf_pbr_impl;

  // HdMtlx and therefore Storm do not seem to properly support MaterialX colorspaces.
  // https://github.com/PixarAnimationStudios/USD/issues/1523
  // https://github.com/PixarAnimationStudios/USD/issues/1632
  // To work around this issue, we force-enable explicit colorspace transformations and
  // undo colorspace transformations that exist because of USD's sRGB detection logic:
  // https://github.com/PixarAnimationStudios/USD/blob/857ffda41f4f1553fe1019ac7c7b4f08c233a7bb/pxr/imaging/plugin/hioOiio/oiioImage.cpp#L470-L471
  // Additionally, we make hdStorm recognize alpha materials as translucent.
  bool hdstorm_compat;

  // If the asset supports the KHR_materials_variants extension, select the material
  // variant at the given index by default.
  int default_material_variant;
};

bool guc_convert(const char* gltf_path,
                 const char* usd_path,
                 const struct guc_options* options);

#ifdef __cplusplus
}
#endif
