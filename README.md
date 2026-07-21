# Solar System

![C++](https://img.shields.io/badge/Language-C%2B%2B17-blue)
![Vulkan](https://img.shields.io/badge/API-Vulkan%201.2-green)
![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey)
![License](https://img.shields.io/badge/Code-MIT-lightgrey)

Vulkan API로 밑바닥부터 만든 태양계 시뮬레이터입니다. 상용 엔진을 쓰지 않고
디바이스 관리, 스왑체인, 렌더 패스까지 직접 구현했습니다.

![Demo](assets/demo.gif)

목표는 **보기 좋은 그림이 아니라 사실에 맞는 그림**입니다. 지형은 탐사선이 실제로
측정한 고도 자료에서 굽고, 별하늘은 Gaia가 관측한 실제 별 위치를 씁니다. 눈에
그럴듯해 보이는 값보다 측정으로 뒷받침되는 값을 택합니다.

---

## 요구 사양

### 하드웨어

| | 최소 | 권장 |
|---|---|---|
| GPU VRAM | **3 GB** | **6 GB 이상** |
| GPU | Vulkan 1.2 지원 | — |
| 저장 공간 | **약 4 GB** | — |
| RAM | 8 GB | 16 GB |

저장 공간은 클론에 1.7 GB(작업 트리 0.9 GB + git 히스토리 0.8 GB), Release에서 받는
텍스처에 0.85 GB, 빌드 산출물과 의존성 소스에 1 GB 남짓입니다. 텍스처가 이미 압축된
`.dds`라 git이 더 줄이지 못합니다. 히스토리가 필요 없다면 `git clone --depth 1`로
더 아낄 수 있습니다.

VRAM이 이 정도 필요한 이유는 텍스처가 **1.6 GiB**이기 때문입니다. 지구 디퓨즈가
16384×8192, 목성이 14400×7200이고 대부분 BC 압축되어 있는데도 그렇습니다.

나머지는 렌더 타겟이 차지하며, **설정에 따라 크게 달라집니다**:

| 렌더 해상도 | MSAA 8× | MSAA 2× |
|---|---|---|
| 1280×720 | 141 MiB | 35 MiB |
| 1920×1080 | 316 MiB | 79 MiB |
| 3200×1800 (1600×900 × 렌더 스케일 2) | **879 MiB** | 220 MiB |

VRAM이 빠듯하면 **Anti-aliasing을 먼저 낮추세요.** 이 프로젝트에서는 MSAA가
프레임 시간을 거의 안 먹지만 렌더 타겟은 샘플 수에 정비례해 커집니다.

**GPU가 반드시 지원해야 하는 것:**

- `textureCompressionBC` — BC7(색상), BC5(노말맵), BC6H(HDR 하늘). 2012년 이후
  데스크톱 GPU라면 전부 됩니다.
- `maxImageDimension2D ≥ 16384` — 지구 디퓨즈가 16384×8192입니다. 오래된 내장
  그래픽은 8192에서 막히기도 합니다.
- `samplerAnisotropy`

### 소프트웨어

| | |
|---|---|
| OS | **Windows** (`windows.h`를 씁니다. 리눅스는 미검증) |
| 컴파일러 | C++17 (MSVC로 검증) |
| CMake | 3.15 이상 |
| **Vulkan SDK** | **필수** — 셰이더 컴파일에 `glslc`가 필요합니다 |

**Vulkan SDK는 실행이 아니라 빌드에 필요합니다.** [LunarG](https://vulkan.lunarg.com/sdk/home)에서
받아 설치하면 `VULKAN_SDK` 환경변수가 자동으로 잡힙니다.

---

## 빌드

사전에 준비할 것은 **Vulkan SDK 설치 하나**입니다. GLFW, GLM, Dear ImGui는 CMake가
알아서 내려받습니다.

```bash
git clone https://github.com/Youl-AI/SolarSystem.git
cd SolarSystem

# 1. 대용량 텍스처 받기 (한 번만)
powershell -ExecutionPolicy Bypass -File scripts\fetch_textures.ps1

# 2. 빌드
cmake -S . -B build
cmake --build build --config Release
```

셰이더도 빌드가 함께 컴파일합니다(`glslc` → `shaders/*.spv`). 따로 할 일은 없습니다.

> **1단계를 건너뛰면 실행할 때 텍스처를 못 찾습니다.**
> 텍스처 대부분은 저장소 안에 있지만, **100 MiB를 넘는 여섯 개**는 GitHub이 거부해서
> [Release](https://github.com/Youl-AI/SolarSystem/releases/tag/textures-v1)에 첨부해
> 배포합니다(지구 디퓨즈·구름·야간, 목성, 하늘 두 층 — 합계 약 845 MB).
> 스크립트는 이미 받은 파일은 건너뛰므로 중간에 끊겼으면 다시 실행하면 됩니다.

### 실행

```bash
build\Release\VulkanApp.exe
```

> **저장소 루트에서 실행해야 합니다.** 실행 파일이 `textures/`와 `shaders/`를 상대
> 경로로 찾습니다. 다른 곳에 두려면 두 폴더를 실행 파일 옆에 함께 복사하세요.

첫 실행에서 텍스처 1.6 GiB를 올리므로 로딩이 좀 걸립니다.

---

## 조작

| | |
|---|---|
| 마우스 드래그 | 시점 회전 |
| 마우스 휠 | 확대·축소 (더 못 다가가면 시야각이 2°까지 좁아집니다) |
| 천체 클릭 | 선택 — 정보 패널 표시 |
| 천체 더블클릭 | 그 천체로 카메라 고정 |
| `Space` | 시간 정지 / 재개 |
| 우상단 톱니바퀴 | 설정 |

설정 항목은 마우스를 올리면 무슨 일을 하는지 설명이 뜹니다.

### 눈여겨볼 설정 두 가지

**Real Scale** — 천체를 실제 거리와 크기(1 AU = 500 유닛)로 배치합니다. 태양계는
대부분 비어 있어서 행성이 점이 됩니다. 그 허전함이 사실입니다.

**Real Stars** — 하늘을 **맨눈으로 보이는 대로** 바꿉니다. 전천 약 9,100개(6.5등급),
희미한 회색 은하수, 그리고 반짝임 없음. 별의 반짝임은 대기를 통과할 때 생기는
현상이라 진공에서는 별이 미동도 하지 않습니다. 끄면 장노출 사진처럼 별 수백만 개에
밝고 화려한 은하수가 됩니다. **Real Scale과는 독립입니다** — 압축된 태양계를 보면서도
진짜 하늘을 볼 수 있습니다.

---

## 저장소 구조

```
src/          엔진과 시뮬레이션 (VulkanBase.hpp = Vulkan 인프라, main.cpp = 나머지 전부)
shaders/      GLSL 원본. 빌드가 .spv로 컴파일한다
textures/     구워 둔 .dds 자산 (1.5 GB)
scripts/      텍스처 굽는 도구와 스크립트, 개발자 모드 실행 배치
assets/       README용 이미지
```

---

## 개발자용

### 성능 계측

```bash
scripts\run_dev.bat
```

`SOLAR_PROFILE=1`을 주고 실행합니다. 설정 창에 **Show GPU Times**가 나타나고, 좌하단에
패스별 GPU 시간이 뜹니다. 소행성이 어느 LOD에 몇 개 들어갔는지, 렌더 타겟이 몇 MiB인지도
함께 나옵니다. 일반 실행에서는 항목 자체가 없고 타임스탬프도 찍지 않습니다.

> **%가 아니라 ms를 보세요.** 한 패스의 비중은 그 패스가 느려져서가 아니라 다른 패스가
> 싸져서 오를 수 있습니다. 실제로 축소해서 소행성을 화면에 가득 채우면 비중이 24%까지
> 오르지만 절대 시간은 0.69 ms로 낮습니다 — 태양 하나가 그 세 배를 씁니다.

### 텍스처 다시 굽기

`scripts/`에 [DirectXTex](https://github.com/microsoft/DirectXTex)의 `texconv.exe`와
`texassemble.exe`가 들어 있습니다(MIT). **런타임에는 필요 없고**, 원본에서 `.dds`를
다시 만들 때만 씁니다. `texassemble`로 낱장을 큐브맵으로 묶고 `texconv`로 압축합니다.

색상 텍스처는 `scripts/make_bc7.sh`로 굽습니다. **sRGB 텍스처에는 반드시 `-srgbi`를
주어야 합니다.** texconv는 출력이 `_SRGB` 포맷이면 입력을 선형으로 가정해 sRGB로 한 번 더
인코딩하는데, JPEG/PNG 바이트는 이미 sRGB라 이중 인코딩이 됩니다(원본 128이 파일에
188로 저장 → GPU가 다시 디코드해 중간톤이 2.3배 밝아짐).

검증할 때는 **디코더로 값을 비교하면 안 됩니다.** 디코드 경로가 같은 변환을 역으로 걸어
차이를 지워버립니다. 같은 이미지를 `BC7_UNORM`으로도 굽고 **압축된 블록 바이트를 직접**
비교해야 합니다.

노말맵은 BC5로 굽고 `-dx10`을 함께 줍니다. 없으면 texconv가 구식 FourCC(`BC5U`) 헤더로
저장해 데이터 시작 위치가 148이 아니라 128이 되고, 로더가 파일을 거릅니다.

---

## 텍스처 형식

색상 텍스처는 BC7로 미리 압축한 `.dds`로 보관합니다. GPU에서 4×4 픽셀 블록을 16바이트로
저장해 VRAM을 정확히 1/4로 줄입니다. 노말맵은 BC5로 굽습니다 — 두 채널만 담고 z는
셰이더가 `sqrt(1-x²-y²)`로 복원하는데, 맵을 단위 벡터로 구웠으므로 정확합니다.

달만 무압축으로 남겼습니다. 블록 압축 오차가 크레이터 테두리에 몰려(평탄부 0.70°
대 테두리 2.61°) 가장 눈에 띄는 곳을 뭉개기 때문입니다.

아래 표의 출처가 배포하는 원본은 JPEG/PNG이며, 이 저장소에는 변환본만 담습니다.

---

## 라이선스와 출처

**소스 코드**는 [MIT License](LICENSE)를 따릅니다.

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
| 텍스처 도구 (`scripts/texconv.exe`, `texassemble.exe`) | [DirectXTex](https://github.com/microsoft/DirectXTex) (Microsoft) | MIT |

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

기본 하늘에서는 두 층에 같은 배율을 곱합니다. `hiptyc + milkyway = starmap`이
성립하므로(포화되지 않은 화소에서 최대 오차 0.000488 = half 반올림 한계) 통합본을
쓰는 것과 결과가 같습니다. Real Stars를 켜면 각 층에 다른 곡선을 걸어 맨눈으로 본
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
