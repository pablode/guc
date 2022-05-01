#pragma once

#include <pxr/usd/usd/stage.h>

#include <unordered_set>

using namespace PXR_NS;

namespace guc
{
  std::string makeStSetName(int index);

  std::string makeUniqueMaterialName(std::string baseName,
                                     const std::unordered_set<std::string>& existingNames);

  std::string makeUniqueImageFileName(const char* nameHint,
                                      const std::string& fileName,
                                      const std::string& fileExt,
                                      const std::unordered_set<std::string>& existingNames);

  SdfPath makeUniqueStageSubpath(UsdStageRefPtr stage,
                                 const std::string& root,
                                 const std::string& baseName,
                                 const std::string& delimiter = "_");

  SdfPath makeMtlxMaterialPath(const std::string& materialName);

  SdfPath makeUsdPreviewSurfaceMaterialPath(const std::string& materialName);
}
