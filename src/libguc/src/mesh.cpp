#include "mesh.h"

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
}
