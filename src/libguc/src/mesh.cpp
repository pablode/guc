#include "mesh.h"

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/diagnostic.h>

#include "debugCodes.h"

namespace guc
{
  bool createGeometryRepresentation(const cgltf_primitive* prim,
                                    const VtArray<int>& inIndices,
                                    VtArray<int>& outIndices,
                                    VtArray<int>& faceVertexCounts)
  {
    const char* INDICES_MISMATCH_ERROR_MSG = "indices count does not match primitive type";

    switch (prim->type)
    {
    case cgltf_primitive_type_points: {
      faceVertexCounts = VtArray<int>(inIndices.size(), 1);
      outIndices = inIndices;
      break;
    }
    case cgltf_primitive_type_lines: {
      if ((inIndices.size() % 2) != 0)
      {
        TF_RUNTIME_ERROR("%s", INDICES_MISMATCH_ERROR_MSG);
        return false;
      }
      faceVertexCounts = VtArray<int>(inIndices.size() / 2, 2);
      outIndices = inIndices;
      break;
    }
    case cgltf_primitive_type_triangles: {
      if ((inIndices.size() % 3) != 0)
      {
        TF_RUNTIME_ERROR("%s", INDICES_MISMATCH_ERROR_MSG);
        return false;
      }
      faceVertexCounts = VtArray<int>(inIndices.size() / 3, 3);
      outIndices = inIndices;
      break;
    }
    case cgltf_primitive_type_line_strip: {
      if (inIndices.size() < 2)
      {
        TF_RUNTIME_ERROR("%s", INDICES_MISMATCH_ERROR_MSG);
        return false;
      }
      faceVertexCounts = VtArray<int>(inIndices.size() - 1, 2);
      outIndices.resize(faceVertexCounts.size() * 2);
      for (int i = 0; i < faceVertexCounts.size(); i++)
      {
        outIndices[i * 2 + 0] = inIndices[i + 0];
        outIndices[i * 2 + 1] = inIndices[i + 1];
      }
      break;
    }
    case cgltf_primitive_type_line_loop: {
      if (inIndices.size() < 2)
      {
        TF_RUNTIME_ERROR("%s", INDICES_MISMATCH_ERROR_MSG);
        return false;
      }
      faceVertexCounts = VtArray<int>(inIndices.size(), 2);
      outIndices.resize(inIndices.size() * 2);
      int i;
      for (i = 0; i < inIndices.size() - 1; i++)
      {
        outIndices[i * 2 + 0] = inIndices[i + 0];
        outIndices[i * 2 + 1] = inIndices[i + 1];
      }
      outIndices[i + 0] = inIndices[inIndices.size() - 1];
      outIndices[i + 1] = inIndices[0];
      break;
    }
    case cgltf_primitive_type_triangle_strip: {
      if (inIndices.size() < 3)
      {
        TF_RUNTIME_ERROR("%s", INDICES_MISMATCH_ERROR_MSG);
        return false;
      }
      faceVertexCounts = VtArray<int>(inIndices.size() - 2, 3);
      outIndices.resize(faceVertexCounts.size() * 3);
      bool forward = true;
      for (int i = 0; i < faceVertexCounts.size(); i++)
      {
        int i0 = i + 0;
        int i1 = forward ? (i + 1) : (i + 2);
        int i2 = forward ? (i + 2) : (i + 1);
        outIndices[i * 3 + 0] = inIndices[i0];
        outIndices[i * 3 + 1] = inIndices[i1];
        outIndices[i * 3 + 2] = inIndices[i2];
        forward = !forward;
      }
      break;
    }
    case cgltf_primitive_type_triangle_fan: {
      if (inIndices.size() < 3)
      {
        TF_RUNTIME_ERROR("%s", INDICES_MISMATCH_ERROR_MSG);
        return false;
      }
      faceVertexCounts = VtArray<int>(inIndices.size() - 2, 3);
      outIndices.resize(faceVertexCounts.size() * 3);
      for (int i = 0; i < faceVertexCounts.size(); i++)
      {
        outIndices[i * 3 + 0] = inIndices[0];
        outIndices[i * 3 + 1] = inIndices[i + 1];
        outIndices[i * 3 + 2] = inIndices[i + 2];
      }
      break;
    }
    default:
      TF_CODING_ERROR("unhandled primitive type %d", int(prim->type));
      return false;
    }
    return true;
  }

  void createFlatNormals(const VtArray<int>& indices,
                         const VtArray<GfVec3f>& positions,
                         VtArray<GfVec3f>& normals)
  {
    TF_VERIFY((indices.size() % 3) == 0);
    normals.resize(positions.size());

    for (int i = 0; i < indices.size(); i += 3)
    {
      int i0 = indices[i + 0];
      int i1 = indices[i + 1];
      int i2 = indices[i + 2];

      const GfVec3f& p0 = positions[i0];
      const GfVec3f& p1 = positions[i1];
      const GfVec3f& p2 = positions[i2];

      GfVec3f e1 = (p1 - p0);
      GfVec3f e2 = (p2 - p0);
      e1.Normalize();
      e2.Normalize();

      GfVec3f n = GfCross(e1, e2);
      n.Normalize();

      normals[i0] = n;
      normals[i1] = n;
      normals[i2] = n;
    }
  }
}
