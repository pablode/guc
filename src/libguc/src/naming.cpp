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

#include "naming.h"

#include <pxr/usd/usd/object.h>
#include <pxr/usd/usdUtils/pipeline.h>
#include <MaterialXFormat/Util.h>

#include <filesystem>

namespace fs = std::filesystem;
namespace mx = MaterialX;

namespace guc
{
  static const SdfPath s_entryPaths[] = {
    SdfPath{ "/Asset" },
    SdfPath{ "/Asset/Scenes" },
    SdfPath{ "/Asset/Nodes" },
    SdfPath{ "/Asset/Materials" },
    SdfPath{ "/Asset/Materials/UsdPreviewSurface" },
    SdfPath{ "/Asset/Materials/MaterialX" },
    SdfPath{ "/Asset/Meshes" },
    SdfPath{ "/Asset/Cameras" },
    SdfPath{ "/Asset/Lights" }
  };
  static_assert(sizeof(s_entryPaths) / sizeof(s_entryPaths[0]) == size_t(EntryPathType::ENUM_SIZE));

  const SdfPath& getEntryPath(EntryPathType type)
  {
    return s_entryPaths[size_t(type)];
  }

  const char* getMaterialVariantSetName()
  {
    return "shadingVariant";
  }

  std::string normalizeVariantName(const std::string& name)
  {
    return TfMakeValidIdentifier(name);
  }

  std::string makeStSetName(int index)
  {
    std::string uvSetBaseName = UsdUtilsGetPrimaryUVSetName(); // likely to be "st"
    return uvSetBaseName + std::to_string(index);
  }

  std::string makeColorSetName(int index)
  {
    // The primvar name for colors is not standardized. I have chosen 'color' for it,
    // and give reasons against the other suggestions discussed in this forum thread:
    // https://groups.google.com/g/usd-interest/c/VOkh0aj-8bU/m/zxrMQ-pJAgAJ
    //
    // 'colorSet': Maya seems to use this primvar name, however if there's a colorSet,
    //             there should also be a texCoordSet / stSet.
    // 'vertexColor': includes the interpolation mode, of which USD has a few. We don't
    //                use "vertexTangents" etc., although we emit per-vertex tangents.
    //
    // Furthermore, 'color' maps directly to the COLOR_ glTF attribute name and goes well
    // with the already existing 'displayColor' primvar. It's just not for the 'display'
    // purpose, but rather part of the acutal data used for shading.
    std::string colorSetBaseName = "color";
    return colorSetBaseName + std::to_string(index);
  }

  std::string makeOpacitySetName(int index)
  {
    std::string opacitySetBaseName = "opacity";
    return opacitySetBaseName + std::to_string(index);
  }

  const static std::unordered_set<std::string> MTLX_TYPE_NAME_SET = {
    /* Basic data types */
    "integer", "boolean", "float", "color3", "color4", "vector2", "vector3",
    "vector4", "matrix33", "matrix44", "string", "filename", "geomname", "integerarray",
    "floatarray", "color3array", "color4array", "vector2array", "vector3array",
    "vector4array", "stringarray", "geomnamearray",
    /* Custom data types */
    "color", "shader", "material"
  };

  const char* DEFAULT_MATERIAL_NAME = "mat";

  std::string makeUniqueMaterialName(std::string baseName,
                                     const std::unordered_set<std::string>& existingNames)
  {
    baseName = mx::createValidName(baseName);
    baseName = TfMakeValidIdentifier(baseName);

    if (baseName.empty() || baseName[0] == '_') // HdStorm has problems with underscore prefixes
    {
      baseName = DEFAULT_MATERIAL_NAME;
    }

    std::string name = baseName;

    int i = 1;
    while (existingNames.find(name) != existingNames.end() ||
           MTLX_TYPE_NAME_SET.find(name) != MTLX_TYPE_NAME_SET.end())
    {
      name = baseName + "_" + std::to_string(i);
      i++;
    }

    return name;
  }

  const char* DEFAULT_IMAGE_FILENAME = "img";

  std::string makeUniqueImageFileName(const char* nameHint,
                                      const std::string& fileName,
                                      const std::string& fileExt,
                                      const std::unordered_set<std::string>& existingNames)
  {
    std::string baseName = fileName;

    if (baseName.empty() && nameHint)
    {
      baseName = TfMakeValidIdentifier(nameHint);
    }

    baseName = fs::path(baseName).replace_extension("").string(); // remove ext if already in img name

    if (baseName.empty())
    {
      baseName = DEFAULT_IMAGE_FILENAME;
    }

    auto finalName = baseName + fileExt;

    int i = 1;
    while (existingNames.find(finalName) != existingNames.end())
    {
      finalName = baseName + "_" + std::to_string(i) + fileExt;
      i++;
    }

    return finalName;
  }

  SdfPath makeUniqueStageSubpath(UsdStageRefPtr stage,
                                 const SdfPath& root,
                                 const std::string& baseName,
                                 const std::string& delimiter)
  {
    SdfPath finalPath = root.AppendElementString(TfMakeValidIdentifier(baseName));

    std::string basePath = finalPath.GetAsString();
    // FIXME: evaluate performance impact of GetObjectAtPath compared to simple hashmap
    for (int i = 1; stage->GetObjectAtPath(finalPath); i++)
    {
      auto newPathStr = basePath + delimiter + std::to_string(i);
      TF_VERIFY(SdfPath::IsValidPathString(newPathStr));
      finalPath = SdfPath(newPathStr);
    }

    return finalPath;
  }

  SdfPath makeMtlxMaterialPath(const std::string& materialName)
  {
    return getEntryPath(EntryPathType::MaterialXMaterials).AppendElementString("Materials").AppendElementString(materialName);
  }

  SdfPath makeUsdPreviewSurfaceMaterialPath(const std::string& materialName)
  {
    return getEntryPath(EntryPathType::PreviewMaterials).AppendElementString("Materials").AppendElementString(materialName);
  }
}
