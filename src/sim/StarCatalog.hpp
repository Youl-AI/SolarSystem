#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

// 카탈로그 한 개 별. id는 stars.csv의 전역 정수 id(0-based, HIP 번호 아님).
// dir은 raDecToDir로 구한 단위 방향 벡터. 등급 열 없음(소스 미제공, 미사용).
struct CatalogStar {
    int id;
    glm::vec3 dir;
};

// 별자리 하나. edges/adjacency는 모두 stars_ 전역 인덱스(= stars() 벡터 인덱스) 기준이지,
// CSV의 id 값이 아니다.
struct Constellation {
    std::string abbr;                                     // "Ori"
    std::string latin;                                    // "Orion"
    std::vector<glm::ivec2> edges;                         // stars_ 전역 인덱스 쌍
    std::unordered_map<int, std::vector<int>> adjacency;   // 전역 stars_ 인덱스 -> 이웃 전역 인덱스들
    glm::vec3 centroidDir;                                 // 이름 라벨용 평균 방향(정규화)
};

// data/constellations/{stars,lines,names}.csv를 읽어 별자리 그래프를 구성하는 순수 데이터 모듈.
// Vulkan/렌더링 의존성 없음.
class StarCatalog {
public:
    bool load(const std::string &dir);      // dir/stars.csv 등 세 파일

    const std::vector<CatalogStar>&   stars() const { return stars_; }
    const std::vector<Constellation>& constellations() const { return constellations_; }

    static glm::vec3 raDecToDir(double raDeg, double decDeg);

    // rayDir(단위)에 각도상 가장 가까운 별을 찾는다. maxAngleRad 안에서만.
    // 반환: 별자리 인덱스. 없으면 -1. outStarIndex = 그 별의 stars_ 인덱스.
    int pickNearest(const glm::vec3 &rayDir, float maxAngleRad, int &outStarIndex) const;

private:
    std::vector<CatalogStar> stars_;
    std::vector<Constellation> constellations_;
    std::unordered_map<int, int> idToIndex_;   // CSV id -> stars_ 인덱스
};
