#include "naming.h"

#include <pxr/usd/usd/object.h>
#include <pxr/usd/usdUtils/pipeline.h>
#include <MaterialXFormat/Util.h>

#include <filesystem>

namespace fs = std::filesystem;
namespace mx = MaterialX;

namespace guc
{
  std::string makeStSetName(int index)
  {
    std::string uvSetBaseName = UsdUtilsGetPrimaryUVSetName(); // likely to be "st"
    return uvSetBaseName + std::to_string(index);
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
                                 const std::string& root,
                                 const std::string& baseName,
                                 const std::string& delimiter)
  {
    std::string basePath = root + "/" + TfMakeValidIdentifier(baseName);

    auto finalPath = SdfPath(basePath);
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
    return SdfPath("/Materials/MaterialX/Materials/" + materialName);
  }

  SdfPath makeUsdPreviewSurfaceMaterialPath(const std::string& materialName)
  {
    return SdfPath("/Materials/UsdPreviewSurface/Materials/" + materialName);
  }
}
