#pragma once

#include <glm/glm.hpp>
#include <vector>

struct AsteroidData {
    glm::mat4 transform;
    int type;
    // 실제 축척으로 갈 때 이 소행성의 '위치'에 곱할 배율. 크기에는 곱하지 않는다.
    //
    // 예전에는 벨트 전체에 200배 스케일 하나를 곱해 위치와 크기를 같이 늘렸다. 그러면
    // 실제 축척에서 소행성 지름이 4만~13만 km가 되어 지구(12,742 km)보다 커진다.
    // 게다가 배율이 하나뿐이라 두 벨트를 각자 제 위치에 놓을 수 없었다 — 소행성대를
    // 맞추면 카이퍼벨트가 딸려 들어온다. 배율을 소행성마다 들려 보내 둘 다 푼다.
    float posScaleReal;
    int pad2, pad3;
};

std::vector<AsteroidData> generateAsteroidBelt(int amount, float minRadius, float maxRadius,
                                               float inclinationRad, float minScale, float maxScale,
                                               float posScaleReal);
