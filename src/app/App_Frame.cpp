#include "SolarSystemApp.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 천체들의 위치가 확정되고 카메라까지 갱신된 뒤에 호출된다.
// 모델 행렬은 카메라 상대 좌표를 쓰므로 반드시 이번 프레임의 camera.target 확정 후여야 한다.
void SolarSystemApp::buildBodyModelMatrices(float easeScale, float slowTime, float time) {
    float curSunRadius = glm::mix(sun.radius, sun.realRadius, easeScale);
    sun.currentModelMat = glm::translate(glm::mat4(1.0f), relativeToCamera(sun.currentPosition))
                        * glm::rotate(glm::mat4(1.0f), glm::radians(sun.axisDirection), glm::vec3(0.0f, 1.0f, 0.0f))
                        * glm::rotate(glm::mat4(1.0f), glm::radians(sun.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f))
                        * glm::rotate(glm::mat4(1.0f), time * glm::radians(sun.rotationSpeed), glm::vec3(0.0f, 1.0f, 0.0f))
                        * glm::scale(glm::mat4(1.0f), glm::vec3(curSunRadius));

    for (int i = 0; i < planets.size(); i++) {
        auto &planet = planets[i];
        float curRadius = glm::mix(planet.radius, planet.realRadius, easeScale);
        planet.currentRenderRadius = curRadius;

        glm::mat4 trajectory = glm::translate(glm::mat4(1.0f), relativeToCamera(planet.currentPosition));

        glm::mat4 selfRotationMat = glm::rotate(glm::mat4(1.0f), glm::radians(planet.axisDirection), glm::vec3(0.0f, 1.0f, 0.0f))
                                  * glm::rotate(glm::mat4(1.0f), glm::radians(planet.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f))
                                  * glm::rotate(glm::mat4(1.0f), slowTime * glm::radians(planet.rotationSpeed), glm::vec3(0.0f, 1.0f, 0.0f));

        planet.currentModelMat = trajectory * selfRotationMat * glm::scale(glm::mat4(1.0f), glm::vec3(curRadius));

        if (planet.hasClouds) {
            glm::mat4 cloudSelfRot = glm::rotate(glm::mat4(1.0f), glm::radians(planet.axisDirection), glm::vec3(0.0f, 1.0f, 0.0f))
                                   * glm::rotate(glm::mat4(1.0f), glm::radians(planet.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f))
                                   * glm::rotate(glm::mat4(1.0f), slowTime * glm::radians(planet.rotationSpeed + 4.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            planet.cloudModelMat = trajectory * cloudSelfRot * glm::scale(glm::mat4(1.0f), glm::vec3(curRadius * 1.005f));
        }
        if (planet.hasAtmosphere) {
            // 완전한 구라 자전이 보이지 않는다. 위치와 크기만 있으면 충분하다.
            planet.atmosphereModelMat = trajectory * glm::scale(glm::mat4(1.0f), glm::vec3(curRadius * kAtmosphereShellScale));
        }
    }

    float curMoonRadius = glm::mix(moon.radius, moon.realRadius, easeScale);
    moon.currentRenderRadius = curMoonRadius;
    glm::mat4 moonBase = glm::translate(glm::mat4(1.0f), relativeToCamera(moon.currentPosition));
    glm::mat4 moonRot = glm::rotate(glm::mat4(1.0f), glm::radians(moon.axisDirection), glm::vec3(0.0f, 1.0f, 0.0f))
                      * glm::rotate(glm::mat4(1.0f), glm::radians(moon.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f))
                      * glm::rotate(glm::mat4(1.0f), slowTime * glm::radians(moon.rotationSpeed), glm::vec3(0.0f, 1.0f, 0.0f));
    moon.currentModelMat = moonBase * moonRot * glm::scale(glm::mat4(1.0f), glm::vec3(curMoonRadius));
}

// 시간을 한 프레임 전진시킨다. deltaTime을 돌려주는 이유는 아래 단계들과 UI가
// 같은 값을 써야 하기 때문이다.
float SolarSystemApp::advanceSimulationTime() {
    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTimePoint).count();
    lastTimePoint = currentTime;

    bool spacePressed = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (spacePressed && !spacePressedLastFrame) isPaused = !isPaused;
    spacePressedLastFrame = spacePressed;
    if (!isPaused) currentAppTime += deltaTime;
    float time = currentAppTime;

    if (!isSimInit) { simulationTime = time * 0.5f; isSimInit = true; } // 첫 프레임 동기화

    if (isRealScaleMode) scaleLerp += deltaTime * 0.5f;
    else scaleLerp -= deltaTime * 0.5f;
    scaleLerp = std::clamp(scaleLerp, 0.0f, 1.0f);

    // 하늘도 같은 방식으로 서서히 넘긴다. 한 프레임에 바꾸면 별이 우수수 사라져 튄다.
    // 축척보다 조금 느리게(0.4) 가야 별이 하나씩 꺼지는 느낌이 난다.
    starsLerp += (settings.realStars ? 1.0f : -1.0f) * deltaTime * 0.4f;
    starsLerp = std::clamp(starsLerp, 0.0f, 1.0f);

    // 체험 버튼(isEclipseEvent)이 눌리지 않았을 때만 시간이 흐릅니다!
    if (!isEclipseEvent && !isPaused) {
        simulationTime += deltaTime * 0.5f;
    }

    return deltaTime;
}

// 🚀 1. 태양(Sun)은 월드 원점에 고정. 모델 행렬은 buildBodyModelMatrices()에서 만든다.
// 🚀 2. 행성 및 위성의 '위치'만 double로 계산한다.
//    모델 행렬은 카메라 업데이트가 끝난 뒤 buildBodyModelMatrices()에서 만든다.
//    (카메라 상대 좌표를 쓰려면 모델 행렬이 이번 프레임의 camera.target을 봐야 하기 때문)
void SolarSystemApp::updateBodyPositions(float slowTime, float easeScale) {
    sun.currentPosition = glm::dvec3(0.0);

    for (int i = 0; i < planets.size(); i++) {
        auto &planet = planets[i];

        double curOrbit = glm::mix((double)planet.orbitRadius, (double)planet.realOrbit, (double)easeScale);

        double a = curOrbit; double e = planet.eccentricity; double b = a * sqrt(1.0 - e * e); double c = a * e;

        double angle = glm::radians((double)planet.initialAngle) + (double)slowTime * glm::radians((double)planet.orbitSpeed);
        glm::dvec3 localPos = glm::dvec3(a * cos(angle) - c, 0.0, b * sin(angle));

        glm::dmat4 orbitTilt = glm::rotate(glm::dmat4(1.0), glm::radians((double)planet.ascendingNode), glm::dvec3(0.0, 1.0, 0.0))
                             * glm::rotate(glm::dmat4(1.0), glm::radians((double)planet.orbitalInclination), glm::dvec3(0.0, 0.0, 1.0))
                             * glm::rotate(glm::dmat4(1.0), glm::radians((double)planet.periapsisAngle), glm::dvec3(0.0, 1.0, 0.0));

        glm::dvec3 parentPos = glm::dvec3(0.0);
        if (planet.parentIndex != -1) {
            parentPos = planets[planet.parentIndex].currentPosition;
        }

        planet.currentPosition = parentPos + glm::dvec3(orbitTilt * glm::dvec4(localPos, 1.0));

        // 달은 지구(index 2)를 부모로 삼아 위치를 잡는다.
        if (i == 2) {
            double curMoonOrbit = glm::mix((double)moon.orbitRadius, (double)moon.realOrbit, (double)easeScale);

            double ma = curMoonOrbit, me = moon.eccentricity, mb = ma * sqrt(1.0 - me * me), mc = ma * me;
            double mAngle = glm::radians((double)moon.initialAngle) + (double)slowTime * glm::radians((double)moon.orbitSpeed);
            glm::dvec3 mLocal = glm::dvec3(ma * cos(mAngle) - mc, 0.0, mb * sin(mAngle));

            glm::dmat4 mOrbitTilt = glm::rotate(glm::dmat4(1.0), glm::radians((double)moon.ascendingNode), glm::dvec3(0.0, 1.0, 0.0))
                                  * glm::rotate(glm::dmat4(1.0), glm::radians((double)moon.orbitalInclination), glm::dvec3(0.0, 0.0, 1.0))
                                  * glm::rotate(glm::dmat4(1.0), glm::radians((double)moon.periapsisAngle), glm::dvec3(0.0, 1.0, 0.0));

            moon.currentPosition = planet.currentPosition + glm::dvec3(mOrbitTilt * glm::dvec4(mLocal, 1.0));
        }
    }
}

// =========================================================
// 카메라 추적 업데이트
// =========================================================
void SolarSystemApp::updateCameraTracking(float deltaTime, float easeScale) {
    glm::dvec3 nextTarget = camera.target;
    float targetRadius = 1.0f;

    if (lockedTargetType == 0) {
        nextTarget = sun.currentPosition;
        targetRadius = glm::mix(sun.radius, sun.realRadius, easeScale);
    }
    else if (lockedTargetType == 1 && lockedPlanetIndex != -1) {
        nextTarget = planets[lockedPlanetIndex].currentPosition;
        targetRadius = glm::mix(planets[lockedPlanetIndex].radius, planets[lockedPlanetIndex].realRadius, easeScale);
    }
    else if (lockedTargetType == 2) {
        nextTarget = moon.currentPosition;
        targetRadius = glm::mix(moon.radius, moon.realRadius, easeScale);
    }

    if (isFirstFrame) {
        camera.target = nextTarget;
        camera.lastNewTarget = nextTarget;
        camera.targetDistance = targetRadius * 6.0f;
        camera.currentDistance = targetRadius * 6.0f;

        // 프레임 1에서 last* 를 현재 값으로 시딩한다. 예전에는 이 세 개가 함수 지역
        // static이라 첫 실행에서 targetRadius/lockedTargetType/lockedPlanetIndex를 캡처했다.
        // 멤버로 올리면 그 캡처가 사라져, lastTargetRadius가 targetRadius와 달라지는 순간
        // 아래 비율 보정 블록이 프레임 1에 돌아 카메라가 튄다. 여기서 같은 값으로 맞춰 준다.
        lastTargetRadius = targetRadius;
        lastTargetType   = lockedTargetType;
        lastTargetIndex  = lockedPlanetIndex;

        isFirstFrame = false;
    }

    // 🚀 [NEW] 천체의 팽창/수축 배율을 프레임 단위로 실시간 추적하여 카메라에 완벽 동기화!
    bool targetChanged = (lastTargetType != lockedTargetType || lastTargetIndex != lockedPlanetIndex);

    // 타겟이 바뀌지 않았을 때만 비율을 곱해줍니다. (다른 행성을 더블클릭할 때 화면이 확 튀는 현상 방지)
    if (!targetChanged) {
        if (!isFirstFrame && lastTargetRadius > 0.0f && lastTargetRadius != targetRadius) {
            float frameRatio = targetRadius / lastTargetRadius; // 이번 프레임에서 천체가 얼마나 커졌는가?

            camera.targetDistance *= frameRatio;  // 목표 줌 거리를 비율에 맞게 조절
            camera.currentDistance *= frameRatio; // 🚀 핵심: 카메라 현재 거리도 즉시 밀려나게 해서 시점 지연(Lag) 완벽 방지
        }
    }

    lastTargetRadius = targetRadius;
    lastTargetType = lockedTargetType;
    lastTargetIndex = lockedPlanetIndex;

    // 🚀 타겟이 누구냐에 따라 카메라 방어막(최소 거리)을 다르게 설정합니다.
    if (lockedTargetType == 0) {
        camera.minDistance = targetRadius * 2.8f;
    } else {
        camera.minDistance = targetRadius * 1.2f;
    }

    if (camera.targetDistance < camera.minDistance) {
        camera.targetDistance = camera.minDistance;
    }

    camera.smoothFollow(nextTarget, deltaTime, targetChanged);
    // 로비(대기화면)에서는 카메라를 아주 천천히 자동 선회시켜 시네마틱한 배경 연출을 준다.
    // (로비에선 마우스 조작이 비활성이라 사용자 입력과 충돌하지 않는다.)
    if (currentAppState == AppState::LOBBY) camera.yaw += deltaTime * 4.0f;
    camera.update(deltaTime);
    // FOV 설정을 바꾸면 현재 화면에 즉시 반영한다. 설정을 바꾼 순간에는 텔레스코프 줌을 취소하고
    // 새 기본 시야각으로 스냅한다(스펙: "현재 줌 상태를 그에 맞춰 재계산"). 그 외 프레임에는
    // 아래로만 클램프해, 사용자가 스크롤로 줌인해 둔 상태(fov < baseFov)는 유지한다.
    camera.baseFov = settings.fovDegrees;
    if (settings.fovDegrees != appliedFov) {
        appliedFov = settings.fovDegrees;
        camera.fov = settings.fovDegrees;
    } else if (camera.fov > camera.baseFov) {
        camera.fov = camera.baseFov;
    }
}

// =========================================================
// 🚀 시네마틱 개기일식 (궤도 고정 & 감속 정지 연출)
// =========================================================
void SolarSystemApp::applyEclipseCinematic(float deltaTime, float easeScale) {
    // 1.5초 만에 카메라가 스무스하게 빨려 들어갑니다.
    if (isEclipseEvent) eclipseLerp += deltaTime * 0.75f;
    else eclipseLerp -= deltaTime * 0.75f;
    eclipseLerp = std::clamp(eclipseLerp, 0.0f, 1.0f);

    if (eclipseLerp > 0.0f) {
        // 1. 타겟을 강제로 달(Moon)로 고정합니다. (더블클릭 효과)
        lockedTargetType = 2;

        glm::vec3 sunPos = glm::vec3(sun.currentPosition);
        glm::vec3 moonPos = glm::vec3(moon.currentPosition);

        // 2. 카메라 -> 달 -> 태양이 일직선이 되는 완벽한 방향 벡터 계산
        glm::vec3 dirFromMoonToSun = glm::normalize(sunPos - moonPos);

        // 3. 카메라-달 거리를 "달의 각크기 == 태양의 각크기"가 되도록 계산한다.
        //    실제 개기일식처럼 달이 태양을 딱 맞게 가리도록. (기존엔 달 반지름 기준 고정이라
        //    real scale에서 달이 태양보다 몇 배 커져 과하게 가렸다.)
        //    달 각크기 = moonR / camDist, 태양 각크기 ≈ sunR / dist(moon,sun) →
        //    camDist = moonR * dist(moon,sun) / sunR.
        float eScale = scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp);
        float moonR = glm::mix(moon.radius, moon.realRadius, eScale);
        float sunR  = glm::mix(sun.radius,  sun.realRadius,  eScale);
        float moonSunDist = (float)glm::distance(moon.currentPosition, sun.currentPosition);
        float targetZoom = moonR * moonSunDist / sunR * 0.97f; // 0.97 → 달을 아주 살짝 크게 해 완전히 가림
        targetZoom = std::max(targetZoom, moonR + 0.05f);      // 달 표면에 파묻히지 않도록 최소 여백

        // =========================================================
        // 🚀 [핵심 수정] perfectCamPos를 perfectCamOffset으로 변경!
        // 카메라 pos는 타겟(달)으로부터의 '상대적인 거리와 방향'만 필요하므로
        // moonPos를 더할 필요 없이 오프셋 방향 벡터만 계산합니다.
        // =========================================================
        glm::vec3 perfectCamOffset = -dirFromMoonToSun * targetZoom;

        // 5. 현재 카메라 상태에서 완벽한 일식 뷰 상태로 부드럽게 섞어줍니다(Lerp).
        float ease = eclipseLerp * eclipseLerp * (3.0f - 2.0f * eclipseLerp);

        camera.target = glm::mix(camera.target, glm::dvec3(moonPos), (double)ease);

        // 절대 좌표가 아닌 오프셋(perfectCamOffset)을 덮어씌웁니다.
        camera.pos = glm::mix(camera.pos, perfectCamOffset, ease);

        // 줌이 뒤로 튕겨나가지 않도록 목표 거리(targetDistance)까지 완벽하게 강제 고정!
        camera.targetDistance = glm::mix(camera.targetDistance, targetZoom, ease);
        camera.currentDistance = glm::mix(camera.currentDistance, targetZoom, ease);
    }
}

// ── 그림자를 주고받을 '계(系)'를 정하고, 해석적 그림자에 넘길 가림 천체들을 모은다 ──
// 태양의 각반지름이 반그림자 폭을 결정하므로, ubo.sunPos/sunRadius도 여기서 지금 실제로
// 렌더되는 크기로 채운다(예전엔 sun.radius 고정이라 리얼 스케일에서 그림자 계산이 어긋났다).
// 그래서 easeScale 하나만 받으면 충분하다.
void SolarSystemApp::collectOccluders(UniformBufferObject &ubo, float easeScale) {
    // 태양의 각반지름이 반그림자 폭을 결정하므로, 지금 실제로 렌더되는 크기를 써야 한다.
    // (예전엔 sun.radius 고정이라 리얼 스케일에서 그림자 계산이 어긋났다)
    ubo.sunPos = relativeToCamera(sun.currentPosition);
    ubo.sunRadius = glm::mix(sun.radius, sun.realRadius, easeScale);

    // ── 그림자를 주고받을 '계(系)'를 정한다 ──────────────────────────────
    // 잠근 대상이 위성이면 모행성계로 올라가고, 행성이면 자기 위성들을 모은다.
    shadowSystemIndices.clear();
    shadowSystemIncludesEarthMoon = false;
    int primary = 2; // 기본값: 지구(태양에 고정됐거나 대상이 없을 때)
    if (lockedTargetType == 1 && lockedPlanetIndex >= 0 && lockedPlanetIndex < (int)planets.size()) {
        primary = planets[lockedPlanetIndex].parentIndex;
        if (primary == -1) primary = lockedPlanetIndex; // 이미 행성이다
    }
    shadowSystemIndices.push_back(primary);
    for (int i = 0; i < (int)planets.size(); ++i)
        if (planets[i].parentIndex == primary) shadowSystemIndices.push_back(i);
    if (primary == 2) shadowSystemIncludesEarthMoon = true; // 달은 planets 밖에 따로 있다

    // ── 해석적 그림자에 넘길 가림 천체들 ─────────────────────────────────
    // 위에서 고른 '계'를 그대로 쓴다. 행성끼리는 실제로 그림자를 드리우지 않으므로
    // 목록에 넣지 않는다(보기 좋으라고 궤도를 압축해 둔 탓에 생기는 가짜 그림자를 막는다).
    // 반지름은 리얼 스케일 전환 중에도 렌더되는 크기를 그대로 따라가도록 mix를 쓴다.
    ubo.occluderCount = 0;
    auto addOccluder = [&](const glm::dvec3 &pos, float r, float sunShrink) {
        if (ubo.occluderCount >= MAX_OCCLUDERS) return;
        ubo.occluders[ubo.occluderCount] = glm::vec4(relativeToCamera(pos), r);
        ubo.occluderParams[ubo.occluderCount] = glm::vec4(ubo.sunRadius / sunShrink, 0.0f, 0.0f, 0.0f);
        ubo.occluderCount++;
    };
    for (int i : shadowSystemIndices)
        addOccluder(planets[i].currentPosition,
                    glm::mix(planets[i].radius, planets[i].realRadius, easeScale),
                    planets[i].shadowSunShrink);
    if (shadowSystemIncludesEarthMoon)
        addOccluder(moon.currentPosition,
                    glm::mix(moon.radius, moon.realRadius, easeScale), moon.shadowSunShrink);

    // 여기는 펜스 대기 뒤라 직전 프레임의 컴퓨트 결과가 확정돼 있다. 0으로 리셋하기
    // 직전에 스냅샷을 떠 둔다(onFrameStart에서 읽으면 아직 GPU가 채우기 전이라 0이 나온다).
    profiler.snapshotDrawCmds(indirectDrawMapped);   // 오버레이의 LOD 표시도 이 스냅샷을 쓴다

    memcpy(indirectDrawMapped, drawCmdTemplates, 12 * sizeof(VkDrawIndexedIndirectCommand));
}

void SolarSystemApp::uploadUbo(UniformBufferObject &ubo, float time) {
    ubo.view = camera.getViewMatrix();
    ubo.proj = glm::perspective(glm::radians(camera.fov), swapChainExtent.width / (float)swapChainExtent.height, 0.01f, 1000000.0f);
    ubo.proj[1][1] *= -1;
    ubo.cameraPos = relativeToCamera(camera.getEyeWorld());
    ubo.time = time;
    ubo.earthPos = relativeToCamera(planets[2].currentPosition); ubo.earthRadius = planets[2].radius;
    ubo.moonPos = relativeToCamera(moon.currentPosition); ubo.moonRadius = moon.radius;

    memcpy(uniformBufferMapped, &ubo, sizeof(ubo));
}

void SolarSystemApp::updateUniformBuffer() {
    float deltaTime = advanceSimulationTime();
    // 자연스럽게 출발하고 도착하는 Smoothstep 곡선 공식 적용
    float easeScale = scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp);
    float slowTime  = simulationTime;
    float time      = currentAppTime;

    updateBodyPositions(slowTime, easeScale);
    updateCameraTracking(deltaTime, easeScale);
    applyEclipseCinematic(deltaTime, easeScale);

    // 반드시 일식 블록 '뒤'여야 한다. 저 블록이 camera.target을 한 번 더 덮어쓰기 때문에,
    // 여기보다 위에서 모델 행렬을 만들면 모델은 일식 전 타겟을, 뷰 행렬은 일식 후 타겟을
    // 보게 되어 카메라 상대 좌표계에서 천체가 통째로 어긋난다.
    buildBodyModelMatrices(easeScale, slowTime, time);
    updateLockedOrbitLine(easeScale, slowTime);

    UniformBufferObject ubo{};
    collectOccluders(ubo, easeScale);
    uploadUbo(ubo, time);

    drawUi();
}

// 고정된 천체의 궤도를 double로 계산해 카메라 상대 좌표 정점으로 굽는다.
// 궤도 자체는 부모(태양 또는 모행성) 기준이므로 부모 위치를 더해 월드 좌표를 만든 뒤 상대화한다.
void SolarSystemApp::updateLockedOrbitLine(float easeScale, float slowTime) {
    lockedOrbitValid = false;
    // 궤도선이 꺼져 있으면 결과를 그릴 곳이 없다. 아래 루프는 16384개 점을 double로
    // 계산해 매 프레임 GPU에 올리므로, 여기서 빠져나가는 것만으로 큰 낭비가 사라진다.
    if (!settings.orbitLines) return;
    if (lockedTargetType != 1 || lockedPlanetIndex < 0 || lockedPlanetIndex >= (int)planets.size()) return;

    const Planet& p = planets[lockedPlanetIndex];

    double a = glm::mix((double)p.orbitRadius, (double)p.realOrbit, (double)easeScale);
    double e = p.eccentricity;
    double b = a * sqrt(1.0 - e * e);
    double c = a * e;

    glm::dmat4 tilt = glm::rotate(glm::dmat4(1.0), glm::radians((double)p.ascendingNode), glm::dvec3(0.0, 1.0, 0.0))
                    * glm::rotate(glm::dmat4(1.0), glm::radians((double)p.orbitalInclination), glm::dvec3(0.0, 0.0, 1.0))
                    * glm::rotate(glm::dmat4(1.0), glm::radians((double)p.periapsisAngle), glm::dvec3(0.0, 1.0, 0.0));

    glm::dvec3 parentPos = glm::dvec3(0.0);
    if (p.parentIndex != -1) parentPos = planets[p.parentIndex].currentPosition;

    // 위상 정렬: 샘플 격자를 행성의 현재 궤도각(physics update와 동일한 식)에 맞춰 회전시켜,
    // 정점 0이 행성 위치(=카메라 초점)에 bit-exact로 오도록 한다. 정점이 매 프레임 행성과 함께
    // 움직이므로, 카메라가 고정 정점들 사이를 미끄러지며 생기던 근거리 호의 아른거림이 사라진다.
    double phase = glm::radians((double)p.initialAngle) + (double)slowTime * glm::radians((double)p.orbitSpeed);

    lockedOrbitScratch.resize(LOCKED_ORBIT_SEGMENTS + 1);
    for (int i = 0; i <= LOCKED_ORBIT_SEGMENTS; ++i) {
        double ang = phase + (double)i / (double)LOCKED_ORBIT_SEGMENTS * 2.0 * M_PI;
        glm::dvec3 local = glm::dvec3(a * cos(ang) - c, 0.0, b * sin(ang));
        glm::dvec3 world = parentPos + glm::dvec3(tilt * glm::dvec4(local, 1.0));
        lockedOrbitScratch[i] = {relativeToCamera(world), glm::vec3(0.2f, 0.4f, 0.0f), glm::vec2(0.0f), glm::vec3(0, 1, 0)};
    }

    memcpy(lockedOrbitMapped, lockedOrbitScratch.data(), sizeof(Vertex) * lockedOrbitScratch.size());
    lockedOrbitValid = true;
}
