cmake_minimum_required(VERSION 3.15)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(VulkanEarth)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Vulkan SDK 자동 탐색 (설치되어 있어야 함)
find_package(Vulkan REQUIRED)

include(FetchContent)

# GLFW 다운로드
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.8
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM 다운로드
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        0.9.9.8
)
FetchContent_MakeAvailable(glm)

# =========================================================
# [NEW] ImGui 다운로드 설정
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.4
)
FetchContent_MakeAvailable(imgui)

# ImGui 다운로드 경로를 변수로 저장
set(IMGUI_DIR ${imgui_SOURCE_DIR})
# =========================================================

# 실행 파일 생성 (main.cpp 외에 ImGui의 필수 소스 파일들도 함께 빌드!)
add_executable(VulkanApp 
    src/main.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

if(MSVC)
    target_compile_options(VulkanApp PRIVATE /utf-8)
endif()

# Vulkan 헤더 및 라이브러리, 그리고 ImGui 경로 연결
target_include_directories(VulkanApp PRIVATE 
    ${Vulkan_INCLUDE_DIRS}
    ${IMGUI_DIR}            # imgui.h 를 찾기 위해 추가
    ${IMGUI_DIR}/backends   # imgui_impl_glfw.h 등을 찾기 위해 추가
)
target_link_libraries(VulkanApp PRIVATE glfw glm Vulkan::Vulkan)