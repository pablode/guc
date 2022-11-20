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

#include "guc.h"

#include <pxr/base/arch/fileSystem.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/zipFile.h>
#include <pxr/usd/usdUtils/dependencies.h>

#include <filesystem>

#include "debugCodes.h"
#include "cgltf_util.h"
#include "converter.h"

using namespace guc;
namespace fs = std::filesystem;

bool convertToUsd(fs::path src_dir,
                  const cgltf_data* gltf_data,
                  fs::path usd_path,
                  const guc_params* params,
                  Converter::FileExports& fileExports)
{
  UsdStageRefPtr stage = UsdStage::CreateNew(usd_path.string());
  if (!stage)
  {
    TF_RUNTIME_ERROR("Unable to open stage at %s", usd_path.string().c_str());
    return false;
  }

  fs::path dst_dir = usd_path.parent_path();
  fs::path mtlx_file_name = usd_path.filename().replace_extension(".mtlx");
  bool copyExistingFiles = true;

  Converter converter(gltf_data, stage, src_dir, dst_dir, mtlx_file_name, copyExistingFiles, *params);

  bool result = converter.convert(fileExports);

  if (result)
  {
    TF_DEBUG(GUC).Msg("saving stage to %s\n", usd_path.string().c_str());
    stage->Save();
  }

  return result;
}

bool guc_convert(const char* gltf_path,
                 const char* usd_path,
                 const guc_params* params)
{
  TF_VERIFY(params);

  // The path we write USDA/USDC files to. If the user wants a USDZ file, we first
  // write these files to a temporary location, zip them, and copy the ZIP file to
  // the destination directory.
  fs::path final_usd_path = usd_path;
  fs::path base_usd_path = usd_path;
  fs::path dst_dir = base_usd_path.parent_path();
  fs::path src_dir = fs::path(gltf_path).parent_path();

  bool export_usdz = base_usd_path.extension() == ".usdz";

  if (export_usdz)
  {
    dst_dir = ArchMakeTmpSubdir(ArchGetTmpDir(), "guc");
    TF_DEBUG(GUC).Msg("using temp dir %s\n", dst_dir.string().c_str());

    if (dst_dir.empty())
    {
      TF_RUNTIME_ERROR("Unable to create temporary directory for USDZ contents");
      return false;
    }

    auto new_usd_filename = base_usd_path.replace_extension(".usdc").filename();
    base_usd_path = dst_dir / new_usd_filename;
    TF_DEBUG(GUC).Msg("temporary USD path: %s\n", base_usd_path.string().c_str());
  }

  cgltf_data* gltf_data = nullptr;
  if (!load_gltf(gltf_path, &gltf_data))
  {
    TF_RUNTIME_ERROR("Unable to load glTF at %s", gltf_path);
    return false;
  }

  Converter::FileExports fileExports;
  bool result = convertToUsd(src_dir, gltf_data, base_usd_path, params, fileExports);

  cgltf_free(gltf_data);

  if (!result)
  {
    return false;
  }

  // In case of USDZ, we have now written the USDC file and all image files to a
  // temporary directory. Next, we invoke Pixar's USDZ API in order to zip them.
  if (export_usdz)
  {
    auto usdz_dst_dir = fs::absolute(final_usd_path).parent_path();
    if (!fs::exists(usdz_dst_dir) && !fs::create_directories(usdz_dst_dir))
    {
      TF_RUNTIME_ERROR("Unable to create destination directory");
      return false;
    }

    TF_DEBUG(GUC).Msg("creating USDZ archive %s\n", final_usd_path.string().c_str());
    UsdZipFileWriter writer = UsdZipFileWriter::CreateNew(final_usd_path.string());

    TF_DEBUG(GUC).Msg("adding %s to USDZ archive at ./%s\n", base_usd_path.string().c_str(), base_usd_path.filename().string().c_str());
    if (writer.AddFile(base_usd_path.string(), base_usd_path.filename().string()) == "")
    {
      TF_RUNTIME_ERROR("Unable to usdzip %s to %s", base_usd_path.string().c_str(), base_usd_path.filename().string().c_str());
      return false; // Fatal error
    }
    for (const auto& fileExport : fileExports)
    {
      std::string srcPath = fileExport.filePath;
      std::string dstPathInUsdz = fileExport.refPath;

      TF_DEBUG(GUC).Msg("adding %s to USDZ archive at ./%s\n", srcPath.c_str(), dstPathInUsdz.c_str());
      if (writer.AddFile(srcPath, dstPathInUsdz) == "")
      {
        TF_RUNTIME_ERROR("Unable to usdzip %s to %s", srcPath.c_str(), dstPathInUsdz.c_str());
        // (non-fatal error)
      }
    }

    if (!writer.Save())
    {
      TF_RUNTIME_ERROR("Unable to save USDZ file");
      return false;
    }

    TF_DEBUG(GUC).Msg("removing temporary directory %s\n", dst_dir.string().c_str());
    fs::remove_all(dst_dir);
  }

  return true;
}
