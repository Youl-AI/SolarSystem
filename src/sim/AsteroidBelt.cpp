#include "AsteroidBelt.hpp"

#include <random>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =========================================================
// 🚀 [NEW] 소행성 생성 엔진 함수들
// =========================================================
// =========================================================
// 🚀 [수정됨] 실제 천문학 기반 소행성대 궤도 생성 알고리즘
// =========================================================
// inclinationRad = 궤도 경사의 레일리 분포 척도. 실제 소행성대는 평균 10도, 카이퍼벨트는
// 12도인데 후자는 5도 미만의 얇은 무리와 20도 안팎으로 퍼진 무리가 섞인 두 갈래다.
// posScaleReal = 실제 축척으로 갈 때 위치에 곱할 배율(크기에는 곱하지 않는다).
std::vector<AsteroidData> generateAsteroidBelt(int amount, float minRadius, float maxRadius,
                                               float inclinationRad, float minScale, float maxScale,
                                               float posScaleReal) {
    std::vector<AsteroidData> result;
    result.reserve(amount);
    
    std::random_device rd; std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    std::normal_distribution<float> normalDis(0.0f, 1.0f); 

    for (int i = 0; i < amount; i++) {
        glm::mat4 model = glm::mat4(1.0f);
        // 반지름은 면적이 고르게 되도록 뽑는다(그냥 균등하게 뽑으면 안쪽이 빽빽해진다).
        float u = dis(gen);
        float radius = std::sqrt(minRadius * minRadius + u * (maxRadius * maxRadius - minRadius * minRadius));
        radius *= 1.0f + normalDis(gen) * 0.10f;              // 이심률로 흩뜨린다

        // 궤도면이 기울어 있어 생기는 높이. 궤도 경사 i와 승교점을 뽑아 기하대로 계산한다.
        // 예전에는 y를 따로 흩뜨린 뒤 전체를 한 번 더 기울여 두 번 겹쳐 넣었다.
        float angle = dis(gen) * 2.0f * M_PI;
        float node  = dis(gen) * 2.0f * M_PI;
        float inc   = inclinationRad * std::sqrt(-2.0f * std::log(std::max(dis(gen), 1e-6f)));  // 레일리 분포
        glm::vec3 pos(radius * std::cos(angle),
                      radius * std::sin(inc) * std::sin(angle - node),
                      radius * std::sin(angle));
        model = glm::translate(model, pos);

        float rotAngle = dis(gen) * 2.0f * M_PI;
        glm::vec3 rotAxis = glm::normalize(glm::vec3(dis(gen) * 2.0f - 1.0f, dis(gen) * 2.0f - 1.0f, dis(gen) * 2.0f - 1.0f));
        model = glm::rotate(model, rotAngle, rotAxis);

        // 크기는 거듭제곱 분포로 뽑는다. 실제 소행성의 크기 분포는 dN/dD ~ D^-3.5로,
        // 충돌로 잘게 부서진 결과 작은 것이 압도적으로 많고 큰 것은 드물다. 균등하게
        // 뽑으면 큰 덩어리가 실제보다 훨씬 자주 보여 자갈밭처럼 된다.
        const float q = 3.5f;
        float lo = std::pow(minScale, 1.0f - q), hi = std::pow(maxScale, 1.0f - q);
        float scale = std::pow(lo + dis(gen) * (hi - lo), 1.0f / (1.0f - q));
        model = glm::scale(model, glm::vec3(scale));
        
        // 0~3까지의 무작위 종류 부여
        int type = static_cast<int>(dis(gen) * 4);
        if (type == 4) type = 3; 

        result.push_back({model, type, posScaleReal, 0, 0});
    }
    return result;
}

