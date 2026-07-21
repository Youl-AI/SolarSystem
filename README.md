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
GPU에서 4×4 픽셀 블록을 16바이트로 저장해 VRAM을 정확히 1/4로 줄입니다. 노말맵은
BC5로 굽습니다 — 두 채널만 담고 z는 셰이더가 sqrt(1-x²-y²)로 복원하는데, 맵을 단위
벡터로 구웠으므로 정확합니다. 달만 무압축으로 남겼습니다. 블록 압축 오차가 크레이터
테두리에 몰려(평탄부 0.70° 대 테두리 2.61°) 가장 눈에 띄는 곳을 뭉개기 때문입니다.
아래 표의 출처가 배포하는 원본은 JPEG/PNG이며, 이 저장소에는 변환본만 담습니다.

굽는 명령에는 반드시 `-srgbi`를 붙입니다. texconv는 출력이 `_SRGB` 포맷이면 입력을
선형으로 가정해 sRGB로 한 번 더 인코딩하는데, JPEG/PNG 바이트는 이미 sRGB라
이중 인코딩이 됩니다(원본 128이 파일에 188로 저장 → GPU가 다시 디코드해 중간톤이
2.3배 밝아짐). 검증은 **디코더로 값을 비교하지 말고** 압축된 블록 바이트를 직접
비교해야 합니다. texconv의 색 관리는 소스 종류와 출력 포맷에 따라 달라져서,
디코드 경로가 같은 변환을 역으로 걸어 차이를 지워버립니다.

**텍스처 자산은 MIT가 아니며, 각 출처의 라이선스를 따릅니다:**

| 자산 | 출처 | 라이선스 |
|---|---|---|
| 별하늘 (`skybox_stars.dds`, `skybox_band.dds`) | NASA SVS [Deep Star Maps 2020](https://svs.gsfc.nasa.gov/4851/) — Gaia DR2 + Hipparcos-2 + Tycho-2, 별 17억 개 | 퍼블릭 도메인 (아래 크레딧 필수) |
| 행성 텍스처 (`textures/planets/`) | [Solar System Scope](https://www.solarsystemscope.com/textures/) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| 목성 (`jupiter.dds`) | Askaniy Anpilogov / Björn Jónsson — Cassini(2000) + Juno 극지방, 14400×7200 | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |
| 지구 (`earth_*.dds`, `earth_normal.dds`) | NASA Blue Marble NG 2004-08, Black Marble 2016, Blue Marble 구름(2001), NOAA ETOPO 2022 | 퍼블릭 도메인 |
| 해왕성 (`4k_neptune.jpg`) | [CelestiaContent](https://github.com/CelestiaProject/CelestiaContent) | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |
| 소행성 텍스처 (`textures/asteroids/`) | [3D Asteroid Catalogue](https://3d-asteroids.space/) | 해당 사이트 이용약관 참조 |
| 달 텍스처 (`8k_moon.jpg`) | [Solar System Scope](https://www.solarsystemscope.com/textures/) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| 갈릴레이 위성·타이탄 (`4k_*.jpg`, `io_glow.png`) | [CelestiaContent](https://github.com/CelestiaProject/CelestiaContent) — 아래 상세 | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) / [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| 실측 지형 노말맵 (`*_normal.dds`, `moon_normal.png`) | NASA / USGS / NOAA 탐사선 고도 데이터 — 아래 상세 | 퍼블릭 도메인 |

### 별하늘 상세

배경은 NASA SVS의 [Deep Star Maps 2020](https://svs.gsfc.nasa.gov/4851/)에서 굽습니다.
Gaia DR2·Hipparcos-2·Tycho-2의 실측 별 17억 개로 만든 자료입니다.

NASA가 요구하는 크레딧입니다:

> NASA/Goddard Space Flight Center Scientific Visualization Studio.
> Gaia DR2: ESA/Gaia/DPAC.

Gaia DR2 부분은 ESA의 [별도 인용 조건](https://gea.esac.esa.int/archive/documentation/GDR2/Miscellaneous/sec_credit_and_citation_instructions/)을 따릅니다.

**두 층으로 나눠 담습니다** — `skybox_stars`는 밝은 별(`hiptyc`), `skybox_band`는
은하수 확산광과 어두운 별(`milkyway`)입니다. 하나로 합치면 안 되는 이유가 있습니다.
별 개수를 실제 하늘(전천 9,100개, 대기 없는 우주의 맨눈 한계 6.5등급)에 맞추는
원시값 문턱은 0.261인데, 은하수 띠는 0.044로 텍셀당 6배 어둡습니다. 그래서 하나의
톤 곡선으로는 별 개수를 맞추는 순간 은하수가 사라집니다. 실제로는 둘 다 보이는데,
점광원과 확산광은 눈의 검출 문턱이 다르기 때문입니다 — 띠는 넓어서 눈이 공간적으로
적분해 인지합니다. 픽셀 하나만 보는 함수로는 표현할 수 없는 차이라 층을 가릅니다.

기본 축척에서는 두 층에 같은 배율을 곱합니다. `hiptyc + milkyway = starmap`이
성립하므로(포화되지 않은 화소에서 최대 오차 0.000488 = half 반올림 한계) 통합본을
쓰는 것과 결과가 같습니다. 실제 축척에서는 각 층에 다른 곡선을 걸어 맨눈으로 본
하늘에 맞춥니다.

정거원통도법 16384×8192를 큐브맵 4096² 6면으로 변환합니다(원본의 각해상도
0.02197°/px와 정확히 일치하는 크기). 보간은 **란초시4**를 씁니다 — 별은 원본에서도
1~2픽셀인 점광원이라, 격자와 어긋난 위치로 리샘플하면 봉우리가 번지며 어두워집니다.
밝은 별 2000개로 잰 봉우리 보존율이 쌍선형 53.3%, 바이큐빅 70.8%, 란초시4 74.6%였습니다.
란초시는 음의 곁잎이 있어 밝은 별 둘레에 음수가 남으므로 0에서 자릅니다.

압축은 **BC6H_UF16**(HDR 전용 블록 압축)입니다. 이게 없으면 해상도 업그레이드가
성립하지 않습니다 — 4096²×6면을 fp16 무압축으로 올리면 층당 805 MiB인데, BC6H로는
밉맵까지 다 넣고 128 MiB입니다.

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
| 금성 | Magellan 전구 지형도 4641 m (고도계 발자국이 10×20 km라 1픽셀 평활화로 격자 잡음 제거) |

이 저장소를 포크하거나 재배포하실 경우, 위 출처 표기를 함께 유지해 주세요.
