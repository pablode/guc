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

#include <pxr/pxr.h>
#include <pxr/usd/sdf/data.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/usd/pcp/dynamicFileFormatInterface.h>
#include <iosfwd>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

#define USDGLTF_FILE_FORMAT_TOKENS     \
  ((Id,      "gltf"))                  \
  ((Version, GUC_VERSION_STRING))      \
  ((Target,  "usd"))

TF_DECLARE_PUBLIC_TOKENS(UsdGlTFFileFormatTokens, USDGLTF_FILE_FORMAT_TOKENS);

TF_DECLARE_WEAK_AND_REF_PTRS(UsdGlTFFileFormat);
TF_DECLARE_WEAK_AND_REF_PTRS(UsdGlTFData);

class UsdGlTFFileFormat : public SdfFileFormat, public PcpDynamicFileFormatInterface
{
protected:
  SDF_FILE_FORMAT_FACTORY_ACCESS;

  UsdGlTFFileFormat();

  virtual ~UsdGlTFFileFormat();

public:
  SdfAbstractDataRefPtr InitData(const FileFormatArguments& args) const override;

  bool CanRead(const std::string &file) const override;

  bool Read(SdfLayer* layer,
            const std::string& resolvedPath,
            bool metadataOnly) const override;

  bool ReadFromString(SdfLayer* layer,
                      const std::string& str) const override;

  bool WriteToString(const SdfLayer& layer,
                     std::string* str,
                     const std::string& comment = std::string())
                     const override;

  bool WriteToStream(const SdfSpecHandle &spec,
                     std::ostream& out,
                     size_t indent) const override;

public:
  void ComposeFieldsForFileFormatArguments(const std::string& assetPath,
                                           const PcpDynamicFileFormatContext& context,
                                           FileFormatArguments* args,
                                           VtValue *dependencyContextData) const override;
};

class UsdGlTFData : public SdfData
{
public:
  bool emitMtlx = false;
};

PXR_NAMESPACE_CLOSE_SCOPE

