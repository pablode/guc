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

#include "image.h"

#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/tf/diagnostic.h>

#include <OpenImageIO/imageio.h>

#include <optional>
#include <unordered_set>

#include "cgltf_util.h"
#include "debugCodes.h"
#include "naming.h"

namespace fs = std::filesystem;

namespace guc
{
  bool readImageDataFromBufferView(const cgltf_buffer_view* bufferView, std::vector<uint8_t>& dstData)
  {
    const uint8_t* srcData = (uint8_t*) bufferView->buffer->data;

    if (bufferView->data)
    {
      TF_WARN("buffer view contains unsupported extension data");
    }

    if (!srcData)
    {
      TF_RUNTIME_ERROR("unable to read buffer view; data is NULL");
      return false;
    }

    srcData += bufferView->offset;
    dstData.resize(bufferView->size);
    std::memcpy(dstData.data(), srcData, dstData.size());
    return true;
  }

  bool readImageDataFromBase64(const char* base64Str, std::vector<uint8_t>& data)
  {
    size_t size = strlen(base64Str);
    size = size - (size / 4);
    if (size >= 2)
    {
      // remove padding bytes
      if (base64Str[size - 2] == '=') { size--; }
      if (base64Str[size - 1] == '=') { size--; }
    }

    if (size == 0)
    {
      TF_WARN("base64 string has no payload");
      return false;
    }

    void* rawData;
    cgltf_options options = {};
    cgltf_result result = cgltf_load_buffer_base64(&options, size, base64Str, &rawData);
    if (result != cgltf_result_success)
    {
      TF_RUNTIME_ERROR("unable to read base64-encoded data");
      return false;
    }

    data.resize(size);
    std::memcpy(data.data(), rawData, size);
    free(rawData);
    return true;
  }

  bool readImageFromFile(const char* filePath, std::vector<uint8_t>& data)
  {
    // Try to use memory mapping
    {
      std::string errMsg;
      auto mappedPtr = ArchMapFileReadOnly(filePath, &errMsg);
      if (mappedPtr)
      {
        size_t size = ArchGetFileMappingLength(mappedPtr);
        data.resize(size);
        std::memcpy(data.data(), mappedPtr.get(), size);
        return true;
      }
      TF_DEBUG(GUC).Msg("unable to mmap %s: %s\n", filePath, errMsg.c_str());
    }

    // Fall back to traditional file reading
    FILE* file = ArchOpenFile(filePath, "rb");
    if (!file)
    {
      TF_RUNTIME_ERROR("unable to open file for reading: %s", filePath);
      return false;
    }

    size_t size = ArchGetFileLength(file);
    data.resize(size);
    int64_t result = ArchPRead(file, data.data(), size, 0);

    ArchCloseFile(ArchFileNo(file));
    return result != -1;
  }

  bool writeImageData(const char* filePath, std::vector<uint8_t>& data)
  {
    FILE* file = ArchOpenFile(filePath, "wb");
    if (!file)
    {
      TF_RUNTIME_ERROR("unable to open file for writing: %s", filePath);
      return false;
    }

    int64_t result = ArchPWrite(file, data.data(), data.size(), 0);

    ArchCloseFile(ArchFileNo(file));
    return result != -1;
  }

  bool readExtensionFromDataSignature(const std::vector<uint8_t>& data, std::string& extension)
  {
    if (data.size() < 3)
    {
      return false;
    }
    else if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
    {
      extension = ".jpg";
      return true;
    }
    else if (data.size() < 8)
    {
      return false;
    }
    else if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E &&
             data[3] == 0x47 && data[4] == 0x0D && data[5] == 0x0A &&
             data[6] == 0x1A && data[7] == 0x0A)
    {
      extension = ".png";
      return true;
    }
    return false;
  }

  bool readImageMetadata(const char* path, int& channelCount, bool& isSrgbInUSD)
  {
    using namespace OIIO;

    auto image = ImageInput::open(path);
    if (!image)
    {
      TF_RUNTIME_ERROR("unable to open file for reading: %s", path);
      return false;
    }

    const ImageSpec& spec = image->spec();
    channelCount = spec.nchannels;
    // Detection logic of HioOIIO_Image::IsColorSpaceSRGB for _sourceColorSpace auto (default value)
    // https://github.com/PixarAnimationStudios/USD/blob/857ffda41f4f1553fe1019ac7c7b4f08c233a7bb/pxr/imaging/plugin/hioOiio/oiioImage.cpp
    isSrgbInUSD = (channelCount == 3 || channelCount == 4) && spec.format == TypeDesc::UINT8;
    image->close();
    return true;
  }

  std::optional<ImageMetadata> exportImage(const cgltf_image* image,
                                           const fs::path& srcDir,
                                           const fs::path& dstDir,
                                           bool copyExistingFiles,
                                           std::unordered_set<std::string>& exportedFileNames)
  {
    std::vector<uint8_t> data;
    std::string srcFilePath;

    const char* uri = image->uri;
    if (uri && strncmp(uri, "data:", 5) == 0)
    {
      const char* comma = strchr(uri, ',');
      if (comma && (comma - uri < 7 || strncmp(comma - 7, ";base64", 7) != 0))
      {
        return std::nullopt;
      }

      if (!readImageDataFromBase64(comma + 1, data))
      {
        return std::nullopt;
      }
    }
    else if (uri && strstr(uri, "://") == NULL)
    {
      srcFilePath = std::string(uri);
      cgltf_decode_uri(srcFilePath.data());
      srcFilePath = (srcDir / srcFilePath).string();

      if (!readImageFromFile(srcFilePath.c_str(), data))
      {
        return std::nullopt;
      }
    }
    else if (image->buffer_view)
    {
      if (!readImageDataFromBufferView(image->buffer_view, data))
      {
        return std::nullopt;
      }
    }
    else
    {
      TF_WARN("no image source; probably defined by unsupported extension");
      return std::nullopt;
    }

    std::string fileExt;
    if (!readExtensionFromDataSignature(data, fileExt))
    {
      return std::nullopt;
    }

    std::string dstFilePath;
    std::string dstRefPath;
    // When the usdGlTF plugin is used, we avoid copying images around
    if (!srcFilePath.empty() && !copyExistingFiles)
    {
      dstFilePath = srcFilePath;
      dstRefPath = srcFilePath;
    }
    else
    {
      // Otherwise, we give them a new name and copy them to the output dir
      std::string srcFileName = fs::path(srcFilePath).filename().string();
      std::string dstFileName = makeUniqueImageFileName(image->name, srcFileName, fileExt, exportedFileNames);
      if (dstFileName.empty())
      {
        return std::nullopt;
      }

      std::string writeFilePath = (dstDir / fs::path(dstFileName)).string();
      TF_DEBUG(GUC).Msg("writing img %s\n", writeFilePath.c_str());
      if (!writeImageData(writeFilePath.c_str(), data))
      {
        return std::nullopt;
      }

      exportedFileNames.insert(dstFileName); // Keep track of generated names

      dstFilePath = writeFilePath;
      dstRefPath = copyExistingFiles ? dstFileName : writeFilePath;
    }

    // Now that an image is guaranteed to exist, read the metadata required for MaterialX shading network creation
    ImageMetadata metadata;
    metadata.filePath = dstFilePath;
    metadata.refPath = dstRefPath;
    if (!readImageMetadata(dstFilePath.c_str(), metadata.channelCount, metadata.isSrgbInUSD))
    {
      TF_RUNTIME_ERROR("unable to read metadata from image %s", dstFilePath.c_str());
      return std::nullopt;
    }

    return metadata;
  }

  void exportImages(const cgltf_image* images,
                    size_t imageCount,
                    const fs::path& srcDir,
                    const fs::path& dstDir,
                    bool copyExistingFiles,
                    ImageMetadataMap& metadata)
  {
    std::unordered_set<std::string> exportedFileNames;

    for (int i = 0; i < imageCount; i++)
    {
      const cgltf_image* image = &images[i];

      auto meta = exportImage(image, srcDir, dstDir, copyExistingFiles, exportedFileNames);

      if (meta.has_value())
      {
        metadata[image] = meta.value();
      }
    }

    TF_DEBUG(GUC).Msg("exported %d images\n", int(metadata.size()));
  }
}
