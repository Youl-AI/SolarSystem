# main.cpp 모듈화 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `src/main.cpp` 3,341줄을 역할별 파일로 나누되, 화면에 나타나는 동작은 한 줄도 바꾸지 않는다.

**Architecture:** 분리를 두 종류로 나눈다. Vulkan 핸들도 앱 상태도 보지 않는 조각(천체 정보표, 설정 ini, 소행성 분포 생성, 메시 생성, GPU 프로파일러)은 독립 타입으로 뽑아 인자로만 소통시킨다. 나머지는 `SolarSystemApp` 클래스를 그대로 둔 채 멤버 함수 정의만 여러 번역 단위에 나눠 담는다. 마지막으로 이름과 내용이 어긋난 거대 함수 둘을, 이미 코드 안에 그어져 있는 경계(주석 배너와 `tsMark` 슬롯)를 따라 쪼갠다.

**Tech Stack:** C++17, Vulkan 1.2, GLFW, GLM, Dear ImGui, CMake 3.15+, MSVC

## Global Constraints

- **동작 변경 금지.** 리팩터 도중 눈에 띄는 버그는 고치지 말고 `docs/superpowers/plans/` 옆에 메모만 남긴다. 동작이 바뀌면 "화면이 전과 같아 보인다"는 유일한 검증 수단이 무의미해진다.
- **범위는 `src/main.cpp`뿐.** `src/VulkanBase.hpp`와 `shaders/*`는 건드리지 않는다.
- **작업 위치:** `C:\Users\hayoul1999.YOUL-HOUSE\Desktop\Github\SolarSystem` (메인 체크아웃), 브랜치 `modularize-main`. `.claude/worktrees/` 아래의 워크트리는 낡은 사본이므로 절대 쓰지 않는다. 모든 명령에 절대 경로를 쓴다.
- **한 태스크 = 한 커밋.** 회귀가 생기면 `git bisect`가 한 커밋 안으로 좁힌다.
- **주석을 지우지 않는다.** 이 코드의 주석은 대부분 "왜 이렇게 했는가"와 과거의 실패를 기록한 것이다. 함수를 옮길 때 주석도 함께 옮긴다.
- **한국어 주석은 UTF-8.** CMake가 MSVC에 `/utf-8`을 주고 있으므로 새 파일도 UTF-8(BOM 없이)로 저장한다.

### 빌드 명령 (모든 태스크 공통)

```bash
cmake --build "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/build" --config Release 2>&1 | tail -20
```

기대: `Ephemeris.vcxproj -> ...\build\Release\Ephemeris.exe` 로 끝나고 에러 0.
`CMakeLists.txt`를 고친 태스크에서는 CMake가 스스로 재구성하므로 별도 configure는 필요 없다.

### 육안 검증 체크리스트 (모든 태스크 공통, `VISUAL-CHECK`로 참조)

`C:\Users\hayoul1999.YOUL-HOUSE\Desktop\Github\SolarSystem\scripts\run_dev.bat` 으로 실행하고 아래를 순서대로 확인한다. 하나라도 다르면 커밋하지 말고 되돌린다.

1. 로비 화면에 `E P H E M E R I S` 제목과 회전하는 배경이 나온다
2. 진입 후 지구가 잠긴 상태로 보이고, 드래그로 궤도 회전 / 스크롤로 줌이 된다
3. 지구를 더블클릭하면 정보 패널에 지름·질량이 뜬다
4. 설정창(기어 아이콘)이 열리고, `Real Scale` 체크 시 태양계가 서서히 팽창한다
5. `Real Stars` 체크 시 별이 서서히 성겨진다
6. 일식 체험 버튼을 누르면 카메라가 달로 빨려 들어가 태양을 가린다
7. 소행성이 전부 보이도록 축소해도 벨트가 정상적으로 그려진다
8. 창 크기를 바꿔도 화면이 깨지지 않는다
9. 종료 시 `settings.ini`가 정상적으로 갱신된다

`SOLAR_PROFILE=1`이 켜져 있으므로 콘솔에 120프레임마다 `[profile]` 줄이 나온다. **그 줄의 구간별 ms 값을 태스크 시작 전과 비교한다.** 패스 순서를 실수로 바꿨다면 여기서 드러난다.

---

## 파일 구조 (최종 형태)

| 파일 | 책임 |
|---|---|
| `src/main.cpp` | 진입점. `SolarSystemApp`을 만들고 `run()`을 호출하며 예외를 MessageBox로 띄운다 |
| `src/third_party_impl.cpp` | stb_image / stb_image_write / tinyexr / tiny_obj_loader의 `_IMPLEMENTATION`을 담는 유일한 번역 단위 |
| `src/app/Types.hpp` | `PushConstants`, `AsteroidCullPush`, `UniformBufferObject`, `Planet`, 공용 상수 |
| `src/app/SolarSystemApp.hpp` | 클래스 선언과 멤버 변수 전부. 구현은 짧은 인라인 접근자만 |
| `src/app/App_Init.cpp` | `initApp`, 태양계 구성, `createPlanet`, `cleanupApp` |
| `src/app/App_Frame.cpp` | 시뮬레이션 갱신 (UI 제외) |
| `src/app/App_Ui.cpp` | ImGui 전부 |
| `src/app/App_Record.cpp` | `recordCommandBuffer`와 패스별 record 함수 |
| `src/app/App_Resources.cpp` | 파이프라인·버퍼·디스크립터·오프스크린·블룸·포스트의 생성/파괴/재생성 |
| `src/app/App_Textures.cpp` | DDS·큐브맵·더미 텍스처 로딩 |
| `src/app/App_Input.cpp` | `onFrameStart`, `throttleFrame`, 마우스 콜백, 레이캐스트 |
| `src/core/Settings.hpp/.cpp` | `GraphicsSettings`, ini 로드·저장 |
| `src/core/GpuProfiler.hpp/.cpp` | 타임스탬프 쿼리 풀과 누산기 |
| `src/sim/CelestialData.hpp/.cpp` | 천체 정보 문자열 표 |
| `src/sim/AsteroidBelt.hpp/.cpp` | `AsteroidData`, 벨트 분포 생성 |
| `src/gfx/MeshGen.hpp/.cpp` | 구·고리·궤도 생성, OBJ 로딩, LOD 단순화 |

---

## Task 1: 골격 만들기 — 클래스를 헤더로, main.cpp는 진입점만

이 태스크는 **코드를 한 줄도 고치지 않고 위치만 옮긴다.** 클래스 본문은 전부 헤더 안에 인라인으로 남는다. 나누는 것은 다음 태스크들의 일이고, 여기서는 그 자리를 만든다.

**Files:**
- Create: `src/app/Types.hpp`
- Create: `src/app/SolarSystemApp.hpp`
- Create: `src/third_party_impl.cpp`
- Modify: `src/main.cpp` (3,341줄 → 약 40줄)
- Modify: `CMakeLists.txt:52-59`

**Interfaces:**
- Consumes: 없음 (첫 태스크)
- Produces: `class SolarSystemApp` (in `src/app/SolarSystemApp.hpp`), `struct PushConstants`, `struct AsteroidCullPush`, `struct UniformBufferObject`, `struct Planet`, `struct GraphicsSettings`, `struct AsteroidData` (모두 `src/app/Types.hpp`), 상수 `MAX_OCCLUDERS`, `kAtmosphereShellScale`, `SHADOW_SUN_SHRINK`, `SHADOW_SUN_SHRINK_MOON`, `kResolutions`, `RESOLUTION_COUNT`

- [ ] **Step 1: 서드파티 구현부를 전용 번역 단위로 옮긴다**

`src/third_party_impl.cpp`를 만든다. 지금 `src/main.cpp:1-20`에 있는 `_IMPLEMENTATION` 정의를 여기로 통째로 옮긴다. 이 매크로들은 **정확히 하나의 .cpp에서만** 정의되어야 하며, 앞으로 .cpp가 늘어나므로 지금 자리를 확정해 둔다.

```cpp
// 헤더 온리 라이브러리들의 구현부를 담는 유일한 번역 단위.
//
// stb / tinyexr / tinyobjloader는 _IMPLEMENTATION을 정의한 곳에 함수 본문을 쏟아낸다.
// 예전에는 main.cpp가 유일한 .cpp라 거기 있어도 됐지만, 파일이 여러 개로 나뉘면
// 두 곳에서 정의되는 순간 링커가 중복 심볼로 죽는다. 그래서 여기 한 곳에 못 박는다.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 0       // miniz.h와 miniz.c를 찾지 마!
#define TINYEXR_USE_STB_ZLIB 1    // 대신 이미 있는 stb_image의 zlib 코드를 사용해!
#include "tinyexr.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
```

- [ ] **Step 2: 공용 타입을 `Types.hpp`로 옮긴다**

`src/app/Types.hpp`를 만들고, `src/main.cpp:44-181`의 아래 항목을 **주석까지 그대로** 옮긴다.

- `struct PushConstants`
- `struct AsteroidCullPush`
- `static constexpr int MAX_OCCLUDERS = 16;` → `inline constexpr int MAX_OCCLUDERS = 16;`
- `static constexpr float kAtmosphereShellScale` → `inline constexpr`
- `static constexpr float SHADOW_SUN_SHRINK` → `inline constexpr`
- `static constexpr float SHADOW_SUN_SHRINK_MOON` → `inline constexpr`
- `struct UniformBufferObject`
- `struct Planet`

`static`을 `inline`으로 바꾸는 이유: `static`은 번역 단위마다 별개의 사본을 만든다. 지금은 .cpp가 하나뿐이라 차이가 없지만, 파일이 늘어나면 사본이 늘어난다. `inline constexpr`은 전체에서 하나다.

`GraphicsSettings` / `kResolutions` / `RESOLUTION_COUNT`는 Task 2에서, `AsteroidData`는 Task 5에서 각자의 파일로 갈 예정이지만, **이번 태스크에서는 `Types.hpp`에 함께 넣는다.** 한 번에 한 가지만 움직인다.

파일 머리:

```cpp
#pragma once

#include "../VulkanBase.hpp"   // Vertex, Vk* 핸들
#include <glm/glm.hpp>
#include <string>
```

- [ ] **Step 3: 클래스를 `SolarSystemApp.hpp`로 옮긴다**

`src/app/SolarSystemApp.hpp`를 만들고, `src/main.cpp:183-3318`의 `class SolarSystemApp { ... };` 전체를 **본문 그대로** 옮긴다. 함수 본문은 손대지 않는다.

파일 머리:

```cpp
#pragma once

#include "Types.hpp"
#include "../VulkanBase.hpp"
#include "../Camera.hpp"

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
```

- [ ] **Step 4: `main.cpp`를 진입점만 남긴다**

`src/main.cpp`를 통째로 아래로 교체한다.

```cpp
#include "app/SolarSystemApp.hpp"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
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
        // CMD 창이 없어도, 크래시가 나면 윈도우 에러 창을 띄워서 원인을 알려준다.
#ifdef _WIN32
        MessageBoxA(nullptr, e.what(), "Ephemeris — Fatal Error", MB_OK | MB_ICONERROR);
#else
        std::cerr << e.what() << std::endl;
#endif
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
```

- [ ] **Step 5: CMake에 새 소스를 등록한다**

`CMakeLists.txt:52-59`의 `add_executable` 첫 줄을 바꾼다.

```cmake
add_executable(Ephemeris 
    src/main.cpp
    src/third_party_impl.cpp
    ${IMGUI_DIR}/imgui.cpp
```

`src/` 를 include 경로에 추가한다 (`target_include_directories`의 목록에 한 줄 추가):

```cmake
target_include_directories(Ephemeris PRIVATE 
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_DIR}            # imgui.h 를 찾기 위해 추가
    ${IMGUI_DIR}/backends   # imgui_impl_glfw.h 등을 찾기 위해 추가
)
```

- [ ] **Step 6: 빌드**

Run: `cmake --build "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/build" --config Release 2>&1 | tail -20`
Expected: 에러 0, `Ephemeris.exe` 생성.

링커가 `stbi_load` 등을 중복 정의로 신고하면 Step 1이 덜 된 것이다 — `SolarSystemApp.hpp`에 `_IMPLEMENTATION` 정의가 남아 있는지 본다.

- [ ] **Step 7: 실행 검증**

`VISUAL-CHECK` 전 항목을 수행한다.

- [ ] **Step 8: 커밋**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add src/main.cpp src/third_party_impl.cpp src/app/Types.hpp src/app/SolarSystemApp.hpp CMakeLists.txt
git commit -m "Move the app class into a header, leaving main.cpp as the entry point"
```

---

## Task 2: 설정 ini를 독립 타입으로

**Files:**
- Create: `src/core/Settings.hpp`, `src/core/Settings.cpp`
- Modify: `src/app/Types.hpp` (`GraphicsSettings`, `kResolutions`, `RESOLUTION_COUNT` 삭제)
- Modify: `src/app/SolarSystemApp.hpp` (`loadSettings`/`saveSettings` 삭제, `#include` 추가, 호출부 2곳 수정)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `struct GraphicsSettings` (Task 1이 `Types.hpp`에 둔 것)
- Produces:
  ```cpp
  struct GraphicsSettings { /* 필드는 기존과 동일 */ };
  inline constexpr int kResolutions[][2] = {...};
  inline constexpr int RESOLUTION_COUNT = 5;
  void loadSettings(GraphicsSettings &s, const char *path = "settings.ini");
  void saveSettings(const GraphicsSettings &s, const char *path = "settings.ini");
  ```

- [ ] **Step 1: 헤더를 만든다**

`src/core/Settings.hpp`:

```cpp
#pragma once

// 그래픽 설정과 settings.ini 입출력.
//
// Vulkan도 앱 상태도 보지 않는다. 값을 담는 구조체와, 그것을 파일과 주고받는
// 함수 둘뿐이다.

struct GraphicsSettings {
    int   resolutionIndex = 2;   // index into kResolutions; 2 = 1920x1080
    float renderScale     = 1.0f;
    int   msaaLevel       = 8;   // 0=off, else 2/4/8; clamped to device support
    bool  vsync           = true;
    float fovDegrees      = 45.0f;
    float exposure        = 1.0f;
    int   frameCap        = 0;   // 0 = unlimited
    bool  showFps         = false;
    bool  showGpuTimes    = false;  // 패스별 GPU 시간 오버레이
    bool  fullscreen      = false;
    bool  orbitLines      = false;
    bool  realScale       = false;
    // 하늘의 사실성. 축척과는 별개의 축이라 따로 둔다 — 압축 축척으로 태양계를 보면서도
    // 맨눈에 실제로 보이는 별하늘을 볼 수 있어야 한다.
    bool  realStars       = false;
    bool  asteroids       = true;   // 끄면 소행성대/카이퍼벨트를 통째로 건너뛴다
};

inline constexpr int kResolutions[][2] = {
    {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}
};
inline constexpr int RESOLUTION_COUNT = 5;

// 파일이 없으면 s를 그대로 둔다(구조체 기본값 유지).
void loadSettings(GraphicsSettings &s, const char *path = "settings.ini");
// 저장 실패는 조용히 무시한다.
void saveSettings(const GraphicsSettings &s, const char *path = "settings.ini");
```

`kResolutions`의 원래 타입은 `static const int[][2]`였다. `inline constexpr`로 바꾸는 이유는 Task 1 Step 2와 같다 — 번역 단위마다 사본이 생기는 것을 막는다.

- [ ] **Step 2: 구현을 옮긴다**

`src/core/Settings.cpp`를 만들고, `src/app/SolarSystemApp.hpp`의 `loadSettings`(원래 `main.cpp:678-701`)와 `saveSettings`(원래 `main.cpp:703-738`) 본문을 **주석까지 그대로** 옮긴다. 멤버 `settings.`를 인자 `s.`로 바꾸고, 하드코딩된 `"settings.ini"`를 `path`로 바꾼다.

```cpp
#include "Settings.hpp"

#include <fstream>
#include <string>
#include <algorithm>

void loadSettings(GraphicsSettings &s, const char *path) {
    std::ifstream f(path);
    if (!f.is_open()) return; // 파일 없으면 구조체 기본값 유지
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        try {
            if      (k == "resolutionIndex") s.resolutionIndex = std::clamp(std::stoi(v), 0, RESOLUTION_COUNT - 1);
            else if (k == "renderScale")     s.renderScale = std::clamp(std::stof(v), 0.5f, 2.0f);
            else if (k == "msaaLevel")       s.msaaLevel = std::stoi(v);
            else if (k == "vsync")           s.vsync = (std::stoi(v) != 0);
            else if (k == "fovDegrees")      s.fovDegrees = std::clamp(std::stof(v), 30.0f, 90.0f);
            else if (k == "exposure")        s.exposure = std::clamp(std::stof(v), 0.3f, 3.0f);
            else if (k == "frameCap")        s.frameCap = std::max(0, std::stoi(v));
            else if (k == "showFps")         s.showFps = (std::stoi(v) != 0);
            else if (k == "showGpuTimes")    s.showGpuTimes = (std::stoi(v) != 0);
            else if (k == "fullscreen")      s.fullscreen = (std::stoi(v) != 0);
            // orbitLines / realScale / realStars는 의도적으로 복원하지 않는다: 매 실행 항상 OFF로 시작하는
            // 런타임 뷰 토글이다. (구버전 settings.ini에 남아 있어도 무시)
        } catch (...) { /* 잘못된 값은 무시하고 기본값 유지 */ }
    }
}

void saveSettings(const GraphicsSettings &s, const char *path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return; // 저장 실패는 조용히 무시
    f << "resolutionIndex=" << s.resolutionIndex << "\n"
      << "renderScale="     << s.renderScale     << "\n"
      << "msaaLevel="       << s.msaaLevel       << "\n"
      << "vsync="           << (s.vsync ? 1 : 0) << "\n"
      << "fovDegrees="      << s.fovDegrees      << "\n"
      << "exposure="        << s.exposure        << "\n"
      << "frameCap="        << s.frameCap        << "\n"
      << "showFps="         << (s.showFps ? 1 : 0) << "\n"
      << "showGpuTimes="    << (s.showGpuTimes ? 1 : 0) << "\n"
      << "fullscreen="      << (s.fullscreen ? 1 : 0) << "\n";
    // orbitLines / realScale / realStars는 저장하지 않는다 — 항상 OFF로 시작하는 런타임 전용 뷰 토글.
}
```

- [ ] **Step 3: 호출부를 고친다**

`src/app/Types.hpp`에서 `GraphicsSettings`, `kResolutions`, `RESOLUTION_COUNT` 정의를 지우고 `#include "../core/Settings.hpp"`를 추가한다.

`src/app/SolarSystemApp.hpp`에서 두 함수 정의를 지우고, 호출부를 바꾼다:

- `initApp()` 안의 `loadSettings();` → `loadSettings(settings);`
- `cleanupApp()` 안의 `saveSettings();` → `saveSettings(settings);`

호출부 위치는 아래로 찾는다:

```bash
grep -n "loadSettings\|saveSettings" "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/src/app/SolarSystemApp.hpp"
```

- [ ] **Step 4: CMake에 추가**

`CMakeLists.txt`의 `add_executable`에 `src/core/Settings.cpp`를 추가한다.

- [ ] **Step 5: 빌드**

Run: `cmake --build "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/build" --config Release 2>&1 | tail -20`
Expected: 에러 0.

- [ ] **Step 6: 설정 왕복 검증**

`VISUAL-CHECK` 수행. 추가로: 설정창에서 해상도를 1600×900으로, 노출을 1.5로 바꾸고 종료한 뒤 `settings.ini`를 열어 `resolutionIndex=1`, `exposure=1.5`가 적혔는지 본다. 다시 실행해 그 값으로 복원되는지 본다. 그리고 `realScale`, `realStars`, `orbitLines` 세 줄이 **파일에 없는지** 확인한다 — 이건 의도된 동작이다.

- [ ] **Step 7: 커밋**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add src/core/Settings.hpp src/core/Settings.cpp src/app/Types.hpp src/app/SolarSystemApp.hpp CMakeLists.txt
git commit -m "Give the settings file its own module"
```

---

## Task 3: GPU 프로파일러를 독립 타입으로

**Files:**
- Create: `src/core/GpuProfiler.hpp`, `src/core/GpuProfiler.cpp`
- Modify: `src/app/SolarSystemApp.hpp` (멤버 11개와 함수 3개 삭제, 사용처 수정)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: 없음 (Vulkan 핸들만 인자로 받는다)
- Produces:
  ```cpp
  class GpuProfiler {
  public:
      static constexpr int SLOTS = 11;
      static const char* const NAMES[10];

      void init(VkPhysicalDevice phys, VkDevice dev);   // SOLAR_PROFILE=1 검사 포함
      void destroy(VkDevice dev);

      bool devTools() const;      // SOLAR_PROFILE=1 인가
      bool enabled()  const;      // 실제로 타임스탬프를 찍는가

      void beginFrame(double nowSeconds);
      void resetPool(VkCommandBuffer cb);
      void mark(VkCommandBuffer cb, uint32_t slot);
      void addRecordMs(double ms);
      void snapshotDrawCmds(const void *mappedIndirectBuffer);
      void collect(VkDevice dev, size_t asteroidCount);

      double smoothMs(int i) const;
      const VkDrawIndexedIndirectCommand *drawCmds() const;
  };
  ```

- [ ] **Step 1: 헤더를 만든다**

`src/core/GpuProfiler.hpp`:

```cpp
#pragma once

#include <vulkan/vulkan.h>
#include <cstddef>

// 패스별 GPU 시간 계측 (개발자 전용).
//
// SOLAR_PROFILE=1 환경변수를 주고 실행할 때만 켜진다. 배포판을 그냥 실행하면
// 타임스탬프도 안 찍고 설정 창에 항목도 나오지 않는다 — 최종 사용자에게 보여 줄
// 정보가 아니고, 오히려 해석을 틀리기 쉬운 숫자다(%는 다른 패스가 싸져도 오른다).
//
// 개발 중에는 scripts/run_dev.bat으로 실행하면 된다.
class GpuProfiler {
public:
    // 슬롯 11개 = 구간 10개. 0시작 1컴퓨트 2소행성 3행성 4은하 5궤도선 6태양 7블러 8포스트 9끝
    static constexpr int SLOTS = 11;
    static const char *const NAMES[10];

    void init(VkPhysicalDevice phys, VkDevice dev);
    void destroy(VkDevice dev);

    bool devTools() const { return devTools_; }
    bool enabled()  const { return profiling_; }

    // 프레임 경계. CPU 프레임 시간을 누적하고 프레임 수를 센다.
    void beginFrame(double nowSeconds);
    // 커맨드 버퍼 맨 앞에서 쿼리 풀을 비운다.
    void resetPool(VkCommandBuffer cb);
    // 패스 경계마다 타임스탬프를 찍는다. 꺼져 있으면 아무 일도 안 한다.
    void mark(VkCommandBuffer cb, uint32_t slot);
    // 커맨드 기록에 걸린 CPU 시간(ms)을 누적한다.
    void addRecordMs(double ms);
    // 간접 그리기 버퍼의 LOD 바구니 스냅샷을 뜬다. 오버레이와 콘솔이 같이 쓴다.
    void snapshotDrawCmds(const void *mappedIndirectBuffer);
    // 직전 프레임의 타임스탬프를 읽어 누적한다(펜스 대기 후라 이미 준비돼 있다).
    void collect(VkDevice dev, size_t asteroidCount);

    // 오버레이용 지수 평활값. 매 프레임 원값을 쓰면 숫자가 읽을 수 없게 떨린다.
    double smoothMs(int i) const { return tsSmoothGpu_[i]; }
    const VkDrawIndexedIndirectCommand *drawCmds() const { return lastDrawCmds_; }

private:
    VkQueryPool tsPool_ = VK_NULL_HANDLE;
    float  tsPeriodNs_  = 0.0f;
    bool   devTools_    = false;
    bool   profiling_   = false;
    int    frameCount_  = 0;
    double accumCpuRecord_ = 0.0, accumCpuFrame_ = 0.0, lastFrameStart_ = 0.0;
    double tsAccumGpu_[10]  = {};
    double tsSmoothGpu_[10] = {};
    VkDrawIndexedIndirectCommand lastDrawCmds_[12] = {};
};
```

- [ ] **Step 2: 구현을 옮긴다**

`src/core/GpuProfiler.cpp`를 만든다. `initProfiling`(원래 `main.cpp:740-758`)과 `collectProfile`(원래 `main.cpp:760-807`)의 본문을 **주석까지 그대로** 옮긴다. 멤버 이름에 `_` 접미사를 붙이고, `physicalDevice`/`device`를 인자로 받는다. `asteroidTransforms.size()`는 인자 `asteroidCount`로 대체한다.

`beginFrame`, `resetPool`, `mark`, `addRecordMs`, `snapshotDrawCmds`는 지금 호출부에 흩어져 있는 코드를 그대로 옮긴 것이다:

```cpp
#include "GpuProfiler.hpp"

#include <iostream>
#include <cstdlib>
#include <cstring>

const char *const GpuProfiler::NAMES[10] = {
    "compute", "asteroids", "bodies", "skybox", "galaxy+atmo",
    "orbits", "sun", "blur", "post", "tail"
};

void GpuProfiler::init(VkPhysicalDevice phys, VkDevice dev) {
    const char *env = std::getenv("SOLAR_PROFILE");
    devTools_ = (env && env[0] == '1');
    if (!devTools_) return;      // 배포 실행에서는 쿼리 풀조차 만들지 않는다

    VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(phys, &props);
    // 타임스탬프를 못 쓰는 큐가 있는 장치에서는 계측을 통째로 끈다.
    if (props.limits.timestampPeriod == 0.0f || props.limits.timestampComputeAndGraphics == VK_FALSE) {
        std::cout << "[profile] 이 장치는 타임스탬프 쿼리를 지원하지 않아 계측을 끕니다.\n";
        return;
    }
    tsPeriodNs_ = props.limits.timestampPeriod;
    VkQueryPoolCreateInfo qi{}; qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = SLOTS;
    if (vkCreateQueryPool(dev, &qi, nullptr, &tsPool_) != VK_SUCCESS) return;
    profiling_ = true;
    std::cout << "[profile] on. timestampPeriod=" << tsPeriodNs_ << " ns/tick\n";
}

void GpuProfiler::destroy(VkDevice dev) {
    if (tsPool_ != VK_NULL_HANDLE) { vkDestroyQueryPool(dev, tsPool_, nullptr); tsPool_ = VK_NULL_HANDLE; }
}

void GpuProfiler::beginFrame(double nowSeconds) {
    if (!profiling_) return;
    if (lastFrameStart_ > 0.0) accumCpuFrame_ += (nowSeconds - lastFrameStart_) * 1000.0;
    lastFrameStart_ = nowSeconds;
    ++frameCount_;
}

void GpuProfiler::resetPool(VkCommandBuffer cb) {
    if (profiling_ && tsPool_ != VK_NULL_HANDLE) vkCmdResetQueryPool(cb, tsPool_, 0, SLOTS);
}

void GpuProfiler::mark(VkCommandBuffer cb, uint32_t slot) {
    if (!profiling_ || tsPool_ == VK_NULL_HANDLE) return;
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPool_, slot);
}

void GpuProfiler::addRecordMs(double ms) { accumCpuRecord_ += ms; }

void GpuProfiler::snapshotDrawCmds(const void *mapped) {
    if (!profiling_ || !mapped) return;
    std::memcpy(lastDrawCmds_, mapped, 12 * sizeof(VkDrawIndexedIndirectCommand));
}
```

`collect()`는 원래 `collectProfile()`과 같다. `tsFrameCount`→`frameCount_`, `tsAccumGpu`→`tsAccumGpu_`, `TS_NAMES`→`NAMES`, `asteroidTransforms.size()`→`asteroidCount`로만 바꾸고 나머지 로직과 주석은 그대로 둔다. 특히 아래 두 가지를 절대 바꾸지 않는다:

- 루프 상한은 `i < 10`이다 (`i < 11`이면 배열 밖을 읽고 쓴다 — 예전에 실제로 있었던 버그다)
- 누산기 리셋은 콘솔 출력 블록 **밖**에 있다 (안에 있으면 콘솔을 껐을 때 값이 영원히 자란다)

- [ ] **Step 3: 사용처를 고친다**

`src/app/SolarSystemApp.hpp`에서 멤버 `tsPool`, `tsPeriodNs`, `devTools`, `profiling`, `tsFrameCount`, `tsAccumCpuRecord`, `tsAccumCpuFrame`, `tsLastFrameStart`, `tsAccumGpu`, `tsSmoothGpu`, `lastDrawCmds`, `TS_SLOTS`, `TS_NAMES`와 함수 `initProfiling`, `collectProfile`, `tsMark`를 지우고, 멤버 하나로 대체한다:

```cpp
GpuProfiler profiler;
```

`#include "../core/GpuProfiler.hpp"`를 추가한다. 그리고 아래 사용처를 전부 바꾼다. 위치는 `grep -n "profiling\|devTools\|tsMark\|tsSmoothGpu\|lastDrawCmds\|tsAccumCpu\|tsFrameCount\|initProfiling\|collectProfile\|tsPool\|TS_NAMES" src/app/SolarSystemApp.hpp` 로 전부 찾는다.

| 원래 (main.cpp 기준 줄) | 바꿀 것 |
|---|---|
| `onFrameStart` 381-387의 `if (profiling) { ... ++tsFrameCount; }` 블록 | `profiler.beginFrame(glfwGetTime());` |
| 1897-1898 `if (profiling && indirectDrawMapped) memcpy(lastDrawCmds, ...)` | `profiler.snapshotDrawCmds(indirectDrawMapped);` |
| 1951 `if (settings.showGpuTimes && profiling)` | `if (settings.showGpuTimes && profiler.enabled())` |
| 1961·1966-1968 `tsSmoothGpu[i]` | `profiler.smoothMs(i)` |
| 1967 `TS_NAMES[i]` | `GpuProfiler::NAMES[i]` |
| 1976 `lastDrawCmds[...]` | `profiler.drawCmds()[...]` |
| 2261-2265 `devTools` / `profiling` | `profiler.devTools()` / `profiler.enabled()` |
| 2340 `if (profiling && tsPool != VK_NULL_HANDLE) vkCmdResetQueryPool(...)` | `profiler.resetPool(cb);` |
| 2341·2382·2403·2444·2462·2490·2541·2560·2597·2607·2608 `tsMark(cb, N)` | `profiler.mark(cb, N)` |
| 2610 `tsAccumCpuRecord += (glfwGetTime() - recordStart) * 1000.0;` | `profiler.addRecordMs((glfwGetTime() - recordStart) * 1000.0);` |
| 2643 `if (tsPool != VK_NULL_HANDLE) { vkDestroyQueryPool(...); }` | `profiler.destroy(device);` |
| 3116·3131 `devTools` | `profiler.devTools()` |
| `initApp` 안의 `initProfiling();` | `profiler.init(physicalDevice, device);` |
| `collectProfile();` 호출부 | `profiler.collect(device, asteroidTransforms.size());` |

- [ ] **Step 4: CMake에 추가**

`CMakeLists.txt`의 `add_executable`에 `src/core/GpuProfiler.cpp`를 추가한다.

- [ ] **Step 5: 빌드**

Run: `cmake --build "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/build" --config Release 2>&1 | tail -20`
Expected: 에러 0.

- [ ] **Step 6: 계측 동작 검증**

`VISUAL-CHECK` 수행. 추가로:

1. `scripts/run_dev.bat`으로 실행 → 콘솔에 `[profile] on. timestampPeriod=` 가 뜨고, 120프레임마다 `[profile] frames=...` 와 `[lodmix] LOD0=...` 가 나오는지 확인
2. 설정창에 `Show GPU Times` 항목이 보이고, 켜면 좌하단 오버레이에 10개 구간이 뜨는지 확인
3. `build/Release/Ephemeris.exe`를 **직접** 실행(환경변수 없이) → `[profile]` 줄이 **안 나오고** 설정창에 `Show GPU Times`가 **안 보이는지** 확인

- [ ] **Step 7: 커밋**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add src/core/GpuProfiler.hpp src/core/GpuProfiler.cpp src/app/SolarSystemApp.hpp CMakeLists.txt
git commit -m "Move GPU timing into a profiler that owns its query pool"
```

---

## Task 4: 천체 정보표를 독립 함수로

**Files:**
- Create: `src/sim/CelestialData.hpp`, `src/sim/CelestialData.cpp`
- Modify: `src/app/SolarSystemApp.hpp` (`getCelestialInfo` 삭제)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: 없음
- Produces: `std::string getCelestialInfo(const std::string &name);` (in `src/sim/CelestialData.hpp`)

- [ ] **Step 1: 헤더를 만든다**

`src/sim/CelestialData.hpp`:

```cpp
#pragma once

#include <string>

// 천체의 제원을 사람이 읽는 문자열로 돌려준다. 정보 패널이 쓴다.
// 이름을 모르면 빈 문자열이 아니라 기존과 동일한 기본 문구를 돌려준다.
std::string getCelestialInfo(const std::string &name);
```

- [ ] **Step 2: 구현을 옮긴다**

`src/sim/CelestialData.cpp`를 만들고, `src/app/SolarSystemApp.hpp`의 `getCelestialInfo`(원래 `main.cpp:288-377`) 본문을 **한 글자도 바꾸지 말고** 옮긴다. 멤버가 아니게 되므로 `std::string getCelestialInfo(const std::string& name) {` 로 시작한다.

```cpp
#include "CelestialData.hpp"
```

문자열 안의 한국어와 `°` 같은 문자가 깨지지 않도록 UTF-8로 저장한다.

- [ ] **Step 3: 호출부를 고친다**

`src/app/SolarSystemApp.hpp`에서 함수 정의를 지우고 `#include "../sim/CelestialData.hpp"`를 추가한다. 호출부(`getCelestialInfo(...)`)는 이름이 같으므로 수정할 필요가 없다.

- [ ] **Step 4: CMake에 추가**

`CMakeLists.txt`의 `add_executable`에 `src/sim/CelestialData.cpp`를 추가한다.

- [ ] **Step 5: 빌드**

Run: `cmake --build "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/build" --config Release 2>&1 | tail -20`
Expected: 에러 0.

- [ ] **Step 6: 정보 패널 검증**

`VISUAL-CHECK` 수행. 추가로 지구·목성·토성·달·명왕성을 각각 더블클릭해 정보 패널의 글자가 깨지지 않고 나오는지 확인한다(한국어와 `°`, `×` 기호 포함).

- [ ] **Step 7: 커밋**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add src/sim/CelestialData.hpp src/sim/CelestialData.cpp src/app/SolarSystemApp.hpp CMakeLists.txt
git commit -m "Move the celestial fact sheet out of the app class"
```

---

## Task 5: 소행성대 분포 생성을 독립 함수로

**Files:**
- Create: `src/sim/AsteroidBelt.hpp`, `src/sim/AsteroidBelt.cpp`
- Modify: `src/app/Types.hpp` (`AsteroidData` 삭제)
- Modify: `src/app/SolarSystemApp.hpp` (`generateAsteroidBelt` 삭제, include 추가)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `struct AsteroidData` (Task 1이 `Types.hpp`에 둔 것)
- Produces:
  ```cpp
  struct AsteroidData { glm::mat4 transform; int type; float posScaleReal; int pad2, pad3; };
  std::vector<AsteroidData> generateAsteroidBelt(int amount, float minRadius, float maxRadius,
                                                 float inclinationRad, float minScale, float maxScale,
                                                 float posScaleReal);
  ```

- [ ] **Step 1: 헤더를 만든다**

`src/sim/AsteroidBelt.hpp`. 원래 `main.cpp:1012-1014`의 시그니처를 그대로 쓴다 — 인자를 바꾸지 않는다.

```cpp
#pragma once

#include <glm/glm.hpp>
#include <vector>

// GPU가 읽는 소행성 하나의 데이터. asteroid_cull.comp의 레이아웃과 일치해야 한다.
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

// 실제 궤도 요소 분포와 충돌 파편의 크기 분포를 따라 벨트 하나를 만든다.
// Vulkan을 보지 않는다 — 순수하게 숫자만 만들어 돌려준다.
std::vector<AsteroidData> generateAsteroidBelt(int amount, float minRadius, float maxRadius,
                                               float inclinationRad, float minScale, float maxScale,
                                               float posScaleReal);
```

- [ ] **Step 2: 구현을 옮긴다**

`src/sim/AsteroidBelt.cpp`를 만들고 본문을 **주석까지 그대로** 옮긴다. 필요한 include는 `<random>`, `<cmath>`, `<glm/gtc/matrix_transform.hpp>`이다. 이 함수는 멤버를 쓰지 않으므로 본문은 손댈 곳이 없다 — Step 4의 빌드가 이를 확인해 준다.

- [ ] **Step 3: 호출부를 고친다**

`src/app/Types.hpp`에서 `AsteroidData` 정의를 지운다. `src/app/SolarSystemApp.hpp`에서 함수 정의를 지우고 `#include "../sim/AsteroidBelt.hpp"`를 추가한다. 호출부는 이름이 같으므로 수정할 필요가 없다.

- [ ] **Step 4: CMake에 추가하고 빌드**

`CMakeLists.txt`의 `add_executable`에 `src/sim/AsteroidBelt.cpp`를 추가한다.

Run: `cmake --build "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/build" --config Release 2>&1 | tail -20`
Expected: 에러 0. 만약 "멤버를 쓸 수 없다"는 에러가 나면 그 멤버를 인자로 추가한다.

- [ ] **Step 5: 벨트 검증**

`VISUAL-CHECK` 수행. 추가로: 소행성이 전부 보이도록 축소한 뒤 `[lodmix]` 콘솔 줄의 전체 개수가 **12,400**인지 확인한다. 그리고 `Real Scale`을 켜서 두 벨트(소행성대·카이퍼벨트)가 각자 다른 거리로 팽창하는지 본다 — 하나의 배율로 뭉개지면 `posScaleReal`이 제대로 전달되지 않은 것이다.

- [ ] **Step 6: 커밋**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add src/sim/AsteroidBelt.hpp src/sim/AsteroidBelt.cpp src/app/Types.hpp src/app/SolarSystemApp.hpp CMakeLists.txt
git commit -m "Move asteroid belt generation out of the app class"
```

---

## Task 6: 메시 생성을 순수 함수로

이 태스크만 **시그니처를 바꾼다.** 지금 `generateSphere`/`generateRing`/`generateOrbit`은 멤버 `vertices`/`indices`에 직접 쓴다. 출력 벡터를 인자로 받게 바꾸면 순수 함수가 되고, 이 프로젝트에서 나중에 테스트를 붙일 수 있는 유일한 부분이 된다.

**Files:**
- Create: `src/gfx/MeshGen.hpp`, `src/gfx/MeshGen.cpp`
- Modify: `src/app/SolarSystemApp.hpp` (함수 5개 삭제, 호출부 수정)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `struct Vertex` (`src/VulkanBase.hpp`)
- Produces:
  ```cpp
  void generateSphere(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                      float radius, int sectorCount, int stackCount);
  void generateRing(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                    float innerRadius, float outerRadius, int sectorCount);
  void generateOrbit(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                     int segmentCount);
  void loadObjModel(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                    const std::string &path,
                    uint32_t &outIndexCount, uint32_t &outFirstIndex, int32_t &outVertexOffset);
  void buildSimplifiedLod(std::vector<Vertex> &verts, std::vector<uint32_t> &inds,
                          uint32_t srcFirstIndex, uint32_t srcIndexCount, int32_t srcVertexOffset,
                          int gridRes,
                          uint32_t &outIndexCount, uint32_t &outFirstIndex, int32_t &outVertexOffset);
  ```

원래 위치: `generateSphere` `main.cpp:2909`, `generateRing` `main.cpp:2934`, `generateOrbit` `main.cpp:2945`, `loadObjModel` `main.cpp:1120`, `buildSimplifiedLod` `main.cpp:1064`.

- [ ] **Step 1: 헤더를 만든다**

`src/gfx/MeshGen.hpp`. 위 Interfaces의 선언을 그대로 쓴다. 파일 머리:

```cpp
#pragma once

#include "../VulkanBase.hpp"   // Vertex
#include <vector>
#include <string>
#include <cstdint>

// 정점·인덱스 버퍼를 채우는 순수 함수들.
//
// 예전에는 멤버 vertices/indices에 직접 썼다. 출력 벡터를 인자로 받으면 Vulkan도
// 앱 상태도 보지 않게 되어, 이 프로젝트에서 유일하게 단위 테스트가 가능한 부분이 된다.
```

- [ ] **Step 2: 구현을 옮긴다**

`src/gfx/MeshGen.cpp`를 만들고 다섯 함수 본문을 **주석까지 그대로** 옮긴다. 본문 안의 `vertices`를 `verts`로, `indices`를 `inds`로 바꾼다. 그 외에는 아무것도 바꾸지 않는다.

`loadObjModel`은 `tiny_obj_loader.h`를 include한다. **`TINYOBJLOADER_IMPLEMENTATION`은 정의하지 않는다** — 구현부는 `src/third_party_impl.cpp`에 있다.

```cpp
#include "MeshGen.hpp"

#include "tiny_obj_loader.h"
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <cmath>
#include <stdexcept>
```

- [ ] **Step 3: 호출부를 고친다**

`src/app/SolarSystemApp.hpp`에서 다섯 함수 정의를 지우고 `#include "../gfx/MeshGen.hpp"`를 추가한다. 호출부를 전부 찾아 앞에 `vertices, indices,`를 끼워 넣는다.

```bash
grep -n "generateSphere(\|generateRing(\|generateOrbit(\|loadObjModel(\|buildSimplifiedLod(" "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/src/app/SolarSystemApp.hpp"
```

예: `generateSphere(1.0f, 64, 32);` → `generateSphere(vertices, indices, 1.0f, 64, 32);`

- [ ] **Step 4: CMake에 추가하고 빌드**

`CMakeLists.txt`의 `add_executable`에 `src/gfx/MeshGen.cpp`를 추가한다.

Run: `cmake --build "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/build" --config Release 2>&1 | tail -20`
Expected: 에러 0.

- [ ] **Step 5: 지오메트리 검증**

`VISUAL-CHECK` 수행. 이 태스크는 **모든 형상**을 건드리므로 아래를 특히 본다:

1. 행성이 구로 보이고 이음매(경도 0도 선)에 틈이 없다
2. 토성 고리가 안쪽·바깥쪽 반지름 그대로 그려진다
3. 궤도선을 켜면 타원이 매끄럽다
4. 소행성이 바위 모양이고, 멀어질 때 LOD가 바뀌어도 구로 튀지 않는다
5. `[lodmix]`의 삼각형 수가 리팩터 전과 같다

- [ ] **Step 6: 커밋**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add src/gfx/MeshGen.hpp src/gfx/MeshGen.cpp src/app/SolarSystemApp.hpp CMakeLists.txt
git commit -m "Turn mesh generation into pure functions"
```

---

## Task 7: 텍스처 로딩과 Vulkan 자원 생성을 .cpp로 내린다

여기서부터 `SolarSystemApp.hpp`는 **선언만 남기기 시작한다.** 이번 태스크의 함수들을 헤더에서는 선언으로 바꾸고, 정의를 새 .cpp로 옮긴다.

**Files:**
- Create: `src/app/App_Textures.cpp`
- Create: `src/app/App_Resources.cpp`
- Modify: `src/app/SolarSystemApp.hpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `class SolarSystemApp` (Task 1), `GpuProfiler` (Task 3)
- Produces: 없음 (같은 클래스의 멤버 함수 정의를 옮길 뿐, 인터페이스는 바뀌지 않는다)

- [ ] **Step 1: `App_Textures.cpp`로 옮긴다**

새 파일 머리:

```cpp
#include "SolarSystemApp.hpp"
```

아래 함수들의 정의를 헤더에서 잘라 이 파일에 붙이고, 이름 앞에 `SolarSystemApp::`을 붙인다. 헤더에는 선언만 남긴다(본문 `{...}`을 `;`로 대체).

| 함수 | 원래 위치 |
|---|---|
| `createColorTexture` | `main.cpp:2660` |
| `loadDDS` | `main.cpp:2676` |
| `ddsPathFor` | `main.cpp:2757` (`static` — 헤더 선언에만 `static`을 남기고 정의에는 붙이지 않는다) |
| `loadTexture` | `main.cpp:2762` |
| `loadGrayIcon` | `main.cpp:2804` |
| `loadCubeDDS` | `main.cpp:2958` |
| `createCubeTextureImage` | `main.cpp:3038` |

기본 인자(`= VK_IMAGE_VIEW_TYPE_2D` 등)가 있다면 **헤더의 선언에만** 적고 정의에는 적지 않는다. 양쪽에 적으면 컴파일 에러가 난다.

- [ ] **Step 2: 빌드**

Run: `cmake --build "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/build" --config Release 2>&1 | tail -20`

`CMakeLists.txt`의 `add_executable`에 `src/app/App_Textures.cpp`를 먼저 추가해야 한다.
Expected: 에러 0.

- [ ] **Step 3: `App_Resources.cpp`로 옮긴다**

같은 방식으로 아래를 옮긴다.

| 함수 | 원래 위치 |
|---|---|
| `destroyOffscreenAndBlurResources` | `main.cpp:582` |
| `recreateSwapChain` | `main.cpp:597` |
| `recreateOffscreenAndBlur` | `main.cpp:618` |
| `recreateOffscreenForMsaa` | `main.cpp:626` |
| `pickMsaaSamples` | `main.cpp:654` |
| `clampMsaaLevel` | `main.cpp:668` |
| `createComputeResources` | `main.cpp:1178` |
| `computeRenderExtent` | `main.cpp:1306` |
| `createOffscreenImages` | `main.cpp:1313` |
| `createOffscreenResources` | `main.cpp:1343` |
| `createBlurImages` | `main.cpp:1375` |
| `createBlurResources` | `main.cpp:1421` |
| `updateBlurDescriptorSets` | `main.cpp:1451` |
| `createBlurPipeline` | `main.cpp:1470` |
| `updatePostDescriptorSets` | `main.cpp:1533` |
| `createPostProcessPipeline` | `main.cpp:1543` |
| `createDescriptorSetLayout` | `main.cpp:2872` |
| `createDescriptorPool` | `main.cpp:2891` |
| `createGraphicsPipeline` | `main.cpp:3182` |
| `createLockedOrbitBuffer` | `main.cpp:3271` |
| `createVertexBuffer` | `main.cpp:3279` |
| `createIndexBuffer` | `main.cpp:3288` |
| `createUniformBuffer` | `main.cpp:3297` |
| `createTextureSampler` | `main.cpp:3298` |

`CMakeLists.txt`에 `src/app/App_Resources.cpp`를 추가한다.

- [ ] **Step 4: 빌드**

Run: `cmake --build "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/build" --config Release 2>&1 | tail -20`
Expected: 에러 0.

- [ ] **Step 5: 자원 재생성 검증**

`VISUAL-CHECK` 수행. 이 태스크는 자원 생성·파괴 전부를 옮기므로 **재생성 경로**를 특히 본다:

1. 설정창에서 해상도를 1280×720 → 3840×2160 → 1920×1080 으로 연달아 바꿔도 죽지 않는다
2. Anti-aliasing을 8× → 2× → 8× 로 바꿔도 죽지 않는다
3. 렌더 스케일을 0.5 → 2.0 으로 바꿔도 죽지 않는다
4. 전체화면 ↔ 창 모드를 세 번 왕복해도 죽지 않는다
5. 종료할 때 검증 레이어 경고나 크래시가 없다 (자원 누수·이중 해제 확인)

- [ ] **Step 6: 커밋**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add src/app/App_Textures.cpp src/app/App_Resources.cpp src/app/SolarSystemApp.hpp CMakeLists.txt
git commit -m "Move texture loading and Vulkan resource creation into their own files"
```

---

## Task 8: 초기화와 입력을 .cpp로 내린다

**Files:**
- Create: `src/app/App_Init.cpp`, `src/app/App_Input.cpp`
- Modify: `src/app/SolarSystemApp.hpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `class SolarSystemApp` (Task 1)
- Produces: 없음

**참고:** 스펙 문서는 스왑체인 재생성을 `App_Input.cpp`에 둔다고 적었지만, Task 7에서 `App_Resources.cpp`로 갔다. 자원의 생성·파괴는 한곳에 모으는 편이 낫고, `App_Input.cpp`는 재생성을 **요청**하는 플래그만 세운다. 이 편차는 의도된 것이다.

- [ ] **Step 1: `App_Init.cpp`로 옮긴다**

파일 머리는 `#include "SolarSystemApp.hpp"`.

| 함수 | 원래 위치 |
|---|---|
| `initApp` | `main.cpp:809` (203줄, 태양계 전체 정의가 여기 있다) |
| `createPlanet` | `main.cpp:2841` |
| `cleanupApp` | `main.cpp:2613` |

- [ ] **Step 2: `App_Input.cpp`로 옮긴다**

| 함수 | 원래 위치 |
|---|---|
| `onFrameStart` | `main.cpp:380` |
| `throttleFrame` | `main.cpp:453` |
| `onMouseButton` | `main.cpp:463` |
| `onMouseMove` | `main.cpp:482` |
| `onMouseScroll` | `main.cpp:488` |
| `handleRaycast` | `main.cpp:494` |

`override` 키워드는 **헤더의 선언에만** 남기고 정의에는 붙이지 않는다.

- [ ] **Step 3: CMake에 추가하고 빌드**

`CMakeLists.txt`의 `add_executable`에 `src/app/App_Init.cpp`와 `src/app/App_Input.cpp`를 추가한다.

Run: `cmake --build "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/build" --config Release 2>&1 | tail -20`
Expected: 에러 0.

- [ ] **Step 4: 입력 검증**

`VISUAL-CHECK` 수행. 추가로:

1. 좌드래그로 카메라가 궤도 회전한다
2. 스크롤로 줌인/줌아웃이 되고, 최소 거리에서 천체 안으로 뚫고 들어가지 않는다
3. 행성을 **한 번** 클릭하면 선택되고, **두 번** 클릭하면 카메라가 그리로 이동한다
4. 스페이스바로 시뮬레이션이 멈추고 다시 흐른다
5. 프레임 제한을 60으로 걸면 FPS가 60 근처에 머무른다

- [ ] **Step 5: 커밋**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add src/app/App_Init.cpp src/app/App_Input.cpp src/app/SolarSystemApp.hpp CMakeLists.txt
git commit -m "Move startup and input handling into their own files"
```

---

## Task 9: UI를 `updateUniformBuffer`에서 떼어낸다

이번 리팩터의 핵심이다. `updateUniformBuffer` 532줄 가운데 뒤쪽 260줄이 전부 ImGui다 — 기어 버튼, EXIT 버튼, FPS 표시, GPU 오버레이, 로비 메뉴, 천체 정보 패널이 "유니폼 버퍼를 갱신한다"는 이름의 함수 안에 들어 있다.

**Files:**
- Create: `src/app/App_Ui.cpp`
- Modify: `src/app/SolarSystemApp.hpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `class SolarSystemApp` (Task 1), `getCelestialInfo` (Task 4), `GpuProfiler::NAMES` (Task 3)
- Produces: `void SolarSystemApp::drawUi(float deltaTime, float easeScale);`

- [ ] **Step 1: UI 구간의 경계를 찾는다**

```bash
grep -n "ImGui_ImplVulkan_NewFrame\|ImGui::Render()" "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/src/app/SolarSystemApp.hpp"
```

원래 `main.cpp` 기준으로 UI 구간은 1906행(`ImGui_ImplVulkan_NewFrame` 직전 배너 주석)부터 `updateUniformBuffer`의 끝까지다. `ImGui::Render()`가 그 구간의 마지막이어야 한다 — 아니라면 뒤에 남은 코드가 UI가 아니므로 옮기지 않는다.

- [ ] **Step 2: 헤더에 선언을 추가한다**

`src/app/SolarSystemApp.hpp`의 private 섹션에 추가한다.

```cpp
    // 이번 프레임의 ImGui를 전부 구성한다. updateUniformBuffer의 뒤쪽 260줄이었다.
    // deltaTime은 FPS 표시가, easeScale은 정보 패널의 현재 반지름 표시가 쓴다.
    void drawUi(float deltaTime, float easeScale);
```

- [ ] **Step 3: `App_Ui.cpp`를 만들고 옮긴다**

```cpp
#include "SolarSystemApp.hpp"
```

`updateUniformBuffer`의 UI 구간을 잘라 `void SolarSystemApp::drawUi(float deltaTime, float easeScale) { ... }` 본문으로 붙인다. 코드는 한 줄도 바꾸지 않는다.

이어서 아래 함수들도 이 파일로 옮긴다(헤더에는 선언만 남긴다).

| 함수 | 원래 위치 |
|---|---|
| `worldToScreen` | `main.cpp:1246` |
| `TextCentered` | `main.cpp:1256` |
| `initImGui` | `main.cpp:1263` |
| `helpTip` | `main.cpp:2165` (`static` — 헤더 선언에만 `static`을 남긴다) |
| `drawSettingsWindow` | `main.cpp:2170` |
| `applyLiveSettings` | `main.cpp:2316` |

- [ ] **Step 4: `updateUniformBuffer`에서 호출로 대체한다**

잘라낸 자리에 한 줄을 넣는다.

```cpp
        drawUi(deltaTime, easeScale);
```

- [ ] **Step 5: 빌드**

`CMakeLists.txt`의 `add_executable`에 `src/app/App_Ui.cpp`를 추가한다.

Run: `cmake --build "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem/build" --config Release 2>&1 | tail -20`

컴파일러가 "선언되지 않은 식별자"를 신고하면, 그 이름이 UI 구간이 쓰던 지역 변수다. `drawUi`의 인자로 추가한다. `deltaTime`과 `easeScale` 외에 더 필요할 수 있다.
Expected: 최종적으로 에러 0.

- [ ] **Step 6: UI 전수 검증**

`VISUAL-CHECK` 수행. 추가로 화면 요소를 하나씩 본다:

1. 로비: 제목 `E P H E M E R I S`, 부제, 시작 버튼, EXIT 버튼
2. 시뮬레이션: 좌상단 오버레이(현재 대상·시간), 우측 정보 패널
3. 기어 버튼 → 설정창의 모든 항목이 뜨고 각 항목에 마우스를 올리면 툴팁이 나온다
4. `Show FPS` 켜면 FPS가 뜬다
5. `Show GPU Times` 켜면 좌하단에 구간별 ms가 뜬다
6. EXIT 버튼으로 정상 종료된다

- [ ] **Step 7: 커밋**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add src/app/App_Ui.cpp src/app/SolarSystemApp.hpp CMakeLists.txt
git commit -m "Lift the UI out of updateUniformBuffer"
```

---

## Task 10: `updateUniformBuffer`를 단계로 쪼갠다

**이 계획에서 가장 위험한 태스크다.** 지역 변수가 단계 사이를 넘나들고, 함수 지역 `static` 변수가 10개 있다. 한 번에 한 단계씩 뽑고, **매번 빌드한다.**

**Files:**
- Create: `src/app/App_Frame.cpp`
- Modify: `src/app/SolarSystemApp.hpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `class SolarSystemApp` (Task 1), `drawUi` (Task 9)
- Produces:
  ```cpp
  float advanceSimulationTime();          // 이번 프레임의 deltaTime을 돌려준다
  void updateBodyPositions(float slowTime);
  void updateCameraTracking(float deltaTime, float easeScale);
  void applyEclipseCinematic(float deltaTime, float easeScale);
  void collectOccluders(UniformBufferObject &ubo, float easeScale);
  void uploadUbo(UniformBufferObject &ubo, float time);
  ```

- [ ] **Step 1: 함수 지역 `static`을 멤버로 올린다**

이것을 먼저 해야 한다. 함수가 나뉘면 `static` 지역 변수는 각 함수에 갇혀 서로 못 본다.

`updateUniformBuffer`의 아래 10개(원래 `main.cpp` 기준 줄)를 삭제하고, 같은 초기값으로 `SolarSystemApp.hpp`의 private 멤버로 옮긴다.

| 원래 줄 | 선언 | 멤버로 |
|---|---|---|
| 1634 | `static auto lastTimePoint = std::chrono::high_resolution_clock::now();` | `std::chrono::time_point<std::chrono::high_resolution_clock> lastTimePoint = std::chrono::high_resolution_clock::now();` |
| 1639 | `static bool spacePressedLastFrame = false;` | `bool spacePressedLastFrame = false;` |
| 1646 | `static float simulationTime = 0.0f;` | `float simulationTime = 0.0f;` |
| 1647 | `static bool isSimInit = false;` | `bool isSimInit = false;` |
| 1731 | `static bool isFirstFrame = true;` | `bool isFirstFrame = true;` |
| 1741 | `static float lastTargetRadius = targetRadius;` | `float lastTargetRadius = 1.0f;` |
| 1742 | `static int lastTargetType = lockedTargetType;` | `int lastTargetType = 1;` |
| 1743 | `static int lastTargetIndex = lockedPlanetIndex;` | `int lastTargetIndex = 2;` |
| 1780 | `static float appliedFov = -1.0f;` | `float appliedFov = -1.0f;` |
| 1792 | `static float eclipseLerp = 0.0f;` | `float eclipseLerp = 0.0f;` |

**초기값에 주의한다.** `lastTargetRadius`/`lastTargetType`/`lastTargetIndex`는 원래 다른 변수로 초기화됐다. 멤버 초기화는 생성 시점에 일어나므로 그 값을 쓸 수 없다. 위 표의 값은 각각 `targetRadius`의 초기값 `1.0f`, `lockedTargetType`의 초기값 `1`, `lockedPlanetIndex`의 초기값 `2`와 같다 — 첫 프레임의 동작이 동일해진다.

빌드하고 `VISUAL-CHECK`를 수행한다. **여기서 한 번 커밋한다.**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add src/app/SolarSystemApp.hpp
git commit -m "Promote the frame function's static locals to members"
```

- [ ] **Step 2: `App_Frame.cpp`를 만들고 `updateUniformBuffer`를 통째로 옮긴다**

```cpp
#include "SolarSystemApp.hpp"
```

`updateUniformBuffer`, `buildBodyModelMatrices`(`main.cpp:1591`), `updateLockedOrbitLine`(`main.cpp:3233`)의 정의를 옮긴다. 헤더에는 선언만 남긴다.

`CMakeLists.txt`에 `src/app/App_Frame.cpp`를 추가하고 빌드한다.

- [ ] **Step 3: `advanceSimulationTime`을 뽑는다**

원래 `main.cpp:1634-1667` 구간(시간 계산, 일시정지, `scaleLerp`, `starsLerp`, `simulationTime` 전진)이다.

```cpp
// 시간을 한 프레임 전진시킨다. deltaTime을 돌려주는 이유는 아래 단계들과 UI가
// 같은 값을 써야 하기 때문이다.
float SolarSystemApp::advanceSimulationTime() {
    // (원래 코드 그대로)
}
```

`updateUniformBuffer` 첫머리를 `float deltaTime = advanceSimulationTime();`으로 바꾼다. `easeScale`, `slowTime`, `time`은 이 함수가 멤버(`scaleLerp`, `simulationTime`, `currentAppTime`)에 쓴 값에서 호출부가 다시 계산한다 — 원래 식을 그대로 남긴다.

빌드 + `VISUAL-CHECK`.

- [ ] **Step 4: `updateBodyPositions`를 뽑는다**

원래 `main.cpp:1669-1712` 구간(행성·달의 위치를 double로 계산). 모델 행렬은 만들지 않는다 — 그건 `buildBodyModelMatrices`의 일이고 반드시 일식 블록 뒤여야 한다.

```cpp
void SolarSystemApp::updateBodyPositions(float slowTime);
```

빌드 + `VISUAL-CHECK`.

- [ ] **Step 5: `updateCameraTracking`을 뽑는다**

원래 `main.cpp:1713-1789` 구간(잠금 대상 추적, 최소 거리, 로비 자동 선회, FOV 반영).

```cpp
void SolarSystemApp::updateCameraTracking(float deltaTime, float easeScale);
```

빌드 + `VISUAL-CHECK`. 특히 **다른 행성을 더블클릭했을 때 화면이 튀지 않는지** 본다 — `targetChanged` 판정이 이 구간에 있다.

- [ ] **Step 6: `applyEclipseCinematic`을 뽑는다**

원래 `main.cpp:1790-1838` 구간.

```cpp
void SolarSystemApp::applyEclipseCinematic(float deltaTime, float easeScale);
```

**호출 위치를 바꾸지 않는다.** 이 블록은 `camera.target`을 덮어쓰므로, 반드시 `buildBodyModelMatrices` **앞**이어야 한다. 앞뒤가 바뀌면 모델은 일식 전 타겟을, 뷰 행렬은 일식 후 타겟을 보게 되어 카메라 상대 좌표계에서 천체가 통째로 어긋난다.

빌드 + `VISUAL-CHECK`. 일식 버튼을 눌러 달이 태양을 정확히 가리는지 본다.

- [ ] **Step 7: `collectOccluders`를 뽑는다**

원래 `main.cpp:1848-1900` 구간(그림자 계 결정 + 가림 천체 수집 + 간접 버퍼 스냅샷).

```cpp
void SolarSystemApp::collectOccluders(UniformBufferObject &ubo, float easeScale);
```

빌드 + `VISUAL-CHECK`. 목성을 잠그고 갈릴레이 위성의 그림자가 목성 표면에 지는지, 지구를 잠그고 달 그림자가 보이는지 확인한다.

- [ ] **Step 8: `uploadUbo`를 뽑는다**

남은 부분(뷰·투영 행렬 작성과 `memcpy(uniformBufferMapped, ...)`).

```cpp
void SolarSystemApp::uploadUbo(UniformBufferObject &ubo, float time);
```

이 시점에서 `updateUniformBuffer`는 아래처럼 짧아져야 한다.

```cpp
void SolarSystemApp::updateUniformBuffer() {
    float deltaTime = advanceSimulationTime();
    float easeScale = scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp);
    float slowTime  = simulationTime;
    float time      = currentAppTime;

    updateBodyPositions(slowTime);
    updateCameraTracking(deltaTime, easeScale);
    applyEclipseCinematic(deltaTime, easeScale);
    // 반드시 일식 블록 뒤여야 한다. 저 블록이 camera.target을 한 번 더 덮어쓰기 때문에,
    // 여기보다 위에서 모델 행렬을 만들면 모델은 일식 전 타겟을, 뷰 행렬은 일식 후 타겟을
    // 보게 되어 카메라 상대 좌표계에서 천체가 통째로 어긋난다.
    buildBodyModelMatrices(easeScale, slowTime, time);

    UniformBufferObject ubo{};
    collectOccluders(ubo, easeScale);
    uploadUbo(ubo, time);

    drawUi(deltaTime, easeScale);
}
```

빌드 + `VISUAL-CHECK` 전 항목.

- [ ] **Step 9: 프로파일 비교**

`scripts/run_dev.bat`으로 실행해 `[profile]` 줄의 `cpuFrame`과 구간별 GPU ms를 Task 9 커밋 시점의 값과 비교한다. 10% 이상 차이가 나면 단계 순서가 바뀐 것이므로 되돌린다.

- [ ] **Step 10: 커밋**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add src/app/App_Frame.cpp src/app/SolarSystemApp.hpp CMakeLists.txt
git commit -m "Break the frame update into named stages"
```

---

## Task 11: `recordCommandBuffer`를 패스별로 쪼갠다

경계는 이미 `tsMark` 슬롯이 그어 놓았다. 그 경계를 그대로 쓴다.

**Files:**
- Create: `src/app/App_Record.cpp`
- Modify: `src/app/SolarSystemApp.hpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `class SolarSystemApp` (Task 1), `GpuProfiler::mark` (Task 3)
- Produces:
  ```cpp
  void recordCompute(VkCommandBuffer cb, float easeScale, float astRot, VkExtent2D renderExtent);
  void recordAsteroids(VkCommandBuffer cb, float easeScale, float beltScale, float astRot, VkExtent2D renderExtent);
  void recordBodies(VkCommandBuffer cb);
  void recordSky(VkCommandBuffer cb, float easeScale);
  void recordAtmosphere(VkCommandBuffer cb);
  void recordOrbits(VkCommandBuffer cb);
  void recordSun(VkCommandBuffer cb, float easeScale);
  void recordBloom(VkCommandBuffer cb, VkExtent2D renderExtent);
  void recordPost(VkCommandBuffer cb, uint32_t imageIndex);
  ```

  인자 목록은 각 구간이 실제로 참조하는 지역 변수를 세어 정한 것이다. 구간 경계를
  한 줄이라도 다르게 잡으면 필요한 인자가 달라지므로, 컴파일러가 "선언되지 않은
  식별자"를 신고하면 그것을 인자로 더한다.

- [ ] **Step 1: `App_Record.cpp`를 만들고 통째로 옮긴다**

```cpp
#include "SolarSystemApp.hpp"
```

`recordCommandBuffer`(`main.cpp:2336-2611`)의 정의를 옮긴다. 헤더에는 선언만 남긴다(`override`는 헤더에만).

`setViewport`(`main.cpp:2322`)와 `setFullViewport`(`main.cpp:2328`)는 짧고 자주 불리므로 **헤더에 인라인으로 남긴다.**

`CMakeLists.txt`에 `src/app/App_Record.cpp`를 추가하고 빌드한다.

- [ ] **Step 2: 한 패스씩 뽑는다**

아래 순서로, **하나 뽑을 때마다 빌드한다.** `tsMark` 호출은 `recordCommandBuffer`에 남기고, 그 사이 코드만 뽑는다 — 그래야 계측 경계가 그대로 유지된다.

| 새 함수 | 뽑을 구간 (원래 `main.cpp` 줄) |
|---|---|
| `recordCompute` | 2342-2381 (`tsMark(cb,0)` 뒤 ~ `tsMark(cb,1)` 앞) |
| `recordAsteroids` | 2383-2402 |
| `recordBodies` | 2404-2443 |
| `recordSky` | 2445-2461 |
| `recordAtmosphere` | 2463-2489 |
| `recordOrbits` | 2491-2540 |
| `recordSun` | 2542-2559 |
| `recordBloom` | 2561-2596 |
| `recordPost` | 2598-2606 |

`vkCmdBeginRenderPass`/`vkCmdEndRenderPass` 쌍이 한 함수 안에서 열고 닫히지 않는 구간이 있다(오프스크린 패스는 `recordAsteroids` 앞에서 열려 `recordSun` 뒤에 닫힌다). **패스를 여닫는 호출은 `recordCommandBuffer`에 남긴다.**

빌드가 "선언되지 않은 식별자"를 신고하면 그것이 구간을 넘나드는 지역 변수다. `easeScale`, `beltScale`, `astRot`, `renderExtent` 등이 해당한다 — 인자로 추가한다.

- [ ] **Step 3: 최종 형태 확인**

`recordCommandBuffer`는 아래 골격이어야 한다.

```cpp
void SolarSystemApp::recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex) {
    double recordStart = glfwGetTime();
    // (커맨드 버퍼 begin, easeScale/beltScale/astRot 계산)
    profiler.resetPool(cb);
    profiler.mark(cb, 0);
    recordCompute(cb, easeScale, astRot, renderExtent);
    profiler.mark(cb, 1);
    vkCmdBeginRenderPass(cb, &offscreenPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    recordAsteroids(cb, easeScale, beltScale, astRot, renderExtent);
    profiler.mark(cb, 2);
    recordBodies(cb);
    profiler.mark(cb, 3);
    recordSky(cb, easeScale);
    profiler.mark(cb, 4);
    recordAtmosphere(cb);
    profiler.mark(cb, 5);
    recordOrbits(cb);
    profiler.mark(cb, 6);
    recordSun(cb, easeScale);
    vkCmdEndRenderPass(cb);
    profiler.mark(cb, 7);
    recordBloom(cb, renderExtent);
    profiler.mark(cb, 8);
    recordPost(cb, imageIndex);
    profiler.mark(cb, 9);
    profiler.mark(cb, 10);
    // (커맨드 버퍼 end)
    profiler.addRecordMs((glfwGetTime() - recordStart) * 1000.0);
}
```

- [ ] **Step 4: 그리기 순서 검증**

`VISUAL-CHECK` 수행. 그리기 순서가 어긋나면 아래에서 드러난다:

1. 대기 껍질이 배경 뒤로 사라지지 않는다 — 지구를 잠그고 가장자리 발광이 보이는지 확인 (대기는 스카이박스 **뒤**에 그려져야 한다)
2. 궤도선이 천체에 가려진다
3. 태양의 홍염이 블룸으로 번진다
4. 소행성 위에서만 대기가 빛나 보이는 증상이 **없다** (이건 예전에 실제로 있었던 버그의 징후다)

- [ ] **Step 5: 프로파일 비교**

`[profile]` 줄의 구간별 GPU ms를 Task 10 커밋 시점과 비교한다. `tsMark` 위치를 옮기지 않았으므로 각 구간의 값이 그대로여야 한다. 특정 구간이 0이 되었다면 그 패스의 코드가 다른 함수로 잘못 들어간 것이다.

- [ ] **Step 6: 커밋**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add src/app/App_Record.cpp src/app/SolarSystemApp.hpp CMakeLists.txt
git commit -m "Break command recording into one function per pass"
```

---

## 마무리 확인

전체 태스크가 끝나면 아래를 확인한다.

- [ ] `wc -l src/main.cpp src/app/*.cpp src/app/*.hpp src/core/*.cpp src/sim/*.cpp src/gfx/*.cpp` — `main.cpp`가 40줄 안팎이고, 어느 파일도 700줄을 넘지 않는다
- [ ] `git log --oneline modularize-main ^main` — 태스크마다 커밋이 하나씩 있다
- [ ] 깨끗한 클론에서 빌드된다:
  ```bash
  cd /tmp && rm -rf ephemeris-check && \
  git clone --depth 1 --branch modularize-main "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem" ephemeris-check && \
  cmake -S ephemeris-check -B ephemeris-check/build && \
  cmake --build ephemeris-check/build --config Release
  ```
  (텍스처가 없어 실행은 안 되지만, 컴파일과 링크가 되는지는 확인된다)
