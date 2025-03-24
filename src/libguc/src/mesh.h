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

#include <pxr/base/vt/array.h>

using namespace PXR_NS;

namespace guc
{
  bool createGeometryRepresentation(const cgltf_primitive* prim,
                                    const VtIntArray& inIndices,
                                    VtIntArray& outIndices,
                                    VtIntArray& faceVertexCounts);

  void createFlatNormals(const VtIntArray& indices,
                         const VtVec3fArray& positions,
                         VtVec3fArray& normals);

  bool createTangents(const VtIntArray& indices,
                      const VtVec3fArray& positions,
                      const VtVec3fArray& normals,
                      const VtVec2fArray& texcoords,
                      bool uniformNormals,
                      VtFloatArray& signs,
                      VtVec3fArray& tangents);
}
