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

#include <filesystem>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

namespace guc
{
  struct ImageMetadata
  {
    std::string filePath;
    std::string refPath;
    int channelCount; // Needed to determine the type of MaterialX <image> nodes
  };

  using ImageMetadataMap = std::unordered_map<const cgltf_image*, ImageMetadata>;

  void processImages(const cgltf_image* images,
                     size_t imageCount,
                     const fs::path& srcDir,
                     const fs::path& dstDir,
                     bool copyExistingFiles,
                     bool genRelativePaths,
                     ImageMetadataMap& metadata);
}
