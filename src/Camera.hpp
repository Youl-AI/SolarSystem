#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

class Camera
{
public:
    glm::vec3 target = glm::vec3(5.0f, 0.0f, 0.0f);
    glm::vec3 pos = glm::vec3(0.0f, 0.0f, 1.0f); 

    // =========================================================
    // 🚀 [NEW] 턴테이블 카메라의 핵심: 절대 회전각(Yaw, Pitch) 유지
    // =========================================================
    float yaw = 90.0f;   // 좌우 회전각
    float pitch = 20.0f; // 상하 회전각

    float targetDistance = 1.2f;
    float currentDistance = 1.2f;
    
    float minDistance = 0.2f; 
    float fov = 45.0f; 

    // 🚀 [NEW] 우주의 절대적인 위쪽은 항상 Y축으로 쾅! 고정합니다. (롤 차단)
    const glm::vec3 WORLD_UP = glm::vec3(0.0f, 1.0f, 0.0f);

    bool mousePressed = false;
    double lastX = 0.0;
    double lastY = 0.0;

    glm::vec3 lastNewTarget = glm::vec3(5.0f, 0.0f, 0.0f);

    void processMouseDrag(double xpos, double ypos)
    {
        if (!mousePressed) return;
        double deltaX = xpos - lastX; 
        double deltaY = ypos - lastY;
        lastX = xpos; lastY = ypos;

        // 1. 마우스 이동량에 따라 Yaw(좌우)와 Pitch(상하) 각도만 조작합니다.
        // (감도를 0.2f로 설정하여 너무 휙휙 돌아가지 않도록 안정감을 더했습니다)
        yaw += static_cast<float>(deltaX) * 0.2f; 
        pitch += static_cast<float>(deltaY) * 0.2f; 

        // 2. 화면이 뒤집히는 것(Gimbal Lock)을 막기 위해 상하 각도를 89도로 엄격히 제한합니다.
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
    }

    void processMouseScroll(double yoffset)
    {
        if (yoffset > 0) { 
            if (targetDistance <= minDistance + 0.001f) {
                fov -= 3.0f;
                if (fov < 2.0f) fov = 2.0f; 
            } else {
                targetDistance -= static_cast<float>(yoffset * targetDistance * 0.15f);
                if (targetDistance < minDistance) targetDistance = minDistance;
            }
        } else { 
            if (fov < 45.0f) {
                fov += 3.0f;
                if (fov > 45.0f) fov = 45.0f;
            } else {
                targetDistance -= static_cast<float>(yoffset * targetDistance * 0.15f);
                if (targetDistance > 1000000.0f) targetDistance = 1000000.0f;
            }
        }
    }

    // targetChanged: 다른 천체로 갈아탄 프레임인지 여부.
    // 갈아탄 순간에 속도 피드포워드를 적용하면 두 천체의 거리만큼 타겟이 순간이동해 버리므로,
    // 그 프레임만 피드포워드를 건너뛰고 아래 보간이 부드럽게 날아가도록 맡긴다.
    void smoothFollow(glm::vec3 newTarget, float deltaTime, bool targetChanged = false)
    {
        if (targetChanged) {
            lastNewTarget = newTarget;
        } else {
            // 공전 중인 천체의 이번 프레임 이동량을 그대로 상쇄해 카메라를 붙여 놓는다.
            // (이게 없으면 보간만으로는 움직이는 천체를 영원히 따라잡지 못하고 속도/5 만큼 뒤처진다)
            glm::vec3 targetVelocity = newTarget - lastNewTarget;
            lastNewTarget = newTarget;
            target += targetVelocity;
        }

        float lerpFactor = 5.0f * deltaTime;
        target = glm::mix(target, newTarget, std::clamp(lerpFactor, 0.0f, 1.0f));
    }

    void update(float deltaTime)
    {
        currentDistance = glm::mix(currentDistance, targetDistance, 5.0f * deltaTime);
        
        // =========================================================
        // 🚀 [NEW] 턴테이블 수학 (구면 좌표계 -> 데카르트 좌표계 변환)
        // =========================================================
        // 매 프레임마다 이전 프레임의 오프셋을 갖다 쓰는 것이 아니라, 
        // 완벽한 구면 좌표계 공식을 통해 타겟 주변의 정확한 궤도 위치를 새로 계산해 냅니다.
        glm::vec3 newPos;
        newPos.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        newPos.y = sin(glm::radians(pitch));
        newPos.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        
        // 계산된 방향에 거리(반지름)를 곱해 최종 오프셋 적용
        pos = glm::normalize(newPos) * currentDistance;
    }

    glm::mat4 getViewMatrix() const
    {
        // =========================================================
        // 🚀 [NEW] 기존의 가변 up 벡터를 지우고, 절대 고정된 WORLD_UP을 사용합니다!
        // =========================================================
        return glm::lookAt(target + pos, target, WORLD_UP);
    }
};