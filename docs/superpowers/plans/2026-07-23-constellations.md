# 별자리(Constellation) 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 배경 하늘 위에 IAU 88개 별자리의 선과 라틴명을 실제 별에 정렬해 그리고, 토글·호버·성장 애니메이션으로 상호작용하게 한다.

**Architecture:** 순수 데이터 모듈 `sim/StarCatalog`가 우리 CSV 포맷을 읽어 별 방향·별자리 그래프를 제공한다. 렌더링은 새 objectType 12 + LINE_LIST 파이프라인으로 배경 하늘 패스에서 `skyMat` 회전을 적용해 그린다. 호버는 방향 내적으로 별을 찾고, BFS로 선이 자라나는 애니메이션을 CPU에서 동적 버퍼로 갱신한다.

**Tech Stack:** C++17, Vulkan 1.2, GLSL, GLM, Dear ImGui, CMake, MSVC

## Global Constraints

- **작업 위치:** `C:\Users\hayoul1999.YOUL-HOUSE\Desktop\Github\SolarSystem` (메인 체크아웃), 브랜치 `constellations`. `.claude/worktrees/`의 워크트리는 낡은 사본이므로 쓰지 않는다. 모든 명령·파일 접근에 **절대 경로**를 쓴다.
- **한 태스크 = 한 커밋.** 회귀 시 `git bisect`로 좁힌다.
- **한국어 주석은 UTF-8**(BOM 없이). CMake가 MSVC에 `/utf-8`을 준다. 화면에 나가는 문자열은 ASCII/라틴만(ImGui 기본 폰트에 한글 글리프 없음).
- **동작 변경 금지 (기존 기능):** 별자리는 순수 추가다. 기존 렌더 경로·셰이더 분기·프레임 로직의 동작을 바꾸지 않는다. 새 objectType 분기와 새 파이프라인만 더한다.
- **데이터는 저장소에 커밋.** 별자리 데이터는 작은 텍스트라 Release가 아니라 `data/constellations/`에 직접 넣는다. **출처·라이선스를 README에 표기한다.**
- **정렬은 측정으로 확정한다.** 별자리 선은 배경 큐브맵의 실제 밝은 별에 얹혀야 한다. 정렬 규약은 짐작하지 말고 알려진 밝은 별로 화면에서 검증한다(Task 3 게이트).

### 빌드 명령 (모든 태스크 공통)

셰이더는 CMake의 Shaders 타깃이 `glslc`로 굽는다. `cmake`는 PATH에 없다:

```
& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build "C:\Users\hayoul1999.YOUL-HOUSE\Desktop\Github\SolarSystem\build" --config Release
```

기대: 에러 0, `Ephemeris.exe` 생성. `.spv`가 바뀌면 이 빌드가 다시 굽는다(셰이더를 고친 태스크는 빌드가 곧 컴파일 확인).

### 실행 (육안 검증)

GUI 앱이라 서브에이전트는 실행하지 않는다(화면을 볼 수 없고 블록된다). 서브에이전트의 검증은 **빌드까지**. 육안 검증은 사용자가 `scripts\run_dev.bat`으로 수행한다.

---

## 자체 데이터 포맷 (외부 소스와 코드를 분리)

Task 1이 검증된 외부 소스를 아래 세 파일로 변환한다. 이후 모든 코드는 **이 포맷만** 읽으므로, 소스의 포맷이 무엇이든 코드 태스크는 영향받지 않는다. 위치: `data/constellations/`.

`stars.csv` — 별자리 선이 참조하는 별. 헤더 한 줄 + `id,raDeg,decDeg,mag`:
```
id,raDeg,decDeg,mag
32349,101.287,-16.716,-1.46
27989,88.793,7.407,0.42
```
- `id`: 원본 카탈로그 번호(HIP 등). 선 파일이 이 id로 별을 가리킨다.
- `raDeg`: 적경 0~360도. `decDeg`: 적위 -90~+90도. `mag`: 겉보기 등급.

`lines.csv` — 별자리 선(간선). 헤더 + `constellation,idA,idB`:
```
constellation,idA,idB
Ori,32349,27989
Ori,27989,26311
```
- `constellation`: 3글자 IAU 약자(예: `Ori`). `idA`,`idB`: `stars.csv`의 별 id 쌍.

`names.csv` — 약자 → 라틴 정식명. 헤더 + `constellation,latin`:
```
constellation,latin
Ori,Orion
UMa,Ursa Major
```

---

## File Structure

| 파일 | 책임 |
|---|---|
| `data/constellations/stars.csv` · `lines.csv` · `names.csv` | 별·선·이름 데이터 (신규) |
| `data/constellations/SOURCE.md` | 출처·라이선스·변환 방법 기록 (신규) |
| `src/sim/StarCatalog.hpp/.cpp` | CSV 로드, RA/Dec→방향, 별자리 그래프, 호버 피킹 (신규, Vulkan 무관) |
| `shaders/shader.vert`, `shader.frag` | objectType 12 분기 (배경 선, 정점색×밝기) |
| `src/app/SolarSystemApp.hpp` | StarCatalog 인스턴스, 파이프라인/버퍼 핸들, 토글·호버·애니 상태 |
| `src/app/Types.hpp` | `GraphicsSettings.constellations` (세션 전용) |
| `src/app/App_Resources.cpp` | `constellationPipeline`(LINE_LIST) 생성/파괴 |
| `src/app/App_Init.cpp` | 카탈로그 로드, 정적 선 버퍼 생성 |
| `src/app/App_Record.cpp` | 별자리 선 그리기 (하늘 패스) |
| `src/app/App_Frame.cpp` | 호버 피킹 + 성장 애니 + 페이드, 동적 버퍼 갱신 |
| `src/app/App_Ui.cpp` | 토글 체크박스 + 툴팁 + 이름 라벨 |
| `CMakeLists.txt` | `src/sim/StarCatalog.cpp` 추가 |
| `README.md` | 데이터 크레딧 |

---

## Task 1: 데이터 확보와 자체 포맷 변환

**Files:**
- Create: `data/constellations/stars.csv`, `data/constellations/lines.csv`, `data/constellations/names.csv`, `data/constellations/SOURCE.md`

**Interfaces:**
- Consumes: 없음
- Produces: 위 「자체 데이터 포맷」의 세 CSV 파일. 이후 모든 태스크가 이 파일만 읽는다.

**참고 — 소스 후보와 라이선스:** 별자리 선은 **허용(비카피레프트) 라이선스**를 골라야 프로젝트에 GPL 등이 전염되지 않는다. 1순위 후보는 **d3-celestial**(Olaf Frohn, BSD-2-Clause)의 `constellations.lines.json` + 별 데이터. Stellarium `constellationship.fab`는 정확하지만 GPL이라 프로젝트 라이선스와 충돌하면 피한다. 최종 선택과 라이선스는 실제 파일을 열어 확인하고 `SOURCE.md`에 적는다.

- [ ] **Step 1: 소스를 고르고 라이선스를 확인한다**

허용 라이선스의 별자리 선 데이터셋을 하나 고른다(1순위: d3-celestial, BSD-2). 라이선스 원문을 열어 재배포·수정 조건을 확인한다. 확인 결과를 `data/constellations/SOURCE.md`에 적는다: 소스 이름, URL, 라이선스, 다운로드한 원본 파일명, 크레딧 표기 문구.

- [ ] **Step 2: 세 CSV로 변환한다**

소스에서 88개 별자리 전부의 선(간선)과, 그 간선이 참조하는 별의 RA/Dec/등급, 그리고 약자→라틴명을 뽑아 `stars.csv`·`lines.csv`·`names.csv`로 저장한다(스크래치패드의 변환 스크립트를 써도 되고, 스크립트는 저장소에 넣지 않아도 된다). 규칙:
- `stars.csv`에는 `lines.csv`가 참조하는 별만 넣는다(중복 id는 한 번만).
- RA는 0~360도(시간 단위면 ×15), Dec는 -90~+90도로 통일.
- `names.csv`는 88개 약자를 라틴 정식명으로. ASCII만(예: `Scorpius`).
- 좌표 반올림은 소수 3자리면 충분하다.

- [ ] **Step 3: 정합성을 확인한다**

아래를 눈으로/간단한 명령으로 확인한다:
- `lines.csv`의 모든 `idA`/`idB`가 `stars.csv`에 존재한다(빠진 id 0개).
- `lines.csv`의 모든 `constellation` 약자가 `names.csv`에 있다.
- 별자리 약자 개수가 88개다.
- 오리온(`Ori`) 간선이 존재하고, 그 별에 시리우스가 아니라 오리온 별들(베텔게우스 HIP 27989, 리겔 HIP 24436 등)이 들어 있다.

- [ ] **Step 4: 커밋**

```bash
cd "C:/Users/hayoul1999.YOUL-HOUSE/Desktop/Github/SolarSystem"
git add data/constellations
git commit -m "Add constellation star and line data in a simple CSV format"
```

---

## Task 2: StarCatalog 모듈 — 로드·좌표변환·그래프

**Files:**
- Create: `src/sim/StarCatalog.hpp`, `src/sim/StarCatalog.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1의 CSV 파일
- Produces:
  ```cpp
  struct CatalogStar { int id; glm::vec3 dir; float magnitude; };
  struct Constellation {
      std::string abbr;                       // "Ori"
      std::string latin;                      // "Orion"
      std::vector<glm::ivec2> edges;          // stars_ 전역 인덱스 쌍
      std::unordered_map<int,std::vector<int>> adjacency; // 전역 stars_ 인덱스 -> 이웃 전역 인덱스들
      glm::vec3 centroidDir;                  // 이름 라벨용 평균 방향(정규화)
  };
  class StarCatalog {
  public:
      bool load(const std::string &dir);      // dir/stars.csv 등 세 파일
      const std::vector<CatalogStar>&   stars() const { return stars_; }
      const std::vector<Constellation>& constellations() const { return constellations_; }
      static glm::vec3 raDecToDir(double raDeg, double decDeg);
      // rayDir(단위)에 각도상 가장 가까운 별을 찾는다. maxAngleRad 안에서만.
      // 반환: 별자리 인덱스. 없으면 -1. outStarIndex = 그 별의 stars_ 인덱스.
      int pickNearest(const glm::vec3 &rayDir, float maxAngleRad, int &outStarIndex) const;
  };
  ```

- [ ] **Step 1: 헤더를 만든다**

`src/sim/StarCatalog.hpp` — 위 Produces의 구조체·클래스 선언. 파일 머리:
```cpp
#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
```
private 멤버: `std::vector<CatalogStar> stars_;`, `std::vector<Constellation> constellations_;`, 그리고 id→stars_ 인덱스 맵 `std::unordered_map<int,int> idToIndex_;`.

- [ ] **Step 2: `raDecToDir`를 구현한다 (초기 규약)**

`src/sim/StarCatalog.cpp`. 적도좌표를 직교 단위 벡터로 바꾼다. **초기 규약**을 아래로 두고, Task 3의 육안 게이트에서 축·부호를 조정한다.
```cpp
glm::vec3 StarCatalog::raDecToDir(double raDeg, double decDeg) {
    double ra  = glm::radians(raDeg);
    double dec = glm::radians(decDeg);
    // 초기 규약: 적경을 Y축 둘레 경도, 적위를 위도로 본다. Task 3에서 배경과 맞춰 확정한다.
    double x = std::cos(dec) * std::cos(ra);
    double y = std::sin(dec);
    double z = std::cos(dec) * std::sin(ra);
    return glm::normalize(glm::vec3((float)x, (float)y, (float)z));
}
```
`#include <cmath>` 추가. `M_PI`는 쓰지 않으므로 가드 불필요.

- [ ] **Step 3: `load`를 구현한다**

세 CSV를 파싱한다. 각 줄을 `,`로 나누고 헤더는 건너뛴다.
- `stars.csv` → `stars_`와 `idToIndex_`. dir = `raDecToDir(ra,dec)`.
- `lines.csv` → 약자별로 edges 수집. 각 id를 `idToIndex_`로 인덱스화(없으면 그 간선은 건너뛰고 계속).
- `names.csv` → 약자→라틴 맵.
- 약자별로 `Constellation`을 만든다: edges(전역 stars_ 인덱스 쌍), adjacency(각 간선의 양끝 전역 인덱스를 서로의 이웃 목록에 추가 — `adjacency[e.x].push_back(e.y)`와 그 반대), centroidDir(그 별자리가 쓰는 별들의 dir 평균을 정규화), latin(맵에서, 없으면 약자 그대로).
파일 하나라도 못 열면 `return false`.

- [ ] **Step 4: `pickNearest`를 구현한다**

```cpp
int StarCatalog::pickNearest(const glm::vec3 &rayDir, float maxAngleRad, int &outStarIndex) const {
    outStarIndex = -1;
    float bestDot = std::cos(maxAngleRad); // 이보다 큰 내적 = 이 각도보다 가까움
    int bestConstellation = -1;
    for (size_t c = 0; c < constellations_.size(); ++c) {
        for (const auto &e : constellations_[c].edges) {
            for (int idx : {e.x, e.y}) {
                float d = glm::dot(rayDir, stars_[idx].dir);
                if (d > bestDot) { bestDot = d; outStarIndex = idx; bestConstellation = (int)c; }
            }
        }
    }
    return bestConstellation;
}
```
`rayDir`는 단위 벡터라고 가정한다. 호출부가 정규화해 넘긴다.

- [ ] **Step 5: 수치 확인**

`raDecToDir`가 정상 범위를 내는지 임시 `assert`로 확인한다(빌드에 남기지 않는다): 적위 +90도 → `y≈1`, 적경 0·적위 0 → `x≈1`. 확인 후 제거. 콘솔에 `[catalog] stars=<N> constellations=<M>`를 한 줄 찍어(로드 성공 확인용) 남긴다.

- [ ] **Step 6: CMake에 추가하고 빌드**

`CMakeLists.txt`의 `add_executable`에 `src/sim/StarCatalog.cpp`를 추가한다.
Run: 위 「빌드 명령」. Expected: 에러 0.

- [ ] **Step 7: 커밋**

```bash
git add src/sim/StarCatalog.hpp src/sim/StarCatalog.cpp CMakeLists.txt
git commit -m "Add StarCatalog: load CSV, RA/Dec to direction, constellation graph"
```

---

## Task 3: 최소 렌더 경로 + 정렬 게이트 (가장 큰 리스크)

오리온 하나만 배경에 그려, 실제 별에 얹히는지 사용자가 육안으로 확인한다. 어긋나면 `raDecToDir` 규약을 조정한다. **이 게이트를 통과해야 이후 태스크가 의미가 있다.**

**Files:**
- Modify: `shaders/shader.vert`, `shaders/shader.frag`, `src/app/App_Resources.cpp`, `src/app/App_Init.cpp`, `src/app/App_Record.cpp`, `src/app/SolarSystemApp.hpp`

**Interfaces:**
- Consumes: `StarCatalog`(Task 2)
- Produces: `VkPipeline constellationPipeline;`, 정적 선 버퍼 `VkBuffer constLineBuffer; VkDeviceMemory constLineMem; uint32_t constLineVertexCount;`, `StarCatalog starCatalog;` (모두 SolarSystemApp 멤버)

- [ ] **Step 1: 셰이더에 objectType 12를 추가한다**

`shaders/shader.vert`의 스카이박스 분기(`if (push.objectType == 3)`)를 objectType 12도 같은 배경 경로를 타도록 넓힌다. 12는 3과 동일한 `rotView`·`xyww`·`skyMat`(instanceModel) 변환을 쓴다:
```glsl
    if (push.objectType == 3 || push.objectType == 12) {
        mat4 rotView = mat4(mat3(ubo.view));
        vec4 pos = ubo.proj * rotView * instanceModel * vec4(inPosition, 1.0);
        gl_Position = pos.xyww;
        fragPos = vec3(0.0);
    } else {
```
`fragColor`(정점 color, location 0 in)는 기존대로 그대로 전달된다.

`shaders/shader.frag`에 궤도선(타입 6) 분기 옆에 타입 12를 추가한다. 정점색 × 밝기(customData)를 알파에 실어, 블룸으로 넘어가지 않게 `writeOut`을 쓰지 않고 직접 출력한다:
```glsl
    else if (fragObjectType == 12) { // 별자리 선
        outColor = vec4(fragColor, push.customData);
        outBrightColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
```
(`push.customData`가 전체 알파. 정점색이 라벤더면 라벤더 선이 된다.)

- [ ] **Step 2: `constellationPipeline`을 만든다**

`src/app/App_Resources.cpp`의 `createGraphicsPipeline` 안, `linePipeline` 생성 직후에 LINE_LIST·깊이쓰기 끄기로 하나 더 만든다(같은 `pipelineInfo` 재사용):
```cpp
    // 별자리 선: 끊긴 간선 그래프라 LINE_LIST. 배경(먼 깊이)에 얹으므로 깊이 쓰기는 끈다
    // (행성이 이미 쓴 깊이로 LEQUAL 테스트만 하면 앞의 행성이 선을 가린다).
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    depthStencil.depthWriteEnable = VK_FALSE;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &constellationPipeline);
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // 뒤 파이프라인을 위해 원복
    depthStencil.depthWriteEnable = VK_TRUE;
```
`recreateOffscreenForMsaa`/정리 코드에서 `linePipeline`을 파괴하는 곳 옆에 `vkDestroyPipeline(device, constellationPipeline, nullptr);`를 추가한다(`grep -n "vkDestroyPipeline(device, linePipeline" src/app/App_Resources.cpp`로 위치 확인).

`SolarSystemApp.hpp`에 멤버 추가: `VkPipeline constellationPipeline;`.

- [ ] **Step 3: 정적 선 버퍼 빌더를 만든다**

`SolarSystemApp.hpp`에 멤버: `StarCatalog starCatalog;`, `VkBuffer constLineBuffer = VK_NULL_HANDLE; VkDeviceMemory constLineMem = VK_NULL_HANDLE; uint32_t constLineVertexCount = 0;`, 그리고 색 상수 `static constexpr glm::vec3 kConstColor = glm::vec3(0.82f, 0.80f, 0.95f);`.

`App_Init.cpp`에 헬퍼를 추가한다. 각 간선을 큰 원으로 `SEG` 등분해 LINE_LIST 정점(연속점마다 두 번 실어 선분 목록으로)으로 만든다. 배경 구 반지름은 1.0(방향 벡터 그대로. 스카이박스와 같은 단위구). 정점 color = `kConstColor`.
```cpp
static glm::vec3 slerpDir(const glm::vec3 &a, const glm::vec3 &b, float t) {
    float d = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
    float ang = std::acos(d);
    if (ang < 1e-4f) return glm::normalize(glm::mix(a, b, t));
    float s = std::sin(ang);
    return glm::normalize((std::sin((1.0f - t) * ang) / s) * a + (std::sin(t * ang) / s) * b);
}
// 간선 목록 -> LINE_LIST 정점. onlyAbbr가 비어있지 않으면 그 별자리만.
void SolarSystemApp::buildConstellationVertices(std::vector<Vertex> &out, const std::string &onlyAbbr) {
    const int SEG = 8; // 큰 원 분할 수
    out.clear();
    for (const auto &c : starCatalog.constellations()) {
        if (!onlyAbbr.empty() && c.abbr != onlyAbbr) continue;
        for (const auto &e : c.edges) {
            glm::vec3 a = starCatalog.stars()[e.x].dir;
            glm::vec3 b = starCatalog.stars()[e.y].dir;
            glm::vec3 prev = a;
            for (int s = 1; s <= SEG; ++s) {
                glm::vec3 cur = slerpDir(a, b, (float)s / SEG);
                Vertex v0{}; v0.pos = prev; v0.color = kConstColor;
                Vertex v1{}; v1.pos = cur;  v1.color = kConstColor;
                out.push_back(v0); out.push_back(v1); // LINE_LIST 한 선분
                prev = cur;
            }
        }
    }
}
```
헤더에 선언 추가: `void buildConstellationVertices(std::vector<Vertex> &out, const std::string &onlyAbbr = "");`. 그리고 **재사용 가능한** GPU 업로드 헬퍼를 만든다 — 정적 버퍼와 Task 6/7의 동적 버퍼가 같은 함수를 쓴다:
```cpp
// buf가 없으면 cap 용량으로 만들고, verts를 채운 뒤 count를 갱신한다.
// HOST_VISIBLE|HOST_COHERENT라 매핑해 memcpy만 하면 된다(동적 갱신에 재사용).
void SolarSystemApp::uploadLines(VkBuffer &buf, VkDeviceMemory &mem, void *&mapped,
                                 uint32_t &count, uint32_t cap, const std::vector<Vertex> &verts);
```
정적 선 버퍼 호출: `uploadLines(constLineBuffer, constLineMem, constLineMapped, constLineVertexCount, staticCap, cv);` (staticCap = 전체 88개 정점 수 이상). 멤버에 `void *constLineMapped = nullptr;`도 추가한다.

- [ ] **Step 4: 로드와 초기 버퍼 생성**

`App_Init.cpp`의 `initApp`에서 카탈로그를 로드하고(실패해도 앱은 계속 — 별자리만 비활성), **게이트용으로 오리온만** 정적 버퍼에 올린다:
```cpp
    if (starCatalog.load("data/constellations")) {
        std::vector<Vertex> cv; buildConstellationVertices(cv, "Ori");
        uploadConstLines(cv);
    }
```
(이 "Ori만"은 Task 4에서 전체로 바꾼다.)

- [ ] **Step 5: 하늘 패스에서 그린다**

`App_Record.cpp`의 `recordSky` 안, 스카이박스 draw 직후(같은 블록 안, `vkCmdDrawIndexed` 다음)에 별자리 선을 그린다. 스카이박스와 같은 `skyMat`를 모델로 넘긴다:
```cpp
    if (constLineVertexCount > 0) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, constellationPipeline);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &constLineBuffer, &off);
        PushConstants cp{skyMat, 12, 1.0f}; // customData=1.0 (완전 불투명, 게이트용)
        vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &cp);
        vkCmdDraw(cb, constLineVertexCount, 1, 0, 0);
        // 뒤(대기 등)가 기존 버텍스 버퍼를 기대하면 복구
        vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets);
    }
```
`vertexBuffers`/`offsets`가 이 함수에 없으면 지역 배열로 선언한다(recordOrbits 패턴과 동일). `skyMat`은 recordSky가 이미 만든다.

- [ ] **Step 6: 빌드**

Run: 위 「빌드 명령」. Expected: 에러 0.

- [ ] **Step 7: 정렬 게이트 (사용자 육안)**

`scripts\run_dev.bat`으로 실행한다. 오리온 자리(삼태성·베텔게우스·리겔)가 화면에 나오도록 카메라를 돌려, **라벤더 선이 배경의 실제 오리온 밝은 별들 위에 정확히 얹히는지** 본다.
- 얹히면 게이트 통과.
- 어긋나면 `raDecToDir`의 축·부호를 조정한다(적경 방향 반전 `sin(ra)→-sin(ra)`, 또는 y·z 축 교환, 또는 상수 오프셋 회전). 조정 → 빌드 → 재확인을 얹힐 때까지 반복한다. 배경 하늘의 좌표 규약은 이 측정으로만 알 수 있다.

- [ ] **Step 8: 커밋**

```bash
git add shaders/shader.vert shaders/shader.frag src/app/App_Resources.cpp src/app/App_Init.cpp src/app/App_Record.cpp src/app/SolarSystemApp.hpp
git commit -m "Render constellation lines on the sky; verify Orion aligns to the real stars"
```

---

## Task 4: 전체 88개 + 토글 + 페이드

**Files:**
- Modify: `src/app/Types.hpp`, `src/app/App_Init.cpp`, `src/app/App_Frame.cpp`, `src/app/App_Record.cpp`, `src/app/App_Ui.cpp`, `src/app/SolarSystemApp.hpp`

**Interfaces:**
- Consumes: Task 3의 렌더 경로
- Produces: `settings.constellations`(bool), `float constLerp`(0~1 페이드)

- [ ] **Step 1: 세팅 토글 추가 (세션 전용)**

`src/app/Types.hpp`의 `GraphicsSettings`에 `realStars` 옆에 추가: `bool constellations = false;`. **저장·복원하지 않는다** — `src/core/Settings.cpp`의 load/save는 손대지 않는다(realStars와 동일하게 세션 전용).

- [ ] **Step 2: 페이드 상태**

`SolarSystemApp.hpp`에 멤버: `float constLerp = 0.0f;`.
`App_Frame.cpp`의 `advanceSimulationTime`에 `starsLerp` 갱신 옆에 추가:
```cpp
    constLerp += (settings.constellations ? 1.0f : -1.0f) * deltaTime * 2.0f;
    constLerp = std::clamp(constLerp, 0.0f, 1.0f);
```
(2.0 = 약 0.5초 페이드.)

- [ ] **Step 3: 정적 버퍼를 전체로 바꾼다**

`App_Init.cpp`의 Task 3 Step 4 코드에서 `"Ori"`를 제거해 88개 전부 올린다:
```cpp
    if (starCatalog.load("data/constellations")) {
        std::vector<Vertex> cv; buildConstellationVertices(cv); // 전체
        uploadConstLines(cv);
    }
```

- [ ] **Step 4: 페이드 알파로 그린다**

`App_Record.cpp`의 recordSky에서 별자리 draw를 `constLerp > 0`일 때만, customData를 은은한 알파로:
```cpp
    if (constLineVertexCount > 0 && constLerp > 0.001f) {
        float easeC = constLerp * constLerp * (3.0f - 2.0f * constLerp);
        ...
        PushConstants cp{skyMat, 12, easeC * 0.5f}; // 0.5 = 켜둔 전체는 은은하게
        ...
    }
```
(`0.5`는 미관 값 — 사용자가 육안으로 조정.)

- [ ] **Step 5: 설정창 체크박스 + 툴팁**

`App_Ui.cpp`의 `drawSettingsWindow`에서 `Real Stars` 체크박스 옆에 추가한다(그 코드의 패턴을 그대로 따른다):
```cpp
    ImGui::Checkbox("Constellations", &settings.constellations);
    helpTip("Draw the 88 IAU constellation lines and names over the sky.\nHover a star to trace its constellation.");
```

- [ ] **Step 6: 빌드 + 육안**

Run: 빌드 명령. 실행 후: 설정창의 `Constellations`를 켜면 88개 선이 은은한 라벤더로 서서히 나타나고, 끄면 서서히 사라진다. 축척·Real Stars와 무관하게 동작한다. 하늘 회전을 따라 함께 돈다.

- [ ] **Step 7: 커밋**

```bash
git add src/app/Types.hpp src/app/App_Init.cpp src/app/App_Frame.cpp src/app/App_Record.cpp src/app/App_Ui.cpp src/app/SolarSystemApp.hpp
git commit -m "Show all 88 constellations behind a session-only toggle with a fade"
```

---

## Task 5: 이름 라벨

**Files:**
- Modify: `src/app/App_Ui.cpp`, `src/app/SolarSystemApp.hpp`

**Interfaces:**
- Consumes: `starCatalog`, `constLerp`, `skyMat`(재계산)
- Produces: 없음(화면 라벨)

- [ ] **Step 1: 라벨을 그린다**

`App_Ui.cpp`의 `drawUi` 안, ImGui 프레임 구성부에 별자리 이름 라벨을 추가한다. 각 별자리 `centroidDir`을 `skyMat`로 회전해 카메라 상대 방향으로 만들고, 화면에 투영해 카메라 앞쪽·화면 안일 때만 라틴명을 그린다. `skyMat`은 `currentAppTime`으로 recordSky와 같은 식으로 재계산한다:
```cpp
    if (constLerp > 0.001f) {
        glm::mat4 skyMat = glm::rotate(glm::mat4(1.0f), currentAppTime * glm::radians(0.5f), glm::vec3(0,1,0));
        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 rotView = glm::mat4(glm::mat3(view)); // 스카이박스와 동일: 회전만
        glm::mat4 proj = glm::perspective(glm::radians(camera.fov),
            swapChainExtent.width / (float)swapChainExtent.height, 0.01f, 1000000.0f);
        proj[1][1] *= -1;
        ImDrawList *dl = ImGui::GetForegroundDrawList();
        for (const auto &c : starCatalog.constellations()) {
            glm::vec4 clip = proj * rotView * skyMat * glm::vec4(c.centroidDir, 1.0f);
            if (clip.w <= 0.0f) continue;                 // 카메라 뒤
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1 || ndc.x > 1 || ndc.y < -1 || ndc.y > 1) continue; // 화면 밖
            float sx = (ndc.x * 0.5f + 0.5f) * swapChainExtent.width;
            float sy = (ndc.y * 0.5f + 0.5f) * swapChainExtent.height;
            ImU32 col = ImGui::GetColorU32(ImVec4(0.82f, 0.80f, 0.95f, constLerp * 0.8f));
            dl->AddText(ImVec2(sx, sy), col, c.latin.c_str());
        }
    }
```
(rotView·proj는 스카이박스 정점 경로와 같은 조합이라 선과 라벨이 일치한다. `camera.fov`·`getViewMatrix`가 실제 존재하는지 `grep`로 확인하고 이름을 맞춘다.)

- [ ] **Step 2: 빌드 + 육안**

Run: 빌드. 실행: 토글을 켜면 각 별자리 중심에 라틴명이 뜨고, 카메라를 돌리면 화면 안 별자리만 라벨이 보이며 선과 같은 위치를 따라간다.

- [ ] **Step 3: 커밋**

```bash
git add src/app/App_Ui.cpp src/app/SolarSystemApp.hpp
git commit -m "Label each constellation with its Latin name"
```

---

## Task 6: 호버 피킹 + 개별 표시 (토글 꺼짐일 때 탐색)

**Files:**
- Modify: `src/app/App_Frame.cpp`, `src/app/App_Input.cpp`, `src/app/App_Record.cpp`, `src/app/App_Ui.cpp`, `src/app/SolarSystemApp.hpp`

**Interfaces:**
- Consumes: `StarCatalog::pickNearest`, `buildConstellationVertices`, `uploadLines`(Task 3)
- Produces: `int hoverConstellation = -1; int hoverRootStar = -1;` (SolarSystemApp 멤버), `glm::vec3 mouseRayDir()` 헬퍼, 동적 호버 버퍼 `VkBuffer growBuffer; VkDeviceMemory growMem; void *growMapped; uint32_t growVertexCount;` (Task 7이 이 버퍼를 애니메이션한다)

- [ ] **Step 1: 마우스 광선 방향**

`SolarSystemApp.hpp`에 멤버 `int hoverConstellation = -1; int hoverRootStar = -1;` 추가. `App_Frame.cpp`(또는 App_Input.cpp)에 현재 마우스 위치에서 하늘 방향 벡터를 만드는 헬퍼를 추가한다. 스카이박스 정점 경로의 역이다: NDC → `inverse(proj)` → `inverse(rotView)` → `inverse(skyMat)` 방향. 마우스 픽셀은 GLFW로 얻는다(`glfwGetCursorPos`). 결과는 큐브맵 프레임의 단위 방향(= 카탈로그 dir과 같은 프레임).

- [ ] **Step 2: 매 프레임 피킹**

`App_Frame.cpp`의 프레임 갱신 어딘가(예: `updateCameraTracking` 뒤)에서, 로비가 아니고 마우스가 UI 위가 아닐 때 `pickNearest`로 호버를 갱신한다:
```cpp
    hoverConstellation = -1; hoverRootStar = -1;
    if (currentAppState == AppState::SIMULATION && !ImGui::GetIO().WantCaptureMouse) {
        glm::vec3 rd = mouseRayDir();
        int star;
        int c = starCatalog.pickNearest(rd, glm::radians(1.5f), star); // 1.5도 안
        if (c >= 0) { hoverConstellation = c; hoverRootStar = star; }
    }
```
(앞의 행성 피킹이 있으면 그쪽을 우선하도록, 기존 피킹이 대상을 잡았으면 호버를 건너뛴다 — 기존 raycast 결과를 확인해 조건을 맞춘다.)

- [ ] **Step 3: 호버한 별자리를 동적 버퍼에 담아 그린다 (성장 없이 전체)**

이 태스크에서는 **성장 없이** 호버한 별자리 전체를 즉시 표시한다(성장은 Task 7이 이 버퍼를 애니메이션한다). 동적 버퍼 `growBuffer`를 만들고(한 별자리 최대 정점 수 용량, `uploadLines` 재사용), 호버 대상이 **바뀔 때만** 다시 채운다:
```cpp
    if (hoverConstellation != prevHoverConstellation) {
        prevHoverConstellation = hoverConstellation;
        if (hoverConstellation >= 0) {
            std::vector<Vertex> hv;
            buildConstellationVertices(hv, starCatalog.constellations()[hoverConstellation].abbr);
            uploadLines(growBuffer, growMem, growMapped, growVertexCount, growCap, hv);
        } else {
            growVertexCount = 0;
        }
    }
```
(`prevHoverConstellation`, `growCap`은 멤버. `growCap` = 한 별자리 최대 정점 수 상한, 로드 시 계산.)

recordSky에서: 토글이 켜졌으면 정적 전체 버퍼(Task 4)를 그리고, `growVertexCount > 0`이면 `growBuffer`도 objectType 12로 그린다(호버 별자리를 겹쳐 강조 — 토글 꺼짐이면 이게 유일한 표시). 강조는 customData 알파를 전체(0.5)보다 높인다.

- [ ] **Step 4: 호버 라벨 강조**

`App_Ui.cpp`의 라벨 루프에서, 토글이 꺼져 있고 이 별자리가 호버 대상이면 그 라벨만 그린다(꺼짐 상태에서 전체 라벨은 숨김). 호버 라벨은 알파를 높여 강조한다.

- [ ] **Step 5: 빌드 + 육안**

Run: 빌드. 실행: 토글 끈 상태에서 배경 별에 마우스를 대면 그 별이 속한 별자리 선과 이름만 나타난다. 마우스를 떼면(다른 곳으로 옮기면) 사라진다. 앞에 행성이 있으면 행성 선택이 우선.

- [ ] **Step 6: 커밋**

```bash
git add src/app/App_Frame.cpp src/app/App_Input.cpp src/app/App_Record.cpp src/app/App_Ui.cpp src/app/SolarSystemApp.hpp
git commit -m "Reveal the hovered constellation when the toggle is off"
```

---

## Task 7: 성장 애니메이션 (BFS 물결) + 페이드 아웃

**Files:**
- Modify: `src/app/App_Frame.cpp`, `src/app/App_Record.cpp`, `src/app/SolarSystemApp.hpp`

**Interfaces:**
- Consumes: 호버 상태·`growBuffer`(Task 6), `Constellation::adjacency`
- Produces: 성장 상태 멤버 `float growTime; float growFadeOut; std::vector<float> edgeDelay;`

Task 6은 호버 별자리 전체를 `growBuffer`에 한 번 채웠다. 이 태스크는 그 채우기를 **매 프레임 진행도까지만** 채우도록 바꿔 자라나게 한다.

- [ ] **Step 1: 성장 상태와 BFS 지연 계산**

`SolarSystemApp.hpp`에 멤버 `float growTime = 0.0f; float growFadeOut = 0.0f; std::vector<float> edgeDelay;`. Task 6의 "호버 대상이 바뀔 때" 블록에서, 다시 채우는 대신 `growTime = 0; growFadeOut = 0;`로 리셋하고 `hoverRootStar`를 뿌리로 `adjacency`를 BFS해 각 별의 깊이를 구한 뒤, 호버 별자리의 각 **간선**에 지연을 매긴다(간선 두 끝 중 뿌리에서 더 얕은 쪽의 깊이 × `EDGE_DUR`). `edgeDelay`(간선 순서와 일치)에 캐시한다. `EDGE_DUR`은 간선당 0.25초 상수.

- [ ] **Step 2: 매 프레임 성장 버퍼 갱신**

`App_Frame.cpp`에서 `hoverConstellation >= 0`이면 `growTime += deltaTime;`. 매 프레임 호버 별자리의 간선을 큰 원 분할하되 **현재 진행도까지만** LINE_LIST 정점을 만들어 `growMapped`에 memcpy하고 `growVertexCount`를 갱신한다: 간선 `e`의 진행 `p = clamp((growTime - edgeDelay[e]) / EDGE_DUR, 0, 1)`. `p<=0`이면 건너뛰고, `0<p<1`이면 뿌리 쪽 끝(간선 두 끝 중 깊이가 얕은 쪽)에서 `p`까지, `p>=1`이면 전체 분할을 낸다. (한 별자리라 매 프레임 수십~수백 정점이라 부담 없음. `growBuffer`는 Task 6이 만든 것을 그대로 쓴다.)

- [ ] **Step 3: 페이드 아웃**

호버가 풀린 뒤에도 잠시 자란 형태를 보여주다 사라지게 한다. 별도 멤버 `int fadingConstellation = -1;`를 두어, 호버가 `-1`로 바뀌는 순간 직전 호버 별자리를 `fadingConstellation`에 넣고 `growFadeOut`을 0부터 `deltaTime * 3.0f`씩 올린다(자란 정점은 그대로 두고 알파만 줄인다). `growFadeOut >= 1`이면 `fadingConstellation = -1; growVertexCount = 0;`으로 그리기를 멈춘다. 새 호버가 들어오면 페이드를 취소하고(`fadingConstellation = -1; growFadeOut = 0;`) 새 성장을 시작한다.

- [ ] **Step 4: 성장 버퍼를 그린다 (강조 알파)**

recordSky의 Task 6 draw를 수정한다: `growVertexCount > 0`일 때 `growBuffer`를 objectType 12로 그리되, customData(알파)를 강조값 × (호버 중이면 1.0, 페이드 중이면 `1 - growFadeOut`)로 준다. 토글이 켜져 있으면 정적 전체(은은) 위에 이 버퍼가 겹쳐 그 별자리만 밝게 강조되고, 꺼져 있으면 이 버퍼가 유일한 표시다.

- [ ] **Step 5: 빌드 + 육안**

Run: 빌드. 실행: 배경 별에 마우스를 대면 그 별에서 선이 이웃으로 스르륵 뻗고, 물결처럼 별자리 전체로 번진다. 마우스를 떼면 자란 형태가 페이드 아웃된다. 토글을 켠 상태에서 호버하면 그 별자리만 더 밝게 강조된다.

- [ ] **Step 6: 커밋**

```bash
git add src/app/App_Frame.cpp src/app/App_Record.cpp src/app/SolarSystemApp.hpp
git commit -m "Grow the hovered constellation outward from the touched star"
```

---

## Task 8: 마무리 — 크레딧과 색 튜닝

**Files:**
- Modify: `README.md`, (선택) `shaders/shader.frag`, 색 상수

**Interfaces:**
- Consumes: 전체
- Produces: 없음

- [ ] **Step 1: README 크레딧**

`README.md`의 크레딧 표(기존 텍스처·아이콘 출처 옆)에 `data/constellations/SOURCE.md`의 소스·라이선스를 한 줄 추가한다. Gaia/NASA 별지도 크레딧과 같은 형식.

- [ ] **Step 2: 색·밝기·굵기 육안 튜닝 (사용자)**

사용자가 실행해 보며 조정하는 미관 값(측정 대상 아님):
- `kConstColor`(라벤더 색조), 켜둔 전체 알파(Task 4의 `0.5`), 호버 강조 알파, 페이드 속도.
- 필요하면 여기서 최종 값을 상수에 반영하고 커밋한다.

- [ ] **Step 3: 커밋**

```bash
git add README.md src/app/SolarSystemApp.hpp
git commit -m "Credit the constellation data source and settle the line colour"
```

---

## 마무리 확인

- [ ] 깨끗한 클론에서 빌드 + `data/constellations`가 저장소에 있어 바로 로드됨
- [ ] 토글 on/off 페이드, 88개 정렬, 라벨, 호버 성장/페이드아웃, 하늘 회전 추종 모두 육안 확인
- [ ] 기존 기능(축척·Real Stars·일식·궤도선·자원 재생성) 회귀 없음
- [ ] README 크레딧, SOURCE.md 라이선스 기록
