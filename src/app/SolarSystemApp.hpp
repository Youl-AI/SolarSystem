#pragma once

#include "Types.hpp"
#include "../VulkanBase.hpp"
#include "../Camera.hpp"
#include "../core/Settings.hpp"
#include "../core/GpuProfiler.hpp"
#include "../sim/CelestialData.hpp"
#include "../sim/AsteroidBelt.hpp"
#include "../gfx/MeshGen.hpp"

#include <glm/gtc/packing.hpp>   // packHalf2x16: 스카이박스를 fp16으로 접어 올린다

// 헤더 온리 라이브러리는 선언만 가져온다. 구현부는 third_party_impl.cpp에 있다.
#include "stb_image.h"
#include "stb_image_write.h"
#include "tinyexr.h"
#include "tiny_obj_loader.h"

#include <random>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <unordered_map>
#include <fstream>
#include <string>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class SolarSystemApp : public VulkanBase {
private:
    Camera camera;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool isRealScaleMode = false;
    // 하늘 전환의 진행도(0 = 사진 같은 하늘, 1 = 맨눈으로 본 하늘). settings.realStars를 향해 전진한다.
    float starsLerp = 0.0f;
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
    // 대기 껍질 전용. 깊이를 쓰지 않고 가산 블렌딩을 쓴다는 점만 다르다.
    VkPipeline atmospherePipeline;
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
    uint32_t asteroidBucketCapacity = 0; // 12개 LOD 바구니 각각의 최대 수용량 = 전체 소행성 수
    // 이번 프레임에 그림자를 주고받는 천체들(현재 보고 있는 행성 + 그 위성들).
    std::vector<int> shadowSystemIndices;
    bool shadowSystemIncludesEarthMoon = false;
    void* indirectDrawMapped = nullptr; 

    // 매 프레임 GPU 명령서를 초기화할 CPU 원본 템플릿
    VkDrawIndexedIndirectCommand drawCmdTemplates[12];
    
    // (LOD 정점 기록장은 그대로 유지)
    uint32_t highLodIndexCount[4], highLodFirstIndex[4]; int32_t highLodVertexOffset[4];
    // 중간 LOD는 타입별로 원본 모델을 단순화해 만든다. 공용 구를 쓰면 조금만 멀어져도
    // 바위가 완벽한 구로 튀어 팝핑이 눈에 띈다.
    uint32_t midLodIndexCount[4] = {}, midLodFirstIndex[4] = {}; int32_t midLodVertexOffset[4] = {};
    // 최하 LOD도 타입별 단순화 메시다. 구를 쓰면 화면에서 몇 픽셀만 돼도 실루엣이 매끈해져
    // 바위 느낌이 사라진다. 아주 거칠게 줄여도 불규칙한 윤곽은 남는다.
    uint32_t lowLodIndexCount[4] = {}, lowLodFirstIndex[4] = {}; int32_t lowLodVertexOffset[4] = {};
    Planet asteroidTypes[4];

    VkSampler textureSampler;
    VkImage texSkybox = VK_NULL_HANDLE, texDummyBlack, texDummyFlatNormal;
    VkDeviceMemory memSkybox = VK_NULL_HANDLE, memDummyBlack, memDummyFlatNormal;
    VkImageView viewSkybox = VK_NULL_HANDLE, viewDummyBlack, viewDummyFlatNormal;
    // 은하수 확산광 층. EXR로 물러났을 때는 만들어지지 않고, 셰이더에는 검은 더미가 간다.
    VkImage texSkyBand = VK_NULL_HANDLE;
    VkDeviceMemory memSkyBand = VK_NULL_HANDLE;
    VkImageView viewSkyBand = VK_NULL_HANDLE;

    Planet sun, moon, saturnRing;
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

    VkImage offscreenColorImage, offscreenBrightImage, offscreenDepthImage;
    VkDeviceMemory offscreenColorMem, offscreenBrightMem, offscreenDepthMem;
    VkImageView offscreenColorView, offscreenBrightView, offscreenDepthView;

    // 4x MSAA: 오프스크린 3D 패스를 멀티샘플로 그린 뒤 위의 단일 샘플 색상/bright 이미지로 resolve한다.
    // msaaColor/msaaBright는 멀티샘플 렌더 타겟이고, 위 offscreenColor/BrightImage가 resolve 대상(계속 SAMPLED).
    // 깊이는 다운스트림에서 안 쓰므로 offscreenDepthImage 자체를 멀티샘플로 만들어 그대로 렌더 타겟으로 쓴다.
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    VkExtent2D renderExtent{}; // 오프스크린 렌더 크기 = renderScale x swapChainExtent
    // 블룸은 정의상 저주파라 절반 해상도로 흐려도 눈에 띄지 않는다. 픽셀 수가 1/4이 되어
    // 6번의 블러 패스 비용이 크게 줄고, 포스트 합성 때 선형 보간으로 부드럽게 확대된다.
    VkExtent2D blurExtent{};
    float appliedRenderScale = 1.0f;
    bool  pendingOffscreenRecreate = false; // 렌더스케일/MSAA 변경 시
    VkImage msaaColorImage = VK_NULL_HANDLE, msaaBrightImage = VK_NULL_HANDLE;
    VkDeviceMemory msaaColorMem = VK_NULL_HANDLE, msaaBrightMem = VK_NULL_HANDLE;
    VkImageView msaaColorView = VK_NULL_HANDLE, msaaBrightView = VK_NULL_HANDLE;

    VkRenderPass offscreenRenderPass;
    VkFramebuffer offscreenFramebuffer;
    VkSampler offscreenSampler;

    // ── 밉체인 블룸 ──────────────────────────────────────────────────────
    // 밉 단계를 내려가며 흐리고(down), 다시 올라오며 더한다(up). 여러 해상도의 번짐이
    // 겹쳐 쌓여 고정 반경 가우시안보다 넓고 부드러운 글로우가 나오면서, 대부분의 작업이
    // 저해상도에서 일어나 비용도 더 싸다.
    static const int BLOOM_MAX_MIPS = 8;
    int bloomMipCount = 0;
    VkImage bloomImage = VK_NULL_HANDLE;
    VkDeviceMemory bloomMem = VK_NULL_HANDLE;
    VkImageView bloomMipViews[BLOOM_MAX_MIPS]{};      // 각 밉 한 단계만 보는 뷰(샘플링/렌더 타겟 겸용)
    VkFramebuffer bloomFramebuffers[BLOOM_MAX_MIPS]{};
    VkExtent2D bloomMipExtents[BLOOM_MAX_MIPS]{};
    // 다운은 덮어쓰기(DONT_CARE), 업은 기존 내용에 더해야 하므로 보존(LOAD). 두 렌더패스는
    // 어태치먼트 포맷·샘플수가 같아 호환되므로 프레임버퍼는 한 벌만 있으면 된다.
    VkRenderPass blurRenderPass;   // 다운샘플용 (기존 이름 유지)
    VkRenderPass bloomUpPass = VK_NULL_HANDLE;
    VkPipeline bloomDownPipeline = VK_NULL_HANDLE;
    VkPipeline bloomUpPipeline = VK_NULL_HANDLE;
    VkDescriptorSet bloomSrcSet = VK_NULL_HANDLE;            // offscreenBright를 읽는 세트
    VkDescriptorSet bloomMipSets[BLOOM_MAX_MIPS]{};          // 각 밉을 읽는 세트

    struct BloomPush { glm::vec2 srcTexel; float radius; };
    VkDescriptorSetLayout blurDescriptorSetLayout;
    VkPipelineLayout blurPipelineLayout;

    VkDescriptorSetLayout postDescriptorSetLayout;
    VkPipelineLayout postPipelineLayout;
    VkPipeline postPipeline;
    VkDescriptorSet postDescriptorSet;


    enum class AppState { LOBBY, SIMULATION };
    AppState currentAppState = AppState::LOBBY;

    // 🚀 [NEW] 개기일식 시네마틱 모드 변수
    bool isEclipseEvent = false;

protected:
    void onFrameStart() override;

    void throttleFrame(double frameStart) override;

    void onMouseButton(int button, int action, int mods) override;

    void onMouseMove(double xpos, double ypos) override;

    void onMouseScroll(double xoffset, double yoffset) override;

    void handleRaycast(double xpos, double ypos, bool isDoubleClick);

    // 오프스크린/블러 이미지·프레임버퍼 전용 파괴 헬퍼. recreateSwapChain()과
    // recreateOffscreenAndBlur() 양쪽에서 재사용한다(Task 6의 MSAA 재생성도 재사용 예정).
    void destroyOffscreenAndBlurResources();

    void recreateSwapChain();

    // 렌더스케일(및 Task 6의 MSAA) 변경 시 오프스크린/블러만 다시 만든다. 스왑체인은 그대로.
    void recreateOffscreenAndBlur();

    void recreateOffscreenForMsaa();

    // 색상과 깊이 어태치먼트가 공통으로 지원하는 최대 샘플 수를 8x로 상한을 두고 고른다.
    // (오프스크린 패스는 두 종류를 한 서브패스에서 쓰므로 교집합이어야 한다)
    // 8x 미지원이면 4x, 2x, 1x 순으로 자동 강등된다.
    VkSampleCountFlagBits pickMsaaSamples();

    // 설정의 MSAA 레벨(0/2/4/8)을 기기 지원으로 클램프해 VkSampleCountFlagBits로 변환한다.
    VkSampleCountFlagBits clampMsaaLevel(int level);

    GpuProfiler profiler;

    void initApp() override;

    void createComputeResources();
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

            // 오프스크린/블러 렌더 크기 = round(renderScale x swapChainExtent). 포스트/UI는 계속 swapChainExtent.
    void computeRenderExtent();

    void createOffscreenImages();

    void createOffscreenResources();

    void createBlurImages();

    void createBlurResources();

    void updateBlurDescriptorSets();

    void createBlurPipeline();

    void updatePostDescriptorSets();

    void createPostProcessPipeline();

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

        // 하늘도 같은 방식으로 서서히 넘긴다. 한 프레임에 바꾸면 별이 우수수 사라져 튄다.
        // 축척보다 조금 느리게(0.4) 가야 별이 하나씩 꺼지는 느낌이 난다.
        starsLerp += (settings.realStars ? 1.0f : -1.0f) * deltaTime * 0.4f;
        starsLerp = std::clamp(starsLerp, 0.0f, 1.0f);
        
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
        // 태양의 각반지름이 반그림자 폭을 결정하므로, 지금 실제로 렌더되는 크기를 써야 한다.
        // (예전엔 sun.radius 고정이라 리얼 스케일에서 그림자 계산이 어긋났다)
        ubo.sunPos = relativeToCamera(sun.currentPosition);
        ubo.sunRadius = glm::mix(sun.radius, sun.realRadius, easeScale);
        ubo.earthPos = relativeToCamera(planets[2].currentPosition); ubo.earthRadius = planets[2].radius;
        ubo.moonPos = relativeToCamera(moon.currentPosition); ubo.moonRadius = moon.radius;

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

        
        memcpy(uniformBufferMapped, &ubo, sizeof(ubo));

        // 여기는 펜스 대기 뒤라 직전 프레임의 컴퓨트 결과가 확정돼 있다. 0으로 리셋하기
        // 직전에 스냅샷을 떠 둔다(onFrameStart에서 읽으면 아직 GPU가 채우기 전이라 0이 나온다).
        profiler.snapshotDrawCmds(indirectDrawMapped);   // 오버레이의 LOD 표시도 이 스냅샷을 쓴다

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

        if (settings.showGpuTimes && profiler.enabled()) {
            // FPS만으로는 '느리다'는 것만 알 뿐 어디가 느린지 알 수 없다. 패스별로 나눠
            // 보여줘야 MSAA, 소행성 수, 대기 셰이더 중 무엇을 손댈지 정할 수 있다.
            //
            // 좌하단에 둔다. 좌상단은 천체 정보 패널, 우상단은 기어, 우하단은 EXIT가 쓴다.
            // 배경도 깔아 준다 — 별밭 위에 흰 글씨만 얹으면 읽을 수가 없다.
            ImGui::SetNextWindowPos(ImVec2(10.0f, swapChainExtent.height - 10.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
            ImGui::SetNextWindowBgAlpha(0.55f);
            ImGui::Begin("GPU Times", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
            double total = 0.0;
            for (int i = 0; i < 10; ++i) total += profiler.smoothMs(i);
            // 라벨은 반드시 ASCII로 쓴다. ImGui 기본 폰트에는 한글 글리프가 없어 전부 '?'가 된다.
            ImGui::Text("GPU %.2f ms  (max %.0f FPS)", total, total > 0.0 ? 1000.0 / total : 0.0);
            ImGui::Separator();
            for (int i = 0; i < 10; ++i) {
                if (profiler.smoothMs(i) < 0.005) continue;   // 눈금 이하인 패스는 줄만 차지한다
                ImGui::Text("%-12s %5.2f ms  %4.1f%%", GpuProfiler::NAMES[i], profiler.smoothMs(i),
                            total > 0.0 ? profiler.smoothMs(i) / total * 100.0 : 0.0);
            }

            // 소행성이 비싸 보일 때 개수 탓인지 LOD 탓인지 가르려면 이 줄이 필요하다.
            // 콘솔에만 찍히고 있었는데, 창으로 실행하면 콘솔이 없어 볼 방법이 없었다.
            uint32_t perLod[3] = {}, tris = 0;
            for (int lod = 0; lod < 3; ++lod)
                for (int type = 0; type < 4; ++type) {
                    const auto &c = profiler.drawCmds()[lod * 4 + type];
                    perLod[lod] += c.instanceCount;
                    tris += c.instanceCount * (c.indexCount / 3);
                }
            ImGui::Separator();
            ImGui::Text("asteroids drawn %u / %zu", perLod[0] + perLod[1] + perLod[2], asteroidTransforms.size());
            ImGui::Text("  LOD  high %u   mid %u   low %u", perLod[0], perLod[1], perLod[2]);
            ImGui::Text("  triangles %.2f M", tris / 1.0e6);

            // MSAA 렌더 타겟은 샘플 수에 정비례해 커진다. 8x에서는 색상/bright/깊이가
            // 합쳐서 수백 MiB인데, 프레임 시간에는 거의 안 잡혀서 눈치채기 어렵다.
            {
                double px = (double)renderExtent.width * renderExtent.height;
                int s = (int)msaaSamples;
                double msaaMiB = px * s * (8.0 + 8.0 + 4.0) / 1048576.0;   // 색상 + bright + 깊이(D32)
                ImGui::Separator();
                ImGui::Text("render %ux%u  MSAA %dx  targets %.0f MiB",
                            renderExtent.width, renderExtent.height, s, msaaMiB);
            }
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
            TextCentered("E P H E M E R I S");
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
            TextCentered("A   M E A S U R E D   S O L A R   S Y S T E M");
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

    // 직전 위젯에 설명을 붙인다.
    //
    // 라벨과 마찬가지로 영문으로만 쓴다 — ImGui 기본 폰트에 한글 글리프가 없어
    // 한글을 넣으면 전부 '?'로 나온다.
    // AllowWhenDisabled를 주는 이유: VIEW 항목들은 로비에서 비활성인데, 그때야말로
    // "이게 뭐 하는 건지" 궁금해서 마우스를 올려 보게 된다.
    static void helpTip(const char *text) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", text);
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
            helpTip("Window size. 'Fullscreen' always renders at the monitor's\n"
                    "native resolution, so the size list does not apply there.\n"
                    "To render fewer pixels without shrinking the window, use\n"
                    "Render Scale below instead.");

            const char* scaleLabels[] = {"0.5x", "0.75x", "1.0x", "1.5x", "2.0x"};
            static const float scaleVals[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
            int scaleIdx = 2; for (int i = 0; i < 5; ++i) if (scaleVals[i] == settings.renderScale) scaleIdx = i;
            if (ImGui::Combo("Render Scale", &scaleIdx, scaleLabels, 5)) settings.renderScale = scaleVals[scaleIdx];
            helpTip("Renders at this multiple of the window size, then resamples\n"
                    "to fit. Cost scales with the square: 2.0x draws 4x the pixels.\n"
                    "\n"
                    "Above 1.0x this is supersampling - it cleans up everything,\n"
                    "including texture and shader detail that MSAA cannot touch.\n"
                    "Below 1.0x it is the cheapest way to gain frame rate.");

            const char* msaaLabels[] = {"Off", "2x", "4x", "8x"};
            static const int msaaVals[] = {0, 2, 4, 8};
            int msaaIdx = 3; for (int i = 0; i < 4; ++i) if (msaaVals[i] == settings.msaaLevel) msaaIdx = i;
            if (ImGui::Combo("Anti-aliasing", &msaaIdx, msaaLabels, 4)) settings.msaaLevel = msaaVals[msaaIdx];
            helpTip("Multisampling. Smooths geometry edges - planet limbs and\n"
                    "orbit lines - but not texture or shader detail.\n"
                    "\n"
                    "Costs little frame time here, but the render targets grow in\n"
                    "direct proportion to the sample count. At 3200x1800 they take\n"
                    "879 MiB at 8x versus 220 MiB at 2x. Lower this first if you\n"
                    "ever run short of video memory.");

            int vsyncIdx = settings.vsync ? 0 : 1;
            const char* vsyncLabels[] = {"On", "Off"};
            if (ImGui::Combo("VSync", &vsyncIdx, vsyncLabels, 2)) settings.vsync = (vsyncIdx == 0);
            helpTip("On: waits for the monitor to refresh before presenting, so\n"
                    "the image never tears. Caps the frame rate at the refresh rate.\n"
                    "Off: presents as soon as a frame is ready. Higher frame rate,\n"
                    "but a frame can be swapped mid-scan and show a torn seam.");
        }

        ImGui::SliderFloat("Field of View", &settings.fovDegrees, 30.0f, 90.0f, "%.0f deg");
        helpTip("Vertical viewing angle. Wider fits more in but stretches the\n"
                "edges; narrower feels like a telephoto lens.\n"
                "This is the resting value - the scroll wheel zooms in from here,\n"
                "down to 2 degrees.");

        ImGui::SliderFloat("Brightness",    &settings.exposure,   0.3f, 3.0f, "%.2f");
        helpTip("Exposure multiplier applied before tone mapping, like the\n"
                "exposure dial on a camera. Raise it to lift dim detail out of\n"
                "shadow; the brightest highlights stay white either way.");

        {
            const char* capLabels[] = {"Unlimited", "30", "60", "120", "144"};
            static const int capVals[] = {0, 30, 60, 120, 144};
            int capIdx = 0; for (int i = 0; i < 5; ++i) if (capVals[i] == settings.frameCap) capIdx = i;
            if (ImGui::Combo("Frame Cap", &capIdx, capLabels, 5)) settings.frameCap = capVals[capIdx];
        }
        helpTip("Holds the frame rate at a target by sleeping between frames.\n"
                "Useful for keeping the GPU quiet and cool when the scene is\n"
                "running far faster than the display can show.");

        ImGui::Checkbox("Show FPS", &settings.showFps);
        helpTip("Frame rate counter in the top-left corner.");

        // 계측 항목은 개발자 세션에서만 보인다(SOLAR_PROFILE=1). 배포 실행에서는
        // 항목 자체가 없다 — 최종 사용자에게 필요한 정보가 아니고, 잘못 읽기도 쉽다.
        if (profiler.devTools()) {
            if (!profiler.enabled()) ImGui::BeginDisabled();
            ImGui::Checkbox("Show GPU Times", &settings.showGpuTimes);
            if (!profiler.enabled()) ImGui::EndDisabled();
            helpTip(profiler.enabled()
                    ? "Per-pass GPU timings in the bottom-left corner, so you can see\n"
                      "which pass a frame is actually spent in rather than guessing.\n"
                      "Also reports how many asteroids are drawn at each level of\n"
                      "detail, and how much memory the render targets occupy.\n"
                      "\n"
                      "Read the milliseconds, not the percentages - a pass can reach\n"
                      "a large share simply because the others got cheaper."
                    : "This GPU does not support timestamp queries.");
        }

        ImGui::SeparatorText("VIEW");
        bool inSim = (currentAppState == AppState::SIMULATION);
        if (!inSim) ImGui::BeginDisabled();

        ImGui::Checkbox("Orbit Lines", &settings.orbitLines);
        helpTip("Draws each body's orbital ellipse, tilted and oriented to its\n"
                "real inclination and ascending node.");

        ImGui::Checkbox("Asteroids", &settings.asteroids);
        helpTip("Draws the main belt and the Kuiper belt - 12,400 bodies whose\n"
                "orbits are sampled from the real populations. Turning this off\n"
                "skips them entirely, though they are cheap: even with every one\n"
                "on screen they cost well under a millisecond.");

        ImGui::Checkbox("Real Scale", &settings.realScale);
        helpTip("Moves every body to its true distance and size instead of the\n"
                "compressed layout, at 1 AU = 500 units.\n"
                "\n"
                "The solar system is mostly empty, so expect the planets to\n"
                "become specks scattered across a great deal of nothing. That\n"
                "emptiness is the point.");

        ImGui::Checkbox("Real Stars", &settings.realStars);
        helpTip("Show the sky as the naked eye would see it from space:\n"
                "about 9,100 stars (magnitude 6.5), a faint grey Milky Way,\n"
                "and no twinkling - starlight only shimmers when it passes\n"
                "through air, so in vacuum the stars sit perfectly still.\n"
                "\n"
                "Off is the long-exposure photograph look: millions of stars\n"
                "and a bright, colourful Milky Way. Independent of Real Scale.");

        if (!inSim) ImGui::EndDisabled();

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120.0f, 30.0f))) { settingsOpen = false; saveSettings(settings); }

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
        double recordStart = glfwGetTime();
        VkCommandBufferBeginInfo beginInfo{}; beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cb, &beginInfo);
        profiler.resetPool(cb);
        profiler.mark(cb, 0);

        // 벨트 행렬은 컴퓨트(LOD 판정)와 그래픽스(실제 그리기)가 똑같은 것을 써야 한다.
        // 예전에는 컴퓨트가 이걸 몰라서 소행성의 '절대 월드 좌표'를 '카메라 상대 좌표'인
        // ubo.cameraPos와 빼는 바람에, 사실상 원점(태양)과의 거리를 재고 있었다. 그 결과
        // 두 벨트 전부가 항상 최고 LOD로 떨어져 매 프레임 수천만 삼각형을 그렸다.
        float easeScale = scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp);
        // 행성들의 평균 팽창 비율(약 200배)을 스케일에 곱해 벨트 전체를 우주 외곽으로 밀어냅니다.
        // 벨트 팽창은 여기서 하지 않는다. 벨트마다 배율이 다르고(소행성대 118, 카이퍼벨트 242)
        // 크기에는 곱하면 안 되기 때문이다. 소행성마다 든 배율로 컴퓨트 셰이더가 위치만 늘린다.
        // 소행성 행렬(SSBO)은 월드 원점 기준이므로, 원점을 렌더 좌표계로 옮겨 앞에 붙인다.
        glm::mat4 astRot = glm::translate(glm::mat4(1.0f), relativeToCamera(glm::dvec3(0.0)))
                         * glm::rotate(glm::mat4(1.0f), currentAppTime * 0.05f, glm::vec3(0.0f, 1.0f, 0.0f));

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet, 0, nullptr);
        // 정점 셰이더가 쓰는 것과 동일한 행렬을 넘겨, 컴퓨트가 렌더 좌표계에서 거리를 재게 한다.
        AsteroidCullPush cullPush{astRot, static_cast<uint32_t>(asteroidTransforms.size()), easeScale};
        vkCmdPushConstants(cb, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(cullPush), &cullPush);

        uint32_t groupCount = (static_cast<uint32_t>(asteroidTransforms.size()) + 255) / 256;
        vkCmdDispatch(cb, groupCount, 1, 1);

        VkMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; 
        // HOST_READ는 계측이 인스턴스 수를 CPU에서 읽기 위해 필요하다(HOST_COHERENT 메모리라도
        // 배리어 없이는 가시성이 보장되지 않는다). 계측이 꺼져 있으면 비용은 무시할 수준이다.
        barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

        VkBuffer vertexBuffers[] = {vertexBuffer}; VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets); vkCmdBindIndexBuffer(cb, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        VkRenderPassBeginInfo offscreenPassInfo{}; offscreenPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        offscreenPassInfo.renderPass = offscreenRenderPass; offscreenPassInfo.framebuffer = offscreenFramebuffer;
        offscreenPassInfo.renderArea.offset = {0, 0}; offscreenPassInfo.renderArea.extent = renderExtent;

        std::array<VkClearValue, 3> clearValues{};
        clearValues[0].color = {{0.01f, 0.01f, 0.01f, 1.0f}}; clearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; clearValues[2].depthStencil = {1.0f, 0};
        offscreenPassInfo.clearValueCount = 3; offscreenPassInfo.pClearValues = clearValues.data();
        
        profiler.mark(cb, 1);
        vkCmdBeginRenderPass(cb, &offscreenPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        setViewport(cb, renderExtent);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets); vkCmdBindIndexBuffer(cb, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // easeScale / beltScale / astRot은 컴퓨트 디스패치 직전에 이미 만들어 두었다(위 참조).
        PushConstants pcAst{astRot, 8};
        vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pcAst);

        if (settings.asteroids) {
            for (int type = 0; type < 4; ++type) {
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &asteroidTypes[type].descriptorSet, 0, nullptr);
                for (int lod = 0; lod < 3; ++lod) {
                    int bucketIndex = lod * 4 + type;
                    VkDeviceSize offset = bucketIndex * sizeof(VkDrawIndexedIndirectCommand);
                    vkCmdDrawIndexedIndirect(cb, indirectDrawBuffer, offset, 1, sizeof(VkDrawIndexedIndirectCommand));
                }
            }
        }

        profiler.mark(cb, 2); // 소행성 그리기 끝
        auto drawObject = [&](const Planet &p, glm::mat4 mat, int type) {
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &p.descriptorSet, 0, nullptr);
            PushConstants pc{mat, type, p.normalAmp}; vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
            vkCmdDrawIndexed(cb, sphereIndexCount, 1, 0, 0, 0); 
        };

        glm::vec3 currentCameraPos = glm::vec3(camera.getEyeWorld());
        float distToSun = glm::length(currentCameraPos - glm::vec3(sun.currentPosition));
        bool shouldRenderSun = distToSun > (sun.radius * 2.6f);

        if (shouldRenderSun) {
            drawObject(sun, sun.currentModelMat, sun.typeId); 
        }

        // 카메라가 천체 안에 들어갔을 때만 건너뛴다.
        //
        // 예전에는 기준이 planet.radius * 1.2f 였는데, 카메라의 최소 거리도 같은 식
        // (targetRadius * 1.2f)이라 기본 축척에서는 두 값이 정확히 같아졌다. 확대해서
        // 최소 거리에 도달하면 부동소수점 잡음만으로 판정이 매 프레임 뒤집혀 행성이
        // 통째로 명멸했다. 실제 축척에서는 최소 거리가 realRadius 기준이라 증상이 없었다.
        // 이제는 실제로 그려지는 반지름을 쓰고, 최소 거리보다 20% 안쪽에서만 걸린다.
        for (const auto &planet : planets) {
            float distToPlanet = glm::length(currentCameraPos - glm::vec3(planet.currentPosition));
            if (distToPlanet > planet.currentRenderRadius) {
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
        if (distToMoon > moon.currentRenderRadius) {
            drawObject(moon, moon.currentModelMat, moon.typeId);
        }
        
        profiler.mark(cb, 3); // 천체 본체 끝 (다음은 스카이박스)
        // 스카이박스는 태양의 디스크립터 세트를 빌려 쓴다(큐브맵은 어느 세트에나 같이 들어 있다).
        // drawObject를 쓰지 않고 푸시 상수를 직접 만드는 이유: customData에 하늘 전환값을
        // 실어야 하기 때문이다. 타입 3 분기는 customData를 달리 쓰지 않으므로 자리가 비어 있다.
        // 이 값으로 셰이더가 '사진 같은 하늘'과 '맨눈으로 본 하늘' 사이를 오간다.
        //
        // 축척(easeScale)이 아니라 별도의 Real Stars 토글을 쓴다. 하늘의 사실성과 거리
        // 축척은 서로 다른 축이라, 압축 축척으로 태양계 전체를 보면서도 실제 별하늘을
        // 볼 수 있어야 한다.
        {
            float easeStars = starsLerp * starsLerp * (3.0f - 2.0f * starsLerp);
            glm::mat4 skyMat = glm::rotate(glm::mat4(1.0f), currentAppTime * glm::radians(0.5f), glm::vec3(0.0f, 1.0f, 0.0f));
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sun.descriptorSet, 0, nullptr);
            PushConstants skyPush{skyMat, 3, easeStars};
            vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &skyPush);
            vkCmdDrawIndexed(cb, sphereIndexCount, 1, 0, 0, 0);
        }
        
        profiler.mark(cb, 4); // 행성/스카이박스 끝

        // 여기에는 멀리 축소했을 때 떠오르던 은하 원반(Milkyway.png를 입힌 판)이 있었다.
        // 하늘 큐브맵이 은하 안에서 밖을 본 모습인데 원반은 은하를 밖에서 본 모습이라,
        // 둘이 동시에 보이면 관측자가 은하 안에도 밖에도 있는 셈이 된다. 게다가 원반은
        // 1229x1229짜리 그림 한 장이라 실측 성도와 나란히 두기에 근거가 약했다.

        // 대기 껍질은 배경이 전부 깔린 뒤에 그려야 한다.
        //
        // 껍질은 일부러 깊이를 쓰지 않는다(자기보다 작은 행성 뒤의 것들을 삼키지 않으려고).
        // 그런데 스카이박스는 천체보다 나중에, 원거리 깊이로, 알파 1로 그려진다. 그래서
        // 행성 실루엣 바깥의 발광 영역은 깊이가 비어 있는 탓에 스카이박스가 통과해 덮어버렸다.
        // 껍질을 만든 이유가 바로 그 바깥 발광인데 그게 통째로 지워지고 있었다.
        //
        // 유일하게 살아남던 곳이 소행성 위였다. 소행성은 깊이를 쓰므로 스카이박스가 거기서만
        // 깊이 테스트에 걸렸고, 그 결과 소행성만 빛나는 점으로 남았다.
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, atmospherePipeline);
        for (const auto &planet : planets) {
            if (!planet.hasAtmosphere) continue;
            float d = glm::length(currentCameraPos - glm::vec3(planet.currentPosition));
            // 껍질 안쪽에 들어갔을 때만 건너뛴다(본체와 같은 이유로 기준을 바꿨다).
            if (d > planet.currentRenderRadius * kAtmosphereShellScale) drawObject(planet, planet.atmosphereModelMat, 11);
        }
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        // =========================================================
        // 🚀 궤도선 렌더링 구역
        // =========================================================
        profiler.mark(cb, 5); // 은하 끝
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

        profiler.mark(cb, 6); // 궤도선 끝
        // 3. 거대한 태양 홍염 (Type 7) - 가까우면 숨김
        if (shouldRenderSun) {
            // 🚀 [수정됨] 태양 홍염도 리얼 스케일에 맞춰 거대하게 팽창합니다.
            float easeScale = scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp); 
            float curSunRadius = glm::mix(sun.radius, sun.realRadius, easeScale);
            
            // 돔 배율 2.5. 홍염 자체는 r <= 1.48까지만 존재하지만, 돔을 1.7로 줄여봤을 때
            // 성능 차이가 측정 노이즈 수준이었다(셰이더의 r > 1.6 조기 discard가 이미
            // 바깥 픽셀을 몇 연산 만에 버리기 때문). 그래서 원래 값을 유지한다.
            // 이 값을 바꾸면 shader.frag의 나눗셈 상수도 같이 맞춰야 한다.
            glm::mat4 haloModel = glm::translate(glm::mat4(1.0f), relativeToCamera(sun.currentPosition)) * glm::scale(glm::mat4(1.0f), glm::vec3(curSunRadius * 2.5f));
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sun.descriptorSet, 0, nullptr);
            PushConstants haloPush{haloModel, 7}; vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &haloPush);
            vkCmdDrawIndexed(cb, sphereIndexCount, 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(cb);

        profiler.mark(cb, 7);
        // ── 밉체인 블룸 ──────────────────────────────────────────────────
        // 한 번의 draw = 전체화면 삼각형 하나. 렌더 타겟과 소스는 같은 이미지의 다른 밉이며,
        // 각 뷰가 한 밉만 덮으므로 레이아웃 전환이 서로 침범하지 않는다.
        auto bloomDraw = [&](VkRenderPass pass, VkPipeline pipe, int dstMip, VkDescriptorSet srcSet,
                             VkExtent2D srcExtent, float radius) {
            VkRenderPassBeginInfo bp{}; bp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            bp.renderPass = pass; bp.framebuffer = bloomFramebuffers[dstMip];
            bp.renderArea.offset = {0, 0}; bp.renderArea.extent = bloomMipExtents[dstMip];
            VkClearValue clear = {{{0.0f, 0.0f, 0.0f, 1.0f}}}; bp.clearValueCount = 1; bp.pClearValues = &clear;

            vkCmdBeginRenderPass(cb, &bp, VK_SUBPASS_CONTENTS_INLINE);
            setViewport(cb, bloomMipExtents[dstMip]);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipelineLayout, 0, 1, &srcSet, 0, nullptr);
            BloomPush bpush{ glm::vec2(1.0f / (float)srcExtent.width, 1.0f / (float)srcExtent.height), radius };
            vkCmdPushConstants(cb, blurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(bpush), &bpush);
            vkCmdDraw(cb, 3, 1, 0, 0);
            vkCmdEndRenderPass(cb);
        };

        // 1) 내려가며 흐린다: 밝기 버퍼 -> 밉0, 밉0 -> 밉1, ...
        bloomDraw(blurRenderPass, bloomDownPipeline, 0, bloomSrcSet, renderExtent, 0.0f);
        for (int i = 1; i < bloomMipCount; ++i)
            bloomDraw(blurRenderPass, bloomDownPipeline, i, bloomMipSets[i - 1], bloomMipExtents[i - 1], 0.0f);

        // 2) 올라오며 더한다: 밉N-1 -> 밉N-2, ... -> 밉0.
        // 여러 해상도의 번짐이 누적되어 넓고 부드러운 글로우가 만들어진다.
        const float kBloomFilterRadius = 1.0f;
        for (int i = bloomMipCount - 1; i > 0; --i)
            bloomDraw(bloomUpPass, bloomUpPipeline, i - 1, bloomMipSets[i], bloomMipExtents[i], kBloomFilterRadius);

        VkRenderPassBeginInfo renderPassInfo{}; renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; renderPassInfo.renderPass = renderPass; renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0}; renderPassInfo.renderArea.extent = swapChainExtent;
        std::array<VkClearValue, 2> swapClear{}; swapClear[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; swapClear[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = 2; renderPassInfo.pClearValues = swapClear.data();
        
        profiler.mark(cb, 8);
        vkCmdBeginRenderPass(cb, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        setFullViewport(cb);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipelineLayout, 0, 1, &postDescriptorSet, 0, nullptr);
        float postExposure = settings.exposure;
        vkCmdPushConstants(cb, postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &postExposure);
        vkCmdDraw(cb, 3, 1, 0, 0);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
        vkCmdEndRenderPass(cb);
        profiler.mark(cb, 9);
        profiler.mark(cb, 10);
        vkEndCommandBuffer(cb);
        profiler.addRecordMs((glfwGetTime() - recordStart) * 1000.0);
    }

    void cleanupApp() override;

private:
    void createColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, VkImage &image, VkDeviceMemory &imageMemory, VkImageView &imageView, VkFormat format);
    // 블록 압축 DDS(BC7 색상, BC5 법선)를 읽어 압축된 블록을 그대로 GPU에 올린다.
    //
    // texconv가 내는 DDS는 헤더 124바이트 + DX10 확장 20바이트 뒤에 밉 레벨이 큰 것부터
    // 차례로 붙어 있다. 블록 압축이라 한 레벨의 크기는 4로 올림한 블록 수 x 16바이트다.
    // GPU가 그 포맷을 못 쓰면 VK_NULL_HANDLE을 돌려 호출자가 원본 이미지로 물러나게 한다.
    VkImageView loadDDS(const std::string &path, bool srgb);

    // BC7로 미리 압축해 둔 .dds가 옆에 있으면 그것을 쓴다.
    //
    // 색상 텍스처는 GPU에서 픽셀당 4바이트로 풀려 있어 VRAM의 대부분을 차지한다. BC7은
    // 4x4 블록을 16바이트로 저장해 정확히 1/4로 줄이면서, 측정상 지금 쓰는 JPEG보다
    // 손실이 작다(달 8K: BC7 44.3dB 대 JPEG q92 40.4dB). 노말맵은 제외했다 — 법선은
    // 블록당 대표색 2개를 잇는 직선으로 근사되지 않아 오차가 커진다.
    //
    // 압축 포맷은 blit으로 밉맵을 만들 수 없으므로 texconv가 구울 때 함께 넣어 둔다.
    static std::string ddsPathFor(const std::string &path);

    VkImageView loadTexture(const std::string &path, VkFormat format);

    // 모노크롬 UI 아이콘 로더. 형태는 PNG의 알파 채널이 정의하고(배경/중앙은 alpha=0으로 투명),
    // 색은 지정한 회색으로 통일한다. 원본 획이 검정이라 ImGui tint(곱셈)로는 밝게 만들 수 없어
    // RGB를 직접 덮어쓴다. 512px 원본을 40px 버튼에 밉맵 없이 축소하면 얇은 획이 계단현상을
    // 일으키므로, CPU 박스 평균으로 64x64로 먼저 줄여 알파 기반 안티앨리어싱을 확보한다.
    VkImageView loadGrayIcon(const std::string &path, uint8_t gray);

    Planet createPlanet(std::string name, int typeId, float radius, float orbitRadius, float orbitSpeed, float eccentricity, float rotSpeed, float axialTilt, float orbIncl, float periAngle, float ascNode, float initAngle, float axisDir, bool hasClouds, std::string diffuse, std::string night, std::string spec, std::string normal, std::string clouds);

    void createDescriptorSetLayout();

    void createDescriptorPool();

    // BC6H로 미리 구운 큐브맵 DDS를 읽는다. 성공하면 true.
    //
    // BC6H는 HDR 전용 블록 압축이라 half 4채널을 픽셀당 1바이트로 담는다. 이게 없으면
    // 해상도 업그레이드가 성립하지 않는다 — 4096^2 x 6면을 fp16으로 올리면 805 MiB인데,
    // BC6H로는 밉맵까지 다 넣고도 128 MiB다(2048^2 fp16 무압축 192 MiB보다도 작다).
    bool loadCubeDDS(const std::string &path, VkImage &outImg, VkDeviceMemory &outMem, VkImageView &outView);

    void createCubeTextureImage();
    void createGraphicsPipeline();

    // 고정된 천체의 궤도를 double로 계산해 카메라 상대 좌표 정점으로 굽는다.
    // 궤도 자체는 부모(태양 또는 모행성) 기준이므로 부모 위치를 더해 월드 좌표를 만든 뒤 상대화한다.
    void updateLockedOrbitLine(float easeScale, float slowTime) {
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

    void createLockedOrbitBuffer();

    void createVertexBuffer();

    void createIndexBuffer();

    void createUniformBuffer();
    void createTextureSampler();
};
