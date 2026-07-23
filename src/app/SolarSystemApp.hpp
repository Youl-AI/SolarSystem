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

    // updateUniformBuffer 프레임 갱신 상태. Task 10에서 함수 지역 static 변수였던 것을
    // 멤버로 승격했다 (함수를 여러 단계로 쪼개면 static 지역 변수는 각 함수에 갇혀 서로 못 본다).
    std::chrono::time_point<std::chrono::high_resolution_clock> lastTimePoint = std::chrono::high_resolution_clock::now();
    bool spacePressedLastFrame = false;
    float simulationTime = 0.0f;
    bool isSimInit = false;
    bool isFirstFrame = true;
    float lastTargetRadius = 1.0f;
    int   lastTargetType   = 1;
    int   lastTargetIndex  = 2;
    float appliedFov = -1.0f; // 첫 프레임에 로드된 FOV를 강제로 반영
    float eclipseLerp = 0.0f;

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

    bool worldToScreen(glm::vec3 worldPos, glm::mat4 view, glm::mat4 proj, float screenWidth, float screenHeight, glm::vec2& outScreenPos);

    void TextCentered(const char* text);

    void initImGui();

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

    // 시간을 한 프레임 전진시킨다. deltaTime을 돌려주는 이유는 아래 단계들과 UI가
    // 같은 값을 써야 하기 때문이다.
    float advanceSimulationTime();

    // 🚀 1. 태양(Sun)은 월드 원점에 고정. 모델 행렬은 buildBodyModelMatrices()에서 만든다.
    // 🚀 2. 행성 및 위성의 '위치'만 double로 계산한다.
    //    모델 행렬은 카메라 업데이트가 끝난 뒤 buildBodyModelMatrices()에서 만든다.
    //    (카메라 상대 좌표를 쓰려면 모델 행렬이 이번 프레임의 camera.target을 봐야 하기 때문)
    // easeScale 파라미터는 브리프의 원래 시그니처(slowTime만)에는 없었지만, 궤도 반지름을
    // orbitRadius↔realOrbit 사이로 보간하는 데 꼭 필요해 추가했다(Task 10 컴파일 오류로 발견).
    void updateBodyPositions(float slowTime, float easeScale);

    // =========================================================
    // 카메라 추적 업데이트
    // =========================================================
    void updateCameraTracking(float deltaTime, float easeScale);

    // =========================================================
    // 🚀 시네마틱 개기일식 (궤도 고정 & 감속 정지 연출)
    // =========================================================
    void applyEclipseCinematic(float deltaTime, float easeScale);

    // ── 그림자를 주고받을 '계(系)'를 정하고, 해석적 그림자에 넘길 가림 천체들을 모은다 ──
    // 태양의 각반지름이 반그림자 폭을 결정하므로, ubo.sunPos/sunRadius도 여기서 지금 실제로
    // 렌더되는 크기로 채운다(예전엔 sun.radius 고정이라 리얼 스케일에서 그림자 계산이 어긋났다).
    // 그래서 easeScale 하나만 받으면 충분하다.
    void collectOccluders(UniformBufferObject &ubo, float easeScale);

    void uploadUbo(UniformBufferObject &ubo, float time);

    // 천체들의 위치가 확정되고 카메라까지 갱신된 뒤에 호출된다.
    // 모델 행렬은 카메라 상대 좌표를 쓰므로 반드시 이번 프레임의 camera.target 확정 후여야 한다.
    void buildBodyModelMatrices(float easeScale, float slowTime, float time);

    // 이번 프레임의 ImGui를 전부 구성한다. updateUniformBuffer의 뒤쪽 254줄이었다.
    void drawUi();

    void updateUniformBuffer() override;

    // 직전 위젯에 설명을 붙인다.
    //
    // 라벨과 마찬가지로 영문으로만 쓴다 — ImGui 기본 폰트에 한글 글리프가 없어
    // 한글을 넣으면 전부 '?'로 나온다.
    // AllowWhenDisabled를 주는 이유: VIEW 항목들은 로비에서 비활성인데, 그때야말로
    // "이게 뭐 하는 건지" 궁금해서 마우스를 올려 보게 된다.
    static void helpTip(const char *text);

    void drawSettingsWindow();

    // settings -> 기존 파생 상태로 동기화. 설정 창이 닫혀 있어도 매 프레임 호출된다.
    void applyLiveSettings();

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
    void updateLockedOrbitLine(float easeScale, float slowTime);

    void createLockedOrbitBuffer();

    void createVertexBuffer();

    void createIndexBuffer();

    void createUniformBuffer();
    void createTextureSampler();
};
