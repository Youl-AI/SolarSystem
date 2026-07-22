cmake_minimum_required(VERSION 3.15)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(Ephemeris)

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
add_executable(Ephemeris
    src/main.cpp
    src/third_party_impl.cpp
    src/core/Settings.cpp
    src/core/GpuProfiler.cpp
    src/sim/CelestialData.cpp
    src/sim/AsteroidBelt.cpp
    src/gfx/MeshGen.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

if(MSVC)
    target_compile_options(Ephemeris PRIVATE /utf-8)
endif()

# Vulkan 헤더 및 라이브러리, 그리고 ImGui 경로 연결
target_include_directories(Ephemeris PRIVATE 
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_DIR}            # imgui.h 를 찾기 위해 추가
    ${IMGUI_DIR}/backends   # imgui_impl_glfw.h 등을 찾기 위해 추가
)
# Vulkan의 깊이 범위는 [0,1]인데 GLM은 기본이 OpenGL의 [-1,1]이다.
# 헤더에서 #define하면 다른 헤더가 먼저 glm을 포함해버려 늦을 수 있으므로 컴파일 정의로 준다.
# (이게 빠지면 glm::ortho가 만든 깊이의 음수 절반이 Vulkan에서 잘려나가, 셰도우 맵이 통째로 빈다)
target_compile_definitions(Ephemeris PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)

target_link_libraries(Ephemeris PRIVATE glfw glm Vulkan::Vulkan)

# =========================================================
# 셰이더 컴파일
#
# .spv는 빌드 산출물이라 .gitignore에 걸려 있다. 그래서 저장소를 새로 클론해 빌드하면
# 실행 파일은 만들어져도 실행하는 순간 셰이더를 못 찾고 죽었다(readFile이 크기를 -1로
# 읽어 "vector too long"으로 터진다). 여기서 함께 구워야 클론 후 바로 실행된다.
#
# 결과물은 shaders/ 안에 둔다. 실행 파일이 상대 경로로 찾기 때문이다.
# 목록에 있는 7개가 main.cpp가 실제로 읽는 전부다. blur.frag과 particle.* 은 어디서도
# 쓰이지 않는 잔재이므로 넣지 않는다(particle.frag은 셰이더도 아니고 main.cpp의 낡은 사본이다).
# =========================================================
if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(Vulkan_GLSLC_EXECUTABLE NAMES glslc
                 HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
endif()
if(NOT Vulkan_GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc를 찾지 못했습니다. Vulkan SDK를 설치하고 VULKAN_SDK 환경변수를 확인하세요.")
endif()

# (원본 파일명, 결과 파일명) 쌍. 이름이 서로 다른 것은 main.cpp가 기대하는 이름에 맞춘 것이다.
set(SHADER_PAIRS
    shader.vert         vert.spv
    shader.frag         frag.spv
    post.vert           post_vert.spv
    post.frag           post_frag.spv
    bloom_down.frag     bloom_down_frag.spv
    bloom_up.frag       bloom_up_frag.spv
    asteroid_cull.comp  asteroid_cull.spv
)

set(SHADER_OUTPUTS "")
list(LENGTH SHADER_PAIRS SHADER_PAIRS_LEN)
math(EXPR SHADER_LAST "${SHADER_PAIRS_LEN} - 2")
foreach(i RANGE 0 ${SHADER_LAST} 2)
    math(EXPR j "${i} + 1")
    list(GET SHADER_PAIRS ${i} shader_src)
    list(GET SHADER_PAIRS ${j} shader_out)
    set(src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader_src}")
    set(dst "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader_out}")
    add_custom_command(
        OUTPUT  "${dst}"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${src}" -o "${dst}"
        DEPENDS "${src}"
        COMMENT "glslc ${shader_src} -> ${shader_out}"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS "${dst}")
endforeach()

add_custom_target(Shaders DEPENDS ${SHADER_OUTPUTS})
add_dependencies(Ephemeris Shaders)