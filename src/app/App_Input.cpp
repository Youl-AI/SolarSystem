#include "SolarSystemApp.hpp"

void SolarSystemApp::onFrameStart() {
    profiler.beginFrame(glfwGetTime());
    profiler.collect(device, asteroidTransforms.size());
    profiler.nextFrame();
    // VSync 변경은 present mode만 바꾸면 되므로 스왑체인 재생성으로 처리.
    if (settings.vsync != appliedVsync) {
        appliedVsync = settings.vsync;
        desiredPresentMode = settings.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
        pendingSwapRecreate = true;
    }
    // 해상도 변경(창모드에서만 창 크기 변경; 전체화면 중이면 다음에 창모드로 돌아올 때 반영).
    if (settings.resolutionIndex != appliedResolutionIndex) {
        appliedResolutionIndex = settings.resolutionIndex;
        if (!isFullscreen) {
            GLFWmonitor* mon = glfwGetPrimaryMonitor(); const GLFWvidmode* vm = glfwGetVideoMode(mon);
            int w = kResolutions[appliedResolutionIndex][0], h = kResolutions[appliedResolutionIndex][1];
            int x = std::max(0, (vm->width - w) / 2), y = std::max(0, (vm->height - h) / 2);
            glfwSetWindowMonitor(window, nullptr, x, y, w, h, 0);
        }
        pendingSwapRecreate = true;
    }

    if (pendingSwapRecreate) {
        pendingSwapRecreate = false;
        vkDeviceWaitIdle(device);
        recreateSwapChain();
    }

    if (settings.renderScale != appliedRenderScale) {
        appliedRenderScale = settings.renderScale;
        pendingOffscreenRecreate = true;
    }
    if (pendingOffscreenRecreate) {
        pendingOffscreenRecreate = false;
        vkDeviceWaitIdle(device);
        recreateOffscreenAndBlur();
    }

    if (clampMsaaLevel(settings.msaaLevel) != msaaSamples) {
        vkDeviceWaitIdle(device);
        msaaSamples = clampMsaaLevel(settings.msaaLevel);
        recreateOffscreenForMsaa();
    }

    if (!fullscreenToggleRequested) return;
    fullscreenToggleRequested = false;

    vkDeviceWaitIdle(device);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    if (!isFullscreen) {
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowMonitor(window, nullptr, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        // 전체화면 해제: 하드코딩된 기본 크기가 아니라 선택된 해상도로 복귀한다.
        int w = kResolutions[settings.resolutionIndex][0];
        int h = kResolutions[settings.resolutionIndex][1];
        int xpos = std::max(0, (mode->width - w) / 2);
        int ypos = std::max(0, (mode->height - h) / 2);
        glfwSetWindowMonitor(window, nullptr, xpos, ypos, w, h, 0);
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
    }
    isFullscreen = !isFullscreen;

    recreateSwapChain();
}

void SolarSystemApp::throttleFrame(double frameStart) {
    if (settings.frameCap <= 0) return;
    double target = 1.0 / (double)settings.frameCap;
    double elapsed = glfwGetTime() - frameStart;
    while (elapsed < target) { // busy-wait의 대안으로 짧게 sleep
        std::this_thread::sleep_for(std::chrono::microseconds(200));
        elapsed = glfwGetTime() - frameStart;
    }
}

void SolarSystemApp::onMouseButton(int button, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            // 🚀 [NEW] Lobby 상태일 땐 레이캐스팅(행성 클릭) 및 카메라 조작을 원천 차단합니다!
            if (currentAppState == AppState::LOBBY) return; 
            
            camera.mousePressed = true;
            glfwGetCursorPos(window, &camera.lastX, &camera.lastY);
            auto now = std::chrono::high_resolution_clock::now();
            bool isDoubleClick = (std::chrono::duration<float>(now - lastClickTime).count() < 0.3f);
            handleRaycast(camera.lastX, camera.lastY, isDoubleClick);
            lastClickTime = now;
        } else if (action == GLFW_RELEASE) camera.mousePressed = false;
    }
}

void SolarSystemApp::onMouseMove(double xpos, double ypos) { 
    // 🚀 [수정] ImGui UI를 조작 중이거나, Lobby 상태일 땐 화면 회전을 막습니다.
    if (ImGui::GetIO().WantCaptureMouse || currentAppState == AppState::LOBBY) return;
    camera.processMouseDrag(xpos, ypos); 
}

void SolarSystemApp::onMouseScroll(double xoffset, double yoffset) { 
    // 🚀 [수정] ImGui UI를 조작 중이거나, Lobby 상태일 땐 줌인/줌아웃을 막습니다.
    if (ImGui::GetIO().WantCaptureMouse || currentAppState == AppState::LOBBY) return;
    camera.processMouseScroll(yoffset); 
}

void SolarSystemApp::handleRaycast(double xpos, double ypos, bool isDoubleClick) {
    float ndcX = (2.0f * (float)xpos) / swapChainExtent.width - 1.0f;
    float ndcY = (2.0f * (float)ypos) / swapChainExtent.height - 1.0f;
    glm::mat4 proj = glm::perspective(glm::radians(camera.fov), swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 1000000.0f);
    proj[1][1] *= -1;
    glm::mat4 view = camera.getViewMatrix();
    
    float minT = FLT_MAX; int hitType = -1; int hitIndex = -1;

    // 🚀 [추가 1] 2D 네임태그(UI 박스) 클릭 검사 함수
    auto check2DBoxHit = [&](glm::vec3 worldPos, const std::string& name, int type, int idx, float yOffset) {
        glm::vec2 sc;
        if (worldToScreen(worldPos, view, proj, swapChainExtent.width, swapChainExtent.height, sc)) {
            // 🚀 [수정됨] ImGui 폰트 측정기로 정확한 텍스트 픽셀 크기 계산
            ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
            float w = textSize.x + 16.0f; // 좌우 패딩 포함
            float h = textSize.y + 8.0f;  // 상하 패딩 포함
            
            // 중앙 정렬을 위해 기준점(X)을 전체 길이의 절반만큼 왼쪽으로 뺍니다.
            float bx = sc.x - (w * 0.5f); 
            float by = sc.y - yOffset;
            
            if (xpos >= bx && xpos <= bx + w && ypos >= by && ypos <= by + h) {
                hitType = type; hitIndex = idx; return true;
            }
        }
        return false;
    };

    bool tagHit = false;
    if (check2DBoxHit(relativeToCamera(sun.currentPosition), "Sun", 0, -1, 35.0f)) tagHit = true;
    if (!tagHit) {
        for (int i = 0; i < planets.size(); i++) {
            if (check2DBoxHit(relativeToCamera(planets[i].currentPosition), planets[i].name, 1, i, 35.0f)) { tagHit = true; break; }
        }
    }
    if (!tagHit) {
        if (check2DBoxHit(relativeToCamera(moon.currentPosition), "Moon", 2, -1, 25.0f)) tagHit = true;
    }

    // 🚀 [추가 2] 2D 태그를 못 눌렀다면 기존처럼 3D 천체 본체 레이캐스팅 진행
    if (!tagHit) {
        glm::vec4 eyeCoords = glm::inverse(proj) * glm::vec4(ndcX, ndcY, 0.5f, 1.0f);
        glm::vec3 rayWorldDir = glm::normalize(glm::vec3(glm::inverse(view) * glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f)));
        glm::vec3 rayOrigin = relativeToCamera(camera.getEyeWorld());

        float easeScale = scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp);
        float curSunRad = glm::mix(sun.radius, sun.realRadius, easeScale);
        float curMoonRad = glm::mix(moon.radius, moon.realRadius, easeScale);

        auto checkIntersect = [&](glm::vec3 center, float radius, int type, int idx) {
            glm::vec3 oc = rayOrigin - center;
            float a = glm::dot(rayWorldDir, rayWorldDir);
            float b = 2.0f * glm::dot(oc, rayWorldDir);
            float c = glm::dot(oc, oc) - radius * radius;
            float discriminant = b * b - 4 * a * c;
            if (discriminant > 0) {
                float t = (-b - sqrt(discriminant)) / (2.0f * a);
                if (t > 0 && t < minT) { minT = t; hitType = type; hitIndex = idx; }
            }
        };

        checkIntersect(relativeToCamera(sun.currentPosition), curSunRad, 0, -1);
        for (int i = 0; i < planets.size(); i++) {
            float curPlanetRad = glm::mix(planets[i].radius, planets[i].realRadius, easeScale);
            checkIntersect(relativeToCamera(planets[i].currentPosition), curPlanetRad, 1, i);
        }
        checkIntersect(relativeToCamera(moon.currentPosition), curMoonRad, 2, -1);
    }

    // 🚀 [추가 3] 단일 클릭(선택 정보창 띄우기) & 더블 클릭(시점 고정) 분리 처리
    if (hitType != -1) {
        selectedTargetType = hitType; 
        selectedPlanetIndex = hitIndex;
    }

    if (hitType != -1 && isDoubleClick) { 
        lockedTargetType = hitType; 
        lockedPlanetIndex = hitIndex; 
        
        float easeScale = scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp);
        float hitRadius = (hitType == 0 ? glm::mix(sun.radius, sun.realRadius, easeScale) : (hitType == 1 ? glm::mix(planets[hitIndex].radius, planets[hitIndex].realRadius, easeScale) : glm::mix(moon.radius, moon.realRadius, easeScale)));
        camera.targetDistance = hitRadius * 6.0f; 
    }
}
