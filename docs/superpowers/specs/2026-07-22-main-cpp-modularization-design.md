# main.cpp 모듈화 설계

## 배경

`src/main.cpp`가 3,341줄이고, 그 전부가 `SolarSystemApp : public VulkanBase` 클래스
하나다. 멤버 변수가 100개 가까이 되고 함수 68개가 그것을 공유한다.

덩치가 큰 함수들:

| 위치 | 함수 | 줄 |
|---|---|---|
| main.cpp:1633 | `updateUniformBuffer` | 532 |
| main.cpp:2336 | `recordCommandBuffer` | 277 |
| main.cpp:809 | `initApp` | 203 |
| main.cpp:2170 | `drawSettingsWindow` | 146 |
| main.cpp:288 | `getCelestialInfo` | 91 |

가장 큰 문제는 크기가 아니라 **이름과 내용의 불일치**다. `updateUniformBuffer`
532줄 가운데 뒤쪽 260줄이 전부 ImGui다 — 기어 버튼, EXIT 버튼, FPS 표시, GPU
오버레이, 로비 메뉴, 천체 정보 패널이 "유니폼 버퍼를 갱신한다"는 이름의 함수 안에
들어 있다.

## 목표

역할이 드러나는 파일 구조를 만들어, 어떤 기능을 고치려 할 때 어느 파일을 열어야
하는지가 자명해지도록 한다. **동작은 한 줄도 바뀌지 않는다.**

## 범위

`src/main.cpp`만 다룬다. `src/VulkanBase.hpp`(686줄, 헤더에 구현 포함)와
`shaders/shader.frag`(735줄, 타입 11개의 if-else 사슬)도 같은 문제를 갖고 있지만
별도 작업으로 남긴다. 한 번에 한 곳만 건드려야 회귀가 생겼을 때 원인을 가릴 수 있다.

## 제약

**테스트가 없다.** 검증 수단은 "빌드된다 + 화면이 전과 같아 보인다"뿐이다. 따라서
리팩터링의 깊이가 곧 위험도이고, 안전장치는 작업 단위를 작게 자르는 것뿐이다.

**작업 위치:** 메인 체크아웃 `C:\Users\hayoul1999.YOUL-HOUSE\Desktop\Github\SolarSystem`
의 새 브랜치. worktree를 쓰지 않는 이유는 텍스처가 작업 트리에 1.6 GB 있고 빌드
캐시에 절대 경로가 박혀 있어, 격리 이득보다 준비 비용이 크기 때문이다.

---

## 설계

분리를 두 종류로 나눈다. 기준은 **Vulkan 핸들과 앱 상태를 보는가**이다.

### A. 진짜 독립 타입

Vulkan도 `SolarSystemApp`도 보지 않는 것들. 별도 타입으로 뽑고 인자로만 소통한다.

| 새 파일 | 담는 것 | 의존성 |
|---|---|---|
| `src/sim/CelestialData.hpp/.cpp` | `getCelestialInfo` 정보 표 | 없음 (문자열만) |
| `src/sim/AsteroidBelt.hpp/.cpp` | `AsteroidData`, `generateAsteroidBelt` | glm |
| `src/gfx/MeshGen.hpp/.cpp` | `generateSphere/Ring/Orbit`, `buildSimplifiedLod`, `loadObjModel` | glm, `Vertex` |
| `src/core/Settings.hpp/.cpp` | `GraphicsSettings`, ini 로드·저장 | 없음 |
| `src/core/GpuProfiler.hpp/.cpp` | 쿼리 풀, `tsMark`, `collectProfile` | Vulkan 핸들만 |

현재 `generateSphere`, `generateRing`, `generateOrbit`은 멤버 `vertices`/`indices`에
직접 쓴다. 이것을 출력 벡터를 인자로 받게 바꾼다. 그러면 순수 함수가 되고, 이
프로젝트에서 단위 테스트를 붙일 수 있는 유일한 부분이 된다.

`GpuProfiler`는 쿼리 풀과 누산기를 소유하고, `VkDevice`와 `timestampPeriod`를
생성 시점에 받는다. `devTools` 게이트도 함께 옮긴다.

### B. 같은 클래스, 다른 파일

나머지는 Vulkan 핸들 100개를 공유한다. 소유권을 재배치하지 않고 파일만 나눈다.
클래스 선언은 한 헤더에 두고, 멤버 함수 정의를 여러 번역 단위에 나눠 담는다.

```
src/main.cpp                  진입점만 (~40줄)
src/app/Types.hpp             여러 파일이 함께 쓰는 구조체
src/app/SolarSystemApp.hpp    클래스 선언 + 멤버 전부 (구현 0줄)
src/app/App_Init.cpp          initApp, 태양계 구성, createPlanet, cleanupApp
src/app/App_Frame.cpp         시뮬레이션 갱신 (UI 제외)
src/app/App_Ui.cpp            ImGui 전부 + drawSettingsWindow + helpTip
src/app/App_Record.cpp        recordCommandBuffer
src/app/App_Resources.cpp     파이프라인·버퍼·디스크립터·오프스크린·블룸·포스트
src/app/App_Textures.cpp      DDS·큐브맵·더미 텍스처
src/app/App_Input.cpp         입력, 레이캐스트, 스왑체인 재생성
```

`SolarSystemApp.hpp`가 목차 역할을 한다. 함수 68개의 이름과 시그니처가 한곳에 모인다.

`PushConstants`, `UniformBufferObject`, `Planet` 등 여러 파일이 함께 쓰는 타입은
`src/app/Types.hpp`에 둔다.

### C. 거대 함수 쪼개기

경계는 새로 정하지 않는다. **이미 코드 안에 그어져 있다.**

`recordCommandBuffer`는 `tsMark(cb, 0)` ~ `tsMark(cb, 10)`이 구간을 나눠 놓았다.
그 경계를 그대로 함수로 만든다:

```
recordCompute, recordAsteroids, recordBodies, recordSky,
recordAtmosphere, recordOrbits, recordSun, recordBloom, recordPost
```

`updateUniformBuffer`는 주석 배너가 나눠 놓았다:

```
advanceSimulationTime, updateBodyPositions, updateCameraTracking,
applyEclipseCinematic, buildBodyModelMatrices(기존), collectOccluders, uploadUbo
```

그리고 뒤쪽 260줄의 ImGui는 `App_Ui.cpp`의 `drawUi()`로 나간다.

**순수 추출이다. 호출 순서를 바꾸지 않는다.** 특히 순서 의존이 주석으로 명시된
곳들이 있다 — 예컨대 모델 행렬 생성은 반드시 일식 블록 뒤여야 한다(일식 블록이
`camera.target`을 다시 덮어쓰기 때문에, 앞에서 만들면 모델은 일식 전 타겟을, 뷰
행렬은 일식 후 타겟을 보게 되어 카메라 상대 좌표계에서 천체가 통째로 어긋난다).
이런 순서는 그대로 두고 이름만 붙인다.

---

## 데이터 흐름

한 프레임의 흐름은 지금과 동일하며, 이름만 드러난다.

```
onFrameStart          App_Input.cpp    스왑체인/MSAA 재생성 요청 처리
updateUniformBuffer   App_Frame.cpp    아래를 순서대로 호출
  advanceSimulationTime                시간, scaleLerp, starsLerp 전진
  updateBodyPositions                  double로 위치 계산 (모델 행렬 아님)
  updateCameraTracking                 잠금 대상 추적, 최소 거리, FOV
  applyEclipseCinematic                camera.target을 덮어쓸 수 있음
  buildBodyModelMatrices               카메라 상대 모델 행렬 (반드시 위 뒤)
  collectOccluders                     해석적 그림자 대상 수집
  uploadUbo                            매핑된 유니폼 버퍼에 기록
  drawUi                 App_Ui.cpp    ImGui 프레임 구성
recordCommandBuffer   App_Record.cpp   패스별 record 함수 호출
collectProfile        GpuProfiler      직전 프레임 타임스탬프 읽기
```

## 오류 처리

바뀌지 않는다. 현재 방식(치명적 오류는 `std::runtime_error` → `main()`에서
MessageBox)을 그대로 유지한다. 파일이 나뉘어도 예외는 번역 단위를 넘어 전파된다.

## 검증

테스트가 없으므로 안전장치는 작업 단위를 작게 자르는 것뿐이다.

- **단계마다** 빌드 + 실행 + 육안 확인 후 커밋. 회귀가 생기면 `git bisect`가 한
  커밋 안으로 좁힌다.
- 순수 이동은 컴파일러가 대부분 잡는다. 실제로 위험한 것은 함수 추출 단계뿐이므로,
  거기서는 **한 함수씩** 추출하고 그때마다 빌드한다.
- 매 단계 육안 확인 항목:
  로비 → 진입 → 지구 잠금 → Real Scale 켜기 → Real Stars 켜기 → 일식 버튼 →
  소행성이 보이도록 축소 → 설정창 열기 → 창 크기 변경
- `SOLAR_PROFILE=1`로 실행해 GPU 오버레이의 구간별 ms가 리팩터 전과 같은지 본다.
  패스 분할이 실수로 순서를 바꿨다면 여기서 드러난다.

## 작업 순서

위험이 낮은 것부터 간다.

| | 작업 | 위험 |
|---|---|---|
| 1 | 클래스를 `SolarSystemApp.hpp`로 옮기고 `main.cpp`는 진입점만 남긴다 | 낮음 |
| 2 | 순수 타입 5개 추출 | 낮음 |
| 3 | 나머지를 6개 `.cpp`로 분배 + CMake 갱신 | 낮음 |
| 4 | UI를 `updateUniformBuffer`에서 `drawUi()`로 분리 | 중간 |
| 5 | `updateUniformBuffer` 단계 추출 | 높음 |
| 6 | `recordCommandBuffer` 패스 추출 | 중간 |

5번이 가장 위험하다. 지역 변수가 단계 사이를 넘나들기 때문에, 어떤 것은 멤버로
올리거나 인자로 넘겨야 한다. 옮기기 전에 각 지역 변수의 생존 범위를 먼저 확인한다.

## 하지 않는 것

- **`VulkanBase.hpp` 분리** — 범위 밖. 별도 작업.
- **`shader.frag` 분리** — 범위 밖. 셰이더는 화면으로만 검증되므로 위험이 크다.
- **`Renderer`/`Simulation` 같은 독립 클래스로의 재설계** — 멤버 100개의 소유권을
  전부 다시 정해야 하고, 검증 수단이 화면뿐이라 회귀 발견이 늦어진다. 파일 경계가
  자리를 잡은 뒤에 다시 판단한다.
- **동작 개선** — 리팩터 도중에 눈에 띄는 문제는 고치지 말고 기록만 한다. 동작
  변경이 섞이면 "화면이 같아 보인다"는 검증이 무의미해진다.
- **테스트 도입** — 이 프로젝트에 테스트 프레임워크가 없다. 순수 함수로 뽑히는
  `MeshGen`에는 나중에 붙일 수 있지만, 이번 작업에 묶지 않는다.
