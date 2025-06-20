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

#include "mesh.h"

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/diagnostic.h>

#include <mikktspace.h>

#include "debugCodes.h"

namespace guc
{
  bool createGeometryRepresentation(const cgltf_primitive* prim,
                                    const VtIntArray& inIndices,
                                    VtIntArray& outIndices,
                                    VtIntArray& faceVertexCounts)
  {
    const char* INDICES_MISMATCH_ERROR_MSG = "indices count does not match primitive type";

    switch (prim->type)
    {
    case cgltf_primitive_type_points: {
      faceVertexCounts = VtIntArray(inIndices.size(), 1);
      outIndices = inIndices;
      break;
    }
    case cgltf_primitive_type_lines: {
      if ((inIndices.size() % 2) != 0)
      {
        TF_RUNTIME_ERROR("%s", INDICES_MISMATCH_ERROR_MSG);
        return false;
      }
      faceVertexCounts = VtIntArray(inIndices.size() / 2, 2);
      outIndices = inIndices;
      break;
    }
    case cgltf_primitive_type_triangles: {
      if ((inIndices.size() % 3) != 0)
      {
        TF_RUNTIME_ERROR("%s", INDICES_MISMATCH_ERROR_MSG);
        return false;
      }
      faceVertexCounts = VtIntArray(inIndices.size() / 3, 3);
      outIndices = inIndices;
      break;
    }
    case cgltf_primitive_type_line_strip: {
      if (inIndices.size() < 2)
      {
        TF_RUNTIME_ERROR("%s", INDICES_MISMATCH_ERROR_MSG);
        return false;
      }
      faceVertexCounts = VtIntArray(inIndices.size() - 1, 2);
      outIndices.resize(faceVertexCounts.size() * 2);
      for (size_t i = 0; i < faceVertexCounts.size(); i++)
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
      faceVertexCounts = VtIntArray(inIndices.size(), 2);
      outIndices.resize(inIndices.size() * 2);
      size_t i;
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
      faceVertexCounts = VtIntArray(inIndices.size() - 2, 3);
      outIndices.resize(faceVertexCounts.size() * 3);
      bool forward = true;
      for (size_t i = 0; i < faceVertexCounts.size(); i++)
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
      faceVertexCounts = VtIntArray(inIndices.size() - 2, 3);
      outIndices.resize(faceVertexCounts.size() * 3);
      for (size_t i = 0; i < faceVertexCounts.size(); i++)
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

  void createFlatNormals(const VtIntArray& indices,
                         const VtVec3fArray& positions,
                         VtVec3fArray& normals)
  {
    TF_VERIFY((indices.size() % 3) == 0);

    size_t triangleCount = indices.size() / 3;
    normals.resize(triangleCount);

    for (size_t i = 0; i < triangleCount; i++)
    {
      int i0 = indices[i * 3 + 0];
      int i1 = indices[i * 3 + 1];
      int i2 = indices[i * 3 + 2];

      const GfVec3f& p0 = positions[i0];
      const GfVec3f& p1 = positions[i1];
      const GfVec3f& p2 = positions[i2];

      GfVec3f e1 = (p1 - p0);
      GfVec3f e2 = (p2 - p0);
      e1.Normalize();
      e2.Normalize();

      GfVec3f n = GfCross(e1, e2);
      n.Normalize();

      normals[i] = n;
    }
  }

  bool createTangents(const VtIntArray& indices,
                      const VtVec3fArray& positions,
                      const VtVec3fArray& normals,
                      const VtVec2fArray& texcoords,
                      bool uniformNormals,
                      VtVec3fArray& tangents,
                      VtVec3fArray& bitangents)
  {
    TF_VERIFY(!texcoords.empty());

    int vertexCount = indices.size();
    tangents.resize(vertexCount);
    bitangents.resize(vertexCount);

    struct UserData {
      const VtIntArray& indices;
      const VtVec3fArray& positions;
      const VtVec3fArray& normals;
      const VtVec2fArray& texcoords;
      bool uniformNormals;
      VtVec3fArray& tangents;
      VtVec3fArray& bitangents;
    } userData = {
      indices, positions, normals, texcoords, uniformNormals, tangents, bitangents
    };

    auto getNumFacesFunc = [](const SMikkTSpaceContext* pContext) {
      UserData* userData = (UserData*) pContext->m_pUserData;
      return (int) userData->indices.size() / 3;
    };

    auto getNumVerticesOfFaceFunc = [](const SMikkTSpaceContext* pContext, const int iFace) {
      return 3;
    };

    auto getPositionFunc = [](const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert) {
      UserData* userData = (UserData*) pContext->m_pUserData;
      int index = userData->indices[iFace * 3 + iVert];
      const GfVec3f& position = userData->positions[index];
      fvPosOut[0] = position[0];
      fvPosOut[1] = position[1];
      fvPosOut[2] = position[2];
    };

    auto getNormalFunc = [](const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert) {
      UserData* userData = (UserData*) pContext->m_pUserData;
      int index = userData->uniformNormals ? iFace : userData->indices[iFace * 3 + iVert];
      const GfVec3f& normal = userData->normals[index];
      fvNormOut[0] = normal[0];
      fvNormOut[1] = normal[1];
      fvNormOut[2] = normal[2];
    };

    auto getTexCoordFunc = [](const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert) {
      UserData* userData = (UserData*) pContext->m_pUserData;
      int index = userData->indices[iFace * 3 + iVert];
      const GfVec2f& texcoord = userData->texcoords[index];
      fvTexcOut[0] = texcoord[0];
      fvTexcOut[1] = texcoord[1];
    };

    auto setTSpaceBasicFunc = [](const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert) {
      UserData* userData = (UserData*) pContext->m_pUserData;

      int normalIndex = userData->uniformNormals ? iFace : userData->indices[iFace * 3 + iVert];
      const GfVec3f& normal = userData->normals[normalIndex];

      GfVec3f tangent(fvTangent[0], fvTangent[1], fvTangent[2]);
      GfVec3f bitangent = GfCross(normal, tangent) * fSign;

      int newTangentIndex = iFace * 3 + iVert;
      userData->tangents[newTangentIndex] = tangent;
      userData->bitangents[newTangentIndex] = bitangent;
    };

    SMikkTSpaceInterface interface;
    interface.m_getNumFaces = getNumFacesFunc;
    interface.m_getNumVerticesOfFace= getNumVerticesOfFaceFunc;
    interface.m_getPosition = getPositionFunc;
    interface.m_getNormal = getNormalFunc;
    interface.m_getTexCoord = getTexCoordFunc;
    interface.m_setTSpaceBasic = setTSpaceBasicFunc;
    interface.m_setTSpace = nullptr;

    SMikkTSpaceContext context;
    context.m_pInterface = &interface;
    context.m_pUserData = &userData;

    return genTangSpaceDefault(&context);
  }
}
