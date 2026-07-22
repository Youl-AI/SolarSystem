#include "MeshGen.hpp"

#include "tiny_obj_loader.h"
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <cmath>
#include <stdexcept>

#include <cfloat>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 원본 메시를 정점 클러스터링으로 단순화해 vertices/indices 뒤에 덧붙인다.
// 바운딩 박스를 gridRes^3 격자로 나누고, 같은 칸에 떨어진 정점들을 대표점 하나로 합친다.
// 두 정점 이상이 같은 칸으로 뭉개진 삼각형은 사라지므로 면 수가 크게 준다.
// 소행성처럼 덩어리진 형태는 이 방식으로도 실루엣이 잘 남는다.
void buildSimplifiedLod(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                        uint32_t srcFirstIndex, uint32_t srcIndexCount, int32_t srcVertexOffset,
                        int gridRes,
                        uint32_t &outIndexCount, uint32_t &outFirstIndex, int32_t &outVertexOffset) {
    glm::vec3 lo(FLT_MAX), hi(-FLT_MAX);
    for (uint32_t i = 0; i < srcIndexCount; ++i) {
        const glm::vec3& p = verts[srcVertexOffset + inds[srcFirstIndex + i]].pos;
        lo = glm::min(lo, p); hi = glm::max(hi, p);
    }
    glm::vec3 extent = glm::max(hi - lo, glm::vec3(1e-6f));

    auto cellOf = [&](const glm::vec3& p) {
        glm::ivec3 c = glm::ivec3(glm::clamp((p - lo) / extent * (float)gridRes, glm::vec3(0.0f), glm::vec3(gridRes - 1)));
        return (c.x * gridRes + c.y) * gridRes + c.z;
    };

    // 칸별로 정점을 평균내어 대표점을 만든다(격자 중심보다 원형이 덜 깨진다).
    std::unordered_map<int, std::pair<Vertex, int>> reps;
    for (uint32_t i = 0; i < srcIndexCount; ++i) {
        const Vertex& v = verts[srcVertexOffset + inds[srcFirstIndex + i]];
        int cell = cellOf(v.pos);
        auto it = reps.find(cell);
        if (it == reps.end()) { reps.emplace(cell, std::make_pair(v, 1)); }
        else {
            it->second.first.pos += v.pos;
            it->second.first.normal += v.normal;
            it->second.first.texCoord += v.texCoord;
            it->second.second++;
        }
    }

    outVertexOffset = static_cast<int32_t>(verts.size());
    outFirstIndex = static_cast<uint32_t>(inds.size());

    std::unordered_map<int, uint32_t> cellToIndex;
    for (auto& kv : reps) {
        Vertex v = kv.second.first;
        float n = (float)kv.second.second;
        v.pos /= n; v.texCoord /= n;
        v.normal = (glm::length(v.normal) > 1e-6f) ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
        cellToIndex[kv.first] = static_cast<uint32_t>(verts.size()) - static_cast<uint32_t>(outVertexOffset);
        verts.push_back(v);
    }

    for (uint32_t i = 0; i + 2 < srcIndexCount; i += 3) {
        int c0 = cellOf(verts[srcVertexOffset + inds[srcFirstIndex + i + 0]].pos);
        int c1 = cellOf(verts[srcVertexOffset + inds[srcFirstIndex + i + 1]].pos);
        int c2 = cellOf(verts[srcVertexOffset + inds[srcFirstIndex + i + 2]].pos);
        if (c0 == c1 || c1 == c2 || c0 == c2) continue; // 축퇴 삼각형은 버린다
        inds.push_back(cellToIndex[c0]);
        inds.push_back(cellToIndex[c1]);
        inds.push_back(cellToIndex[c2]);
    }
    outIndexCount = static_cast<uint32_t>(inds.size()) - outFirstIndex;
    std::cout << "[lod] simplified " << (srcIndexCount / 3) << " -> " << (outIndexCount / 3) << " tris\n";
}

void loadObjModel(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                  const std::string &path,
                  uint32_t &outIndexCount, uint32_t &outFirstIndex, int32_t &outVertexOffset) {
    tinyobj::attrib_t attrib; std::vector<tinyobj::shape_t> shapes; std::vector<tinyobj::material_t> materials; std::string warn, err;
    outVertexOffset = static_cast<int32_t>(verts.size());
    outFirstIndex = static_cast<uint32_t>(inds.size());

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
        std::cerr << "OBJ 로드 실패! 대체용 미니 구체를 생성합니다. 에러: " << warn << err << "\n";
        generateSphere(verts, inds, 1.0f, 16, 16);
        outIndexCount = static_cast<uint32_t>(inds.size()) - outFirstIndex;
        return;
    }

    // 1. 모델의 실제 크기를 재기 위한 바운딩 박스 변수
    glm::vec3 minBoundary(FLT_MAX);
    glm::vec3 maxBoundary(-FLT_MAX);

    // 첫 번째 패스: 모델이 얼마나 거대한지 최댓값/최솟값 측정
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            glm::vec3 pos = { attrib.vertices[3 * index.vertex_index + 0], attrib.vertices[3 * index.vertex_index + 1], attrib.vertices[3 * index.vertex_index + 2] };
            minBoundary = glm::min(minBoundary, pos);
            maxBoundary = glm::max(maxBoundary, pos);
        }
    }

    // 중심점과 가장 긴 축의 길이 계산
    glm::vec3 center = (maxBoundary + minBoundary) / 2.0f;
    glm::vec3 extents = maxBoundary - minBoundary;
    float maxExtent = std::max(extents.x, std::max(extents.y, extents.z));
    if (maxExtent == 0.0f) maxExtent = 1.0f; // 0으로 나누기 방지

    uint32_t localVertexCount = 0;
    // 두 번째 패스: 정점을 1x1x1 크기로 깎아서 조립
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            // 🚀 [핵심 1] 엄청나게 큰 좌표를 중심으로 끌고 온 뒤, 최대 크기(maxExtent)로 나누어 강제로 1.0 안에 욱여넣습니다!
            glm::vec3 rawPos = { attrib.vertices[3 * index.vertex_index + 0], attrib.vertices[3 * index.vertex_index + 1], attrib.vertices[3 * index.vertex_index + 2] };
            vertex.pos = (rawPos - center) / (maxExtent * 0.5f);

            if (index.texcoord_index >= 0) vertex.texCoord = { attrib.texcoords[2 * index.texcoord_index + 0], 1.0f - attrib.texcoords[2 * index.texcoord_index + 1] };

            // 🚀 [핵심 2] 스캔 데이터에 빛 반사(Normal) 데이터가 없으면, 정점의 위치를 기반으로 가짜 법선을 만들어 NaN 에러(파란 괴물)를 막습니다.
            if (index.normal_index >= 0) {
                vertex.normal = { attrib.normals[3 * index.normal_index + 0], attrib.normals[3 * index.normal_index + 1], attrib.normals[3 * index.normal_index + 2] };
            } else {
                vertex.normal = glm::normalize(vertex.pos);
            }

            vertex.color = {1.0f, 1.0f, 1.0f};
            verts.push_back(vertex);
            inds.push_back(localVertexCount++);
        }
    }
    outIndexCount = static_cast<uint32_t>(inds.size()) - outFirstIndex;
}

void generateSphere(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                    float radius, int sectorCount, int stackCount) {
    float x, y, z, xy; float sectorStep = 2 * M_PI / sectorCount; float stackStep = M_PI / stackCount; float sectorAngle, stackAngle;
    for (int i = 0; i <= stackCount; ++i) {
        stackAngle = M_PI / 2 - i * stackStep;
        xy = radius * cosf(stackAngle);
        // 🚀 [수정] 위아래 높이를 Z가 아니라 'Y축'으로 잡아줍니다!
        y = radius * sinf(stackAngle);

        for (int j = 0; j <= sectorCount; ++j) {
            sectorAngle = j * sectorStep;
            x = xy * cosf(sectorAngle);
            // 🚀 [수정] 둥근 적도 평면을 Y가 아니라 'Z축'으로 잡아줍니다!
            z = xy * sinf(sectorAngle);

            verts.push_back({glm::vec3(x, y, z), glm::vec3(1.0f), glm::vec2((float)j / sectorCount, (float)i / stackCount), glm::normalize(glm::vec3(x, y, z))});
        }
    }
    for (int i = 0; i < stackCount; ++i) {
        int k1 = i * (sectorCount + 1); int k2 = k1 + sectorCount + 1;
        for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            if (i != 0) { inds.push_back(k1); inds.push_back(k2); inds.push_back(k1 + 1); }
            if (i != (stackCount - 1)) { inds.push_back(k1 + 1); inds.push_back(k2); inds.push_back(k2 + 1); }
        }
    }
}
void generateRing(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                  float innerRadius, float outerRadius, int sectorCount) {
    for (int i = 0; i <= sectorCount; ++i) {
        float angle = (float)i / sectorCount * 2.0f * M_PI; float cosA = cosf(angle); float sinA = sinf(angle);
        verts.push_back({glm::vec3(innerRadius * cosA, 0.0f, innerRadius * sinA), glm::vec3(1.0f), glm::vec2(0.0f, (float)i / sectorCount), glm::vec3(0, 1, 0)});
        verts.push_back({glm::vec3(outerRadius * cosA, 0.0f, outerRadius * sinA), glm::vec3(1.0f), glm::vec2(1.0f, (float)i / sectorCount), glm::vec3(0, 1, 0)});
    }
    for (int i = 0; i < sectorCount; ++i) {
        int k1 = i * 2; int k2 = k1 + 1; int k3 = (i + 1) * 2; int k4 = k3 + 1;
        inds.push_back(k1); inds.push_back(k3); inds.push_back(k2); inds.push_back(k2); inds.push_back(k3); inds.push_back(k4);
    }
}
void generateOrbit(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                   int segmentCount) {
    for (int i = 0; i <= segmentCount; ++i) {
        float angle = (float)i / segmentCount * 2.0f * M_PI;
        // 🚀 빛 번짐(Bloom) 폭주 방지: 형광 노란색의 채도는 유지하되 밝기를 0.2, 0.4 수준으로 낮춥니다.
        verts.push_back({glm::vec3(cosf(angle), 0.0f, sinf(angle)), glm::vec3(0.2f, 0.4f, 0.0f), glm::vec2(0.0f), glm::vec3(0, 1, 0)});
        inds.push_back(i);
    }
}
