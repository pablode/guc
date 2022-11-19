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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <guc.h>

void print_usage()
{
  fprintf(stderr, "guc %s - glTF to USD converter\n", GUC_VERSION_STRING);
  fprintf(stderr, "\n");
  fprintf(stderr, "Usage: guc <gltf_path> <usd_path> [options]\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "--emit-mtlx                             Emit MaterialX materials in addition to UsdPreviewSurfaces\n");
  fprintf(stderr, "--mtlx-as-usdshade                      Convert and inline MaterialX materials with UsdMtlx\n");
  fprintf(stderr, "--explicit-colorspace-transforms        Explicitly transform colorspaces using MaterialX nodes\n");
  fprintf(stderr, "--gltf-pbr-impl runtime|file|flattened  How the MaterialX glTF PBR is provided. Default: runtime\n");
  fprintf(stderr, "--hdstorm-compat                        Apply compatibility tweaks for the USD hdStorm renderer\n");
}

int main(int argc, const char* argv[])
{
  if (argc < 3)
  {
    print_usage();
    return EXIT_FAILURE;
  }

  const char* gltf_path = argv[1];
  const char* usd_path = argv[2];

  struct guc_params params;
  params.emit_mtlx = false;
  params.mtlx_as_usdshade = false;
  params.explicit_colorspace_transforms = false;
  params.gltf_pbr_impl = GUC_GLTF_PBR_IMPL_RUNTIME;
  params.hdstorm_compat = false;

  for (int i = 3; i < argc; i++)
  {
    const char* arg = argv[i];

    if (strlen(arg) > 2)
    {
      arg += 2;

      if (!strcmp(arg, "emit-mtlx"))
      {
        params.emit_mtlx = true;
        continue;
      }
      else if (!strcmp(arg, "mtlx-as-usdshade"))
      {
        params.mtlx_as_usdshade = true;
        continue;
      }
      else if (!strcmp(arg, "explicit-colorspace-transforms"))
      {
        params.explicit_colorspace_transforms = true;
        continue;
      }
      else if (!strcmp(arg, "gltf-pbr-impl") && ++i < argc)
      {
        const char* val = argv[i];
        if (!strcmp(val, "runtime"))
        {
          // default value
          continue;
        }
        else if (!strcmp(val, "file"))
        {
          params.gltf_pbr_impl = GUC_GLTF_PBR_IMPL_FILE;
          continue;
        }
        else if (!strcmp(val, "flattened"))
        {
          params.gltf_pbr_impl = GUC_GLTF_PBR_IMPL_FLATTENED;
          continue;
        }
      }
      else if (!strcmp(arg, "hdstorm-compat"))
      {
        params.hdstorm_compat = true;
        continue;
      }
    }

    print_usage();
    return EXIT_FAILURE;
  }

  bool result = guc_convert(gltf_path, usd_path, &params);

  return result ? EXIT_SUCCESS : EXIT_FAILURE;
}
