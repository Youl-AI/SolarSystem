#include "app/SolarSystemApp.hpp"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    SolarSystemApp app;
    try {
        app.run();
    } catch (const std::exception &e) {
        // 🚀 [핵심 해결] CMD 창이 없어도, 크래시가 나면 윈도우 에러 창을 띄워서 원인을 알려줍니다!
#ifdef _WIN32
        MessageBoxA(nullptr, e.what(), "Ephemeris — Fatal Error", MB_OK | MB_ICONERROR);
#else
        std::cerr << e.what() << std::endl;
#endif
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
