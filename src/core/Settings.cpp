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
