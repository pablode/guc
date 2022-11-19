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
    std::string exportedFileName;
    int channelCount;
    // USD makes an incorrect assumption that we have to work around by undoing an incorrect sRGB-to-linear transform in our MaterialX network gen:
    // https://github.com/PixarAnimationStudios/USD/blob/857ffda41f4f1553fe1019ac7c7b4f08c233a7bb/pxr/imaging/plugin/hioOiio/oiioImage.cpp#L470-L471
    // The stb_image sRGB detection logic is slightly different, but since guc requires OIIO, we don't have to care about it.
    // Our UsdPreviewSurface generator is fine too, since there we set explicit sourceColorSpace inputs.
    bool isSrgbInUSD;
  };

  using ImageMetadataMap = std::unordered_map<const cgltf_image*, ImageMetadata>;

  void exportImages(const cgltf_image* images,
                    size_t imageCount,
                    const fs::path& srcDir,
                    const fs::path& dstDir,
                    bool copyImageFiles,
                    ImageMetadataMap& metadata);
}
