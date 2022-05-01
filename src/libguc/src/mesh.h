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
}
