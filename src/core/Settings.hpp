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
    // 88개 IAU 별자리 선·이름을 하늘 위에 그린다. realStars와 같은 세션 전용 뷰 토글이라
    // 저장하지 않는다(항상 OFF로 시작).
    bool  constellations  = false;
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
