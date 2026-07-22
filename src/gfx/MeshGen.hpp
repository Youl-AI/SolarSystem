#pragma once

#include "../VulkanBase.hpp"   // Vertex
#include <vector>
#include <string>
#include <cstdint>

// 정점·인덱스 버퍼를 채우는 순수 함수들.
//
// 예전에는 멤버 vertices/indices에 직접 썼다. 출력 벡터를 인자로 받으면 Vulkan도
// 앱 상태도 보지 않게 되어, 이 프로젝트에서 유일하게 단위 테스트가 가능한 부분이 된다.

void generateSphere(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                    float radius, int sectorCount, int stackCount);
void generateRing(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                  float innerRadius, float outerRadius, int sectorCount);
void generateOrbit(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                   int segmentCount);
void loadObjModel(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                  const std::string &path,
                  uint32_t &outIndexCount, uint32_t &outFirstIndex, int32_t &outVertexOffset);
void buildSimplifiedLod(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                        uint32_t srcFirstIndex, uint32_t srcIndexCount, int32_t srcVertexOffset,
                        int gridRes,
                        uint32_t &outIndexCount, uint32_t &outFirstIndex, int32_t &outVertexOffset);
