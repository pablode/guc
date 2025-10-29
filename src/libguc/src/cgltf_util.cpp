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

#include "cgltf_util.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/gf/math.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolvedPath.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <meshoptimizer.h>

#ifdef GUC_USE_DRACO
#include <draco/compression/decode.h>
#include <draco/core/decoder_buffer.h>
#endif

#include <string.h>
#include <unordered_map>

#include "debugCodes.h"

using namespace PXR_NS;

namespace detail
{
  constexpr static const char* GLTF_EXT_MESHOPT_COMPRESSION_EXTENSION_NAME = "EXT_meshopt_compression";
#ifdef GUC_USE_DRACO
  constexpr static const char* GLTF_KHR_DRACO_MESH_COMPRESSION_EXTENSION_NAME = "KHR_draco_mesh_compression";
#endif

  bool extensionSupported(const char* name)
  {
    return strcmp(name, GLTF_EXT_MESHOPT_COMPRESSION_EXTENSION_NAME) == 0 ||
#ifdef GUC_USE_DRACO
           strcmp(name, GLTF_KHR_DRACO_MESH_COMPRESSION_EXTENSION_NAME) == 0 ||
#endif
           strcmp(name, "KHR_lights_punctual") == 0 ||
           strcmp(name, "KHR_materials_clearcoat") == 0 ||
           strcmp(name, "KHR_materials_emissive_strength") == 0 ||
           strcmp(name, "KHR_materials_ior") == 0 ||
           strcmp(name, "KHR_materials_iridescence") == 0 ||
           strcmp(name, "KHR_materials_pbrSpecularGlossiness") == 0 ||
           strcmp(name, "KHR_materials_sheen") == 0 ||
           strcmp(name, "KHR_materials_specular") == 0 ||
           strcmp(name, "KHR_materials_transmission") == 0 ||
           strcmp(name, "KHR_materials_unlit") == 0 ||
           strcmp(name, "KHR_materials_variants") == 0 ||
           strcmp(name, "KHR_materials_volume") == 0 ||
           strcmp(name, "KHR_mesh_quantization") == 0 ||
           strcmp(name, "KHR_texture_transform") == 0;
  }

  struct BufferHolder
  {
    std::unordered_map<const char*, std::shared_ptr<const char>> map;
  };

  cgltf_result readFile(const cgltf_memory_options* memory_options,
                        const cgltf_file_options* file_options,
                        const char* path,
                        cgltf_size* size,
                        void** data)
  {
    TF_DEBUG(GUC).Msg("reading file %s\n", path);

    ArResolver& resolver = ArGetResolver();
    std::string identifier = resolver.CreateIdentifier(path);
    TF_DEBUG(GUC).Msg("normalized path to %s\n", identifier.c_str());

    ArResolvedPath resolvedPath = resolver.Resolve(identifier);
    if (!resolvedPath)
    {
      TF_RUNTIME_ERROR("unable to resolve %s", path);
      return cgltf_result_file_not_found;
    }

    std::string resolvedPathStr = resolvedPath.GetPathString();
    TF_DEBUG(GUC).Msg("resolved path to %s\n", resolvedPathStr.c_str());

    std::shared_ptr<ArAsset> asset = resolver.OpenAsset(ArResolvedPath(path));
    if (!asset)
    {
      TF_RUNTIME_ERROR("unable to open asset %s", resolvedPathStr.c_str());
      return cgltf_result_file_not_found;
    }

    std::shared_ptr<const char> buffer = asset->GetBuffer();
    if (!buffer)
    {
      TF_RUNTIME_ERROR("unable to open buffer for %s", resolvedPathStr.c_str());
      return cgltf_result_io_error;
    }

    const char* bufferPtr = buffer.get();
    (*size) = asset->GetSize();
    (*data) = (void*) bufferPtr;

    auto bufferHolder = (BufferHolder*) file_options->user_data;
    bufferHolder->map[bufferPtr] = buffer;

    return cgltf_result_success;
  }

  void releaseFile(const cgltf_memory_options* memory_options,
                   const cgltf_file_options* file_options,
                   void* data)
  {
    auto bufferPtr = (const char*) data;
    auto bufferHolder = (BufferHolder*) file_options->user_data;
    bufferHolder->map.erase(bufferPtr);
  }

#ifdef GUC_USE_DRACO
  template<typename T>
  bool convertDracoVertexAttributes(const draco::PointAttribute* attribute,
                                    size_t vertexCount,
                                    uint8_t* result)
  {
    uint32_t elemSize = sizeof(T) * attribute->num_components();

    const static uint32_t MAX_COMPONENT_COUNT = 4;

    T elems[MAX_COMPONENT_COUNT];
    for (size_t i = 0; i < vertexCount; i++)
    {
      draco::PointIndex pointIndex(i);
      draco::AttributeValueIndex valueIndex = attribute->mapped_index(pointIndex);

      if (!attribute->ConvertValue<T>(valueIndex, attribute->num_components(), elems))
      {
        return false;
      }

      uint32_t elemOffset = i * elemSize;
      memcpy(&result[elemOffset], &elems[0], elemSize);
    }
    return true;
  }

  bool convertDracoVertexAttributes(cgltf_component_type componentType,
                                    const draco::PointAttribute* attribute,
                                    size_t vertexCount,
                                    uint8_t* result)
  {
    bool success = false;
    switch (componentType)
    {
    case cgltf_component_type_r_8:
      success = convertDracoVertexAttributes<int8_t>(attribute, vertexCount, result);
    case cgltf_component_type_r_8u:
      success = convertDracoVertexAttributes<uint8_t>(attribute, vertexCount, result);
    case cgltf_component_type_r_16:
      success = convertDracoVertexAttributes<int16_t>(attribute, vertexCount, result);
    case cgltf_component_type_r_16u:
      success = convertDracoVertexAttributes<uint16_t>(attribute, vertexCount, result);
    case cgltf_component_type_r_32u:
      success = convertDracoVertexAttributes<uint32_t>(attribute, vertexCount, result);
    case cgltf_component_type_r_32f:
      success = convertDracoVertexAttributes<float>(attribute, vertexCount, result);
      break;
    default:
      break;
    }
    return success;
  }

  bool decompressDraco(cgltf_data* data)
  {
    using EncodedAttributes = std::map<int /*dracoUid*/, cgltf_accessor* /*source accessor*/>;

    std::map<const cgltf_draco_mesh_compression*, EncodedAttributes> attributeBuffersToDecompress;

    //
    // Phase 1: Collect draco buffers and attributes to decode. Multiple prims can reference the same buffer
    //          with a disjunct or overlapping set of attributes, which is why we use the map/set construct.
    //          We also overwrite the primitive indices data source which we fill in the second phase.
    //
    for (size_t i = 0; i < data->meshes_count; ++i)
    {
      const cgltf_mesh& mesh = data->meshes[i];

      for (size_t j = 0; j < mesh.primitives_count; j++)
      {
        const cgltf_primitive& primitive = mesh.primitives[j];
        if (!primitive.has_draco_mesh_compression)
        {
          continue;
        }

        const cgltf_draco_mesh_compression* draco = &primitive.draco_mesh_compression;
        if (attributeBuffersToDecompress.count(draco) == 0)
        {
          attributeBuffersToDecompress[draco] = {};
        }

        EncodedAttributes& mapEnty = attributeBuffersToDecompress[draco];

        // Spec: "the attributes defined in the extension must be a subset of the attributes of the primitive."
        for (size_t i = 0; i < draco->attributes_count; i++)
        {
          const cgltf_attribute* dracoAttr = &draco->attributes[i];

          cgltf_attribute* srcAttr = nullptr;
          for (size_t j = 0; j < primitive.attributes_count; j++)
          {
            cgltf_attribute* attr = &primitive.attributes[i];

            if (strcmp(attr->name, dracoAttr->name) == 0)
            {
              srcAttr = attr;
              break;
            }
          }
          TF_AXIOM(srcAttr); // ensured by validation

          auto dracoUid = int(cgltf_accessor_index(data, dracoAttr->data));
          mapEnty[dracoUid] = srcAttr->data;
        }

        primitive.indices->offset = 0;
        primitive.indices->stride = sizeof(uint32_t);
        primitive.indices->buffer_view = draco->buffer_view;
      }
    }

    //
    // Phase 2: In this second phase, we decode attributes from the draco buffers. We can't allocate new
    //          accessors, buffers and buffer views because this invalidates all of cgltf's pointer references.
    //          Instead, we allocate the .data field of the draco buffer view, which takes precedence over the
    //          encoded buffer, and use it as the decoded data source.
    //
    for (auto it = attributeBuffersToDecompress.begin(); it != attributeBuffersToDecompress.end(); ++it)
    {
      // Decompress into draco mesh
      const cgltf_draco_mesh_compression* draco = it->first;
      cgltf_buffer_view* bufferView = draco->buffer_view;
      cgltf_buffer* buffer = bufferView->buffer;

      const char* bufferData = &((const char*) buffer->data)[bufferView->offset];

      draco::DecoderBuffer decoderBuffer;
      decoderBuffer.Init(bufferData, bufferView->size);

      auto geomType = draco::Decoder::GetEncodedGeometryType(&decoderBuffer);
      if (!geomType.ok() || geomType.value() != draco::TRIANGULAR_MESH)
      {
        TF_RUNTIME_ERROR("unsupported Draco geometry type");
        return false;
      }

      draco::Decoder decoder;
      auto decodeResult = decoder.DecodeMeshFromBuffer(&decoderBuffer);
      if (!decodeResult.ok())
      {
        TF_RUNTIME_ERROR("Draco failed to decode mesh from buffer");
        return false;
      }

      const std::unique_ptr<draco::Mesh>& mesh = decodeResult.value();

      // Allocate decoded buffer
      const EncodedAttributes& attrsToDecode = it->second;

      uint32_t vertexCount = mesh->num_points();
      uint32_t faceCount = mesh->num_faces();
      uint32_t indexCount = faceCount * 3;

      uint32_t indicesSize = indexCount * sizeof(uint32_t);

      size_t attributesSize = 0;
      for (const auto& c : attrsToDecode)
      {
        cgltf_accessor* srcAccessor = c.second;

        attributesSize += vertexCount * srcAccessor->stride;
      }

      bufferView->data = malloc(indicesSize + attributesSize);

      // Write decoded data
      auto baseIndex = draco::FaceIndex(0);
      TF_VERIFY(sizeof(mesh->face(baseIndex)[0]) == 4);
      memcpy(bufferView->data, &mesh->face(baseIndex)[0], indicesSize);

      size_t attributeOffset = indicesSize;
      for (const auto& c : attrsToDecode)
      {
        int dracoUid = c.first;

        const draco::PointAttribute* dracoAttr = mesh->GetAttributeByUniqueId(dracoUid);
        if (!dracoAttr)
        {
          TF_RUNTIME_ERROR("invalid Draco attribute uid");
          return false;
        }

        cgltf_accessor* srcAccessor = c.second;

        TF_VERIFY(srcAccessor->count == vertexCount);
        if (!convertDracoVertexAttributes(srcAccessor->component_type,
                                          dracoAttr,
                                          vertexCount,
                                          &((uint8_t*) bufferView->data)[attributeOffset]))
        {
          TF_RUNTIME_ERROR("failed to decode Draco attribute");
          return false;
        }

        srcAccessor->buffer_view = draco->buffer_view;
        srcAccessor->offset = attributeOffset;

        attributeOffset += vertexCount * srcAccessor->stride;
      }
    }

    return true;
  }
#endif

  // Based on https://github.com/jkuhlmann/cgltf/pull/129
  cgltf_result decompressMeshopt(cgltf_data* data)
  {
    for (size_t i = 0; i < data->buffer_views_count; ++i)
    {
      cgltf_buffer_view& bufferView = data->buffer_views[i];

      if (!bufferView.has_meshopt_compression)
      {
        continue;
      }

      const cgltf_meshopt_compression& mc = bufferView.meshopt_compression;

      const unsigned char* source = (const unsigned char*) mc.buffer->data;
      if (!source)
      {
        return cgltf_result_invalid_gltf;
      }

      source += mc.offset;

      void* result = malloc(mc.count * mc.stride);
      if (!result)
      {
        return cgltf_result_out_of_memory;
      }

      int errorCode = -1;

      switch (mc.mode)
      {
      default:
      case cgltf_meshopt_compression_mode_invalid:
        break;

      case cgltf_meshopt_compression_mode_attributes:
        errorCode = meshopt_decodeVertexBuffer(result, mc.count, mc.stride, source, mc.size);
        break;

      case cgltf_meshopt_compression_mode_triangles:
        errorCode = meshopt_decodeIndexBuffer(result, mc.count, mc.stride, source, mc.size);
        break;

      case cgltf_meshopt_compression_mode_indices:
        errorCode = meshopt_decodeIndexSequence(result, mc.count, mc.stride, source, mc.size);
        break;
      }

      if (errorCode != 0)
      {
        free(result);
        return cgltf_result_io_error;
      }

      switch (mc.filter)
      {
      default:
      case cgltf_meshopt_compression_filter_none:
        break;

      case cgltf_meshopt_compression_filter_octahedral:
        meshopt_decodeFilterOct(result, mc.count, mc.stride);
        break;

      case cgltf_meshopt_compression_filter_quaternion:
        meshopt_decodeFilterQuat(result, mc.count, mc.stride);
        break;

      case cgltf_meshopt_compression_filter_exponential:
        meshopt_decodeFilterExp(result, mc.count, mc.stride);
        break;
      }

      bufferView.data = result;
    }

    return cgltf_result_success;
  }
}

namespace guc
{
  bool load_gltf(const char* gltfPath, cgltf_data** data, bool validate)
  {
    detail::BufferHolder* bufferHolder = new detail::BufferHolder;

    cgltf_file_options fileOptions = {};
    fileOptions.read = detail::readFile;
    fileOptions.release = detail::releaseFile;
    fileOptions.user_data = bufferHolder;

    cgltf_options options = {};
    options.file = fileOptions;

    cgltf_result result = cgltf_parse_file(&options, gltfPath, data);

    if (result != cgltf_result_success)
    {
      TF_RUNTIME_ERROR("unable to parse glTF file: %s", cgltf_error_string(result));
      delete bufferHolder;
      return false;
    }

    result = cgltf_load_buffers(&options, *data, gltfPath);

    if (result != cgltf_result_success)
    {
      TF_RUNTIME_ERROR("unable to load glTF buffers: %s", cgltf_error_string(result));
      free_gltf(*data);
      return false;
    }

    if (validate)
    {
      result = cgltf_validate(*data);

      if (result != cgltf_result_success)
      {
        TF_RUNTIME_ERROR("unable to validate glTF: %s", cgltf_error_string(result));
        free_gltf(*data);
        return false;
      }
    }

    bool meshoptCompressionRequired = false;
#ifdef GUC_USE_DRACO
    bool dracoMeshCompressionRequired = false;
#endif

    for (size_t i = 0; i < (*data)->extensions_required_count; i++)
    {
      const char* ext = (*data)->extensions_required[i];
      TF_DEBUG(GUC).Msg("extension required: %s\n", ext);

      if (strcmp(ext, detail::GLTF_EXT_MESHOPT_COMPRESSION_EXTENSION_NAME) == 0)
      {
        meshoptCompressionRequired = true;
      }

#ifdef GUC_USE_DRACO
      if (strcmp(ext, detail::GLTF_KHR_DRACO_MESH_COMPRESSION_EXTENSION_NAME) == 0)
      {
        dracoMeshCompressionRequired = true;
      }
#endif

      if (detail::extensionSupported(ext))
      {
        continue;
      }

      TF_RUNTIME_ERROR("extension %s not supported", ext);
      free_gltf(*data);
      return false;
    }

    result = detail::decompressMeshopt(*data);

    if (result != cgltf_result_success)
    {
      const char* errStr = "unable to decode meshoptimizer data: %s";

      if (meshoptCompressionRequired)
      {
        TF_RUNTIME_ERROR(errStr, guc::cgltf_error_string(result));
        free_gltf(*data);
        return false;
      }

      TF_WARN(errStr, guc::cgltf_error_string(result));
    }

#ifdef GUC_USE_DRACO
    if (!detail::decompressDraco(*data))
    {
      const char* errStr = "unable to decode Draco data";

      if (dracoMeshCompressionRequired)
      {
        TF_RUNTIME_ERROR("%s", errStr);
        free_gltf(*data);
        return false;
      }

      TF_WARN("%s", errStr);
    }
#endif

    for (size_t i = 0; i < (*data)->extensions_used_count; i++)
    {
      const char* ext = (*data)->extensions_used[i];
      TF_DEBUG(GUC).Msg("extension used: %s\n", ext);

      if (detail::extensionSupported(ext))
      {
        continue;
      }

      TF_WARN("optional extension %s not suppported", ext);
    }

    return true;
  }

  void free_gltf(cgltf_data* data)
  {
    auto bufferHolder = (detail::BufferHolder*) data->file.user_data;
    cgltf_free(data); // releases buffers in buffer holder
    delete bufferHolder;
  }

  const char* cgltf_error_string(cgltf_result result)
  {
    TF_VERIFY(result != cgltf_result_success);
    TF_VERIFY(result != cgltf_result_invalid_options);
    switch (result)
    {
    case cgltf_result_legacy_gltf:
      return "legacy glTF not supported";
    case cgltf_result_data_too_short:
    case cgltf_result_invalid_json:
    case cgltf_result_invalid_gltf:
      return "malformed glTF";
    case cgltf_result_unknown_format:
      return "unknown format";
    case cgltf_result_file_not_found:
      return "file not found";
    case cgltf_result_io_error:
      return "io error";
    case cgltf_result_out_of_memory:
      return "out of memory";
    default:
      return "unknown";
    }
  }

  const cgltf_accessor* cgltf_find_accessor(const cgltf_primitive* primitive,
                                            const char* name)
  {
    for (size_t j = 0; j < primitive->attributes_count; j++)
    {
      const cgltf_attribute* attribute = &primitive->attributes[j];

      if (strcmp(attribute->name, name) == 0)
      {
        return attribute->data;
      }
    }

    return nullptr;
  }

  bool cgltf_transform_required(const cgltf_texture_transform& transform)
  {
    return !GfIsClose(transform.offset[0], 0.0f, 1e-5f) ||
           !GfIsClose(transform.offset[1], 0.0f, 1e-5f) ||
           !GfIsClose(transform.rotation, 0.0f, 1e-5f) ||
           !GfIsClose(transform.scale[0], 1.0f, 1e-5f) ||
           !GfIsClose(transform.scale[1], 1.0f, 1e-5f);
  }
}
