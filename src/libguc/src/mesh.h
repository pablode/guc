#pragma once

#include <cgltf.h>

#include <pxr/base/vt/array.h>

using namespace PXR_NS;

namespace guc
{
  bool createGeometryRepresentation(const cgltf_primitive* prim,
                                    const VtArray<int>& inIndices,
                                    VtArray<int>& outIndices,
                                    VtArray<int>& faceVertexCounts);

  void createFlatNormals(const VtArray<int>& indices,
                         const VtArray<GfVec3f>& positions,
                         VtArray<GfVec3f>& normals);

  bool createTangents(const VtArray<int>& indices,
                      const VtArray<GfVec3f>& positions,
                      const VtArray<GfVec3f>& normals,
                      const VtArray<GfVec2f>& texcoords,
                      VtArray<float>& signs,
                      VtArray<GfVec3f>& tangents);

  bool createBitangents(const VtArray<GfVec3f>& normals,
                        const VtArray<GfVec3f>& tangents,
                        const VtArray<float>& signs,
                        VtArray<GfVec3f>& bitangents);
}
