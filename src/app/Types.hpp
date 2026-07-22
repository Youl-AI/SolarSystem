#pragma once

#include "../VulkanBase.hpp"   // Vertex, Vk* 핸들
#include "../core/Settings.hpp"
#include <glm/glm.hpp>
#include <string>

struct PushConstants {
    alignas(16) glm::mat4 model;
    int objectType;
    float customData;
};

// 소행성 LOD 컴퓨트용 푸시 상수. asteroid_cull.comp의 BeltPush와 레이아웃이 일치해야 한다.
struct AsteroidCullPush {
    alignas(16) glm::mat4 beltModel;
    uint32_t asteroidCount;
    float scaleLerp;     // 0 = 기본 축척, 1 = 실제 축척. 벨트마다 팽창 배율이 달라서 넘긴다.
};

// 한 행성계 안에서 서로 그림자를 주고받는 천체 수의 상한.
// 가장 많은 목성계도 본체 + 갈릴레이 위성 4개라 여유가 크다.
inline constexpr int MAX_OCCLUDERS = 16;

// 대기 껍질의 반지름 배율. shader.frag의 kAtmoShellScale과 반드시 같아야 한다.
inline constexpr float kAtmosphereShellScale = 1.025f;

// 그림자 계산에 쓰는 태양은 화면에 그려지는 태양보다 작다. 둘이 같아야 할 이유가 없고,
// 같게 두면 그림자가 망가진다. 이 시뮬레이션의 태양은 각크기가 실제의 수십 배라
// (목성에서 봤을 때 실제 0.051도 vs 압축 스케일 4.7도) 반그림자가 과도하게 넓어지고
// 본영 원뿔이 짧아져, 멀리 있는 위성은 본영이 모행성에 닿지도 못한다.
// 실제로는 칼리스토도 목성에 또렷한 그림자를 드리우는데 그게 사라져 있었다.
//
// 태양 반지름을 5로 나누면 갈릴레이 위성 넷의 각반지름 비가 실제값에 들어맞는다
// (칼리스토 0.29 -> 1.45, 실제 1.49). 그림자의 크기는 그대로고 경계만 제대로 조여진다.
inline constexpr float SHADOW_SUN_SHRINK = 5.0f;

// 달만 예외. 시뮬레이션이 궤도를 균일하게 압축하지 않아 하나의 계수로는 모든 쌍이 안 맞는다.
// 위 5.0을 그대로 쓰면 달의 비가 2.43이 되어 늘 깊은 개기가 되고 금환일식이 사라진다.
// 2.0이면 0.97로, 달이 태양을 '간신히' 덮는 실제 지구의 값과 같아진다.
// 개기와 금환이 둘 다 일어나는 건 이 비가 1 근처인 지구뿐이라, 이 값은 지킬 가치가 있다.
// 2.0은 비가 0.94라 금환일식만 나오고 검은 본영이 원리상 생기지 않았다(달이 태양보다
// 각도상 작아 정중앙에서도 테두리가 링으로 남는다). 2.2면 1.03이 되어 개기가 성립한다.
// 실제 지구는 궤도 이심률 때문에 이 비가 0.94~1.08을 오가며 금환과 개기가 번갈아 일어난다.
inline constexpr float SHADOW_SUN_SHRINK_MOON = 2.2f;

struct UniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
    alignas(16) glm::vec3 cameraPos;
    float time;
    alignas(16) glm::vec3 sunPos;
    float sunRadius;
    alignas(16) glm::vec3 earthPos;
    float earthRadius;
    alignas(16) glm::vec3 moonPos;
    float moonRadius;
    // 해석적 그림자용 가림 천체 목록. xyz = 카메라 상대 중심, w = 지금 렌더되는 반지름.
    // 셰도우 맵을 대체한다. 구가 구에 드리우는 그림자는 광선-구 교차로 정확히 풀리므로
    // 텍셀이라는 개념이 없어 아무리 확대해도 계단이 생기지 않는다.
    alignas(16) glm::vec4 occluders[MAX_OCCLUDERS];
    // x = 이 천체의 그림자를 계산할 때 쓸 태양 반지름. 렌더되는 태양과 일부러 다르게 둔다.
    alignas(16) glm::vec4 occluderParams[MAX_OCCLUDERS];
    int occluderCount;
};

struct Planet {
    std::string name; int typeId;
    float radius; float orbitRadius; float orbitSpeed; float eccentricity; float rotationSpeed; float axialTilt; bool hasClouds;
    
    float orbitalInclination; 
    float periapsisAngle;     
    float ascendingNode;      
    float initialAngle;       
    float axisDirection;

    float realRadius = 1.0f;
    float realOrbit = 1.0f;

    // 이 천체가 그림자를 드리울 때 쓸 태양 축소 계수(자세한 이유는 SHADOW_SUN_SHRINK 주석).
    // 값이 클수록 태양을 작게 보므로 본영이 넓어지고 반영 테두리가 얇아진다.
    // 시뮬레이션이 궤도를 균일하게 압축하지 않아 천체마다 필요한 값이 다르다.
    // 기본값은 그림자를 드리울 일이 없는 천체용이라 아무 값이나 무방하다.
    float shadowSunShrink = SHADOW_SUN_SHRINK;

    // 0보다 크면 이 천체는 대기 껍질을 그린다(지금은 지구만).
    bool hasAtmosphere = false;
    glm::mat4 atmosphereModelMat = glm::mat4(1.0f);

    // normal 맵의 접선 성분을 셰이더에서 몇 배로 증폭할지.
    //
    // 8비트 normal 맵은 x·y가 128 근처에 몰려 있어서(실측 DEM은 범위의 20%밖에 안 쓴다)
    // 셰이더에서 5배로 키우면 양자화 계단도 같이 5배가 되어 명암 경계에 띠가 생긴다.
    // 그래서 실측 DEM 맵은 구울 때 미리 S배 증폭해 8비트 범위를 채우고, 여기서 5/S를 넘긴다.
    // 중간 정규화가 세 성분을 같은 값으로 나누므로 결과 법선은 5배 증폭과 정확히 동일하다.
    float normalAmp = 5.0f;

    // 🚀 [NEW] 위성 시스템을 위한 부모 행성 인덱스 (-1이면 태양을 공전)
    int parentIndex = -1; 

    glm::mat4 currentModelMat; glm::dvec3 currentPosition; glm::mat4 cloudModelMat;

    // 이번 프레임에 실제로 그려지는 반지름(축척 전환 중에는 radius와 realRadius 사이의 보간값).
    // 카메라가 천체 안에 들어갔는지 판정할 때 radius를 쓰면 실제 크기와 어긋난다.
    float currentRenderRadius = 0.0f;
    VkImageView viewDiffuse, viewNight, viewSpecular, viewNormal, viewClouds;
    VkDescriptorSet descriptorSet;
};
