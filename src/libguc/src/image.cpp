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
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolvedPath.h>

#ifdef GUC_USE_OIIO
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/filesystem.h>
#else
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#endif

#include <optional>
#include <unordered_set>

#include "cgltf_util.h"
#include "debugCodes.h"
#include "naming.h"

namespace fs = std::filesystem;

namespace detail
{
  using namespace guc;

  bool readImageDataFromBufferView(const cgltf_buffer_view* bufferView,
                                   size_t& dstSize,
                                   std::shared_ptr<const char>& dstData)
  {
    const char* srcData = (const char*) bufferView->buffer->data;

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

    dstSize = bufferView->size;
    dstData = std::shared_ptr<const char>(srcData, [](const char* ptr) {
      // Do not free the memory - it's owned by cgltf_data which is
      // externally managed and exceeds the lifetime of this shared_ptr.
    });
    return true;
  }

  bool readImageDataFromBase64(const char* base64Str,
                               size_t& size,
                               std::shared_ptr<const char>& data)
  {
    size = strlen(base64Str);

    size_t padding = 0;
    if (size >= 2 && base64Str[size - 2] == '=')
    {
      padding = 2;
    }
    else if (size >= 1 && base64Str[size - 1] == '=')
    {
      padding = 1;
    }

    size = (size / 4) * 3 - padding;

    if (size == 0)
    {
      TF_WARN("base64 string has no payload");
      return false;
    }

    void* dataPtr;
    cgltf_options options = {};
    cgltf_result result = cgltf_load_buffer_base64(&options, size, base64Str, &dataPtr);
    if (result != cgltf_result_success)
    {
      TF_RUNTIME_ERROR("unable to read base64-encoded data");
      return false;
    }

    data = std::shared_ptr<const char>((const char*) dataPtr, [](const char* ptr) {
      free((void*) ptr);
    });
    return true;
  }

  bool readImageFromFile(const char* path,
                         size_t& size,
                         std::shared_ptr<const char>& data)
  {
    TF_DEBUG(GUC).Msg("reading image %s\n", path);

    ArResolver& resolver = ArGetResolver();
    std::string identifier = resolver.CreateIdentifier(path);
    TF_DEBUG(GUC).Msg("normalized path to %s\n", identifier.c_str());

    ArResolvedPath resolvedPath = resolver.Resolve(identifier);
    if (!resolvedPath)
    {
      TF_RUNTIME_ERROR("unable to resolve %s", path);
      return false;
    }

    std::string resolvedPathStr = resolvedPath.GetPathString();
    TF_DEBUG(GUC).Msg("resolved path to %s\n", resolvedPathStr.c_str());

    std::shared_ptr<ArAsset> asset = resolver.OpenAsset(resolvedPath);
    if (!asset)
    {
      TF_RUNTIME_ERROR("unable to open asset %s", resolvedPathStr.c_str());
      return false;
    }

    std::shared_ptr<const char> buffer = asset->GetBuffer();
    if (!buffer)
    {
      TF_RUNTIME_ERROR("unable to open buffer for %s", resolvedPathStr.c_str());
      return false;
    }

    size = asset->GetSize();
    data = asset->GetBuffer();
    return true;
  }

  bool writeImageData(const char* filePath, size_t size, const std::shared_ptr<const char>& data)
  {
    FILE* file = ArchOpenFile(filePath, "wb");
    if (!file)
    {
      TF_RUNTIME_ERROR("unable to open file for writing: %s", filePath);
      return false;
    }

    int64_t result = ArchPWrite(file, data.get(), size, 0);

    ArchCloseFile(ArchFileNo(file));

    if (result == -1)
    {
      TF_RUNTIME_ERROR("unable to read from file %s", filePath);
      return false;
    }
    return true;
  }

  bool readExtensionFromDataSignature(size_t size, const std::shared_ptr<const char>& data, std::string& extension)
  {
    const static std::array<uint8_t, 3> JPEG_HEADER = { 0xFF, 0xD8, 0xFF };
    const static std::array<uint8_t, 8> PNG_HEADER = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

    if (size < JPEG_HEADER.size())
    {
      return false;
    }
    else if (memcmp(data.get(), JPEG_HEADER.data(), JPEG_HEADER.size()) == 0)
    {
      extension = ".jpg";
      return true;
    }
    else if (size < PNG_HEADER.size())
    {
      return false;
    }
    else if (memcmp(data.get(), PNG_HEADER.data(), PNG_HEADER.size()) == 0)
    {
      extension = ".png";
      return true;
    }

    return false;
  }

  bool decodeImageMetadata(const std::shared_ptr<const char>& buffer,
                           size_t bufferSize,
                           const char* path, // only a hint
                           int& channelCount)
  {
#ifdef GUC_USE_OIIO
    OIIO::Filesystem::IOMemReader memReader((void*) buffer.get(), bufferSize);

    auto image = OIIO::ImageInput::open(path, nullptr, &memReader);
    if (image)
    {
      assert(image->supports("ioproxy"));

      const OIIO::ImageSpec& spec = image->spec();
      channelCount = spec.nchannels;
      image->close();
      return true;
    }
    else
    {
      std::string errStr = OIIO::geterror();
      TF_RUNTIME_ERROR("OpenImageIO %s", errStr.c_str());
    }
#else
    int width, height;
    int ok = stbi_info_from_memory((const stbi_uc*) buffer.get(), bufferSize, &width, &height, &channelCount);
    if (ok)
    {
      return true;
    }
#endif
    TF_RUNTIME_ERROR("unable to open file for reading: %s", path);
    return false;
  }

  bool readImageMetadata(const char* path, int& channelCount)
  {
    size_t size;
    std::shared_ptr<const char> data;
    if (!readImageFromFile(path, size, data))
    {
      return false;
    }

    return decodeImageMetadata(data, size, path, channelCount);
  }

  std::optional<ImageMetadata> processImage(const cgltf_image* image,
                                            const fs::path& srcDir,
                                            const fs::path& dstDir,
                                            bool copyExistingFiles,
                                            bool genRelativePaths,
                                            std::unordered_set<std::string>& generatedFileNames)
  {
    size_t size = 0;
    std::shared_ptr<const char> data;
    std::string srcFilePath;

    const char* uri = image->uri;
    if (uri && strncmp(uri, "data:", 5) == 0)
    {
      const char* comma = strchr(uri, ',');
      if (comma && (comma - uri < 7 || strncmp(comma - 7, ";base64", 7) != 0))
      {
        return std::nullopt;
      }

      if (!readImageDataFromBase64(comma + 1, size, data))
      {
        return std::nullopt;
      }
    }
    else if (uri)
    {
      srcFilePath = std::string(uri);
      cgltf_decode_uri(srcFilePath.data());

      if (!readImageFromFile(srcFilePath.c_str(), size, data))
      {
        return std::nullopt;
      }
    }
    else if (image->buffer_view)
    {
      if (!readImageDataFromBufferView(image->buffer_view, size, data))
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
    if (!readExtensionFromDataSignature(size, data, fileExt))
    {
      // Doesn't matter what the mime type or path extension is if the image can not be read
      const char* hint = image->name;
      if (!srcFilePath.empty())
      {
        hint = srcFilePath.c_str();
      }
      if (!hint || !strcmp(hint, ""))
      {
        hint = "embedded";
      }
      TF_RUNTIME_ERROR("unable to determine image data type (hint: %s)", hint);
      return std::nullopt;
    }

    bool genNewFileName = srcFilePath.empty() || genRelativePaths;
    bool writeNewFile = srcFilePath.empty() || copyExistingFiles;

    std::string dstRefPath = srcFilePath;
    if (genNewFileName)
    {
      std::string srcFileName = fs::path(srcFilePath).filename().string();
      std::string dstFileName = makeUniqueImageFileName(image->name, srcFileName, fileExt, generatedFileNames);

      generatedFileNames.insert(dstFileName);

      dstRefPath = dstFileName;
    }

    std::string dstFilePath = srcFilePath;
    if (writeNewFile)
    {
      TF_VERIFY(genNewFileName); // Makes no sense to write a file to its source path

      std::string writeFilePath = (dstDir / fs::path(dstRefPath)).string();
      TF_DEBUG(GUC).Msg("writing img %s\n", writeFilePath.c_str());
      if (!writeImageData(writeFilePath.c_str(), size, data))
      {
        return std::nullopt;
      }

      dstFilePath = writeFilePath;

      if (!genRelativePaths)
      {
        dstRefPath = writeFilePath;
      }
    }

    ImageMetadata metadata;
    metadata.filePath = dstFilePath;
    metadata.refPath = dstRefPath;

    // Read the metadata required for MaterialX shading network creation
    if (!readImageMetadata(dstFilePath.c_str(), metadata.channelCount))
    {
      TF_RUNTIME_ERROR("unable to read metadata of image %s", dstFilePath.c_str());
      return std::nullopt;
    }

    return metadata;
  }
}

namespace guc
{
  void processImages(const cgltf_image* images,
                     size_t imageCount,
                     const fs::path& srcDir,
                     const fs::path& dstDir,
                     bool copyExistingFiles,
                     bool genRelativePaths,
                     ImageMetadataMap& metadata)
  {
    std::unordered_set<std::string> generatedFileNames;

    for (size_t i = 0; i < imageCount; i++)
    {
      const cgltf_image* image = &images[i];

      auto meta = detail::processImage(image, srcDir, dstDir, copyExistingFiles, genRelativePaths, generatedFileNames);

      if (meta.has_value())
      {
        metadata[image] = meta.value();
      }
    }

    TF_DEBUG(GUC).Msg("processed %d images\n", int(metadata.size()));
  }
}
