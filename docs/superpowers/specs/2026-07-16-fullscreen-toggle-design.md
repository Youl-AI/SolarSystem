# 전체화면/창모드 전환 버튼 설계

## 배경

현재 `SolarSystemApp`(main.cpp)은 `LOBBY`(대기 화면)와 `SIMULATION`(시뮬레이션) 두 상태를 오가는데, 창은 `VulkanBase`에서 `GLFW_RESIZABLE = GLFW_FALSE`, `WIDTH=1024`/`HEIGHT=768` 고정 크기로 생성되고 리사이즈/스왑체인 재생성 로직이 프로젝트에 전혀 없다. 우측 상단에는 `SIMULATION` 상태에서만 보이는 "Settings Panel"(Real Scale, 궤도선 표시 버튼)이 있다.

요구사항: 우측 최상단, 기존 Settings Panel 위에 전체화면⇄창모드 전환 버튼을 추가하고, 이 버튼은 LOBBY/SIMULATION 양쪽에서 항상 보이게 한다.

## 확정된 방향

- 전체화면 방식: **보더리스 전체화면**(창 테두리 제거 + 모니터 네이티브 해상도로 창 크기/위치 조정). OS 독점 전체화면 모드 전환은 사용하지 않는다.
- 창모드 크기: **항상 1024x768 고정**. 사용자가 드래그로 리사이즈하는 기능은 만들지 않는다. 상태는 "전체화면 ↔ 1024x768 창" 두 가지만 존재한다.
- 단축키(F11 등)는 만들지 않는다. 버튼 클릭으로만 전환한다.
- 버튼 패널은 기존 Settings Panel과 **별도의 독립 패널**로 만들어 LOBBY/SIMULATION 양쪽에서 항상 그린다. 기존 Settings Panel은 새 패널과 겹치지 않도록 아래로 이동한다.

## 아키텍처: 스왑체인 재생성 인프라

보더리스 전체화면은 창의 프레임버퍼 크기를 모니터 해상도로 바꾸기 때문에, 스왑체인·깊이버퍼·오프스크린 블룸/블러 버퍼·관련 파이프라인 뷰포트를 전부 그 크기에 맞게 다시 만들어야 한다. 이 프로젝트에는 지금까지 이런 재생성 경로가 없으므로 새로 구축한다.

### VulkanBase.hpp 변경

1. `createSwapChain()`이 `swapChainExtent`를 상수 `WIDTH`/`HEIGHT` 대신 `glfwGetFramebufferSize(window, &w, &h)`로 구한 실제 프레임버퍼 크기로 설정하도록 수정한다.
2. `cleanupSwapChain()`(새 protected 메서드)을 추가한다. `swapChainFramebuffers`, `depthImageView`/`depthImage`/`depthImageMemory`, `swapChainImageViews`, `swapChain`만 파괴한다(`renderPass`, `commandPool`, `device` 등은 건드리지 않는다). 기존 `cleanupCore()`는 이 메서드를 재사용하도록 리팩터링한다.
3. `mainLoop()`의 루프 안, `glfwPollEvents()` 직후 `drawFrame()` 호출 전에 새 가상 함수 `onFrameStart()`(기본 구현은 빈 함수)를 호출하도록 추가한다. 프레임 경계(아직 `vkAcquireNextImageKHR` 호출 전) 밖에서 안전하게 스왑체인을 재생성하기 위한 훅이다.

### main.cpp 변경 — 렌더링 파이프라인

1. `graphicsPipeline`, `linePipeline`, `blurPipeline`, `postPipeline` 4개 파이프라인 생성 시 `VK_DYNAMIC_STATE_VIEWPORT`/`VK_DYNAMIC_STATE_SCISSOR`를 추가한다(뷰포트를 정적으로 굽지 않는다). `shadowPipeline`은 2048x2048 고정이므로 그대로 둔다.
2. `recordCommandBuffer()`에서 오프스크린 패스, 블러 루프, 최종(post) 패스 각각 `vkCmdBeginRenderPass` 직후 현재 `swapChainExtent` 기준으로 `vkCmdSetViewport`/`vkCmdSetScissor`를 호출한다.
3. 이렇게 하면 전체화면 전환 시 파이프라인 객체 자체는 재생성할 필요가 없다(초기화 시 한 번만 생성).

### main.cpp 변경 — 리소스 재생성

1. `createBlurPipeline()`/`createPostProcessPipeline()`에서 디스크립터셋 write 부분을 각각 `updateBlurDescriptorSets()`/`updatePostDescriptorSets()`라는 별도 메서드로 분리한다(디스크립터셋 자체의 할당은 최초 1회만, 재생성 시에는 이미지뷰만 다시 write).
2. `recreateSwapChain()` 메서드를 추가한다:
   - `vkDeviceWaitIdle(device)`
   - 앱 소유 리소스 파괴: `blurFramebuffers[2]`/`blurImages`/`blurViews`/`blurMemories`, `offscreenFramebuffer`와 offscreen color/bright/depth 이미지·뷰·메모리
   - `cleanupSwapChain()` 호출 (base)
   - `createSwapChain(); createImageViews(); createDepthResources(); createFramebuffers();` (base, 기존 메서드 그대로 재사용)
   - `createOffscreenResources(); createBlurResources();` (app, 기존 메서드 그대로 재사용)
   - `updateBlurDescriptorSets(); updatePostDescriptorSets();`
3. `offscreenRenderPass`, `blurRenderPass`, `renderPass`, `shadowRenderPass`는 해상도에 의존하지 않으므로 재생성하지 않는다.

### main.cpp 변경 — 전체화면 토글

1. 멤버 추가: `bool isFullscreen = false;`, `bool fullscreenToggleRequested = false;`
2. 버튼 클릭 시 `fullscreenToggleRequested = true`만 설정한다.
3. `onFrameStart()`를 오버라이드해서 플래그가 켜져 있으면:
   - `vkDeviceWaitIdle(device)`
   - `GLFWmonitor* monitor = glfwGetPrimaryMonitor(); const GLFWvidmode* mode = glfwGetVideoMode(monitor);`
   - 전체화면 진입: `glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE)` → `glfwSetWindowMonitor(window, nullptr, 0, 0, mode->width, mode->height, mode->refreshRate)`
   - 창모드 복귀: `glfwSetWindowMonitor(window, nullptr, centerX, centerY, WIDTH, HEIGHT, 0)`(모니터 중앙 배치) → `glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE)`
   - `recreateSwapChain()` 호출, `isFullscreen` 토글, `fullscreenToggleRequested = false`

## UI 변경

- `if(LOBBY)/else if(SIMULATION)` 분기 **바깥**에 새 ImGui 창 `"Fullscreen Panel"`을 추가해서 두 상태 모두에서 항상 그린다.
  - 위치 `(swapChainExtent.width - 260, 20)`, 크기 `(240, 70)`
  - 버튼 1개(230x40): `isFullscreen == false`면 파란색 "ENTER FULLSCREEN", `true`면 주황색 "EXIT FULLSCREEN" — 기존 Real Scale 버튼과 동일한 색상 전환 패턴 재사용
  - 버튼 클릭 시 `fullscreenToggleRequested = true`
- 기존 `"Settings Panel"`(Real Scale/궤도선, SIMULATION 전용)의 Y좌표를 `20 → 100`으로 내려서 새 패널과 겹치지 않게 한다.

## 테스트 계획

- 빌드 성공 확인 (CMake/MSBuild)
- 앱 실행 후 LOBBY 화면에서 전체화면 버튼이 보이는지, 클릭 시 보더리스 전체화면으로 전환되는지 확인
- 전체화면 상태에서 "LAUNCH MISSION" → SIMULATION 진입, 렌더링(행성, 블룸, 그림자, 소행성대)이 깨지지 않는지 확인
- SIMULATION에서 전체화면 → 창모드 → 전체화면을 반복 전환하며 크래시/디스크립터 오류/뷰포트 어긋남이 없는지 확인
- 창모드 복귀 시 1024x768, 화면 중앙 배치 확인
- Real Scale/궤도선 버튼 및 우측 패널 레이아웃이 새 패널과 겹치지 않는지 확인
