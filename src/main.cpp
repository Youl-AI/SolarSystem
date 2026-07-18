#include "VulkanBase.hpp"
#include "Camera.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 0       // miniz.h와 miniz.c를 찾지 마!
#define TINYEXR_USE_STB_ZLIB 1    // 대신 이미 있는 stb_image의 zlib 코드를 사용해!
#include "tinyexr.h"

// [NEW] OBJ 로더 및 난수 생성 라이브러리 추가
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include <random>

#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <fstream>
#include <string>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
#include <windows.h>
#endif

struct PushConstants {
    alignas(16) glm::mat4 model;
    int objectType; 
    float customData;
};

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
    alignas(16) glm::mat4 lightSpaceMatrix;
};

struct GraphicsSettings {
    int   resolutionIndex = 2;   // index into kResolutions; 2 = 1920x1080
    float renderScale     = 1.0f;
    int   msaaLevel       = 8;   // 0=off, else 2/4/8; clamped to device support
    bool  vsync           = true;
    float fovDegrees      = 45.0f;
    float exposure        = 1.0f;
    int   frameCap        = 0;   // 0 = unlimited
    bool  showFps         = false;
    bool  fullscreen      = false;
    bool  orbitLines      = false;
    bool  realScale       = false;
};

static const int kResolutions[][2] = {
    {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}
};
static const int RESOLUTION_COUNT = 5;

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
    
    // 🚀 [NEW] 위성 시스템을 위한 부모 행성 인덱스 (-1이면 태양을 공전)
    int parentIndex = -1; 

    glm::mat4 currentModelMat; glm::dvec3 currentPosition; glm::mat4 cloudModelMat;
    VkImageView viewDiffuse, viewNight, viewSpecular, viewNormal, viewClouds;
    VkDescriptorSet descriptorSet;
};

struct AsteroidData {
    glm::mat4 transform;
    int type;
    int pad1, pad2, pad3;
};

class SolarSystemApp : public VulkanBase {
private:
    Camera camera;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool isRealScaleMode = false;
    GraphicsSettings settings;
    bool showOrbits = false; // 디폴트는 OFF
    float scaleLerp = 0.0f;  // 0.0(시각 비율) ~ 1.0(리얼 비율) 사이를 스르륵 오가는 타이머

    bool isFullscreen = false;
    bool fullscreenToggleRequested = false;
    bool settingsOpen = false;
    VkDescriptorSet gearTexId = VK_NULL_HANDLE;  // assets/settings.png를 ImGui 텍스처로 등록한 핸들
    bool pendingSwapRecreate = false;   // 해상도/VSync 변경 시
    int  appliedResolutionIndex = 2;
    bool appliedVsync = true;

    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    VkPipeline linePipeline;
    VkDescriptorPool descriptorPool;
    VkDescriptorPool imguiPool;

    VkBuffer vertexBuffer; VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer; VkDeviceMemory indexBufferMemory;
    VkBuffer uniformBuffer; VkDeviceMemory uniformBufferMemory;
    void *uniformBufferMapped;

    // =========================================================
    // 🚀 [NEW] GPU-Driven 소행성 파이프라인 전용 버퍼 및 변수
    // =========================================================
    std::vector<AsteroidData> asteroidTransforms;
    
    // Compute Shader 구동용 파이프라인
    VkDescriptorSetLayout computeDescriptorSetLayout;
    VkPipelineLayout computePipelineLayout;
    VkPipeline computePipeline;
    VkDescriptorSet computeDescriptorSet;

    // 3가지 핵심 GPU 버퍼
    VkBuffer computeInputBuffer;  VkDeviceMemory computeInputMem;  // (읽기전용) 원본 2만개
    VkBuffer computeOutputBuffer; VkDeviceMemory computeOutputMem; // (쓰기전용) 계산 완료된 행렬
    VkBuffer indirectDrawBuffer;  VkDeviceMemory indirectDrawMem;  // (명령용) GPU 스스로 내릴 명령서
    void* indirectDrawMapped = nullptr; 

    // 매 프레임 GPU 명령서를 초기화할 CPU 원본 템플릿
    VkDrawIndexedIndirectCommand drawCmdTemplates[12];
    
    // (LOD 정점 기록장은 그대로 유지)
    uint32_t highLodIndexCount[4], highLodFirstIndex[4]; int32_t highLodVertexOffset[4];
    uint32_t midLodIndexCount = 0, midLodFirstIndex = 0; int32_t midLodVertexOffset = 0;
    uint32_t lowLodIndexCount = 0, lowLodFirstIndex = 0; int32_t lowLodVertexOffset = 0;
    Planet asteroidTypes[4];

    VkSampler textureSampler;
    VkImage texSkybox, texDummyBlack, texDummyFlatNormal;
    VkDeviceMemory memSkybox, memDummyBlack, memDummyFlatNormal;
    VkImageView viewSkybox, viewDummyBlack, viewDummyFlatNormal;

    Planet sun, moon, saturnRing, milkyWay;
    uint32_t galaxyIndexCount = 0, galaxyFirstIndex = 0;
    int32_t galaxyVertexOffset = 0;
    std::vector<Planet> planets;

    // 고정된 천체의 궤도선은 매 프레임 CPU에서 double로, 카메라 상대 좌표로 다시 만든다.
    // 공유 단위원 + 모델 행렬 방식으로는 GPU가 34000짜리 항끼리 빼면서 정밀도를 잃기 때문.
    static constexpr int LOCKED_ORBIT_SEGMENTS = 16384;
    VkBuffer lockedOrbitBuffer = VK_NULL_HANDLE;
    VkDeviceMemory lockedOrbitMemory = VK_NULL_HANDLE;
    void* lockedOrbitMapped = nullptr;
    bool lockedOrbitValid = false;
    std::vector<Vertex> lockedOrbitScratch; // 매 프레임 재사용해 힙 할당을 피한다

    uint32_t sphereIndexCount = 0, ringIndexCount = 0, orbitIndexCount = 0, orbitFirstIndex = 0;
    int32_t ringVertexOffset = 0, orbitVertexOffset = 0;

    int lockedTargetType = 1; int lockedPlanetIndex = 2;
    int selectedTargetType = -1; 
    int selectedPlanetIndex = -1;
    std::chrono::time_point<std::chrono::high_resolution_clock> lastClickTime;
    float currentAppTime = 0.0f;
    bool isPaused = false;

    std::vector<VkImage> allImages;
    std::vector<VkDeviceMemory> allMemories;
    std::vector<VkImageView> allViews;

    // 🚀 [추가] 천체 정보를 반환하는 도서관 함수
    std::string getCelestialInfo(const std::string& name) {
        // 1. 항성 및 지구형 행성
        if (name == "Sun") return "Diameter: 1,392,700 km\nMass: 1.989 x 10^30 kg\nSurface Temp: 5,500 °C\nType: Yellow Dwarf (G2V)";
        if (name == "Mercury") return "Diameter: 4,879 km\nMass: 3.30 x 10^23 kg\nGravity: 3.7 m/s^2\nDay Length: 58d 15h";
        if (name == "Venus") return "Diameter: 12,104 km\nMass: 4.87 x 10^24 kg\nGravity: 8.87 m/s^2\nSurface Temp: 462 °C";
        if (name == "Earth") return "Diameter: 12,742 km\nMass: 5.97 x 10^24 kg\nGravity: 9.8 m/s^2\nSurface Temp: 14 °C";
        if (name == "Mars") return "Diameter: 6,779 km\nMass: 6.39 x 10^23 kg\nGravity: 3.71 m/s^2\nMoons: Phobos, Deimos";
        
        // 2. 목성형 행성 (가스/얼음 거성)
        if (name == "Jupiter") return "Diameter: 139,820 km\nMass: 1.89 x 10^27 kg\nGravity: 24.79 m/s^2\nType: Gas Giant";
        if (name == "Saturn") return "Diameter: 116,460 km\nMass: 5.68 x 10^26 kg\nGravity: 10.44 m/s^2\nType: Gas Giant";
        if (name == "Uranus") return "Diameter: 50,724 km\nMass: 8.68 x 10^25 kg\nGravity: 8.69 m/s^2\nType: Ice Giant";
        if (name == "Neptune") return "Diameter: 49,244 km\nMass: 1.02 x 10^26 kg\nGravity: 11.15 m/s^2\nType: Ice Giant";

        // 3. 주요 위성 (지구, 목성, 토성)
        if (name == "Moon") return "Diameter: 3,474 km\nMass: 7.34 x 10^22 kg\nGravity: 1.62 m/s^2\nType: Earth's Moon";
        if (name == "Io") return "Diameter: 3,642 km\nMass: 8.93 x 10^22 kg\nGravity: 1.79 m/s^2\nType: Galilean Moon (Volcanic)";
        if (name == "Europa") return "Diameter: 3,121 km\nMass: 4.79 x 10^22 kg\nGravity: 1.31 m/s^2\nType: Galilean Moon (Ice Ocean)";
        if (name == "Ganymede") return "Diameter: 5,268 km\nMass: 1.48 x 10^23 kg\nGravity: 1.42 m/s^2\nType: Galilean Moon (Largest)";
        if (name == "Callisto") return "Diameter: 4,820 km\nMass: 1.07 x 10^23 kg\nGravity: 1.23 m/s^2\nType: Galilean Moon (Cratered)";
        if (name == "Titan") return "Diameter: 5,149 km\nMass: 1.34 x 10^23 kg\nGravity: 1.35 m/s^2\nType: Saturnian Moon (Atmosphere)";

        // 4. 왜소행성 (소행성대 및 카이퍼 벨트)
        if (name == "Ceres") return "Diameter: 939 km\nMass: 9.39 x 10^20 kg\nGravity: 0.28 m/s^2\nType: Dwarf Planet (Asteroid Belt)";
        if (name == "Pluto") return "Diameter: 2,376 km\nMass: 1.30 x 10^22 kg\nGravity: 0.62 m/s^2\nType: Dwarf Planet (Kuiper Belt)";
        if (name == "Haumea") return "Diameter: ~1,632 km\nMass: 4.01 x 10^21 kg\nGravity: 0.40 m/s^2\nType: Dwarf Planet (Oblong)";
        if (name == "Makemake") return "Diameter: 1,430 km\nMass: ~3.1 x 10^21 kg\nGravity: 0.50 m/s^2\nType: Dwarf Planet";
        if (name == "Eris") return "Diameter: 2,326 km\nMass: 1.66 x 10^22 kg\nGravity: 0.82 m/s^2\nType: Dwarf Planet (Scattered Disc)";

        // 예외 처리 (엔진에 추가될 미지의 소행성 대비)
        return "Diameter: N/A\nMass: Unknown\nGravity: Unknown\nType: Celestial Body";
    }

    VkImage offscreenColorImage, offscreenBrightImage, offscreenDepthImage;
    VkDeviceMemory offscreenColorMem, offscreenBrightMem, offscreenDepthMem;
    VkImageView offscreenColorView, offscreenBrightView, offscreenDepthView;

    // 4x MSAA: 오프스크린 3D 패스를 멀티샘플로 그린 뒤 위의 단일 샘플 색상/bright 이미지로 resolve한다.
    // msaaColor/msaaBright는 멀티샘플 렌더 타겟이고, 위 offscreenColor/BrightImage가 resolve 대상(계속 SAMPLED).
    // 깊이는 다운스트림에서 안 쓰므로 offscreenDepthImage 자체를 멀티샘플로 만들어 그대로 렌더 타겟으로 쓴다.
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    VkExtent2D renderExtent{}; // 오프스크린/블러 렌더 크기 = renderScale x swapChainExtent
    float appliedRenderScale = 1.0f;
    bool  pendingOffscreenRecreate = false; // 렌더스케일/MSAA 변경 시
    VkImage msaaColorImage = VK_NULL_HANDLE, msaaBrightImage = VK_NULL_HANDLE;
    VkDeviceMemory msaaColorMem = VK_NULL_HANDLE, msaaBrightMem = VK_NULL_HANDLE;
    VkImageView msaaColorView = VK_NULL_HANDLE, msaaBrightView = VK_NULL_HANDLE;

    VkRenderPass offscreenRenderPass;
    VkFramebuffer offscreenFramebuffer;
    VkSampler offscreenSampler;

    VkImage blurImages[2];
    VkDeviceMemory blurMemories[2];
    VkImageView blurViews[2];
    VkRenderPass blurRenderPass;
    VkFramebuffer blurFramebuffers[2];
    VkDescriptorSetLayout blurDescriptorSetLayout;
    VkPipelineLayout blurPipelineLayout;
    VkPipeline blurPipeline;
    VkDescriptorSet blurDescriptorSets[3];

    VkDescriptorSetLayout postDescriptorSetLayout;
    VkPipelineLayout postPipelineLayout;
    VkPipeline postPipeline;
    VkDescriptorSet postDescriptorSet;

    VkImage shadowImage;
    VkDeviceMemory shadowMemory;
    VkImageView shadowView;
    VkSampler shadowSampler;
    VkRenderPass shadowRenderPass;
    VkFramebuffer shadowFramebuffer;
    VkPipeline shadowPipeline;
    VkPipelineLayout shadowPipelineLayout;

    enum class AppState { LOBBY, SIMULATION };
    AppState currentAppState = AppState::LOBBY;

    // 🚀 [NEW] 개기일식 시네마틱 모드 변수
    bool isEclipseEvent = false;

protected:
    void onFrameStart() override {
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

    void throttleFrame(double frameStart) override {
        if (settings.frameCap <= 0) return;
        double target = 1.0 / (double)settings.frameCap;
        double elapsed = glfwGetTime() - frameStart;
        while (elapsed < target) { // busy-wait의 대안으로 짧게 sleep
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            elapsed = glfwGetTime() - frameStart;
        }
    }

    void onMouseButton(int button, int action, int mods) override {
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
    
    void onMouseMove(double xpos, double ypos) override { 
        // 🚀 [수정] ImGui UI를 조작 중이거나, Lobby 상태일 땐 화면 회전을 막습니다.
        if (ImGui::GetIO().WantCaptureMouse || currentAppState == AppState::LOBBY) return;
        camera.processMouseDrag(xpos, ypos); 
    }
    
    void onMouseScroll(double xoffset, double yoffset) override { 
        // 🚀 [수정] ImGui UI를 조작 중이거나, Lobby 상태일 땐 줌인/줌아웃을 막습니다.
        if (ImGui::GetIO().WantCaptureMouse || currentAppState == AppState::LOBBY) return;
        camera.processMouseScroll(yoffset); 
    }

    void handleRaycast(double xpos, double ypos, bool isDoubleClick) {
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

    // 오프스크린/블러 이미지·프레임버퍼 전용 파괴 헬퍼. recreateSwapChain()과
    // recreateOffscreenAndBlur() 양쪽에서 재사용한다(Task 6의 MSAA 재생성도 재사용 예정).
    void destroyOffscreenAndBlurResources() {
        for (int i = 0; i < 2; i++) {
            vkDestroyFramebuffer(device, blurFramebuffers[i], nullptr);
            vkDestroyImageView(device, blurViews[i], nullptr);
            vkDestroyImage(device, blurImages[i], nullptr);
            vkFreeMemory(device, blurMemories[i], nullptr);
        }
        vkDestroyFramebuffer(device, offscreenFramebuffer, nullptr);
        vkDestroyImageView(device, offscreenColorView, nullptr); vkDestroyImage(device, offscreenColorImage, nullptr); vkFreeMemory(device, offscreenColorMem, nullptr);
        vkDestroyImageView(device, offscreenBrightView, nullptr); vkDestroyImage(device, offscreenBrightImage, nullptr); vkFreeMemory(device, offscreenBrightMem, nullptr);
        vkDestroyImageView(device, offscreenDepthView, nullptr); vkDestroyImage(device, offscreenDepthImage, nullptr); vkFreeMemory(device, offscreenDepthMem, nullptr);
        vkDestroyImageView(device, msaaColorView, nullptr); vkDestroyImage(device, msaaColorImage, nullptr); vkFreeMemory(device, msaaColorMem, nullptr);
        vkDestroyImageView(device, msaaBrightView, nullptr); vkDestroyImage(device, msaaBrightImage, nullptr); vkFreeMemory(device, msaaBrightMem, nullptr);
    }

    void recreateSwapChain() {
        vkDeviceWaitIdle(device);

        destroyOffscreenAndBlurResources();

        cleanupSwapChain();

        createSwapChain();
        computeRenderExtent(); // 창 크기 변경 시 오프스크린도 scale x 새-창크기로 유지
        createImageViews();
        createDepthResources();
        createFramebuffers();

        createOffscreenImages();
        createBlurImages();
        updateBlurDescriptorSets();
        updatePostDescriptorSets();
    }

    // 렌더스케일(및 Task 6의 MSAA) 변경 시 오프스크린/블러만 다시 만든다. 스왑체인은 그대로.
    void recreateOffscreenAndBlur() {
        destroyOffscreenAndBlurResources();
        createOffscreenImages();
        createBlurImages();
        updateBlurDescriptorSets();
        updatePostDescriptorSets();
    }

    void recreateOffscreenForMsaa() {
        // 오프스크린 파이프라인(graphics/line)과 렌더패스는 샘플 수가 생성 시 고정되므로 다시 만든다.
        // 블러/포스트는 resolve된 단일 샘플 이미지를 쓰므로 MSAA와 무관 — 렌더패스/이미지는 그대로 두고
        // resolve 대상 뷰만 새로 바인딩한다.
        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        vkDestroyPipeline(device, linePipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

        vkDestroyFramebuffer(device, offscreenFramebuffer, nullptr);
        vkDestroyImageView(device, offscreenColorView, nullptr); vkDestroyImage(device, offscreenColorImage, nullptr); vkFreeMemory(device, offscreenColorMem, nullptr);
        vkDestroyImageView(device, offscreenBrightView, nullptr); vkDestroyImage(device, offscreenBrightImage, nullptr); vkFreeMemory(device, offscreenBrightMem, nullptr);
        vkDestroyImageView(device, offscreenDepthView, nullptr); vkDestroyImage(device, offscreenDepthImage, nullptr); vkFreeMemory(device, offscreenDepthMem, nullptr);
        vkDestroyImageView(device, msaaColorView, nullptr); vkDestroyImage(device, msaaColorImage, nullptr); vkFreeMemory(device, msaaColorMem, nullptr);
        vkDestroyImageView(device, msaaBrightView, nullptr); vkDestroyImage(device, msaaBrightImage, nullptr); vkFreeMemory(device, msaaBrightMem, nullptr);
        vkDestroyRenderPass(device, offscreenRenderPass, nullptr);
        vkDestroySampler(device, offscreenSampler, nullptr);

        createOffscreenResources();  // 렌더패스 + 이미지 + 프레임버퍼 + 샘플러 (새 msaaSamples 반영)
        createGraphicsPipeline();    // graphics + line + pipelineLayout (새 msaaSamples 반영)

        updateBlurDescriptorSets();  // 새 offscreenBrightView + 새 샘플러 재바인딩
        updatePostDescriptorSets();  // 새 offscreenColorView + 새 샘플러 재바인딩
    }

    // 색상과 깊이 어태치먼트가 공통으로 지원하는 최대 샘플 수를 8x로 상한을 두고 고른다.
    // (오프스크린 패스는 두 종류를 한 서브패스에서 쓰므로 교집합이어야 한다)
    // 8x 미지원이면 4x, 2x, 1x 순으로 자동 강등된다.
    VkSampleCountFlagBits pickMsaaSamples() {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts
                                  & props.limits.framebufferDepthSampleCounts;
        VkSampleCountFlagBits chosen = VK_SAMPLE_COUNT_1_BIT;
        if (counts & VK_SAMPLE_COUNT_8_BIT)      chosen = VK_SAMPLE_COUNT_8_BIT;
        else if (counts & VK_SAMPLE_COUNT_4_BIT) chosen = VK_SAMPLE_COUNT_4_BIT;
        else if (counts & VK_SAMPLE_COUNT_2_BIT) chosen = VK_SAMPLE_COUNT_2_BIT;
        fprintf(stderr, "[MSAA] selected %dx (device supports mask 0x%x)\n", (int)chosen, (unsigned)counts);
        return chosen;
    }

    // 설정의 MSAA 레벨(0/2/4/8)을 기기 지원으로 클램프해 VkSampleCountFlagBits로 변환한다.
    VkSampleCountFlagBits clampMsaaLevel(int level) {
        VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physicalDevice, &props);
        VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
        auto ok = [&](VkSampleCountFlagBits b){ return (counts & b) != 0; };
        if (level >= 8 && ok(VK_SAMPLE_COUNT_8_BIT)) return VK_SAMPLE_COUNT_8_BIT;
        if (level >= 4 && ok(VK_SAMPLE_COUNT_4_BIT)) return VK_SAMPLE_COUNT_4_BIT;
        if (level >= 2 && ok(VK_SAMPLE_COUNT_2_BIT)) return VK_SAMPLE_COUNT_2_BIT;
        return VK_SAMPLE_COUNT_1_BIT;
    }

    void loadSettings() {
        std::ifstream f("settings.ini");
        if (!f.is_open()) return; // 파일 없으면 구조체 기본값 유지
        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            try {
                if      (k == "resolutionIndex") settings.resolutionIndex = std::clamp(std::stoi(v), 0, RESOLUTION_COUNT - 1);
                else if (k == "renderScale")     settings.renderScale = std::clamp(std::stof(v), 0.5f, 2.0f);
                else if (k == "msaaLevel")       settings.msaaLevel = std::stoi(v);
                else if (k == "vsync")           settings.vsync = (std::stoi(v) != 0);
                else if (k == "fovDegrees")      settings.fovDegrees = std::clamp(std::stof(v), 30.0f, 90.0f);
                else if (k == "exposure")        settings.exposure = std::clamp(std::stof(v), 0.3f, 3.0f);
                else if (k == "frameCap")        settings.frameCap = std::max(0, std::stoi(v));
                else if (k == "showFps")         settings.showFps = (std::stoi(v) != 0);
                else if (k == "fullscreen")      settings.fullscreen = (std::stoi(v) != 0);
                // orbitLines / realScale은 의도적으로 복원하지 않는다: 매 실행 항상 OFF로 시작하는
                // 런타임 뷰 토글이다. (구버전 settings.ini에 남아 있어도 무시)
            } catch (...) { /* 잘못된 값은 무시하고 기본값 유지 */ }
        }
    }

    void saveSettings() {
        std::ofstream f("settings.ini", std::ios::trunc);
        if (!f.is_open()) return; // 저장 실패는 조용히 무시
        f << "resolutionIndex=" << settings.resolutionIndex << "\n"
          << "renderScale="     << settings.renderScale     << "\n"
          << "msaaLevel="       << settings.msaaLevel       << "\n"
          << "vsync="           << (settings.vsync ? 1 : 0) << "\n"
          << "fovDegrees="      << settings.fovDegrees      << "\n"
          << "exposure="        << settings.exposure        << "\n"
          << "frameCap="        << settings.frameCap        << "\n"
          << "showFps="         << (settings.showFps ? 1 : 0) << "\n"
          << "fullscreen="      << (settings.fullscreen ? 1 : 0) << "\n";
        // orbitLines / realScale은 저장하지 않는다 — 항상 OFF로 시작하는 런타임 전용 뷰 토글.
    }

    void initApp() override {
        loadSettings();
        msaaSamples = clampMsaaLevel(settings.msaaLevel);
        appliedResolutionIndex = -1; // 강제로 첫 프레임에 해상도 적용
        appliedVsync = settings.vsync;
        desiredPresentMode = settings.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;

        generateSphere(1.0f, 64, 64);
        sphereIndexCount = static_cast<uint32_t>(indices.size());
        
        ringVertexOffset = static_cast<int32_t>(vertices.size());
        uint32_t ringFirstIdx = static_cast<uint32_t>(indices.size());
        generateRing(1.2f, 2.2f, 64);
        ringIndexCount = static_cast<uint32_t>(indices.size()) - ringFirstIdx;
        
        orbitVertexOffset = static_cast<int32_t>(vertices.size());
        orbitFirstIndex = static_cast<uint32_t>(indices.size());
        // 모든 궤도가 공유하는 단위 원이라 가장 빡센 궤도(리얼 스케일 Eris, 반지름 34000)에 맞춰야 한다.
        // 다각형의 현이 진짜 타원 안쪽으로 파고드는 오차가 궤도반경 x (1 - cos(pi/N)) 이므로,
        // 128분할이면 Eris 기준 10.2 유닛(= 그 천체 반지름의 113배)이 어긋나 궤도선이 천체를 완전히 빗나간다.
        generateOrbit(4096);

        orbitIndexCount = static_cast<uint32_t>(indices.size()) - orbitFirstIndex;

        galaxyVertexOffset = static_cast<int32_t>(vertices.size());
        galaxyFirstIndex = static_cast<uint32_t>(indices.size());
        generateGalaxyDisk(1.0f, 64);
        galaxyIndexCount = static_cast<uint32_t>(indices.size()) - galaxyFirstIndex;

        // 1. 4종류의 소행성 모델 순차적 로딩 (대소문자 완벽 일치)
        std::string objPaths[4] = {
            "textures/asteroids/Bennu_Radar.obj",
            "textures/asteroids/gaspra_stooke.obj",
            "textures/asteroids/Ida_Stooke.obj",
            "textures/asteroids/Itokawa_Radar.obj"
        };
        for (int i = 0; i < 4; i++) {
            loadObjModel(objPaths[i], highLodIndexCount[i], highLodFirstIndex[i], highLodVertexOffset[i]);
        }

        // 2. 중간 화질(Mid LOD) 뼈대 생성
        midLodVertexOffset = static_cast<int32_t>(vertices.size());
        midLodFirstIndex = static_cast<uint32_t>(indices.size());
        generateSphere(1.0f, 16, 16); 
        midLodIndexCount = static_cast<uint32_t>(indices.size()) - midLodFirstIndex;

        // 3. 최하 화질(Low LOD) 뼈대 생성
        lowLodVertexOffset = static_cast<int32_t>(vertices.size());
        lowLodFirstIndex = static_cast<uint32_t>(indices.size());
        generateSphere(1.0f, 6, 6); 
        lowLodIndexCount = static_cast<uint32_t>(indices.size()) - lowLodFirstIndex;

        // 엔진 필수 리소스 초기화 (삭제 불가)
        createCubeTextureImage();
        createTextureSampler();
        createColorTexture(0, 0, 0, 0, texDummyBlack, memDummyBlack, viewDummyBlack, VK_FORMAT_R8G8B8A8_UNORM);
        createColorTexture(128, 128, 255, 255, texDummyFlatNormal, memDummyFlatNormal, viewDummyFlatNormal, VK_FORMAT_R8G8B8A8_UNORM);
        createShadowResources();

        // 4. 2만 개의 소행성 데이터 수학적 생성 (다중 모델 부여 포함) 및 SSBO 업로드
        asteroidTransforms = generateAsteroidBelt(10000, 9.0f, 12.0f, 0.4f, 0.008f, 0.025f); // 소행성대
        auto kuiperTransforms = generateAsteroidBelt(30000, 75.0f, 100.0f, 1.5f, 0.08f, 0.25f); // 카이퍼벨트
        asteroidTransforms.insert(asteroidTransforms.end(), kuiperTransforms.begin(), kuiperTransforms.end()); 
        
        // 파이프라인 및 버퍼 초기화 (삭제 불가)
        createVertexBuffer(); createIndexBuffer(); createUniformBuffer();
        createLockedOrbitBuffer();
        createDescriptorSetLayout(); createDescriptorPool();

        // 🚀 [수정] 반드시 풀(Pool)과 유니폼 버퍼가 생성된 "다음"에 Compute 자원을 만들어야 합니다!
        createComputeResources();
        createOffscreenResources();
        createBlurResources();
        createBlurPipeline();
        createPostProcessPipeline();
        createShadowPipeline();

        // =========================================================
        // 🚀 [태양계 전면 재설계] 영역(Domain) 침범 방지 및 연쇄 확장 시스템
        // 1. 목성을 22.0으로 밀어내어 위성-소행성대 충돌 원천 차단
        // 2. 외행성들의 거리를 비례에 맞춰 확장 (해왕성 62.0)
        // 3. 카이퍼벨트(왜소행성)를 해왕성 밖(75.0 ~ 100.0)으로 완벽히 밀어냄
        // =========================================================
        
        sun = createPlanet("Sun", 4, 1.80f, 0.0f, 0.0f, 0.0f, 0.9f, 7.25f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, "textures/sun.jpg", "", "", "", "");
        
        // 1. 내행성 (수~화)
        planets.push_back(createPlanet("Mercury", 0, 0.11f, 3.50f, 15.00f, 0.205f, 0.43f, 0.03f, 7.0f, 77.0f, 48.0f, 120.0f, 45.0f, false, "textures/planets/mercury.jpg", "", "", "", ""));  // index 0
        planets.push_back(createPlanet("Venus", 0, 0.19f, 4.80f, 11.00f, 0.006f, 0.1f, 177.36f, 3.4f, 131.0f, 76.0f, 30.0f, 100.0f, true, "textures/planets/venus.jpg", "", "", "", "textures/planets/venus_atmosphere.jpg"));  // index 1
        planets.push_back(createPlanet("Earth", 0, 0.20f, 6.00f, 10.00f, 0.016f, 25.0f, 23.44f, 0.0f, 102.0f, 0.0f, 200.0f, 0.0f, true, "textures/planets/earth.jpg", "textures/planets/earth_night.jpg", "textures/planets/earth_specular.jpg", "textures/planets/earth_normal.jpg", "textures/planets/earth_clouds.jpg"));  // index 2
        planets.push_back(createPlanet("Mars", 0, 0.14f, 7.50f, 8.50f, 0.093f, 24.3f, 25.19f, 1.85f, 336.0f, 49.0f, 310.0f, 210.0f, false, "textures/planets/mars.jpg", "", "", "", ""));  // index 3
        
        // 2. 소행성대 (세레스가 11.0에서 기준선 역할을 합니다)
        planets.push_back(createPlanet("Ceres", 1, 0.04f, 11.00f, 6.50f, 0.076f, 66.6f, 4.0f, 10.6f, 73.0f, 80.0f, 0.0f, 0.0f, false, "textures/asteroids/ceres.jpg", "", "", "", ""));  // index 4
        
        // 3. 외행성 (목성계 확장에 따른 연쇄 이동 적용)
        planets.push_back(createPlanet("Jupiter", 9, 0.80f, 22.00f, 4.50f, 0.048f, 60.6f, 3.13f, 1.3f, 14.0f, 100.0f, 45.0f, 30.0f, false, "textures/planets/jupiter.jpg", "", "", "", "")); // index 5 (위성 부모)
        planets.push_back(createPlanet("Saturn", 9, 0.70f, 34.00f, 3.30f, 0.056f, 56.0f, 26.73f, 2.49f, 92.0f, 113.0f, 150.0f, 120.0f, false, "textures/planets/saturn.jpg", "", "", "", ""));  // index 6 (타이탄 부모)
        planets.push_back(createPlanet("Uranus", 9, 0.40f, 48.00f, 2.50f, 0.046f, 34.8f, 97.77f, 0.77f, 170.0f, 74.0f, 280.0f, 250.0f, false, "textures/planets/uranus.jpg", "", "", "", ""));  // index 7
        planets.push_back(createPlanet("Neptune", 9, 0.39f, 62.00f, 2.00f, 0.009f, 37.2f, 28.32f, 1.77f, 44.0f, 131.0f, 90.0f, 80.0f, false, "textures/planets/neptune.jpg", "", "", "", ""));  // index 8
        
        // 4. 달 및 토성 고리
        moon = createPlanet("Moon", 1, 0.08f, 0.75f, 45.0f, 0.054f, 45.0f, 6.68f, 5.14f, 318.0f, 125.0f, 45.0f, 0.0f, false, "textures/moons/moon.jpg", "", "", "textures/moons/moon_normal.jpg", "");
        saturnRing = createPlanet("SaturnRing", 5, 1.60f, 0.0f, 0.0f, 0.0f, 0.0f, 26.73f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, "textures/planets/saturn_ring.png", "", "", "", "");
        
        // 5. 카이퍼 벨트 및 왜소행성 (외행성들이 밀려난 만큼, 바깥쪽 영역(75~100)으로 완벽하게 밀려납니다)
        planets.push_back(createPlanet("Pluto", 1, 0.07f, 75.00f, 1.70f, 0.248f, 3.9f, 122.5f, 17.1f, 113.8f, 110.0f, 90.0f, 0.0f, false, "textures/asteroids/pluto.jpg", "", "", "", "")); // index 9
        planets.push_back(createPlanet("Haumea", 1, 0.06f, 80.00f, 1.60f, 0.196f, 153.8f, 28.0f, 28.2f, 239.0f, 122.0f, 45.0f, 0.0f, false, "textures/asteroids/haumea.jpg", "", "", "", "")); // index 10
        planets.push_back(createPlanet("Makemake", 1, 0.05f, 85.00f, 1.50f, 0.161f, 26.3f, 29.0f, 29.0f, 295.0f, 79.0f, 120.0f, 0.0f, false, "textures/asteroids/makemake.jpg", "", "", "", "")); // index 11
        planets.push_back(createPlanet("Eris", 1, 0.07f, 100.00f, 1.30f, 0.436f, 23.1f, 78.0f, 44.0f, 44.0f, 36.0f, 200.0f, 0.0f, false, "textures/asteroids/eris.jpg", "", "", "", "")); // index 12

        // 6. 목성 & 토성 위성 (목성계 안전 반경 내 공전)
        Planet io = createPlanet("Io", 1, 0.09f, 1.2f, 40.0f, 0.004f, 40.0f, 0.0f, 0.04f, 0.0f, 0.0f, 0.0f, 0.0f, false, "textures/moons/io.jpg", "", "", "", ""); 
        io.parentIndex = 5; planets.push_back(io); 
        
        Planet europa = createPlanet("Europa", 1, 0.08f, 1.9f, 20.0f, 0.009f, 20.0f, 0.0f, 0.47f, 0.0f, 0.0f, 45.0f, 0.0f, false, "textures/moons/europa.jpg", "", "", "", ""); 
        europa.parentIndex = 5; planets.push_back(europa);
        
        Planet ganymede = createPlanet("Ganymede", 1, 0.12f, 3.0f, 10.0f, 0.001f, 10.0f, 0.0f, 0.2f, 0.0f, 0.0f, 90.0f, 0.0f, false, "textures/moons/ganymede.jpg", "", "", "", ""); 
        ganymede.parentIndex = 5; planets.push_back(ganymede);
        
        Planet callisto = createPlanet("Callisto", 1, 0.11f, 5.4f, 4.2f, 0.007f, 4.2f, 0.0f, 0.28f, 0.0f, 0.0f, 135.0f, 0.0f, false, "textures/moons/callisto.jpg", "", "", "", ""); 
        callisto.parentIndex = 5; planets.push_back(callisto);

        Planet titan = createPlanet("Titan", 1, 0.11f, 2.0f, 5.0f, 0.028f, 5.0f, 0.0f, 0.33f, 0.0f, 0.0f, 0.0f, 0.0f, true, "textures/moons/titan.jpg", "", "", "", "textures/moons/titan_atmosphere.jpg"); 
        titan.parentIndex = 6; planets.push_back(titan);
        
        // 6. 4가지 소행성 개별 텍스처 로딩 (기존 asteroidBelt 대체)
        std::string texPaths[4] = {
            "textures/asteroids/Bennu.jpg",
            "textures/asteroids/Gaspra.jpg",
            "textures/asteroids/Ida.jpg",
            "textures/asteroids/Itokawa.jpg"
        };
        for (int i = 0; i < 4; i++) {
            // 🚀 [수정됨] 소행성은 궤도 연산을 SSBO에서 따로 하므로, 추가된 5대 요소 자리에 0.0f 5개를 빵꾸울워 줍니다!
            asteroidTypes[i] = createPlanet("AsteroidType" + std::to_string(i), 8, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, texPaths[i], "", "", "", "");
        }

        milkyWay = createPlanet("Milky Way", 10, 200000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, "textures/milkyway.png", "", "", "", "");

        // =========================================================
        // 🚀 [NEW] 리얼 스케일(Real Scale) 타겟 데이터 주입
        // =========================================================
        sun.realRadius = 54.0f; sun.realOrbit = 0.0f;
        moon.realRadius = 0.13f; moon.realOrbit = 1.5f; 
        for (auto& p : planets) {
            if (p.name == "Mercury") { p.realRadius = 0.19f; p.realOrbit = 195.0f; }
            else if (p.name == "Venus") { p.realRadius = 0.47f; p.realOrbit = 360.0f; }
            else if (p.name == "Earth") { p.realRadius = 0.50f; p.realOrbit = 500.0f; }
            else if (p.name == "Mars") { p.realRadius = 0.26f; p.realOrbit = 760.0f; }
            else if (p.name == "Ceres") { p.realRadius = 0.03f; p.realOrbit = 1385.0f; }
            else if (p.name == "Jupiter") { p.realRadius = 5.60f; p.realOrbit = 2600.0f; }
            else if (p.name == "Saturn") { p.realRadius = 4.72f; p.realOrbit = 4790.0f; }
            else if (p.name == "Uranus") { p.realRadius = 2.00f; p.realOrbit = 9600.0f; }
            else if (p.name == "Neptune") { p.realRadius = 1.94f; p.realOrbit = 15050.0f; }
            else if (p.name == "Pluto") { p.realRadius = 0.09f; p.realOrbit = 19750.0f; }
            else if (p.name == "Haumea") { p.realRadius = 0.06f; p.realOrbit = 21550.0f; }
            else if (p.name == "Makemake") { p.realRadius = 0.05f; p.realOrbit = 22650.0f; }
            else if (p.name == "Eris") { p.realRadius = 0.09f; p.realOrbit = 34000.0f; }
            else if (p.name == "Io") { p.realRadius = 0.14f; p.realOrbit = 6.2f; } 
            else if (p.name == "Europa") { p.realRadius = 0.12f; p.realOrbit = 9.8f; }
            else if (p.name == "Ganymede") { p.realRadius = 0.20f; p.realOrbit = 15.7f; }
            else if (p.name == "Callisto") { p.realRadius = 0.18f; p.realOrbit = 27.6f; }
            else if (p.name == "Titan") { p.realRadius = 0.20f; p.realOrbit = 12.2f; }
        }

        createGraphicsPipeline();
        initImGui();
    }

    // =========================================================
    // 🚀 [NEW] 소행성 생성 엔진 함수들
    // =========================================================
    // =========================================================
    // 🚀 [수정됨] 실제 천문학 기반 소행성대 궤도 생성 알고리즘
    // =========================================================
    std::vector<AsteroidData> generateAsteroidBelt(int amount, float minRadius, float maxRadius, float yVariance, float minScale, float maxScale) {
        std::vector<AsteroidData> result;
        result.reserve(amount);
        
        std::random_device rd; std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(0.0f, 1.0f);
        std::normal_distribution<float> normalDis(0.0f, 1.0f); 

        for (int i = 0; i < amount; i++) {
            glm::mat4 model = glm::mat4(1.0f);
            float angle = dis(gen) * 2.0f * M_PI;
            float radius = minRadius + dis(gen) * (maxRadius - minRadius) + normalDis(gen) * 0.5f;
            float x = radius * cos(angle); float y = normalDis(gen) * yVariance; float z = radius * sin(angle);
            glm::vec3 pos = glm::vec3(x, y, z);

            float inclination = normalDis(gen) * 0.15f; 
            glm::vec3 tiltAxis = glm::normalize(glm::vec3(dis(gen) * 2.0f - 1.0f, 0.0f, dis(gen) * 2.0f - 1.0f));
            glm::mat4 orbitTilt = glm::rotate(glm::mat4(1.0f), inclination, tiltAxis);
            
            pos = glm::vec3(orbitTilt * glm::vec4(pos, 1.0f));
            model = glm::translate(model, pos);

            float rotAngle = dis(gen) * 2.0f * M_PI;
            glm::vec3 rotAxis = glm::normalize(glm::vec3(dis(gen) * 2.0f - 1.0f, dis(gen) * 2.0f - 1.0f, dis(gen) * 2.0f - 1.0f));
            model = glm::rotate(model, rotAngle, rotAxis);

            float scale = minScale + dis(gen) * (maxScale - minScale);
            model = glm::scale(model, glm::vec3(scale));
            
            // 0~3까지의 무작위 종류 부여
            int type = static_cast<int>(dis(gen) * 4);
            if (type == 4) type = 3; 

            result.push_back({model, type, 0, 0, 0});
        }
        return result;
    }

    void loadObjModel(const std::string& path, uint32_t& outIndexCount, uint32_t& outFirstIndex, int32_t& outVertexOffset) {
        tinyobj::attrib_t attrib; std::vector<tinyobj::shape_t> shapes; std::vector<tinyobj::material_t> materials; std::string warn, err;
        outVertexOffset = static_cast<int32_t>(vertices.size());
        outFirstIndex = static_cast<uint32_t>(indices.size());

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
            std::cerr << "OBJ 로드 실패! 대체용 미니 구체를 생성합니다. 에러: " << warn << err << "\n";
            generateSphere(1.0f, 16, 16); 
            outIndexCount = static_cast<uint32_t>(indices.size()) - outFirstIndex;
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
                vertices.push_back(vertex);
                indices.push_back(localVertexCount++);
            }
        }
        outIndexCount = static_cast<uint32_t>(indices.size()) - outFirstIndex;
    }

    void createComputeResources() {
        VkDeviceSize inputSize = asteroidTransforms.size() * sizeof(AsteroidData);
        createBuffer(inputSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, computeInputBuffer, computeInputMem);
        void* inData; vkMapMemory(device, computeInputMem, 0, inputSize, 0, &inData);
        memcpy(inData, asteroidTransforms.data(), inputSize);
        vkUnmapMemory(device, computeInputMem);

        VkDeviceSize outputSize = 240000 * sizeof(glm::mat4);
        createBuffer(outputSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, computeOutputBuffer, computeOutputMem);

        VkDeviceSize indirectSize = 12 * sizeof(VkDrawIndexedIndirectCommand);
        createBuffer(indirectSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indirectDrawBuffer, indirectDrawMem);
        vkMapMemory(device, indirectDrawMem, 0, indirectSize, 0, &indirectDrawMapped);

        for (int lod = 0; lod < 3; ++lod) {
            for (int type = 0; type < 4; ++type) {
                int bucket = lod * 4 + type;
                drawCmdTemplates[bucket].instanceCount = 0;
                drawCmdTemplates[bucket].firstInstance = bucket * 20000;
                if (lod == 0) { drawCmdTemplates[bucket].indexCount = highLodIndexCount[type]; drawCmdTemplates[bucket].firstIndex = highLodFirstIndex[type]; drawCmdTemplates[bucket].vertexOffset = highLodVertexOffset[type]; }
                else if (lod == 1) { drawCmdTemplates[bucket].indexCount = midLodIndexCount; drawCmdTemplates[bucket].firstIndex = midLodFirstIndex; drawCmdTemplates[bucket].vertexOffset = midLodVertexOffset; }
                else { drawCmdTemplates[bucket].indexCount = lowLodIndexCount; drawCmdTemplates[bucket].firstIndex = lowLodFirstIndex; drawCmdTemplates[bucket].vertexOffset = lowLodVertexOffset; }
            }
        }
        memcpy(indirectDrawMapped, drawCmdTemplates, indirectSize);

        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
        bindings[0].binding = 0; bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; bindings[0].descriptorCount = 1; bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding = 1; bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bindings[1].descriptorCount = 1; bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[2].binding = 2; bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bindings[2].descriptorCount = 1; bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[3].binding = 3; bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bindings[3].descriptorCount = 1; bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{}; layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; layoutInfo.bindingCount = 4; layoutInfo.pBindings = bindings.data();
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &computeDescriptorSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{}; allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; allocInfo.descriptorPool = descriptorPool; allocInfo.descriptorSetCount = 1; allocInfo.pSetLayouts = &computeDescriptorSetLayout;
        vkAllocateDescriptorSets(device, &allocInfo, &computeDescriptorSet);

        VkDescriptorBufferInfo uboInfo{uniformBuffer, 0, sizeof(UniformBufferObject)};
        VkDescriptorBufferInfo inInfo{computeInputBuffer, 0, inputSize};
        VkDescriptorBufferInfo outInfo{computeOutputBuffer, 0, outputSize};
        VkDescriptorBufferInfo indInfo{indirectDrawBuffer, 0, indirectSize};

        std::array<VkWriteDescriptorSet, 4> writes{};
        for(int i=0; i<4; i++) { writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet = computeDescriptorSet; writes[i].dstBinding = i; writes[i].descriptorCount = 1; writes[i].descriptorType = (i==0) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; }
        writes[0].pBufferInfo = &uboInfo; writes[1].pBufferInfo = &inInfo; writes[2].pBufferInfo = &outInfo; writes[3].pBufferInfo = &indInfo;
        vkUpdateDescriptorSets(device, 4, writes.data(), 0, nullptr);

        auto compCode = readFile("shaders/asteroid_cull.spv"); 
        VkShaderModule compMod = createShaderModule(compCode);
        VkPipelineShaderStageCreateInfo stageInfo{}; stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT; stageInfo.module = compMod; stageInfo.pName = "main";
        
        VkPipelineLayoutCreateInfo pLayoutInfo{}; pLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pLayoutInfo.setLayoutCount = 1; pLayoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        vkCreatePipelineLayout(device, &pLayoutInfo, nullptr, &computePipelineLayout);
        
        VkComputePipelineCreateInfo pipelineInfo{}; pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO; pipelineInfo.stage = stageInfo; pipelineInfo.layout = computePipelineLayout;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline);
        vkDestroyShaderModule(device, compMod, nullptr);
    }
    // =========================================================

    bool worldToScreen(glm::vec3 worldPos, glm::mat4 view, glm::mat4 proj, float screenWidth, float screenHeight, glm::vec2& outScreenPos) {
        glm::vec4 clipSpacePos = proj * view * glm::vec4(worldPos, 1.0f);
        if (clipSpacePos.w < 0.1f) return false;

        glm::vec3 ndcSpacePos = glm::vec3(clipSpacePos) / clipSpacePos.w;
        outScreenPos.x = (ndcSpacePos.x + 1.0f) * 0.5f * screenWidth;
        outScreenPos.y = (ndcSpacePos.y + 1.0f) * 0.5f * screenHeight; 
        return true;
    }

    void TextCentered(const char* text) {
        float windowWidth = ImGui::GetWindowSize().x;
        float textWidth = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
        ImGui::Text("%s", text);
    }

    void initImGui() {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 }
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 100;
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        ImGui::StyleColorsDark();
        ImGui::GetIO().IniFilename = nullptr;

        ImGui_ImplGlfw_InitForVulkan(window, true);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = instance;
        init_info.PhysicalDevice = physicalDevice;
        init_info.Device = device;
        init_info.QueueFamily = 0;
        init_info.Queue = graphicsQueue;
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = imguiPool;
        init_info.Subpass = 0;
        init_info.MinImageCount = 2;
        init_info.ImageCount = 2;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.RenderPass = renderPass;
        ImGui_ImplVulkan_Init(&init_info);

        // 기어 버튼용 아이콘. loadTexture가 이미지/뷰/메모리를 allImages 등에 등록해 수명을 관리하고,
        // AddTexture로 얻은 디스크립터셋은 imguiPool과 함께 해제된다. 로드 실패 시 gearTexId는 NULL로
        // 남아, 렌더링 쪽에서 기존 "[=]" 텍스트 버튼으로 폴백한다.
        VkImageView gearView = loadGrayIcon("assets/settings.png", 175);  // 어두운 배경에서도 보이는 밝은 회색
        if (gearView != VK_NULL_HANDLE)
            gearTexId = ImGui_ImplVulkan_AddTexture(textureSampler, gearView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void createShadowResources() {
        VkFormat depthFormat = findDepthFormat(); 
        VkImageCreateInfo iInfo{}; iInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        iInfo.imageType = VK_IMAGE_TYPE_2D; iInfo.extent.width = 2048; iInfo.extent.height = 2048;
        iInfo.extent.depth = 1; iInfo.mipLevels = 1; iInfo.arrayLayers = 1;
        iInfo.format = depthFormat; iInfo.tiling = VK_IMAGE_TILING_OPTIMAL; iInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        iInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        iInfo.samples = VK_SAMPLE_COUNT_1_BIT; iInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateImage(device, &iInfo, nullptr, &shadowImage);
        
        VkMemoryRequirements memReqs; vkGetImageMemoryRequirements(device, shadowImage, &memReqs);
        VkMemoryAllocateInfo allocInfo{}; allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size; allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &allocInfo, nullptr, &shadowMemory);
        vkBindImageMemory(device, shadowImage, shadowMemory, 0);
        
        VkImageViewCreateInfo vInfo{}; vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vInfo.image = shadowImage; vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; vInfo.format = depthFormat;
        vInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vInfo.subresourceRange.baseMipLevel = 0; vInfo.subresourceRange.levelCount = 1;
        vInfo.subresourceRange.baseArrayLayer = 0; vInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &vInfo, nullptr, &shadowView);
        
        VkSamplerCreateInfo sInfo{}; sInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sInfo.magFilter = VK_FILTER_LINEAR; sInfo.minFilter = VK_FILTER_LINEAR;
        sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; 
        vkCreateSampler(device, &sInfo, nullptr, &shadowSampler);
        
        VkAttachmentDescription attachment{};
        attachment.format = depthFormat; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        
        VkAttachmentReference depthRef{}; depthRef.attachment = 0; depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 0; subpass.pDepthStencilAttachment = &depthRef;
        
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        VkRenderPassCreateInfo rpInfo{}; rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1; rpInfo.pAttachments = &attachment; rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass; rpInfo.dependencyCount = 2; rpInfo.pDependencies = deps.data();
        vkCreateRenderPass(device, &rpInfo, nullptr, &shadowRenderPass);
        
        VkFramebufferCreateInfo fbInfo{}; fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = shadowRenderPass; fbInfo.attachmentCount = 1; fbInfo.pAttachments = &shadowView;
        fbInfo.width = 2048; fbInfo.height = 2048; fbInfo.layers = 1;
        vkCreateFramebuffer(device, &fbInfo, nullptr, &shadowFramebuffer);
    }

    void createShadowPipeline() {
        VkPushConstantRange pcRange{}; pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; pcRange.offset = 0; pcRange.size = sizeof(PushConstants);
        VkPipelineLayoutCreateInfo layInfo{}; layInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; layInfo.setLayoutCount = 1; layInfo.pSetLayouts = &descriptorSetLayout; layInfo.pushConstantRangeCount = 1; layInfo.pPushConstantRanges = &pcRange;
        vkCreatePipelineLayout(device, &layInfo, nullptr, &shadowPipelineLayout);
        
        auto vertCode = readFile("shaders/shadow_vert.spv");
        VkShaderModule vertMod = createShaderModule(vertCode);
        VkPipelineShaderStageCreateInfo vS{}; vS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vS.stage = VK_SHADER_STAGE_VERTEX_BIT; vS.module = vertMod; vS.pName = "main";
        
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        VkPipelineVertexInputStateCreateInfo vI{}; vI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vI.vertexBindingDescriptionCount = 1; vI.pVertexBindingDescriptions = &bindingDescription;
        vI.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()); vI.pVertexAttributeDescriptions = attributeDescriptions.data();
        
        VkPipelineInputAssemblyStateCreateInfo iA{}; iA.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; iA.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        
        VkViewport vp{}; vp.width = 2048.0f; vp.height = 2048.0f; vp.maxDepth = 1.0f;
        VkRect2D sc{}; sc.extent = {2048, 2048};
        VkPipelineViewportStateCreateInfo vpS{}; vpS.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vpS.viewportCount = 1; vpS.pViewports = &vp; vpS.scissorCount = 1; vpS.pScissors = &sc;
        
        VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_FRONT_BIT; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
        rs.depthBiasEnable = VK_TRUE; rs.depthBiasConstantFactor = 1.25f; rs.depthBiasSlopeFactor = 1.75f;
        
        VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        
        VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        
        VkGraphicsPipelineCreateInfo pInfo{}; pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pInfo.stageCount = 1; pInfo.pStages = &vS; pInfo.pVertexInputState = &vI; pInfo.pInputAssemblyState = &iA; pInfo.pViewportState = &vpS; pInfo.pRasterizationState = &rs; pInfo.pMultisampleState = &ms; pInfo.pDepthStencilState = &ds; pInfo.layout = shadowPipelineLayout; pInfo.renderPass = shadowRenderPass;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &shadowPipeline);
        vkDestroyShaderModule(device, vertMod, nullptr);
    }

    // 오프스크린/블러 렌더 크기 = round(renderScale x swapChainExtent). 포스트/UI는 계속 swapChainExtent.
    void computeRenderExtent() {
        uint32_t w = std::max(1u, (uint32_t)std::lround(swapChainExtent.width  * settings.renderScale));
        uint32_t h = std::max(1u, (uint32_t)std::lround(swapChainExtent.height * settings.renderScale));
        renderExtent = {w, h};
    }

    void createOffscreenImages() {
        computeRenderExtent();
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT; VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        auto makeImg = [&](VkFormat fmt, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem, VkImageView& view, VkImageAspectFlags aspect, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) {
            VkImageCreateInfo iInfo{}; iInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; iInfo.imageType = VK_IMAGE_TYPE_2D;
            iInfo.extent.width = renderExtent.width; iInfo.extent.height = renderExtent.height; iInfo.extent.depth = 1; iInfo.mipLevels = 1; iInfo.arrayLayers = 1; iInfo.format = fmt; iInfo.tiling = VK_IMAGE_TILING_OPTIMAL; iInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; iInfo.usage = usage; iInfo.samples = samples; iInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateImage(device, &iInfo, nullptr, &img);
            VkMemoryRequirements mReqs; vkGetImageMemoryRequirements(device, img, &mReqs);
            VkMemoryAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            aInfo.allocationSize = mReqs.size; aInfo.memoryTypeIndex = findMemoryType(mReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device, &aInfo, nullptr, &mem); vkBindImageMemory(device, img, mem, 0);
            VkImageViewCreateInfo vInfo{}; vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vInfo.image = img; vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vInfo.format = fmt; vInfo.subresourceRange.aspectMask = aspect; vInfo.subresourceRange.baseMipLevel = 0; vInfo.subresourceRange.levelCount = 1; vInfo.subresourceRange.baseArrayLayer = 0; vInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &vInfo, nullptr, &view);
        };
        // resolve 대상(단일 샘플, 이후 블러/포스트가 SAMPLED). MSAA 색상/bright가 여기로 resolve된다.
        makeImg(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, offscreenColorImage, offscreenColorMem, offscreenColorView, VK_IMAGE_ASPECT_COLOR_BIT);
        makeImg(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, offscreenBrightImage, offscreenBrightMem, offscreenBrightView, VK_IMAGE_ASPECT_COLOR_BIT);

        // 멀티샘플 렌더 타겟들. 색상/bright는 resolve되므로 SAMPLED 불필요. 깊이는 MSAA 렌더 타겟 겸용.
        makeImg(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, msaaColorImage, msaaColorMem, msaaColorView, VK_IMAGE_ASPECT_COLOR_BIT, msaaSamples);
        makeImg(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, msaaBrightImage, msaaBrightMem, msaaBrightView, VK_IMAGE_ASPECT_COLOR_BIT, msaaSamples);
        makeImg(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, offscreenDepthImage, offscreenDepthMem, offscreenDepthView, VK_IMAGE_ASPECT_DEPTH_BIT, msaaSamples);

        // 어태치먼트 순서는 렌더패스와 반드시 일치: 0=MSAA색상 1=MSAA-bright 2=MSAA깊이 3=resolve색상 4=resolve-bright
        std::array<VkImageView, 5> fbAtts = {msaaColorView, msaaBrightView, offscreenDepthView, offscreenColorView, offscreenBrightView};
        VkFramebufferCreateInfo fbInfo{}; fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fbInfo.renderPass = offscreenRenderPass; fbInfo.attachmentCount = 5; fbInfo.pAttachments = fbAtts.data(); fbInfo.width = renderExtent.width; fbInfo.height = renderExtent.height; fbInfo.layers = 1;
        vkCreateFramebuffer(device, &fbInfo, nullptr, &offscreenFramebuffer);
    }

    void createOffscreenResources() {
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT; VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

        // 어태치먼트 순서는 프레임버퍼와 반드시 일치:
        //   0=MSAA색상 1=MSAA-bright 2=MSAA깊이 (전부 msaaSamples, resolve/렌더용이라 storeOp=DONT_CARE)
        //   3=resolve색상 4=resolve-bright (단일 샘플, loadOp=DONT_CARE로 resolve가 전체를 덮어씀, STORE, 이후 SAMPLED)
        std::array<VkAttachmentDescription, 5> atts{};
        atts[0].format = colorFormat; atts[0].samples = msaaSamples; atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        atts[1] = atts[0];
        atts[2].format = depthFormat; atts[2].samples = msaaSamples; atts[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        atts[3].format = colorFormat; atts[3].samples = VK_SAMPLE_COUNT_1_BIT; atts[3].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE; atts[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[3].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        atts[4] = atts[3];

        VkAttachmentReference colRefs[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        VkAttachmentReference depRef{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        // resolve 참조는 색상 참조와 인덱스가 1:1 대응한다. resolve[i]가 color[i]를 다운샘플한다.
        VkAttachmentReference resolveRefs[2] = {{3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 2; subpass.pColorAttachments = colRefs; subpass.pResolveAttachments = resolveRefs; subpass.pDepthStencilAttachment = &depRef;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0; deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL; deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpInfo{}; rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpInfo.attachmentCount = 5; rpInfo.pAttachments = atts.data(); rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass; rpInfo.dependencyCount = 2; rpInfo.pDependencies = deps.data();
        vkCreateRenderPass(device, &rpInfo, nullptr, &offscreenRenderPass);

        createOffscreenImages();

        VkSamplerCreateInfo sInfo{}; sInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO; sInfo.magFilter = VK_FILTER_LINEAR; sInfo.minFilter = VK_FILTER_LINEAR; sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(device, &sInfo, nullptr, &offscreenSampler);
    }

    void createBlurImages() {
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        auto makeImg = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
            VkImageCreateInfo iInfo{}; iInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; iInfo.imageType = VK_IMAGE_TYPE_2D; iInfo.extent.width = renderExtent.width; iInfo.extent.height = renderExtent.height; iInfo.extent.depth = 1; iInfo.mipLevels = 1; iInfo.arrayLayers = 1; iInfo.format = colorFormat; iInfo.tiling = VK_IMAGE_TILING_OPTIMAL; iInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; iInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; iInfo.samples = VK_SAMPLE_COUNT_1_BIT; iInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateImage(device, &iInfo, nullptr, &img);
            VkMemoryRequirements mReqs; vkGetImageMemoryRequirements(device, img, &mReqs);
            VkMemoryAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; aInfo.allocationSize = mReqs.size; aInfo.memoryTypeIndex = findMemoryType(mReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device, &aInfo, nullptr, &mem); vkBindImageMemory(device, img, mem, 0);
            VkImageViewCreateInfo vInfo{}; vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vInfo.image = img; vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; vInfo.format = colorFormat; vInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vInfo.subresourceRange.baseMipLevel = 0; vInfo.subresourceRange.levelCount = 1; vInfo.subresourceRange.baseArrayLayer = 0; vInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &vInfo, nullptr, &view);
        };

        for(int i = 0; i < 2; i++) makeImg(blurImages[i], blurMemories[i], blurViews[i]);

        for(int i = 0; i < 2; i++) {
            VkFramebufferCreateInfo fbInfo{}; fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fbInfo.renderPass = blurRenderPass; fbInfo.attachmentCount = 1; fbInfo.pAttachments = &blurViews[i]; fbInfo.width = renderExtent.width; fbInfo.height = renderExtent.height; fbInfo.layers = 1;
            vkCreateFramebuffer(device, &fbInfo, nullptr, &blurFramebuffers[i]);
        }
    }

    void createBlurResources() {
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkAttachmentDescription att{}; att.format = colorFormat; att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &colorRef;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0; deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL; deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpInfo{}; rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpInfo.attachmentCount = 1; rpInfo.pAttachments = &att; rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass; rpInfo.dependencyCount = 2; rpInfo.pDependencies = deps.data();
        vkCreateRenderPass(device, &rpInfo, nullptr, &blurRenderPass);

        createBlurImages();
    }

    void updateBlurDescriptorSets() {
        auto mI = [&](VkImageView v){ VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = offscreenSampler; return i; };
        VkDescriptorImageInfo i0 = mI(offscreenBrightView); VkDescriptorImageInfo i1 = mI(blurViews[0]); VkDescriptorImageInfo i2 = mI(blurViews[1]);
        std::array<VkWriteDescriptorSet, 3> wr{};
        for(int i=0; i<3; i++) { wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr[i].dstSet = blurDescriptorSets[i]; wr[i].dstBinding = 0; wr[i].descriptorCount = 1; wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; }
        wr[0].pImageInfo = &i0; wr[1].pImageInfo = &i1; wr[2].pImageInfo = &i2;
        vkUpdateDescriptorSets(device, 3, wr.data(), 0, nullptr);
    }

    void createBlurPipeline() {
        VkDescriptorSetLayoutBinding b{}; b.binding = 0; b.descriptorCount = 1; b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layInfo{}; layInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; layInfo.bindingCount = 1; layInfo.pBindings = &b;
        vkCreateDescriptorSetLayout(device, &layInfo, nullptr, &blurDescriptorSetLayout);

        VkDescriptorSetAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; aInfo.descriptorPool = descriptorPool; aInfo.descriptorSetCount = 3;
        std::vector<VkDescriptorSetLayout> layouts(3, blurDescriptorSetLayout); aInfo.pSetLayouts = layouts.data();
        vkAllocateDescriptorSets(device, &aInfo, blurDescriptorSets);

        updateBlurDescriptorSets();

        VkPushConstantRange pc{}; pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; pc.offset = 0; pc.size = sizeof(int);
        VkPipelineLayoutCreateInfo pLayInfo{}; pLayInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pLayInfo.setLayoutCount = 1; pLayInfo.pSetLayouts = &blurDescriptorSetLayout; pLayInfo.pushConstantRangeCount = 1; pLayInfo.pPushConstantRanges = &pc;
        vkCreatePipelineLayout(device, &pLayInfo, nullptr, &blurPipelineLayout);

        auto vCode = readFile("shaders/post_vert.spv"); auto fCode = readFile("shaders/blur_frag.spv");
        VkShaderModule vMod = createShaderModule(vCode); VkShaderModule fMod = createShaderModule(fCode);
        VkPipelineShaderStageCreateInfo vS{}; vS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; vS.stage = VK_SHADER_STAGE_VERTEX_BIT; vS.module = vMod; vS.pName = "main";
        VkPipelineShaderStageCreateInfo fS{}; fS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; fS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fS.module = fMod; fS.pName = "main";
        VkPipelineShaderStageCreateInfo sS[] = {vS, fS};

        VkPipelineVertexInputStateCreateInfo vI{}; vI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo iA{}; iA.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; iA.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{}; vp.width = swapChainExtent.width; vp.height = swapChainExtent.height; vp.maxDepth = 1.0f; VkRect2D sc{}; sc.extent = swapChainExtent;
        VkPipelineViewportStateCreateInfo vpS{}; vpS.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vpS.viewportCount = 1; vpS.pViewports = &vp; vpS.scissorCount = 1; vpS.pScissors = &sc;
        std::array<VkDynamicState, 2> dynamicStatesBlur = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicStateBlur{}; dynamicStateBlur.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dynamicStateBlur.dynamicStateCount = static_cast<uint32_t>(dynamicStatesBlur.size()); dynamicStateBlur.pDynamicStates = dynamicStatesBlur.data();
        VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; ds.depthTestEnable = VK_FALSE;
        VkPipelineColorBlendAttachmentState cbA{}; cbA.colorWriteMask = 0xF; cbA.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo cbS{}; cbS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cbS.attachmentCount = 1; cbS.pAttachments = &cbA;

        VkGraphicsPipelineCreateInfo pInfo{}; pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pInfo.stageCount = 2; pInfo.pStages = sS; pInfo.pVertexInputState = &vI; pInfo.pInputAssemblyState = &iA; pInfo.pViewportState = &vpS; pInfo.pRasterizationState = &rs; pInfo.pMultisampleState = &ms; pInfo.pDepthStencilState = &ds; pInfo.pColorBlendState = &cbS; pInfo.layout = blurPipelineLayout; pInfo.renderPass = blurRenderPass; pInfo.pDynamicState = &dynamicStateBlur;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &blurPipeline);
        vkDestroyShaderModule(device, fMod, nullptr); vkDestroyShaderModule(device, vMod, nullptr);
    }

    void updatePostDescriptorSets() {
        auto mI = [&](VkImageView v){ VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = offscreenSampler; return i; };
        VkDescriptorImageInfo cI = mI(offscreenColorView); VkDescriptorImageInfo brI = mI(blurViews[1]);
        std::array<VkWriteDescriptorSet, 2> wr{};
        wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr[0].dstSet = postDescriptorSet; wr[0].dstBinding = 0; wr[0].descriptorCount = 1; wr[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr[0].pImageInfo = &cI;
        wr[1] = wr[0]; wr[1].dstBinding = 1; wr[1].pImageInfo = &brI;
        vkUpdateDescriptorSets(device, 2, wr.data(), 0, nullptr);
    }

    void createPostProcessPipeline() {
        std::array<VkDescriptorSetLayoutBinding, 2> b{};
        b[0].binding = 0; b[0].descriptorCount = 1; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b[1].binding = 1; b[1].descriptorCount = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layInfo{}; layInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; layInfo.bindingCount = 2; layInfo.pBindings = b.data();
        vkCreateDescriptorSetLayout(device, &layInfo, nullptr, &postDescriptorSetLayout);

        VkDescriptorSetAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; aInfo.descriptorPool = descriptorPool; aInfo.descriptorSetCount = 1; aInfo.pSetLayouts = &postDescriptorSetLayout;
        vkAllocateDescriptorSets(device, &aInfo, &postDescriptorSet);

        updatePostDescriptorSets();

        VkPushConstantRange postPcRange{}; postPcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; postPcRange.offset = 0; postPcRange.size = sizeof(float);
        VkPipelineLayoutCreateInfo pLayInfo{}; pLayInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pLayInfo.setLayoutCount = 1; pLayInfo.pSetLayouts = &postDescriptorSetLayout; pLayInfo.pushConstantRangeCount = 1; pLayInfo.pPushConstantRanges = &postPcRange;
        vkCreatePipelineLayout(device, &pLayInfo, nullptr, &postPipelineLayout);

        auto vCode = readFile("shaders/post_vert.spv"); auto fCode = readFile("shaders/post_frag.spv");
        VkShaderModule vMod = createShaderModule(vCode); VkShaderModule fMod = createShaderModule(fCode);
        VkPipelineShaderStageCreateInfo vS{}; vS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; vS.stage = VK_SHADER_STAGE_VERTEX_BIT; vS.module = vMod; vS.pName = "main";
        VkPipelineShaderStageCreateInfo fS{}; fS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; fS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fS.module = fMod; fS.pName = "main";
        VkPipelineShaderStageCreateInfo sS[] = {vS, fS};

        VkPipelineVertexInputStateCreateInfo vI{}; vI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo iA{}; iA.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; iA.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{}; vp.width = swapChainExtent.width; vp.height = swapChainExtent.height; vp.maxDepth = 1.0f; VkRect2D sc{}; sc.extent = swapChainExtent;
        VkPipelineViewportStateCreateInfo vpS{}; vpS.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vpS.viewportCount = 1; vpS.pViewports = &vp; vpS.scissorCount = 1; vpS.pScissors = &sc;
        std::array<VkDynamicState, 2> dynamicStatesPost = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicStatePost{}; dynamicStatePost.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dynamicStatePost.dynamicStateCount = static_cast<uint32_t>(dynamicStatesPost.size()); dynamicStatePost.pDynamicStates = dynamicStatesPost.data();
        VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; ds.depthTestEnable = VK_FALSE;
        VkPipelineColorBlendAttachmentState cbA{}; cbA.colorWriteMask = 0xF; cbA.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo cbS{}; cbS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cbS.attachmentCount = 1; cbS.pAttachments = &cbA;

        VkGraphicsPipelineCreateInfo pInfo{}; pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pInfo.stageCount = 2; pInfo.pStages = sS; pInfo.pVertexInputState = &vI; pInfo.pInputAssemblyState = &iA; pInfo.pViewportState = &vpS; pInfo.pRasterizationState = &rs; pInfo.pMultisampleState = &ms; pInfo.pDepthStencilState = &ds; pInfo.pColorBlendState = &cbS; pInfo.layout = postPipelineLayout; pInfo.renderPass = renderPass; pInfo.pDynamicState = &dynamicStatePost;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &postPipeline);
        vkDestroyShaderModule(device, fMod, nullptr); vkDestroyShaderModule(device, vMod, nullptr);
    }

    // 렌더러가 그리는 좌표계로 월드 좌표를 옮긴다. 모든 모델 행렬/UBO 좌표/피킹이 이 함수를 거친다.
    // 뺄셈을 double로 해야 하는 이유: 리얼 스케일에서 두 항이 모두 46000 수준이라 float32로 빼면
    // 결과(수십분의 1 유닛)에 0.0039짜리 오차가 남아 왜소행성(반지름 0.09)이 떨리고 각져 보인다.
    glm::vec3 relativeToCamera(const glm::dvec3& worldPos) const {
        return glm::vec3(worldPos - camera.target);
    }

    // 천체들의 위치가 확정되고 카메라까지 갱신된 뒤에 호출된다.
    // 모델 행렬은 카메라 상대 좌표를 쓰므로 반드시 이번 프레임의 camera.target 확정 후여야 한다.
    void buildBodyModelMatrices(float easeScale, float slowTime, float time) {
        float curSunRadius = glm::mix(sun.radius, sun.realRadius, easeScale);
        sun.currentModelMat = glm::translate(glm::mat4(1.0f), relativeToCamera(sun.currentPosition))
                            * glm::rotate(glm::mat4(1.0f), glm::radians(sun.axisDirection), glm::vec3(0.0f, 1.0f, 0.0f))
                            * glm::rotate(glm::mat4(1.0f), glm::radians(sun.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f))
                            * glm::rotate(glm::mat4(1.0f), time * glm::radians(sun.rotationSpeed), glm::vec3(0.0f, 1.0f, 0.0f))
                            * glm::scale(glm::mat4(1.0f), glm::vec3(curSunRadius));

        for (int i = 0; i < planets.size(); i++) {
            auto &planet = planets[i];
            float curRadius = glm::mix(planet.radius, planet.realRadius, easeScale);

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
        }

        float curMoonRadius = glm::mix(moon.radius, moon.realRadius, easeScale);
        glm::mat4 moonBase = glm::translate(glm::mat4(1.0f), relativeToCamera(moon.currentPosition));
        glm::mat4 moonRot = glm::rotate(glm::mat4(1.0f), glm::radians(moon.axisDirection), glm::vec3(0.0f, 1.0f, 0.0f))
                          * glm::rotate(glm::mat4(1.0f), glm::radians(moon.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f))
                          * glm::rotate(glm::mat4(1.0f), slowTime * glm::radians(moon.rotationSpeed), glm::vec3(0.0f, 1.0f, 0.0f));
        moon.currentModelMat = moonBase * moonRot * glm::scale(glm::mat4(1.0f), glm::vec3(curMoonRadius));
    }

    void updateUniformBuffer() override {
        static auto lastTimePoint = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTimePoint).count();
        lastTimePoint = currentTime;

        static bool spacePressedLastFrame = false;
        bool spacePressed = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spacePressed && !spacePressedLastFrame) isPaused = !isPaused;
        spacePressedLastFrame = spacePressed;
        if (!isPaused) currentAppTime += deltaTime;
        float time = currentAppTime; 

        static float simulationTime = 0.0f;
        static bool isSimInit = false;
        if (!isSimInit) { simulationTime = time * 0.5f; isSimInit = true; } // 첫 프레임 동기화

        if (isRealScaleMode) scaleLerp += deltaTime * 0.5f; 
        else scaleLerp -= deltaTime * 0.5f; 
        scaleLerp = std::clamp(scaleLerp, 0.0f, 1.0f);
        
        // 자연스럽게 출발하고 도착하는 Smoothstep 곡선 공식 적용
        float easeScale = scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp);

        // 체험 버튼(isEclipseEvent)이 눌리지 않았을 때만 시간이 흐릅니다!
        if (!isEclipseEvent && !isPaused) {
            simulationTime += deltaTime * 0.5f; 
        }
        
        float slowTime = simulationTime;

        // 🚀 1. 태양(Sun)은 월드 원점에 고정. 모델 행렬은 buildBodyModelMatrices()에서 만든다.
        sun.currentPosition = glm::dvec3(0.0);

        // 🚀 2. 행성 및 위성의 '위치'만 double로 계산한다.
        //    모델 행렬은 카메라 업데이트가 끝난 뒤 buildBodyModelMatrices()에서 만든다.
        //    (카메라 상대 좌표를 쓰려면 모델 행렬이 이번 프레임의 camera.target을 봐야 하기 때문)
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

        // =========================================================
        // 카메라 추적 업데이트
        // =========================================================
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
        
        static bool isFirstFrame = true;
        if (isFirstFrame) {
            camera.target = nextTarget;
            camera.lastNewTarget = nextTarget;
            camera.targetDistance = targetRadius * 6.0f;
            camera.currentDistance = targetRadius * 6.0f;
            isFirstFrame = false;
        }

        // 🚀 [NEW] 천체의 팽창/수축 배율을 프레임 단위로 실시간 추적하여 카메라에 완벽 동기화!
        static float lastTargetRadius = targetRadius;
        static int lastTargetType = lockedTargetType;
        static int lastTargetIndex = lockedPlanetIndex;

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
        static float appliedFov = -1.0f; // 첫 프레임에 로드된 FOV를 강제로 반영
        camera.baseFov = settings.fovDegrees;
        if (settings.fovDegrees != appliedFov) {
            appliedFov = settings.fovDegrees;
            camera.fov = settings.fovDegrees;
        } else if (camera.fov > camera.baseFov) {
            camera.fov = camera.baseFov;
        }

        // =========================================================
        // 🚀 시네마틱 개기일식 (궤도 고정 & 감속 정지 연출)
        // =========================================================
        static float eclipseLerp = 0.0f;
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

        // 반드시 일식 블록 '뒤'여야 한다. 저 블록이 camera.target을 한 번 더 덮어쓰기 때문에,
        // 여기보다 위에서 모델 행렬을 만들면 모델은 일식 전 타겟을, 뷰 행렬은 일식 후 타겟을
        // 보게 되어 카메라 상대 좌표계에서 천체가 통째로 어긋난다.
        buildBodyModelMatrices(easeScale, slowTime, time);
        updateLockedOrbitLine(easeScale, slowTime);

        UniformBufferObject ubo{};
        ubo.view = camera.getViewMatrix();
        ubo.proj = glm::perspective(glm::radians(camera.fov), swapChainExtent.width / (float)swapChainExtent.height, 0.01f, 1000000.0f);
        ubo.proj[1][1] *= -1;
        ubo.cameraPos = relativeToCamera(camera.getEyeWorld());
        ubo.time = time;
        ubo.sunPos = relativeToCamera(sun.currentPosition); ubo.sunRadius = sun.radius;
        ubo.earthPos = relativeToCamera(planets[2].currentPosition); ubo.earthRadius = planets[2].radius;
        ubo.moonPos = relativeToCamera(moon.currentPosition); ubo.moonRadius = moon.radius;

        // 태양(원점)에 고정된 상태면 태양 자신을 그림자 초점으로 삼을 수 없으므로 지구로 대체한다.
        glm::dvec3 shadowFocusWorld = nextTarget;
        if (lockedTargetType == 0) shadowFocusWorld = planets[2].currentPosition;
        glm::vec3 shadowFocusPos = relativeToCamera(shadowFocusWorld);
        glm::vec3 sunPosRel = relativeToCamera(sun.currentPosition);

        glm::vec3 lightDir = glm::normalize(shadowFocusPos - sunPosRel);
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if(abs(glm::dot(lightDir, up)) > 0.99f) up = glm::vec3(0.0f, 0.0f, 1.0f);
        glm::mat4 lightView = glm::lookAt(sunPosRel, shadowFocusPos, up);
        float orthoSize = targetRadius * 5.0f + 2.0f; 
        if (lockedTargetType == 0) orthoSize = 30.0f;
        
        glm::mat4 lightProj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, 200.0f);
        lightProj[1][1] *= -1;
        ubo.lightSpaceMatrix = lightProj * lightView;
        
        memcpy(uniformBufferMapped, &ubo, sizeof(ubo));

        memcpy(indirectDrawMapped, drawCmdTemplates, 12 * sizeof(VkDrawIndexedIndirectCommand));

        ImGui_ImplVulkan_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

        // =========================================================
        // 0. 우측 상단 설정(기어) 버튼 (LOBBY/SIMULATION 공통) + 설정 창
        // =========================================================
        ImGui::SetNextWindowPos(ImVec2(swapChainExtent.width - 84.0f, 20.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(64.0f, 64.0f), ImGuiCond_Always);
        ImGui::Begin("Gear", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));   // 배경 완전 투명 (기어만 보이도록)
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));   // 호버 하이라이트 없음
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
        bool gearClicked = gearTexId
            ? ImGui::ImageButton("gear", gearTexId, ImVec2(40.0f, 40.0f))   // assets/settings.png 아이콘
            : ImGui::Button("[=]", ImVec2(50.0f, 40.0f));                   // 로드 실패 시 텍스트 폴백
        if (gearClicked) settingsOpen = !settingsOpen;
        ImGui::PopStyleColor(3);
        ImGui::End();

        // =========================================================
        // 0-b. 우측 하단 EXIT 버튼 (LOBBY/SIMULATION 공통) — 앱 자체를 종료한다.
        //      메뉴의 플랫 사이파이 스타일을 따르되, 종료 의미로 붉은 톤을 쓴다.
        // =========================================================
        {
            const float exitW = 96.0f, exitH = 38.0f;
            ImGui::SetNextWindowPos(ImVec2(swapChainExtent.width - exitW - 30.0f, swapChainExtent.height - exitH - 26.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(exitW + 20.0f, exitH + 20.0f), ImGuiCond_Always);
            ImGui::Begin("ExitBtn", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.15f, 0.15f, 0.22f)); // 평소 투명한 붉은 톤
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.22f, 0.22f, 0.60f)); // 호버 시 채워짐
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f, 0.30f, 0.30f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.90f, 0.40f, 0.40f, 0.85f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            if (ImGui::Button("EXIT", ImVec2(exitW, exitH))) glfwSetWindowShouldClose(window, GLFW_TRUE);
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
            ImGui::End();
        }

        applyLiveSettings();            // settings -> 기존 상태 동기화 (창이 닫혀 있어도 매 프레임)
        if (settingsOpen) drawSettingsWindow();

        if (settings.showFps) {
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
            ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
            ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
            ImGui::End();
        }

        if (currentAppState == AppState::LOBBY) {
            // ---------------------------------------------------------
            // ① 대기 화면(LOBBY): 우주 시뮬레이션 스타일 HUD
            // 박스형 카드 대신, 살아있는 씬 위에 은은한 그라데이션 스크림 띠 +
            // 얇은 HUD 액센트 라인 + 플랫한 사이파이 버튼으로 구성한다.
            // ---------------------------------------------------------
            float cx = swapChainExtent.width * 0.5f;
            float cy = swapChainExtent.height * 0.5f;

            // 1) 가독성용 수평 그라데이션 스크림 (배경 드로리스트, 위아래로 부드럽게 페이드 → 하드 엣지 없음)
            ImDrawList* bgList = ImGui::GetBackgroundDrawList();
            const float bandH = 175.0f;
            const ImU32 scrimDark = IM_COL32(3, 6, 12, 150);
            const ImU32 scrimClear = IM_COL32(3, 6, 12, 0);
            bgList->AddRectFilledMultiColor(ImVec2(0.0f, cy - bandH), ImVec2((float)swapChainExtent.width, cy), scrimClear, scrimClear, scrimDark, scrimDark);
            bgList->AddRectFilledMultiColor(ImVec2(0.0f, cy), ImVec2((float)swapChainExtent.width, cy + bandH), scrimDark, scrimDark, scrimClear, scrimClear);

            // 2) 투명·오토리사이즈 창을 화면 중앙(피벗 0.5,0.5)에 → 빈 여백 없이 콘텐츠만
            ImGui::SetNextWindowPos(ImVec2(cx, cy), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::Begin("Main Menu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize);

            // 타이틀
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.93f, 1.0f, 1.0f));
            ImGui::SetWindowFontScale(2.3f);
            TextCentered("SOLAR SYSTEM ENGINE");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();

            // 타이틀 밑 HUD 액센트 라인 (타이틀 폭에 맞춰 가운데, 시안 + 양 끝 캡)
            {
                ImVec2 rmin = ImGui::GetItemRectMin();
                ImVec2 rmax = ImGui::GetItemRectMax();
                float lineY = rmax.y + 5.0f;
                float lineCx = (rmin.x + rmax.x) * 0.5f;
                float lineHalf = (rmax.x - rmin.x) * 0.5f;
                ImDrawList* wl = ImGui::GetWindowDrawList();
                wl->AddLine(ImVec2(lineCx - lineHalf, lineY), ImVec2(lineCx + lineHalf, lineY), IM_COL32(70, 150, 230, 200), 1.5f);
                wl->AddCircleFilled(ImVec2(lineCx - lineHalf, lineY), 2.5f, IM_COL32(120, 200, 255, 230));
                wl->AddCircleFilled(ImVec2(lineCx + lineHalf, lineY), 2.5f, IM_COL32(120, 200, 255, 230));
            }

            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            // 태그라인 (자간을 넓힌 HUD 느낌으로 공백 삽입)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.62f, 0.72f, 1.0f));
            ImGui::SetWindowFontScale(0.95f);
            TextCentered("R E A L - T I M E   V U L K A N   S I M U L A T I O N     v2.0");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();

            ImGui::Dummy(ImVec2(0.0f, 24.0f));

            // 플랫 사이파이 버튼: 평소 투명한 시안 + 테두리, 호버 시 채워짐
            const float btnW = 270.0f, btnH = 46.0f;
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - btnW) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.45f, 0.75f, 0.22f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.60f, 0.95f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.30f, 0.70f, 1.00f, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.45f, 0.75f, 1.0f, 0.9f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::SetWindowFontScale(1.15f);
            if (ImGui::Button("LAUNCH  MISSION", ImVec2(btnW, btnH))) {
                currentAppState = AppState::SIMULATION; // 시뮬레이션 진입
            }
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);

            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.5f, 0.58f, 1.0f));
            ImGui::SetWindowFontScale(0.85f);
            TextCentered("Press LAUNCH to enter the simulation");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();

            ImGui::End();
        }
        else if (currentAppState == AppState::SIMULATION) {
            // ---------------------------------------------------------
            // ② 인게임 시뮬레이션(SIMULATION) 상태의 UI 연출
            // ---------------------------------------------------------
            ImGui::SetNextWindowPos(ImVec2(0, 0)); 
            ImGui::SetNextWindowSize(ImVec2((float)swapChainExtent.width, (float)swapChainExtent.height));
            
            // 1. 기존 HUD 오버레이 (행성 이름표)
            ImGui::Begin("Overlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
            ImDrawList* drawList = ImGui::GetWindowDrawList(); glm::vec2 screenPos;
            glm::mat4 viewMat = camera.getViewMatrix();
            glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), swapChainExtent.width / (float)swapChainExtent.height, 0.01f, 1000000.0f); projMat[1][1] *= -1;

            auto drawTagBox = [&](glm::vec3 pos, const std::string& text, ImU32 col, float yOffset, bool isSelected) {
                if (worldToScreen(pos, viewMat, projMat, swapChainExtent.width, swapChainExtent.height, screenPos)) {
                    // 🚀 [수정됨] 텍스트 크기를 정밀하게 측정하고 패딩(여백)을 부여합니다.
                    ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
                    ImVec2 padding = ImVec2(8.0f, 4.0f); // 좌우 8px, 상하 4px 여백
                    
                    float w = textSize.x + padding.x * 2.0f;
                    float h = textSize.y + padding.y * 2.0f;
                    
                    // 중앙 정렬: 렌더링 시작점(X)을 전체 박스 길이의 절반만큼 왼쪽으로 옮김
                    ImVec2 pMin = ImVec2(screenPos.x - (w * 0.5f), screenPos.y - yOffset);
                    ImVec2 pMax = ImVec2(pMin.x + w, pMin.y + h);
                    
                    // 🚀 [디자인] SF 레이더 스타일의 형광 초록(Neon Green) 테마 적용
                    // 선택됨: 밝고 쨍한 형광 초록 테두리 + 짙은 초록색 반투명 배경
                    // 미선택: 어둡고 탁한 초록 테두리 + 검은색 반투명 배경
                    ImU32 bgCol = isSelected ? IM_COL32(10, 50, 20, 220) : IM_COL32(0, 0, 0, 150);
                    ImU32 borderCol = isSelected ? IM_COL32(57, 255, 20, 255) : IM_COL32(30, 120, 30, 150);
                    
                    // 선택 시 글자색도 흰색/회색에서 밝은 형광 초록으로 오버라이드
                    ImU32 textCol = isSelected ? IM_COL32(57, 255, 20, 255) : col;
                    
                    drawList->AddRectFilled(pMin, pMax, bgCol, 4.0f);
                    drawList->AddRect(pMin, pMax, borderCol, 4.0f, 0, isSelected ? 2.0f : 1.0f);
                    
                    // 계산된 패딩 위치에 텍스트를 그리면 소름 돋도록 완벽한 중앙 정렬 완성!
                    drawList->AddText(ImVec2(pMin.x + padding.x, pMin.y + padding.y), textCol, text.c_str());
                }
            };

            drawTagBox(relativeToCamera(sun.currentPosition), "Sun", IM_COL32(255, 200, 0, 255), 35.0f, (selectedTargetType == 0));
            for (int i = 0; i < planets.size(); i++) {
                drawTagBox(relativeToCamera(planets[i].currentPosition), planets[i].name, IM_COL32(255, 255, 255, 200), 35.0f, (selectedTargetType == 1 && selectedPlanetIndex == i));
            }
            drawTagBox(relativeToCamera(moon.currentPosition), "Moon", IM_COL32(200, 200, 200, 200), 25.0f, (selectedTargetType == 2));
            ImGui::End();

            // =========================================================
            // 2. 좌측 상단 정보 패널 (단일 클릭 시 등장)
            // =========================================================
            ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("Information Panel", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);

            if (selectedTargetType != -1) {
                std::string targetName = "";
                if (selectedTargetType == 0) targetName = "Sun";
                else if (selectedTargetType == 1) targetName = planets[selectedPlanetIndex].name;
                else if (selectedTargetType == 2) targetName = "Moon";

                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.8f, 1.0f), ">> TARGET : %s", targetName.c_str());
                ImGui::Spacing();
                ImGui::Text("%s", getCelestialInfo(targetName).c_str());
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                if (selectedTargetType == 2) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.8f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.9f, 0.9f));
                    if (ImGui::Button(isEclipseEvent ? "EXIT ECLIPSE MODE" : "EXPERIENCE TOTAL ECLIPSE", ImVec2(240.0f, 40.0f))) {
                        isEclipseEvent = !isEclipseEvent; 
                    }
                    ImGui::PopStyleColor(2);
                }
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select a celestial body\nor name tag to view info.");
            }
            ImGui::End();
        }

        ImGui::Render();
    }

    void drawSettingsWindow() {
        // 설정 창은 해상도가 바뀌어도 항상 화면 중앙에 고정한다(같은 상대 위치 + 잘림 방지).
        // 크기는 고정: 내용이 고정된 컨트롤 목록이라 해상도에 맞춰 키우면 여백만 늘어 비어 보인다.
        // 440x480은 최소 해상도(1280x720)에도 여유롭게 들어간다. NoMove로 드래그 스냅백도 방지.
        const ImVec2 settingsSize(440.0f, 480.0f);
        ImGui::SetNextWindowPos(ImVec2(swapChainExtent.width * 0.5f - settingsSize.x * 0.5f,
                                       swapChainExtent.height * 0.5f - settingsSize.y * 0.5f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(settingsSize, ImGuiCond_Always);
        ImGui::Begin("SETTINGS", &settingsOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

        ImGui::SeparatorText("GRAPHICS");

        // Resolution / Render Scale / MSAA / VSync: 값은 여기서 고르고, 실제 적용은 Task 3-5에서
        // pendingRecreate 경로가 담당한다. 이 태스크에서는 위젯만 배치하고 settings에 반영한다.
        {
            // Display: 창모드 해상도 목록 + 마지막 항목 "Fullscreen"을 하나의 셀렉터로 통합.
            // 전체화면에서는 해상도 조정이 무의미하므로(항상 모니터 네이티브로 렌더) 합쳤다.
            // 저해상도로 "다운스케일"된 느낌이 필요하면 아래 Render Scale이 그 역할을 한다.
            const char* displayLabels[] = {"1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Fullscreen"};
            int displayIdx = settings.fullscreen ? RESOLUTION_COUNT : settings.resolutionIndex;
            if (ImGui::Combo("Display", &displayIdx, displayLabels, RESOLUTION_COUNT + 1)) {
                if (displayIdx == RESOLUTION_COUNT) {
                    settings.fullscreen = true;              // resolutionIndex는 유지 → 해제 시 복귀
                } else {
                    settings.fullscreen = false;
                    settings.resolutionIndex = displayIdx;
                }
            }

            const char* scaleLabels[] = {"0.5x", "0.75x", "1.0x", "1.5x", "2.0x"};
            static const float scaleVals[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
            int scaleIdx = 2; for (int i = 0; i < 5; ++i) if (scaleVals[i] == settings.renderScale) scaleIdx = i;
            if (ImGui::Combo("Render Scale", &scaleIdx, scaleLabels, 5)) settings.renderScale = scaleVals[scaleIdx];

            const char* msaaLabels[] = {"Off", "2x", "4x", "8x"};
            static const int msaaVals[] = {0, 2, 4, 8};
            int msaaIdx = 3; for (int i = 0; i < 4; ++i) if (msaaVals[i] == settings.msaaLevel) msaaIdx = i;
            if (ImGui::Combo("Anti-aliasing", &msaaIdx, msaaLabels, 4)) settings.msaaLevel = msaaVals[msaaIdx];

            int vsyncIdx = settings.vsync ? 0 : 1;
            const char* vsyncLabels[] = {"On", "Off"};
            if (ImGui::Combo("VSync", &vsyncIdx, vsyncLabels, 2)) settings.vsync = (vsyncIdx == 0);
        }

        ImGui::SliderFloat("Field of View", &settings.fovDegrees, 30.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("Brightness",    &settings.exposure,   0.3f, 3.0f, "%.2f");

        {
            const char* capLabels[] = {"Unlimited", "30", "60", "120", "144"};
            static const int capVals[] = {0, 30, 60, 120, 144};
            int capIdx = 0; for (int i = 0; i < 5; ++i) if (capVals[i] == settings.frameCap) capIdx = i;
            if (ImGui::Combo("Frame Cap", &capIdx, capLabels, 5)) settings.frameCap = capVals[capIdx];
        }
        ImGui::Checkbox("Show FPS", &settings.showFps);

        ImGui::SeparatorText("VIEW");
        bool inSim = (currentAppState == AppState::SIMULATION);
        if (!inSim) ImGui::BeginDisabled();
        ImGui::Checkbox("Orbit Lines", &settings.orbitLines);
        ImGui::Checkbox("Real Scale", &settings.realScale);
        if (!inSim) ImGui::EndDisabled();

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120.0f, 30.0f))) { settingsOpen = false; saveSettings(); }

        ImGui::End();
    }

    // settings -> 기존 파생 상태로 동기화. 설정 창이 닫혀 있어도 매 프레임 호출된다.
    void applyLiveSettings() {
        showOrbits      = settings.orbitLines;
        isRealScaleMode = settings.realScale;   // scaleLerp 타이머가 이 값을 향해 전진한다(기존 로직)
        if (settings.fullscreen != isFullscreen) fullscreenToggleRequested = true;
    }

    void setViewport(VkCommandBuffer cb, VkExtent2D e) {
        VkViewport vp{}; vp.width = (float)e.width; vp.height = (float)e.height; vp.maxDepth = 1.0f;
        VkRect2D sc{}; sc.extent = e;
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
    }
    void setFullViewport(VkCommandBuffer cb) { setViewport(cb, swapChainExtent); } // 포스트/스왑체인 패스용

    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex) override {
        VkCommandBufferBeginInfo beginInfo{}; beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cb, &beginInfo);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet, 0, nullptr);
        
        uint32_t groupCount = (static_cast<uint32_t>(asteroidTransforms.size()) + 255) / 256;
        vkCmdDispatch(cb, groupCount, 1, 1); 

        VkMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; 
        barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT; 
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

        // 1. 섀도우 맵핑 패스
        VkRenderPassBeginInfo shadowPassInfo{}; shadowPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        shadowPassInfo.renderPass = shadowRenderPass; shadowPassInfo.framebuffer = shadowFramebuffer;
        shadowPassInfo.renderArea.offset = {0, 0}; shadowPassInfo.renderArea.extent = {2048, 2048}; 
        VkClearValue depthClear; depthClear.depthStencil = {1.0f, 0};
        shadowPassInfo.clearValueCount = 1; shadowPassInfo.pClearValues = &depthClear;

        vkCmdBeginRenderPass(cb, &shadowPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
        
        VkBuffer vertexBuffers[] = {vertexBuffer}; VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets); vkCmdBindIndexBuffer(cb, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        auto drawShadowObject = [&](const Planet &p, glm::mat4 mat) {
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout, 0, 1, &p.descriptorSet, 0, nullptr);
            PushConstants pc{mat, 0};
            vkCmdPushConstants(cb, shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
            vkCmdDrawIndexed(cb, sphereIndexCount, 1, 0, 0, 0); 
        };
        for (const auto &planet : planets) drawShadowObject(planet, planet.currentModelMat);
        drawShadowObject(moon, moon.currentModelMat);

        vkCmdEndRenderPass(cb);

        VkRenderPassBeginInfo offscreenPassInfo{}; offscreenPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        offscreenPassInfo.renderPass = offscreenRenderPass; offscreenPassInfo.framebuffer = offscreenFramebuffer;
        offscreenPassInfo.renderArea.offset = {0, 0}; offscreenPassInfo.renderArea.extent = renderExtent;

        std::array<VkClearValue, 3> clearValues{};
        clearValues[0].color = {{0.01f, 0.01f, 0.01f, 1.0f}}; clearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; clearValues[2].depthStencil = {1.0f, 0};
        offscreenPassInfo.clearValueCount = 3; offscreenPassInfo.pClearValues = clearValues.data();
        
        vkCmdBeginRenderPass(cb, &offscreenPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        setViewport(cb, renderExtent);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets); vkCmdBindIndexBuffer(cb, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // 🚀 [수정됨] 소행성대와 카이퍼벨트도 리얼 스케일 팽창 적용
        float easeScale = scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp); 
        // 행성들의 평균 팽창 비율(약 200배)을 스케일에 곱해 벨트 전체를 우주 외곽으로 밀어냅니다.
        float beltScale = glm::mix(1.0f, 200.0f, easeScale); 

        // 소행성 행렬(SSBO)은 월드 원점 기준이므로, 원점을 렌더 좌표계로 옮겨 앞에 붙인다.
        glm::mat4 astRot = glm::translate(glm::mat4(1.0f), relativeToCamera(glm::dvec3(0.0)))
                         * glm::rotate(glm::mat4(1.0f), currentAppTime * 0.05f, glm::vec3(0.0f, 1.0f, 0.0f));
        astRot = glm::scale(astRot, glm::vec3(beltScale)); // 전체 입자에 스케일 적용!
        
        PushConstants pcAst{astRot, 8}; 
        vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pcAst);

        for (int type = 0; type < 4; ++type) {
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &asteroidTypes[type].descriptorSet, 0, nullptr);
            for (int lod = 0; lod < 3; ++lod) {
                int bucketIndex = lod * 4 + type;
                VkDeviceSize offset = bucketIndex * sizeof(VkDrawIndexedIndirectCommand);
                vkCmdDrawIndexedIndirect(cb, indirectDrawBuffer, offset, 1, sizeof(VkDrawIndexedIndirectCommand));
            }
        }

        auto drawObject = [&](const Planet &p, glm::mat4 mat, int type) {
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &p.descriptorSet, 0, nullptr);
            PushConstants pc{mat, type}; vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
            vkCmdDrawIndexed(cb, sphereIndexCount, 1, 0, 0, 0); 
        };

        glm::vec3 currentCameraPos = glm::vec3(camera.getEyeWorld());
        float distToSun = glm::length(currentCameraPos - glm::vec3(sun.currentPosition));
        bool shouldRenderSun = distToSun > (sun.radius * 2.6f);

        if (shouldRenderSun) {
            drawObject(sun, sun.currentModelMat, sun.typeId); 
        }

        for (const auto &planet : planets) {
            float distToPlanet = glm::length(currentCameraPos - glm::vec3(planet.currentPosition));
            if (distToPlanet > (planet.radius * 1.2f)) {
                drawObject(planet, planet.currentModelMat, planet.typeId);
                if (planet.hasClouds) drawObject(planet, planet.cloudModelMat, 2);
                if (planet.name == "Saturn") {
                    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &saturnRing.descriptorSet, 0, nullptr);
                    PushConstants ringPush{planet.currentModelMat, 5}; 
                    vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &ringPush);
                    vkCmdDrawIndexed(cb, ringIndexCount, 1, sphereIndexCount, ringVertexOffset, 0);
                }
            }
        }

        float distToMoon = glm::length(currentCameraPos - glm::vec3(moon.currentPosition));
        if (distToMoon > (moon.radius * 1.2f)) {
            drawObject(moon, moon.currentModelMat, moon.typeId);
        }
        
        drawObject(sun, glm::rotate(glm::mat4(1.0f), currentAppTime * glm::radians(0.5f), glm::vec3(0.0f, 1.0f, 0.0f)), 3);
        
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &milkyWay.descriptorSet, 0, nullptr);
        
        float galaxyDrop = glm::mix(-15.0f, -2000.0f, scaleLerp);
        
        float fadeStart = glm::mix(1500.0f, 250000.0f, scaleLerp);
        float fadeEnd   = glm::mix(5000.0f, 900000.0f, scaleLerp); 
        
        float galaxyAlpha = (camera.currentDistance - fadeStart) / (fadeEnd - fadeStart);
        galaxyAlpha = std::clamp(galaxyAlpha, 0.0f, 1.0f);
        // pow(t,3)은 거의 안 보이다가 끝에서 확 뜨는 딱딱한 곡선이라 부자연스러웠다.
        // smoothstep(ease-in-out)으로 바꿔 자연스럽게 서서히 나타나게 한다.
        galaxyAlpha = galaxyAlpha * galaxyAlpha * (3.0f - 2.0f * galaxyAlpha);
        
        // 🚀 [핵심 추가] 천문학적 고증: 은하의 중심핵(Core)을 태양계로부터 X축 방향으로 확 밀어냅니다!
        // 우리 태양계는 은하 중심으로부터 반경의 약 60% 지점에 위치해 있습니다.
        float galaxyOffsetX = milkyWay.radius * 0.35f; 

        // 🚀 [수정됨] glm::translate의 X축 위치에 galaxyOffsetX를 넣어줍니다.
        // 이제 태양계(0.0)는 중심핵에서 한참 떨어진 나선팔 쪽에 위치하게 됩니다.
        glm::mat4 mwModel = glm::translate(glm::mat4(1.0f), relativeToCamera(glm::dvec3(galaxyOffsetX, galaxyDrop, 0.0)))
                          * glm::rotate(glm::mat4(1.0f), glm::radians(60.0f), glm::vec3(1.0f, 0.0f, 0.0f)) 
                          * glm::rotate(glm::mat4(1.0f), currentAppTime * 0.0005f, glm::vec3(0.0f, 1.0f, 0.0f)) 
                          * glm::scale(glm::mat4(1.0f), glm::vec3(milkyWay.radius));
                          
        PushConstants mwPush{mwModel, 10, galaxyAlpha}; 
        vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &mwPush);
        vkCmdDrawIndexed(cb, galaxyIndexCount, 1, galaxyFirstIndex, galaxyVertexOffset, 0);

        // =========================================================
        // 🚀 궤도선 렌더링 구역
        // =========================================================
        if (showOrbits) {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sun.descriptorSet, 0, nullptr);
            for (int pi = 0; pi < (int)planets.size(); ++pi) {
                const auto &planet = planets[pi];
                // 고정된 천체의 궤도는 아래에서 고정밀 버퍼로 따로 그린다.
                if (lockedOrbitValid && lockedTargetType == 1 && pi == lockedPlanetIndex) continue;
                float curOrbit = glm::mix(planet.orbitRadius, planet.realOrbit, scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp));
                float a = curOrbit, e = planet.eccentricity, b = a * sqrt(1.0f - e * e), c = a * e; 
                glm::mat4 orbitTilt = glm::rotate(glm::mat4(1.0f), glm::radians(planet.ascendingNode), glm::vec3(0.0f, 1.0f, 0.0f))
                                    * glm::rotate(glm::mat4(1.0f), glm::radians(planet.orbitalInclination), glm::vec3(0.0f, 0.0f, 1.0f))
                                    * glm::rotate(glm::mat4(1.0f), glm::radians(planet.periapsisAngle), glm::vec3(0.0f, 1.0f, 0.0f));
                                    
                glm::dvec3 orbitCenterWorld = glm::dvec3(0.0);
                if (planet.parentIndex != -1) orbitCenterWorld = planets[planet.parentIndex].currentPosition;
                glm::mat4 orbitCenter = glm::translate(glm::mat4(1.0f), relativeToCamera(orbitCenterWorld));
                                    
                glm::mat4 orbitModel = orbitCenter * orbitTilt * glm::translate(glm::mat4(1.0f), glm::vec3(-c, 0.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(a, 1.0f, b));
                PushConstants orbitPush{orbitModel, 6}; vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &orbitPush);
                vkCmdDrawIndexed(cb, orbitIndexCount, 1, orbitFirstIndex, orbitVertexOffset, 0);
            }

            // 고정된 천체의 궤도선: 이미 카메라 상대 좌표로 구워져 있으므로 모델 행렬은 단위 행렬.
            if (lockedOrbitValid) {
                VkDeviceSize lockedOffset = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &lockedOrbitBuffer, &lockedOffset);
                PushConstants lockedPush{glm::mat4(1.0f), 6};
                vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &lockedPush);
                vkCmdDraw(cb, LOCKED_ORBIT_SEGMENTS + 1, 1, 0, 0);
                vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets);
            }
        
            float curMoonOrbit = glm::mix(0.75f, moon.realOrbit, scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp));
            float ma = curMoonOrbit, me = moon.eccentricity, mb = ma * sqrt(1.0f - me * me), mc = ma * me;
            glm::mat4 mOrbitTilt = glm::rotate(glm::mat4(1.0f), glm::radians(moon.ascendingNode), glm::vec3(0.0f, 1.0f, 0.0f))
                                 * glm::rotate(glm::mat4(1.0f), glm::radians(moon.orbitalInclination), glm::vec3(0.0f, 0.0f, 1.0f))
                                 * glm::rotate(glm::mat4(1.0f), glm::radians(moon.periapsisAngle), glm::vec3(0.0f, 1.0f, 0.0f));
                                 
            glm::mat4 moonOrbitModel = glm::translate(glm::mat4(1.0f), relativeToCamera(planets[2].currentPosition)) * mOrbitTilt * glm::translate(glm::mat4(1.0f), glm::vec3(-mc, 0.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(ma, 1.0f, mb));
            
            PushConstants moonOrbitPush{moonOrbitModel, 6}; 
            vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &moonOrbitPush);
            vkCmdDrawIndexed(cb, orbitIndexCount, 1, orbitFirstIndex, orbitVertexOffset, 0);
        }

        // =========================================================
        // 🚀 [중요] 궤도선을 모두 그린 후, 태양을 그리기 위해 면(Triangle) 파이프라인으로 완벽 복구
        // =========================================================
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        // 3. 거대한 태양 홍염 (Type 7) - 가까우면 숨김
        if (shouldRenderSun) {
            // 🚀 [수정됨] 태양 홍염도 리얼 스케일에 맞춰 거대하게 팽창합니다.
            float easeScale = scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp); 
            float curSunRadius = glm::mix(sun.radius, sun.realRadius, easeScale);
            
            glm::mat4 haloModel = glm::translate(glm::mat4(1.0f), relativeToCamera(sun.currentPosition)) * glm::scale(glm::mat4(1.0f), glm::vec3(curSunRadius * 2.5f));
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sun.descriptorSet, 0, nullptr);
            PushConstants haloPush{haloModel, 7}; vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &haloPush);
            vkCmdDrawIndexed(cb, sphereIndexCount, 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(cb);

        bool horizontal = true; int blurAmount = 6; 
        for (int i = 0; i < blurAmount; i++) {
            VkRenderPassBeginInfo blurPassInfo{}; blurPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; blurPassInfo.renderPass = blurRenderPass;
            blurPassInfo.framebuffer = blurFramebuffers[horizontal ? 0 : 1]; blurPassInfo.renderArea.offset = {0, 0}; blurPassInfo.renderArea.extent = renderExtent;
            VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}}; blurPassInfo.clearValueCount = 1; blurPassInfo.pClearValues = &clearColor;
            vkCmdBeginRenderPass(cb, &blurPassInfo, VK_SUBPASS_CONTENTS_INLINE); setViewport(cb, renderExtent); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline);
            int setIndex = (i == 0) ? 0 : (horizontal ? 2 : 1);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipelineLayout, 0, 1, &blurDescriptorSets[setIndex], 0, nullptr);
            int horizInt = horizontal ? 1 : 0; vkCmdPushConstants(cb, blurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &horizInt);
            vkCmdDraw(cb, 3, 1, 0, 0); vkCmdEndRenderPass(cb); horizontal = !horizontal; 
        }

        VkRenderPassBeginInfo renderPassInfo{}; renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; renderPassInfo.renderPass = renderPass; renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0}; renderPassInfo.renderArea.extent = swapChainExtent;
        std::array<VkClearValue, 2> swapClear{}; swapClear[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; swapClear[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = 2; renderPassInfo.pClearValues = swapClear.data();
        
        vkCmdBeginRenderPass(cb, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        setFullViewport(cb);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipelineLayout, 0, 1, &postDescriptorSet, 0, nullptr);
        float postExposure = settings.exposure;
        vkCmdPushConstants(cb, postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &postExposure);
        vkCmdDraw(cb, 3, 1, 0, 0);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
        vkCmdEndRenderPass(cb);
        vkEndCommandBuffer(cb);
    }

    void cleanupApp() override {
        ImGui_ImplVulkan_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext(); vkDestroyDescriptorPool(device, imguiPool, nullptr);
        
        // [NEW] SSBO 버퍼 반환 
        vkDestroyPipeline(device, computePipeline, nullptr);
        vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
        vkDestroyBuffer(device, computeInputBuffer, nullptr); vkFreeMemory(device, computeInputMem, nullptr);
        vkDestroyBuffer(device, computeOutputBuffer, nullptr); vkFreeMemory(device, computeOutputMem, nullptr);
        vkDestroyBuffer(device, indirectDrawBuffer, nullptr); vkFreeMemory(device, indirectDrawMem, nullptr);

        vkDestroySampler(device, shadowSampler, nullptr); vkDestroyImageView(device, shadowView, nullptr);
        vkDestroyImage(device, shadowImage, nullptr); vkFreeMemory(device, shadowMemory, nullptr);
        vkDestroyPipeline(device, shadowPipeline, nullptr); vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
        vkDestroyFramebuffer(device, shadowFramebuffer, nullptr); vkDestroyRenderPass(device, shadowRenderPass, nullptr);

        vkDestroySampler(device, textureSampler, nullptr);
        for (auto view : allViews) vkDestroyImageView(device, view, nullptr);
        for (auto img : allImages) vkDestroyImage(device, img, nullptr);
        for (auto mem : allMemories) vkFreeMemory(device, mem, nullptr);

        vkDestroyPipeline(device, blurPipeline, nullptr); vkDestroyPipelineLayout(device, blurPipelineLayout, nullptr); vkDestroyDescriptorSetLayout(device, blurDescriptorSetLayout, nullptr); vkDestroyRenderPass(device, blurRenderPass, nullptr);
        for(int i=0; i<2; i++) { vkDestroyFramebuffer(device, blurFramebuffers[i], nullptr); vkDestroyImageView(device, blurViews[i], nullptr); vkDestroyImage(device, blurImages[i], nullptr); vkFreeMemory(device, blurMemories[i], nullptr); }
        
        vkDestroyPipeline(device, postPipeline, nullptr); vkDestroyPipelineLayout(device, postPipelineLayout, nullptr); vkDestroyDescriptorSetLayout(device, postDescriptorSetLayout, nullptr); vkDestroyFramebuffer(device, offscreenFramebuffer, nullptr); vkDestroyRenderPass(device, offscreenRenderPass, nullptr); vkDestroySampler(device, offscreenSampler, nullptr);
        vkDestroyImageView(device, offscreenColorView, nullptr); vkDestroyImage(device, offscreenColorImage, nullptr); vkFreeMemory(device, offscreenColorMem, nullptr);
        vkDestroyImageView(device, offscreenBrightView, nullptr); vkDestroyImage(device, offscreenBrightImage, nullptr); vkFreeMemory(device, offscreenBrightMem, nullptr);
        vkDestroyImageView(device, offscreenDepthView, nullptr); vkDestroyImage(device, offscreenDepthImage, nullptr); vkFreeMemory(device, offscreenDepthMem, nullptr);
        vkDestroyImageView(device, msaaColorView, nullptr); vkDestroyImage(device, msaaColorImage, nullptr); vkFreeMemory(device, msaaColorMem, nullptr);
        vkDestroyImageView(device, msaaBrightView, nullptr); vkDestroyImage(device, msaaBrightImage, nullptr); vkFreeMemory(device, msaaBrightMem, nullptr);

        vkDestroyImageView(device, viewSkybox, nullptr); vkDestroyImage(device, texSkybox, nullptr); vkFreeMemory(device, memSkybox, nullptr);
        vkDestroyImageView(device, viewDummyBlack, nullptr); vkDestroyImage(device, texDummyBlack, nullptr); vkFreeMemory(device, memDummyBlack, nullptr);
        vkDestroyImageView(device, viewDummyFlatNormal, nullptr); vkDestroyImage(device, texDummyFlatNormal, nullptr); vkFreeMemory(device, memDummyFlatNormal, nullptr);
        vkDestroyBuffer(device, indexBuffer, nullptr); vkFreeMemory(device, indexBufferMemory, nullptr);
        vkDestroyBuffer(device, vertexBuffer, nullptr); vkFreeMemory(device, vertexBufferMemory, nullptr);
        vkDestroyBuffer(device, uniformBuffer, nullptr); vkFreeMemory(device, uniformBufferMemory, nullptr);
        vkDestroyBuffer(device, lockedOrbitBuffer, nullptr); vkFreeMemory(device, lockedOrbitMemory, nullptr);
        vkDestroyDescriptorPool(device, descriptorPool, nullptr); vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        vkDestroyPipeline(device, graphicsPipeline, nullptr); vkDestroyPipeline(device, linePipeline, nullptr); vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }

private:
    void createColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, VkImage &image, VkDeviceMemory &imageMemory, VkImageView &imageView, VkFormat format) {
        uint8_t pixels[4] = {r, g, b, a}; VkDeviceSize imageSize = 4; VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
        void *data; vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data); memcpy(data, pixels, static_cast<size_t>(imageSize)); vkUnmapMemory(device, stagingBufferMemory);
        createImage(1, 1, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, imageMemory);
        transitionImageLayout(image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, image, 1, 1);
        transitionImageLayout(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vkDestroyBuffer(device, stagingBuffer, nullptr); vkFreeMemory(device, stagingBufferMemory, nullptr);
        imageView = createImageView(image, format, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    VkImageView loadTexture(const std::string &path, VkFormat format) {
        if (path.empty()) return (format == VK_FORMAT_R8G8B8A8_UNORM) ? viewDummyFlatNormal : viewDummyBlack;
        VkImage img; VkDeviceMemory mem; VkImageView view; int texWidth, texHeight, texChannels;
        stbi_uc *pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels) { std::cerr << "텍스처 없음, 더미 사용: " << path << "\n"; return viewDummyBlack; }
        VkDeviceSize imageSize = texWidth * texHeight * 4; VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
        void *data; vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data); memcpy(data, pixels, static_cast<size_t>(imageSize)); vkUnmapMemory(device, stagingBufferMemory);
        stbi_image_free(pixels);
        createImage(texWidth, texHeight, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, img, mem);
        transitionImageLayout(img, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, img, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
        transitionImageLayout(img, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vkDestroyBuffer(device, stagingBuffer, nullptr); vkFreeMemory(device, stagingBufferMemory, nullptr);
        view = createImageView(img, format, VK_IMAGE_ASPECT_COLOR_BIT);
        allImages.push_back(img); allMemories.push_back(mem); allViews.push_back(view); return view;
    }

    // 모노크롬 UI 아이콘 로더. 형태는 PNG의 알파 채널이 정의하고(배경/중앙은 alpha=0으로 투명),
    // 색은 지정한 회색으로 통일한다. 원본 획이 검정이라 ImGui tint(곱셈)로는 밝게 만들 수 없어
    // RGB를 직접 덮어쓴다. 512px 원본을 40px 버튼에 밉맵 없이 축소하면 얇은 획이 계단현상을
    // 일으키므로, CPU 박스 평균으로 64x64로 먼저 줄여 알파 기반 안티앨리어싱을 확보한다.
    VkImageView loadGrayIcon(const std::string &path, uint8_t gray) {
        int sw, sh, ch;
        stbi_uc *src = stbi_load(path.c_str(), &sw, &sh, &ch, STBI_rgb_alpha);
        if (!src) { std::cerr << "아이콘 없음: " << path << "\n"; return VK_NULL_HANDLE; }

        const int tw = 64, th = 64;
        std::vector<uint8_t> dst(tw * th * 4);
        for (int ty = 0; ty < th; ++ty) {
            for (int tx = 0; tx < tw; ++tx) {
                int x0 = tx * sw / tw, x1 = (tx + 1) * sw / tw;
                int y0 = ty * sh / th, y1 = (ty + 1) * sh / th;
                if (x1 <= x0) x1 = x0 + 1;
                if (y1 <= y0) y1 = y0 + 1;
                uint32_t aSum = 0, n = 0;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) { aSum += src[(y * sw + x) * 4 + 3]; ++n; }
                int di = (ty * tw + tx) * 4;
                dst[di + 0] = gray; dst[di + 1] = gray; dst[di + 2] = gray;
                dst[di + 3] = (uint8_t)(aSum / n);   // 다운스케일된 알파 = 부드러운 회색 기어 형태
            }
        }
        stbi_image_free(src);

        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;   // 평면 UI 회색이라 sRGB 변환 없이 그대로 표시
        VkDeviceSize imageSize = (VkDeviceSize)tw * th * 4; VkBuffer sb; VkDeviceMemory sbm;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sb, sbm);
        void *data; vkMapMemory(device, sbm, 0, imageSize, 0, &data); memcpy(data, dst.data(), (size_t)imageSize); vkUnmapMemory(device, sbm);
        VkImage img; VkDeviceMemory mem;
        createImage(tw, th, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, img, mem);
        transitionImageLayout(img, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(sb, img, (uint32_t)tw, (uint32_t)th);
        transitionImageLayout(img, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vkDestroyBuffer(device, sb, nullptr); vkFreeMemory(device, sbm, nullptr);
        VkImageView view = createImageView(img, format, VK_IMAGE_ASPECT_COLOR_BIT);
        allImages.push_back(img); allMemories.push_back(mem); allViews.push_back(view); return view;
    }

    Planet createPlanet(std::string name, int typeId, float radius, float orbitRadius, float orbitSpeed, float eccentricity, float rotSpeed, float axialTilt, float orbIncl, float periAngle, float ascNode, float initAngle, float axisDir, bool hasClouds, std::string diffuse, std::string night, std::string spec, std::string normal, std::string clouds) {
        Planet p; p.name = name; p.typeId = typeId; p.radius = radius; p.orbitRadius = orbitRadius; p.orbitSpeed = orbitSpeed; p.eccentricity = eccentricity; p.rotationSpeed = rotSpeed; p.axialTilt = axialTilt; p.hasClouds = hasClouds;
        
        // 새로 추가된 5대 요소 맵핑
        p.orbitalInclination = orbIncl; p.periapsisAngle = periAngle; p.ascendingNode = ascNode; p.initialAngle = initAngle; p.axisDirection = axisDir;

        p.viewDiffuse = loadTexture(diffuse, VK_FORMAT_R8G8B8A8_SRGB); p.viewNight = loadTexture(night, VK_FORMAT_R8G8B8A8_SRGB); p.viewSpecular = loadTexture(spec, VK_FORMAT_R8G8B8A8_UNORM); p.viewNormal = loadTexture(normal, VK_FORMAT_R8G8B8A8_UNORM); p.viewClouds = loadTexture(clouds, VK_FORMAT_R8G8B8A8_SRGB);
        VkDescriptorSetAllocateInfo allocInfo{}; allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; allocInfo.descriptorPool = descriptorPool; allocInfo.descriptorSetCount = 1; allocInfo.pSetLayouts = &descriptorSetLayout;
        vkAllocateDescriptorSets(device, &allocInfo, &p.descriptorSet);
        
        VkDescriptorBufferInfo bInfo{}; bInfo.buffer = uniformBuffer; bInfo.offset = 0; bInfo.range = sizeof(UniformBufferObject);
        auto mkImgInfo = [&](VkImageView v) { VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = textureSampler; return i; };
        
        VkDescriptorImageInfo iDiff = mkImgInfo(p.viewDiffuse), iNight = mkImgInfo(p.viewNight), iSpec = mkImgInfo(p.viewSpecular), iNorm = mkImgInfo(p.viewNormal), iCloud = mkImgInfo(p.viewClouds), iSky = mkImgInfo(viewSkybox);
        VkDescriptorImageInfo iShadow{}; iShadow.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; iShadow.imageView = shadowView; iShadow.sampler = shadowSampler;

        VkDescriptorBufferInfo ssboInfo{}; ssboInfo.buffer = computeOutputBuffer; ssboInfo.offset = 0; ssboInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 9> writes{}; 
        for (int i = 0; i < 9; i++) { writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet = p.descriptorSet; writes[i].dstBinding = i; writes[i].descriptorCount = 1; }
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[0].pBufferInfo = &bInfo;
        for (int i = 1; i < 8; i++) writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &iDiff; writes[2].pImageInfo = &iNight; writes[3].pImageInfo = &iSpec; writes[4].pImageInfo = &iNorm; writes[5].pImageInfo = &iCloud; writes[6].pImageInfo = &iSky; writes[7].pImageInfo = &iShadow;
        writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[8].pBufferInfo = &ssboInfo;

        vkUpdateDescriptorSets(device, 9, writes.data(), 0, nullptr); return p;
    }

    void createDescriptorSetLayout() {
        std::array<VkDescriptorSetLayoutBinding, 9> b{}; // [NEW] 바인딩 슬롯 9개로 확장
        for (int i = 0; i < 9; i++) { b[i].binding = i; b[i].descriptorCount = 1; b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; }
        b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; b[0].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
        for (int i = 1; i < 8; i++) b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[4].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
        
        // [NEW] 8번 바인딩: 버텍스 셰이더에서 사용하는 대용량 데이터 버퍼(SSBO)
        b[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; 
        b[8].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{}; layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; layoutInfo.bindingCount = 9; layoutInfo.pBindings = b.data();
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);
    }

    void createDescriptorPool() {
        std::array<VkDescriptorPoolSize, 3> poolSizes{};
        // 천체가 늘어날 것에 대비해 내부 슬롯 용량도 빵빵하게 늘려줍니다.
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; poolSizes[0].descriptorCount = 100; 
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; poolSizes[1].descriptorCount = 500; 
        poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; poolSizes[2].descriptorCount = 100;

        VkDescriptorPoolCreateInfo poolInfo{}; 
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; 
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size()); 
        poolInfo.pPoolSizes = poolSizes.data(); 
        
        // 🚀 [핵심 해결] 최대 세트 개수를 20개에서 100개로 대폭 확장합니다!
        poolInfo.maxSets = 100; 
        
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
    }

    void generateSphere(float radius, int sectorCount, int stackCount) {
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
                
                vertices.push_back({glm::vec3(x, y, z), glm::vec3(1.0f), glm::vec2((float)j / sectorCount, (float)i / stackCount), glm::normalize(glm::vec3(x, y, z))});
            }
        }
        for (int i = 0; i < stackCount; ++i) {
            int k1 = i * (sectorCount + 1); int k2 = k1 + sectorCount + 1;
            for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
                if (i != 0) { indices.push_back(k1); indices.push_back(k2); indices.push_back(k1 + 1); }
                if (i != (stackCount - 1)) { indices.push_back(k1 + 1); indices.push_back(k2); indices.push_back(k2 + 1); }
            }
        }
    }
    void generateRing(float innerRadius, float outerRadius, int sectorCount) {
        for (int i = 0; i <= sectorCount; ++i) {
            float angle = (float)i / sectorCount * 2.0f * M_PI; float cosA = cosf(angle); float sinA = sinf(angle);
            vertices.push_back({glm::vec3(innerRadius * cosA, 0.0f, innerRadius * sinA), glm::vec3(1.0f), glm::vec2(0.0f, (float)i / sectorCount), glm::vec3(0, 1, 0)});
            vertices.push_back({glm::vec3(outerRadius * cosA, 0.0f, outerRadius * sinA), glm::vec3(1.0f), glm::vec2(1.0f, (float)i / sectorCount), glm::vec3(0, 1, 0)});
        }
        for (int i = 0; i < sectorCount; ++i) {
            int k1 = i * 2; int k2 = k1 + 1; int k3 = (i + 1) * 2; int k4 = k3 + 1;
            indices.push_back(k1); indices.push_back(k3); indices.push_back(k2); indices.push_back(k2); indices.push_back(k3); indices.push_back(k4);
        }
    }
    void generateOrbit(int segmentCount) {
        for (int i = 0; i <= segmentCount; ++i) {
            float angle = (float)i / segmentCount * 2.0f * M_PI;
            // 🚀 빛 번짐(Bloom) 폭주 방지: 형광 노란색의 채도는 유지하되 밝기를 0.2, 0.4 수준으로 낮춥니다.
            vertices.push_back({glm::vec3(cosf(angle), 0.0f, sinf(angle)), glm::vec3(0.2f, 0.4f, 0.0f), glm::vec2(0.0f), glm::vec3(0, 1, 0)});
            indices.push_back(i);
        }
    }
    void generateGalaxyDisk(float radius, int sectorCount) {
        vertices.push_back({glm::vec3(0.0f), glm::vec3(1.0f), glm::vec2(0.5f, 0.5f), glm::vec3(0, 1, 0)}); // 중심점
        for (int i = 0; i <= sectorCount; ++i) {
            float angle = (float)i / sectorCount * 2.0f * M_PI;
            float u = 0.5f + 0.5f * cosf(angle); float v = 0.5f + 0.5f * sinf(angle);
            vertices.push_back({glm::vec3(radius * cosf(angle), 0.0f, radius * sinf(angle)), glm::vec3(1.0f), glm::vec2(u, v), glm::vec3(0, 1, 0)});
        }
        for (int i = 1; i <= sectorCount; ++i) {
            indices.push_back(0); indices.push_back(i); indices.push_back(i + 1);
        }
    }
    void createCubeTextureImage() {
        // 🚀 확장자를 .exr로 설정합니다! (textures 폴더 안에 6장의 EXR 파일이 있어야 합니다)
        std::vector<std::string> cubeFaces = {
            "textures/right.exr", "textures/left.exr", 
            "textures/top.exr", "textures/bottom.exr", 
            "textures/front.exr", "textures/back.exr"
        };
        
        int texWidth = 0, texHeight = 0; 
        
        // 🚀 8비트 정수가 아닌 32비트 실수(float) 포인터 배열 사용
        float *pixels[6]; 
        const char* err = nullptr;

        for (int i = 0; i < 6; i++) { 
            int width, height;
            // 🚀 tinyexr 라이브러리를 사용하여 EXR 파일 로드
            int ret = LoadEXR(&pixels[i], &width, &height, cubeFaces[i].c_str(), &err);
            
            if (ret != TINYEXR_SUCCESS) {
                if (err) {
                    std::string errorMsg = err;
                    FreeEXRErrorMessage(err); // 에러 메시지 메모리 누수 방지
                    throw std::runtime_error("EXR 로드 실패: " + cubeFaces[i] + " - " + errorMsg);
                }
                throw std::runtime_error("EXR 로드 실패: " + cubeFaces[i]);
            }
            
            // 6면의 해상도가 같아야 하므로 첫 번째 파일의 해상도를 기준으로 삼음
            if (i == 0) {
                texWidth = width;
                texHeight = height;
            }
        }
        
        // 🚀 1픽셀당 4바이트(8비트)에서 16바이트(32비트 float * 4채널)로 용량 4배 확장
        VkDeviceSize layerSize = static_cast<VkDeviceSize>(texWidth) * static_cast<VkDeviceSize>(texHeight) * 4ull * sizeof(float); 
        VkDeviceSize imageSize = layerSize * 6; 
        
        VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
        void *data; 
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        
        for (int i = 0; i < 6; i++) { 
            memcpy(static_cast<char *>(data) + (layerSize * i), pixels[i], static_cast<size_t>(layerSize)); 
            // 🚀 tinyexr은 내부적으로 malloc을 쓰기 때문에 반드시 free()로 메모리를 해제해야 합니다.
            free(pixels[i]); 
        }
        vkUnmapMemory(device, stagingBufferMemory);
        
        // 🚀 [핵심] Vulkan 이미지 포맷을 실수형(SFLOAT)으로 변경하여 빛의 다이나믹 레인지를 보존합니다.
        VkFormat hdrFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
        
        VkImageCreateInfo imageInfo{}; imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.extent.width = texWidth; imageInfo.extent.height = texHeight; imageInfo.extent.depth = 1; imageInfo.mipLevels = 1; imageInfo.arrayLayers = 6; 
        imageInfo.format = hdrFormat; // 새로운 HDR 포맷 적용
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL; imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateImage(device, &imageInfo, nullptr, &texSkybox);
        
        VkMemoryRequirements memReqs; vkGetImageMemoryRequirements(device, texSkybox, &memReqs); 
        VkMemoryAllocateInfo allocInfo{}; allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocInfo.allocationSize = memReqs.size; allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &allocInfo, nullptr, &memSkybox); vkBindImageMemory(device, texSkybox, memSkybox, 0);
        
        transitionImageLayout(texSkybox, hdrFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6);
        
        VkCommandBuffer cb = beginSingleTimeCommands();
        std::vector<VkBufferImageCopy> bufferCopyRegions;
        for (uint32_t face = 0; face < 6; face++) { 
            VkBufferImageCopy region{}; region.bufferOffset = layerSize * face; region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; region.imageSubresource.mipLevel = 0; region.imageSubresource.baseArrayLayer = face; region.imageSubresource.layerCount = 1; region.imageExtent = {(uint32_t)texWidth, (uint32_t)texHeight, 1}; 
            bufferCopyRegions.push_back(region); 
        }
        vkCmdCopyBufferToImage(cb, stagingBuffer, texSkybox, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bufferCopyRegions.size(), bufferCopyRegions.data());
        endSingleTimeCommands(cb);
        
        transitionImageLayout(texSkybox, hdrFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 6);
        
        vkDestroyBuffer(device, stagingBuffer, nullptr); vkFreeMemory(device, stagingBufferMemory, nullptr);
        
        viewSkybox = createImageView(texSkybox, hdrFormat, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_CUBE, 6);
    }
    void createGraphicsPipeline() {
        auto vertShaderCode = readFile("shaders/vert.spv"); auto fragShaderCode = readFile("shaders/frag.spv");
        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode); VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{}; vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT; vertShaderStageInfo.module = vertShaderModule; vertShaderStageInfo.pName = "main";
        VkPipelineShaderStageCreateInfo fragShaderStageInfo{}; fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fragShaderStageInfo.module = fragShaderModule; fragShaderStageInfo.pName = "main";
        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
        auto bindingDescription = Vertex::getBindingDescription(); auto attributeDescriptions = Vertex::getAttributeDescriptions();
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{}; vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; vertexInputInfo.vertexBindingDescriptionCount = 1; vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()); vertexInputInfo.pVertexBindingDescriptions = &bindingDescription; vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{}; inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{}; viewport.width = (float)swapChainExtent.width; viewport.height = (float)swapChainExtent.height; viewport.maxDepth = 1.0f; VkRect2D scissor{}; scissor.extent = swapChainExtent;
        VkPipelineViewportStateCreateInfo viewportState{}; viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; viewportState.viewportCount = 1; viewportState.pViewports = &viewport; viewportState.scissorCount = 1; viewportState.pScissors = &scissor;
        std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{}; dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()); dynamicState.pDynamicStates = dynamicStates.data();
        VkPipelineRasterizationStateCreateInfo rasterizer{}; rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rasterizer.polygonMode = VK_POLYGON_MODE_FILL; rasterizer.lineWidth = 1.0f; rasterizer.cullMode = VK_CULL_MODE_NONE; rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        // 오프스크린 패스가 MSAA이므로 여기 그리는 두 파이프라인(graphics/line)의 샘플 수도 일치시켜야 한다.
        VkPipelineMultisampleStateCreateInfo multisampling{}; multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; multisampling.rasterizationSamples = msaaSamples;
        VkPipelineDepthStencilStateCreateInfo depthStencil{}; depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; depthStencil.depthTestEnable = VK_TRUE; depthStencil.depthWriteEnable = VK_TRUE; depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        
        VkPipelineColorBlendAttachmentState colorBlendAttachment[2]{};
        colorBlendAttachment[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; colorBlendAttachment[0].blendEnable = VK_TRUE; colorBlendAttachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; colorBlendAttachment[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; colorBlendAttachment[0].colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment[1] = colorBlendAttachment[0]; 

        VkPipelineColorBlendStateCreateInfo colorBlending{}; colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; colorBlending.attachmentCount = 2; colorBlending.pAttachments = colorBlendAttachment;

        VkPushConstantRange pushConstantRange{}; pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; pushConstantRange.offset = 0; pushConstantRange.size = sizeof(PushConstants);
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{}; pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pipelineLayoutInfo.setLayoutCount = 1; pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout; pipelineLayoutInfo.pushConstantRangeCount = 1; pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
        
        VkGraphicsPipelineCreateInfo pipelineInfo{}; pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pipelineInfo.stageCount = 2; pipelineInfo.pStages = shaderStages; pipelineInfo.pVertexInputState = &vertexInputInfo; pipelineInfo.pInputAssemblyState = &inputAssembly; pipelineInfo.pViewportState = &viewportState; pipelineInfo.pRasterizationState = &rasterizer; pipelineInfo.pMultisampleState = &multisampling; pipelineInfo.pDepthStencilState = &depthStencil; pipelineInfo.pColorBlendState = &colorBlending; pipelineInfo.layout = pipelineLayout; pipelineInfo.renderPass = offscreenRenderPass; pipelineInfo.pDynamicState = &dynamicState;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);

        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &linePipeline);

        vkDestroyShaderModule(device, fragShaderModule, nullptr); vkDestroyShaderModule(device, vertShaderModule, nullptr);
    }

    // 고정된 천체의 궤도를 double로 계산해 카메라 상대 좌표 정점으로 굽는다.
    // 궤도 자체는 부모(태양 또는 모행성) 기준이므로 부모 위치를 더해 월드 좌표를 만든 뒤 상대화한다.
    void updateLockedOrbitLine(float easeScale, float slowTime) {
        lockedOrbitValid = false;
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

    void createLockedOrbitBuffer() {
        VkDeviceSize bufferSize = sizeof(Vertex) * (LOCKED_ORBIT_SEGMENTS + 1);
        createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     lockedOrbitBuffer, lockedOrbitMemory);
        vkMapMemory(device, lockedOrbitMemory, 0, bufferSize, 0, &lockedOrbitMapped);
    }

    void createVertexBuffer() {
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
        createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexBuffer, vertexBufferMemory);
        void *data;
        vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), (size_t)bufferSize);
        vkUnmapMemory(device, vertexBufferMemory);
    }

    void createIndexBuffer() {
        VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
        createBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indexBuffer, indexBufferMemory);
        void *data;
        vkMapMemory(device, indexBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, indices.data(), (size_t)bufferSize);
        vkUnmapMemory(device, indexBufferMemory);
    }

    void createUniformBuffer() { VkDeviceSize bufferSize = sizeof(UniformBufferObject); createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffer, uniformBufferMemory); vkMapMemory(device, uniformBufferMemory, 0, bufferSize, 0, &uniformBufferMapped); }
    void createTextureSampler() { VkSamplerCreateInfo samplerInfo{}; samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO; samplerInfo.magFilter = VK_FILTER_LINEAR; samplerInfo.minFilter = VK_FILTER_LINEAR; samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT; samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT; samplerInfo.anisotropyEnable = VK_FALSE; samplerInfo.compareEnable = VK_FALSE; samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler); }
};

#ifdef _WIN32
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    SolarSystemApp app;
    try { 
        app.run(); 
    } catch (const std::exception &e) { 
        // 🚀 [핵심 해결] CMD 창이 없어도, 크래시가 나면 윈도우 에러 창을 띄워서 원인을 알려줍니다!
#ifdef _WIN32
        MessageBoxA(nullptr, e.what(), "Vulkan Engine Fatal Error", MB_OK | MB_ICONERROR);
#else
        std::cerr << e.what() << std::endl; 
#endif
        return EXIT_FAILURE; 
    }
    return EXIT_SUCCESS;
}