# AstroScale: Solar System Engine

![C++](https://img.shields.io/badge/Language-C++17-blue)
![Vulkan](https://img.shields.io/badge/API-Vulkan-green)
![C](https://img.shields.io/badge/Language-C-yellow)
![License](https://img.shields.io/badge/License-MIT-lightgrey)

> "High-performance Solar System simulation powered by Vulkan API."

![AstroScale Demo](assets/demo.mp4)

---

## 🚀 About
AstroScale은 C++와 Vulkan API를 사용하여 처음부터 끝까지 직접 설계한 **엔진 아키텍처 프로젝트**입니다. Vulkan의 인스턴스, 스왑체인, 커맨드 버퍼 관리 등 코어 인프라를 직접 구현하여 효율적인 GPU 리소스 관리를 실현했습니다[cite: 1]. 이 프로젝트는 단순히 우주를 시각화하는 것을 넘어, 그래픽스 엔진이 어떻게 데이터를 메모리에 올리고 GPU에 효율적으로 전달하는지에 대한 고민을 담고 있습니다[cite: 1].

## ✨ Key Features
* **Engine Architecture:** Vulkan 코어 인프라(Instance, Device, SwapChain, RenderPass)를 직접 상속 구조로 설계[cite: 1].
* **Modern Graphics:** ImGui를 활용한 실시간 디버깅 UI 및 HDR 환경 구축[cite: 2].
* **Data-Driven:** 고효율 리소스 로딩(Texture/Shader) 및 버퍼 관리 시스템[cite: 1].
* **Scientific Simulation:** 천체 물리 법칙을 기반으로 한 교육용 시뮬레이션 환경 제공.

## 🛠️ Tech Stack
이 프로젝트는 최신 C++ 오픈소스 라이브러리와 함께 빌드됩니다[cite: 2].
* **Vulkan API:** 차세대 그래픽스 API
* **GLFW:** 윈도우 생성 및 입력 제어[cite: 1]
* **GLM:** 수학 연산 및 행렬 변환
* **Dear ImGui:** 고성능 즉시 모드(Immediate Mode) GUI[cite: 2]

## 📦 Build Instructions
CMake와 FetchContent를 사용하여 빌드 환경을 간편하게 구성할 수 있습니다[cite: 2].

    # 1. 저장소 복제
    git clone https://github.com/your-username/AstroScale.git
    cd AstroScale

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
이 프로젝트는 유지보수 가능한 엔진 구조를 설계하는 데 중점을 두었습니다. 추상화된 베이스 클래스(`VulkanBase`)를 통해 확장성을 고려했으며, Vulkan의 복잡한 파이프라인 관리 방식을 실무 수준에서 익히는 계기가 되었습니다[cite: 1].
