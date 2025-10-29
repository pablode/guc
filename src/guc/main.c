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
#include <stdbool.h>

#include <cargs.h>

#include <guc.h>

#include "license.h"

static struct cag_option cmd_options[] = {
  {
    .identifier = 'm',
    .access_letters = "m",
    .access_name = "emit-mtlx",
    .value_name = NULL,
    .description = "Emit MaterialX materials in addition to UsdPreviewSurfaces"
  },
  {
    .identifier = 'u',
    .access_letters = "u",
    .access_name = "mtlx-as-usdshade",
    .value_name = NULL,
    .description = "Convert and inline MaterialX materials into the USD layer using UsdMtlx"
  },
  {
    .identifier = 'v',
    .access_letters = "v",
    .access_name = "default-material-variant",
    .value_name = "<index>",
    .description = "Index of the material variant that is selected by default"
  },
  {
    .identifier = 's',
    .access_letters = "s",
    .access_name = "skip-validation",
    .value_name = NULL,
    .description = "Skip glTF validation for reduced processing time"
  },
  {
    .identifier = 'l',
    .access_letters = "l",
    .access_name = "licenses",
    .value_name = NULL,
    .description = "Print the license of guc and third-party libraries"
  },
  {
    .identifier = 'h',
    .access_letters = "h",
    .access_name = "help",
    .value_name = NULL,
    .description = "Show the command help"
  },
};

int main(int argc, char* argv[])
{
  struct guc_options options = {
    .emit_mtlx = false,
    .mtlx_as_usdshade = false,
    .default_material_variant = 0,
    .skip_validation = false
  };

  cag_option_context context;
  cag_option_init(&context, cmd_options, CAG_ARRAY_SIZE(cmd_options), argc, argv);
  while (cag_option_fetch(&context))
  {
    switch (cag_option_get_identifier(&context))
    {
    case 'm':
      options.emit_mtlx = true;
      break;
    case 'u':
      options.mtlx_as_usdshade = true;
      break;
    case 'v': {
      const char* value = cag_option_get_value(&context);
      options.default_material_variant = atoi(value); // fall back to 0 on error
      break;
    }
    case 's':
      options.skip_validation = true;
      break;
    case 'l': {
      printf("%s\n", license_text);
      return EXIT_SUCCESS;
    }
    case 'h': {
      printf("guc %s - glTF to USD converter\n\n", GUC_VERSION_STRING);
      printf("Usage: guc [options] [--] <gltf_path> <usd_path>\n\n");
      printf("Options:\n");
      cag_option_print(cmd_options, CAG_ARRAY_SIZE(cmd_options), stdout);
      return EXIT_SUCCESS;
    }
    case '?': {
      cag_option_print_error(&context, stderr);
      return EXIT_FAILURE;
    }
    }
  }

  int param_index = cag_option_get_index(&context);

  if (param_index < argc && !strcmp(argv[param_index], "--"))
  {
    param_index++;
  }

  if (param_index >= argc)
  {
    fprintf(stderr, "Missing positional argument <gltf_path>.\n");
    return EXIT_FAILURE;
  }
  if ((param_index + 1) >= argc)
  {
    fprintf(stderr, "Missing positional argument <usd_path>.\n");
    return EXIT_FAILURE;
  }
  if ((argc - param_index) != 2)
  {
    fprintf(stderr, "Too many positional arguments.\n");
    return EXIT_FAILURE;
  }

  const char* gltf_path = argv[param_index];
  const char* usd_path = argv[param_index + 1];

  bool result = guc_convert(gltf_path, usd_path, &options);

  return result ? EXIT_SUCCESS : EXIT_FAILURE;
}
