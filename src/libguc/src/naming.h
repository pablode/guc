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
