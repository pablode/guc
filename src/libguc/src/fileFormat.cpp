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

#include "fileFormat.h"

#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/envSetting.h>
#include <pxr/usd/usd/usdcFileFormat.h>

#include <filesystem>

#include "guc_params.h"
#include "cgltf_util.h"
#include "converter.h"
#include "debugCodes.h"

using namespace guc;
namespace fs = std::filesystem;

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(
  UsdGlTFFileFormatTokens,
  USDGLTF_FILE_FORMAT_TOKENS
);

TF_REGISTRY_FUNCTION(TfType)
{
  SDF_DEFINE_FILE_FORMAT(UsdGlTFFileFormat, SdfFileFormat);
}

TF_DEFINE_ENV_SETTING(USDGLTF_ENABLE_MTLX, false,
                      "Whether the UsdGlTF Sdf plugin emits MaterialX materials or not.")

// glTF files can contain embedded images. In order to support them in our Sdf file
// format plugin, we create a temporary directory for each glTF file, write the images
// to it, and reference them. Afterwards, this directory gets deleted, however I was
// unable to use the Sdf file format destructor for this purpose, as it does not seem
// to get called. Instead, we instantiate an object with a static lifetime.
class UsdGlTFTmpDirHolder
{
private:
  std::vector<std::string> m_dirPaths;

public:
  std::string makeDir()
  {
    std::string dir = ArchMakeTmpSubdir(ArchGetTmpDir(), "usdGlTF");
    TF_DEBUG(GUC).Msg("created temp dir %s\n", dir.c_str());
    m_dirPaths.push_back(dir);
    return dir;
  }
  ~UsdGlTFTmpDirHolder()
  {
    for (const std::string& dir : m_dirPaths)
    {
      TF_DEBUG(GUC).Msg("deleting temp dir %s\n", dir.c_str());
      fs::remove_all(fs::path(dir));
    }
  }
};

static UsdGlTFTmpDirHolder s_tmpDirHolder;

UsdGlTFFileFormat::UsdGlTFFileFormat()
  : SdfFileFormat(
    UsdGlTFFileFormatTokens->Id,
    UsdGlTFFileFormatTokens->Version,
    UsdGlTFFileFormatTokens->Target,
    UsdGlTFFileFormatTokens->Id)
{
}

UsdGlTFFileFormat::~UsdGlTFFileFormat()
{
}

bool UsdGlTFFileFormat::CanRead(const std::string& filePath) const
{
  // FIXME: implement? In my tests, this is not even called.
  return false;
}

bool UsdGlTFFileFormat::Read(SdfLayer* layer,
                             const std::string& resolvedPath,
                             bool metadataOnly) const
{
  cgltf_data* gltf_data = nullptr;
  if (!load_gltf(resolvedPath.c_str(), &gltf_data))
  {
    TF_RUNTIME_ERROR("unable to load glTF file %s", resolvedPath.c_str());
    return false;
  }

  guc_params params = {};
  params.emit_mtlx = TfGetEnvSetting(USDGLTF_ENABLE_MTLX);
  params.mtlx_as_usdshade = true;
  params.explicit_colorspace_transforms = false;
  params.gltf_pbr_impl = GUC_GLTF_PBR_IMPL_RUNTIME;
  params.hdstorm_compat = false;

  fs::path srcDir = fs::path(resolvedPath).parent_path();
  fs::path dstDir = s_tmpDirHolder.makeDir();
  fs::path mtlxFileName = ""; // Emitted as UsdShade
  bool copyExistingFiles = false;
  bool genRelativePaths = false;

  SdfLayerRefPtr tmpLayer = SdfLayer::CreateAnonymous(".usdc");
  UsdStageRefPtr stage = UsdStage::Open(tmpLayer);

  Converter converter(gltf_data, stage, srcDir, dstDir, mtlxFileName, copyExistingFiles, genRelativePaths, params);

  Converter::FileExports fileExports; // only used for USDZ
  converter.convert(fileExports);

  cgltf_free(gltf_data);

  layer->TransferContent(tmpLayer);

  return true;
}

bool UsdGlTFFileFormat::ReadFromString(SdfLayer* layer,
                                       const std::string& str) const
{
  // glTF files often reference other files (e.g. a .bin payload or images).
  // Hence, without a file location, most glTF files can not be loaded correctly.
  return false;
}

bool UsdGlTFFileFormat::WriteToString(const SdfLayer& layer,
                                      std::string* str,
                                      const std::string& comment) const
{
  // Not supported, and never will be. Write USDC instead.
  SdfFileFormatConstPtr usdcFormat =  SdfFileFormat::FindById(UsdUsdcFileFormatTokens->Id);
  return usdcFormat->WriteToString(layer, str, comment);
}

bool UsdGlTFFileFormat::WriteToStream(const SdfSpecHandle &spec,
                                      std::ostream& out,
                                      size_t indent) const
{
  // Not supported, and never will be. Write USDC instead.
  SdfFileFormatConstPtr usdcFormat =  SdfFileFormat::FindById(UsdUsdcFileFormatTokens->Id);
  return usdcFormat->WriteToStream(spec, out, indent);
}

PXR_NAMESPACE_CLOSE_SCOPE
