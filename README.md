# Solar System: Immersive, high-performance solar system simulation

![C++](https://img.shields.io/badge/Language-C++17-blue)
![Vulkan](https://img.shields.io/badge/API-Vulkan-green)
![C](https://img.shields.io/badge/Language-C-yellow)
![License](https://img.shields.io/badge/License-MIT-lightgrey)

> "High-performance Solar System simulation powered by Vulkan API."

![AstroScale Demo](assets/demo.gif)

---

## 🚀 About
SolarSystem은 C++와 Vulkan API를 활용해 밑바닥부터 직접 설계한 **고성능 태양계 시뮬레이션 엔진**입니다. 

단순히 상용 게임 엔진을 사용하는 것에서 벗어나, 디바이스 관리부터 스왑체인, 렌더 패스에 이르기까지 그래픽스 파이프라인의 핵심 인프라를 직접 구현하며 현대적인 렌더링 과정을 깊이 있게 탐구하고자 시작한 프로젝트입니다. 
과학적 데이터의 정확성과 고해상도 렌더링 사이의 균형을 맞추며, 효율적인 리소스 관리와 GPU 가속 컴퓨팅을 실험하는 플랫폼으로 발전시키고 있습니다.

## ✨ Key Features
* **Engine Architecture:** Vulkan 코어 인프라(Instance, Device, SwapChain, RenderPass)를 직접 상속 구조로 설계.
* **Modern Graphics:** ImGui를 활용한 실시간 디버깅 UI 및 HDR 환경 구축.
* **Data-Driven:** 고효율 리소스 로딩(Texture/Shader) 및 버퍼 관리 시스템.
* **Scientific Simulation:** 천체 물리 법칙을 기반으로 한 교육용 시뮬레이션 환경 제공.

## 🛠️ Tech Stack
이 프로젝트는 최신 C++ 오픈소스 라이브러리와 함께 빌드됩니다.
* **Vulkan API:** 차세대 그래픽스 API
* **GLFW:** 윈도우 생성 및 입력 제어
* **GLM:** 수학 연산 및 행렬 변환
* **Dear ImGui:** 고성능 즉시 모드(Immediate Mode) GUI

## 📦 Build Instructions
CMake와 FetchContent를 사용하여 빌드 환경을 간편하게 구성할 수 있습니다.

    # 1. 저장소 복제
    git clone https://github.com/Youl-AI/SolarSystem.git
    cd SolarSystem

    # 2. 빌드 디렉토리 생성
    mkdir build
    cd build

    # 3. CMake 설정 및 빌드
    cmake ..
    cmake --build . --config Release

> **Note:** 빌드 후 `VulkanApp.exe`와 같은 경로에 `textures/` 및 `shaders/` 폴더가 있어야 정상적으로 실행됩니다.

## 📂 Folder Structure
    .
    ├── src/            # 메인 소스 코드 및 Vulkan 베이스 클래스
    ├── shaders/        # GLSL 셰이더 소스 (vert, frag)
    ├── textures/       # 고해상도 행성 및 우주 텍스처
    └── CMakeLists.txt  # FetchContent를 사용한 의존성 관리

## 💡 Author's Note
이 프로젝트는 유지보수 가능한 엔진 구조를 설계하는 데 중점을 두었습니다. 추상화된 베이스 클래스(`VulkanBase`)를 통해 확장성을 고려했으며, Vulkan의 복잡한 파이프라인 관리 방식을 실무 수준에서 익히는 계기가 되었습니다.

## 📜 License & Credits

**소스 코드**는 [MIT License](LICENSE)를 따릅니다.

**텍스처 형식:** 색상 텍스처는 BC7로 미리 압축한 `.dds`로 보관합니다.
GPU에서 4×4 픽셀 블록을 16바이트로 저장해 VRAM을 정확히 1/4로 줄이며, 측정상 손실은
원본 JPEG보다 작습니다(달 8K 기준 BC7 44.3 dB 대 JPEG q92 40.4 dB). 노말맵은 제외했습니다 —
법선은 블록당 대표색 2개를 잇는 직선으로 근사되지 않아 오차가 커집니다(수성 0.78°, 달 2.26°).
아래 표의 출처가 배포하는 원본은 JPEG/PNG이며, 이 저장소에는 변환본만 담습니다.

**텍스처 자산은 MIT가 아니며, 각 출처의 라이선스를 따릅니다:**

| 자산 | 출처 | 라이선스 |
|---|---|---|
| 행성 텍스처 (`textures/planets/`) | [Solar System Scope](https://www.solarsystemscope.com/textures/) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| 해왕성 (`4k_neptune.jpg`) | [CelestiaContent](https://github.com/CelestiaProject/CelestiaContent) | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |
| 소행성 텍스처 (`textures/asteroids/`) | [3D Asteroid Catalogue](https://3d-asteroids.space/) | 해당 사이트 이용약관 참조 |
| 달 텍스처 (`8k_moon.jpg`) | [Solar System Scope](https://www.solarsystemscope.com/textures/) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| 갈릴레이 위성·타이탄 (`4k_*.jpg`, `io_glow.png`) | [CelestiaContent](https://github.com/CelestiaProject/CelestiaContent) — 아래 상세 | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) / [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| 실측 지형 노말맵 (`*_normal.png`) | NASA / USGS 탐사선 고도 데이터 — 아래 상세 | 퍼블릭 도메인 |

### 위성 텍스처 상세 (CC BY — 저작자 표시 필수)

[CelestiaContent](https://github.com/CelestiaProject/CelestiaContent)의 4096×2048 맵을 이 엔진의 알베도 규약에 맞게 밝기만 조정해 사용했습니다.
`io_glow.png`는 Celestia `io.png`의 알파 채널(화산 열점 마스크)을 분리한 것입니다.

| 텍스처 | 저작자 | 라이선스 |
|---|---|---|
| 이오, 이오 열점 | ItzImcool, AstroChara / NASA·JPL-Caltech·ASI·USGS, SwRI·MSSS·Gerald Eichstädt·Jason Perry·John Rogers | CC BY 4.0 |
| 가니메데 | Askaniy Anpilogov / NASA·JPL-Caltech·ASI·USGS, Björn Jónsson, SwRI·MSSS·Brian Swift | CC BY 3.0 |
| 타이탄 | Askaniy Anpilogov, Pedro Garcia, AstroChara / Martonchik & Orton (1994), Karkoschka et al. (2016), Seignovert et al. (2019), NASA·JPL-Caltech·ASI·USGS | CC BY 3.0 |
| 유로파, 칼리스토 | CelestiaContent 프로젝트 / NASA·JPL-Caltech·USGS | CC BY 3.0 |

해왕성은 [Irwin et al. (2024)](https://www.ox.ac.uk/news/2024-01-05-new-images-reveal-what-neptune-and-uranus-really-look-0)이 정정한 실제 색을 따릅니다.
흔히 알려진 진한 파란색은 보이저 2호가 구름 띠를 강조하려 채도를 올린 결과이고, 실제 해왕성은 천왕성과 비슷한 옅은 녹청색입니다.

### 노말맵 상세

미국 정부 저작물(퍼블릭 도메인)인 탐사선 실측 고도 자료(DEM)에서 직접 구웠습니다.

| 천체 | 원본 자료 |
|---|---|
| 지구 | NOAA ETOPO 2022 전구 지형 30초 (바다는 해수면 높이로 평탄화) |
| 달 | LRO LOLA 전구 DEM 118 m |
| 수성 | MESSENGER 전구 DEM 665 m |
| 화성 | MGS MOLA 전구 DEM |
| 금성 | Magellan 전구 지형도 4641 m |

이 저장소를 포크하거나 재배포하실 경우, 위 출처 표기를 함께 유지해 주세요.
