# Graphics Settings Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the scattered fullscreen/orbit/real-scale buttons with one gear icon opening a settings window (resolution, render scale, MSAA, VSync, FOV, exposure, frame cap/FPS, fullscreen, orbit lines, real scale), persisted to `settings.ini`.

**Architecture:** A single `GraphicsSettings` struct is the one source of truth; scattered state (`showOrbits`, `scaleLerp` target, `isFullscreen`, `msaaSamples`, camera FOV) derives from it. Heavy changes (resolution, VSync, render scale, MSAA) are applied at a frame boundary in `onFrameStart()` via a pending-flag, exactly like the existing fullscreen toggle. Light changes (FOV, exposure, frame cap, toggles) take effect next frame. The offscreen 3D pass renders at `renderScale × window`, resolved/composited to the window-sized swapchain so the UI stays crisp.

**Tech Stack:** C++17, Vulkan 1.2, GLFW 3.3.8, Dear ImGui 1.90.4, GLM, CMake/MSVC on Windows.

## Global Constraints

- Design reference: `docs/superpowers/specs/2026-07-18-graphics-settings-panel-design.md`
- No automated test suite. "Testing" = (a) `"$CMAKE" --build build --config Debug` succeeds with no new warnings, (b) the app launches and a screenshot shows correct rendering, (c) no Vulkan crash on the recreation paths.
- **`cmake` is NOT on PATH.** Use: `CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"` then `"$CMAKE" --build build --config Debug`. Run from the repo/worktree root. `build/` is already configured (Visual Studio 18 2026, x64) — do NOT re-run configure.
- The executable is `build/Debug/VulkanApp.exe` and must run with the worktree root as cwd (loads `shaders/*.spv`, `textures/*`, and now `settings.ini` via relative paths).
- **Shaders must be recompiled after editing `.frag`/`.vert`:** `"/c/VulkanSDK/1.4.350.0/Bin/glslc.exe" shaders/post.frag -o shaders/post_frag.spv` (only Task 6 touches a shader). `.spv` are gitignored.
- **Do NOT attempt to click, drive, or screenshot the app from a subagent.** Synthetic mouse input does not reach the app here (verified). Build verification is the implementer's job; visual + interactive verification is the controller's/user's. Stop at a clean build and report.
- **Stay inside the worktree** `c:\Users\hayoul1999.YOUL-HOUSE\Desktop\Github\SolarSystem\.claude\worktrees\camera-relative`. Never read/write the sibling main checkout at `...\SolarSystem\`.
- Do not change the real-scale distance/radius values or the camera-relative rendering math.

---

## Task 1: `GraphicsSettings` struct + `settings.ini` load/save

Introduce the single settings struct and its persistence. No UI and no behavior change yet — this task only adds the struct, loads it at startup into the existing scattered state, and saves it. Existing defaults must be preserved exactly.

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Produces: `struct GraphicsSettings { int resolutionIndex; float renderScale; int msaaLevel; bool vsync; float fovDegrees; float exposure; int frameCap; bool showFps; bool fullscreen; bool orbitLines; bool realScale; };`
- Produces: member `GraphicsSettings settings;`
- Produces: `void loadSettings();` and `void saveSettings();`
- Produces: `static const int RESOLUTION_COUNT = 5;` and `static const int kResolutions[5][2];` (720p..2160p)

- [ ] **Step 1: Add the struct and resolution table**

In `src/main.cpp`, immediately above `struct Planet {`, add:

```cpp
struct GraphicsSettings {
    int   resolutionIndex = 2;   // index into kResolutions; 2 = 1920x1080
    float renderScale     = 1.0f;
    int   msaaLevel       = 8;   // 0=off, else 2/4/8; clamped to device support
    bool  vsync           = true;
    float fovDegrees      = 45.0f;
    float exposure        = 1.0f;
    int   frameCap        = 0;   // 0 = unlimited
    bool  showFps         = false;
    bool  fullscreen      = false;
    bool  orbitLines      = false;
    bool  realScale       = false;
};

static const int kResolutions[][2] = {
    {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}
};
static const int RESOLUTION_COUNT = 5;
```

- [ ] **Step 2: Add the settings member**

In `src/main.cpp`, find:

```cpp
    bool showOrbits = false; // 디폴트는 OFF
```

Add immediately above it:

```cpp
    GraphicsSettings settings;
```

- [ ] **Step 3: Add load/save (key=value ini)**

Immediately above `void initApp() override {` in `src/main.cpp`, add:

```cpp
    void loadSettings() {
        std::ifstream f("settings.ini");
        if (!f.is_open()) return; // 파일 없으면 구조체 기본값 유지
        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            try {
                if      (k == "resolutionIndex") settings.resolutionIndex = std::clamp(std::stoi(v), 0, RESOLUTION_COUNT - 1);
                else if (k == "renderScale")     settings.renderScale = std::clamp(std::stof(v), 0.5f, 2.0f);
                else if (k == "msaaLevel")       settings.msaaLevel = std::stoi(v);
                else if (k == "vsync")           settings.vsync = (std::stoi(v) != 0);
                else if (k == "fovDegrees")      settings.fovDegrees = std::clamp(std::stof(v), 30.0f, 90.0f);
                else if (k == "exposure")        settings.exposure = std::clamp(std::stof(v), 0.3f, 3.0f);
                else if (k == "frameCap")        settings.frameCap = std::max(0, std::stoi(v));
                else if (k == "showFps")         settings.showFps = (std::stoi(v) != 0);
                else if (k == "fullscreen")      settings.fullscreen = (std::stoi(v) != 0);
                else if (k == "orbitLines")      settings.orbitLines = (std::stoi(v) != 0);
                else if (k == "realScale")       settings.realScale = (std::stoi(v) != 0);
            } catch (...) { /* 잘못된 값은 무시하고 기본값 유지 */ }
        }
    }

    void saveSettings() {
        std::ofstream f("settings.ini", std::ios::trunc);
        if (!f.is_open()) return; // 저장 실패는 조용히 무시
        f << "resolutionIndex=" << settings.resolutionIndex << "\n"
          << "renderScale="     << settings.renderScale     << "\n"
          << "msaaLevel="       << settings.msaaLevel       << "\n"
          << "vsync="           << (settings.vsync ? 1 : 0) << "\n"
          << "fovDegrees="      << settings.fovDegrees      << "\n"
          << "exposure="        << settings.exposure        << "\n"
          << "frameCap="        << settings.frameCap        << "\n"
          << "showFps="         << (settings.showFps ? 1 : 0) << "\n"
          << "fullscreen="      << (settings.fullscreen ? 1 : 0) << "\n"
          << "orbitLines="      << (settings.orbitLines ? 1 : 0) << "\n"
          << "realScale="       << (settings.realScale ? 1 : 0) << "\n";
    }
```

If `<fstream>` / `<string>` are not already included at the top of `src/main.cpp`, add `#include <fstream>` and `#include <string>` with the other includes.

- [ ] **Step 4: Load at startup and seed `msaaSamples` from it**

In `initApp()`, find:

```cpp
        msaaSamples = pickMsaaSamples();
```

Replace with:

```cpp
        loadSettings();
        msaaSamples = clampMsaaLevel(settings.msaaLevel);
```

Then add, next to `pickMsaaSamples()`:

```cpp
    // 설정의 MSAA 레벨(0/2/4/8)을 기기 지원으로 클램프해 VkSampleCountFlagBits로 변환한다.
    VkSampleCountFlagBits clampMsaaLevel(int level) {
        VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physicalDevice, &props);
        VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
        auto ok = [&](VkSampleCountFlagBits b){ return (counts & b) != 0; };
        if (level >= 8 && ok(VK_SAMPLE_COUNT_8_BIT)) return VK_SAMPLE_COUNT_8_BIT;
        if (level >= 4 && ok(VK_SAMPLE_COUNT_4_BIT)) return VK_SAMPLE_COUNT_4_BIT;
        if (level >= 2 && ok(VK_SAMPLE_COUNT_2_BIT)) return VK_SAMPLE_COUNT_2_BIT;
        return VK_SAMPLE_COUNT_1_BIT;
    }
```

- [ ] **Step 5: Build**

Run: `"$CMAKE" --build build --config Debug`
Expected: succeeds, no new warnings. (Behavior unchanged: settings.ini does not exist yet, so defaults are used; `clampMsaaLevel(8)` == old `pickMsaaSamples()`.)

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "Add GraphicsSettings struct and settings.ini load/save"
```

---

## Task 2: Gear icon + settings window skeleton; move existing toggles in

Add the gear button (both screens) and the settings window. Move the existing Fullscreen / Orbit Lines / Real Scale controls into it and delete the old standalone panels. Wire the toggles through `settings` so the struct stays the single source of truth. Resolution/scale/MSAA/VSync/FOV/exposure/framecap widgets are added but only the already-working toggles are functional this task (the heavy ones land in Tasks 3-6; show them disabled or as no-ops with a TODO-free "applied next task" — see Step 4).

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `GraphicsSettings settings;` (Task 1)
- Produces: member `bool settingsOpen = false;`
- Produces: `void drawSettingsWindow();`
- Consumes/derives: `showOrbits`, `scaleLerp` target, `fullscreenToggleRequested`

- [ ] **Step 1: Add the open-state member**

In `src/main.cpp`, find:

```cpp
    bool fullscreenToggleRequested = false;
```

Add immediately below it:

```cpp
    bool settingsOpen = false;
```

- [ ] **Step 2: Replace the standalone fullscreen panel with a gear button**

In `src/main.cpp`, find the block that begins:

```cpp
        // =========================================================
        // 0. 우측 상단 전체화면 토글 패널 (LOBBY/SIMULATION 공통 표시)
        // =========================================================
        ImGui::SetNextWindowPos(ImVec2(swapChainExtent.width - 260.0f, 20.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(240.0f, 70.0f), ImGuiCond_Always);
        ImGui::Begin("Fullscreen Panel", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);

        if (isFullscreen) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.2f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.4f, 0.3f, 0.9f));
            if (ImGui::Button("EXIT FULLSCREEN", ImVec2(230.0f, 40.0f))) fullscreenToggleRequested = true;
            ImGui::PopStyleColor(2);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 1.0f, 0.9f));
            if (ImGui::Button("ENTER FULLSCREEN", ImVec2(230.0f, 40.0f))) fullscreenToggleRequested = true;
            ImGui::PopStyleColor(2);
        }
        ImGui::End();
```

Replace with:

```cpp
        // =========================================================
        // 0. 우측 상단 설정(기어) 버튼 (LOBBY/SIMULATION 공통) + 설정 창
        // =========================================================
        ImGui::SetNextWindowPos(ImVec2(swapChainExtent.width - 80.0f, 20.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(60.0f, 60.0f), ImGuiCond_Always);
        ImGui::Begin("Gear", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.3f, 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.7f, 0.9f));
        if (ImGui::Button("[=]", ImVec2(50.0f, 40.0f))) settingsOpen = !settingsOpen; // 기어 대용 라벨
        ImGui::PopStyleColor(2);
        ImGui::End();

        applyLiveSettings();            // settings -> 기존 상태 동기화 (창이 닫혀 있어도 매 프레임)
        if (settingsOpen) drawSettingsWindow();
```

(The `[=]` label is a placeholder glyph for the gear; the controller may swap it for an icon later. It is not a blocker.)

- [ ] **Step 3: Delete the old simulation-screen "Settings Panel" (orbit + real-scale) window**

In `src/main.cpp`, find and delete this entire block (it contains ONLY the real-scale and orbit-line buttons, both now in the settings window). Leave nametags and the selected-body info panel intact.

Find (and delete all of it, including the leading comment line):

```cpp
            // 3. 우측 상단 뷰포트 설정 패널 (리얼스케일 + 궤도선 완벽 복구!)
            // =========================================================
            ImGui::SetNextWindowPos(ImVec2(swapChainExtent.width - 260.0f, 100.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(240.0f, 120.0f), ImGuiCond_Always);
            ImGui::Begin("Settings Panel", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
            
            if (isRealScaleMode) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.2f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.4f, 0.3f, 0.9f));
                if (ImGui::Button("RETURN TO VISUAL SCALE", ImVec2(230.0f, 40.0f))) isRealScaleMode = false;
                ImGui::PopStyleColor(2);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 1.0f, 0.9f));
                if (ImGui::Button("ENABLE REAL SCALE", ImVec2(230.0f, 40.0f))) isRealScaleMode = true;
                ImGui::PopStyleColor(2);
            }
            
            ImGui::Spacing();

            if (showOrbits) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 1.0f, 0.0f, 0.7f)); 
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); 
                if (ImGui::Button("ORBIT LINES : ON", ImVec2(230.0f, 40.0f))) showOrbits = false;
                ImGui::PopStyleColor(2);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.8f)); 
                if (ImGui::Button("ORBIT LINES : OFF", ImVec2(230.0f, 40.0f))) showOrbits = true;
                ImGui::PopStyleColor();
            }
            ImGui::End();
```

Keep the surrounding `if (currentAppState == AppState::SIMULATION) { ... }` structure and everything else in it. Only this window is removed. (Note: the preceding `// 3.` comment line and the `=====` divider above it belong to this block — remove the divider line too if it is orphaned.)

- [ ] **Step 4: Add `drawSettingsWindow()`**

Immediately above `void drawSettingsWindow` is used, add the method (place it near the other UI helpers in `src/main.cpp`):

```cpp
    void drawSettingsWindow() {
        ImGui::SetNextWindowPos(ImVec2(swapChainExtent.width * 0.5f - 220.0f, swapChainExtent.height * 0.5f - 240.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(440.0f, 480.0f), ImGuiCond_Appearing);
        ImGui::Begin("SETTINGS", &settingsOpen, ImGuiWindowFlags_NoCollapse);

        ImGui::SeparatorText("GRAPHICS");

        // Resolution / Render Scale / MSAA / VSync: 값은 여기서 고르고, 실제 적용은 Task 3-5에서
        // pendingRecreate 경로가 담당한다. 이 태스크에서는 위젯만 배치하고 settings에 반영한다.
        {
            const char* resLabels[] = {"1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160"};
            ImGui::Combo("Resolution", &settings.resolutionIndex, resLabels, RESOLUTION_COUNT);

            const char* scaleLabels[] = {"0.5x", "0.75x", "1.0x", "1.5x", "2.0x"};
            static const float scaleVals[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
            int scaleIdx = 2; for (int i = 0; i < 5; ++i) if (scaleVals[i] == settings.renderScale) scaleIdx = i;
            if (ImGui::Combo("Render Scale", &scaleIdx, scaleLabels, 5)) settings.renderScale = scaleVals[scaleIdx];

            const char* msaaLabels[] = {"Off", "2x", "4x", "8x"};
            static const int msaaVals[] = {0, 2, 4, 8};
            int msaaIdx = 3; for (int i = 0; i < 4; ++i) if (msaaVals[i] == settings.msaaLevel) msaaIdx = i;
            if (ImGui::Combo("Anti-aliasing", &msaaIdx, msaaLabels, 4)) settings.msaaLevel = msaaVals[msaaIdx];

            int vsyncIdx = settings.vsync ? 0 : 1;
            const char* vsyncLabels[] = {"On", "Off"};
            if (ImGui::Combo("VSync", &vsyncIdx, vsyncLabels, 2)) settings.vsync = (vsyncIdx == 0);
        }

        ImGui::SliderFloat("Field of View", &settings.fovDegrees, 30.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("Brightness",    &settings.exposure,   0.3f, 3.0f, "%.2f");

        {
            const char* capLabels[] = {"Unlimited", "30", "60", "120", "144"};
            static const int capVals[] = {0, 30, 60, 120, 144};
            int capIdx = 0; for (int i = 0; i < 5; ++i) if (capVals[i] == settings.frameCap) capIdx = i;
            if (ImGui::Combo("Frame Cap", &capIdx, capLabels, 5)) settings.frameCap = capVals[capIdx];
        }
        ImGui::Checkbox("Show FPS", &settings.showFps);
        ImGui::Checkbox("Fullscreen", &settings.fullscreen);

        ImGui::SeparatorText("VIEW");
        bool inSim = (currentAppState == AppState::SIMULATION);
        if (!inSim) ImGui::BeginDisabled();
        ImGui::Checkbox("Orbit Lines", &settings.orbitLines);
        ImGui::Checkbox("Real Scale", &settings.realScale);
        if (!inSim) ImGui::EndDisabled();

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120.0f, 30.0f))) settingsOpen = false;

        ImGui::End();
    }

    // settings -> 기존 파생 상태로 동기화. 설정 창이 닫혀 있어도 매 프레임 호출된다.
    void applyLiveSettings() {
        showOrbits      = settings.orbitLines;
        isRealScaleMode = settings.realScale;   // scaleLerp 타이머가 이 값을 향해 전진한다(기존 로직)
        if (settings.fullscreen != isFullscreen) fullscreenToggleRequested = true;
    }
```

- [ ] **Step 5: (Real-scale routing is done in `applyLiveSettings`.)**

No separate change needed: `applyLiveSettings()` sets `isRealScaleMode = settings.realScale`, and the existing `scaleLerp` advance already keys off `isRealScaleMode`:

```cpp
        if (isRealScaleMode) scaleLerp += deltaTime * 0.5f;
        else scaleLerp -= deltaTime * 0.5f;
```

Confirm this block is intact and unchanged (do not edit it).

- [ ] **Step 6: Save on close**

In `drawSettingsWindow`, find:

```cpp
        if (ImGui::Button("Close", ImVec2(120.0f, 30.0f))) settingsOpen = false;
```

Replace with:

```cpp
        if (ImGui::Button("Close", ImVec2(120.0f, 30.0f))) { settingsOpen = false; saveSettings(); }
```

- [ ] **Step 7: Build + commit**

Run: `"$CMAKE" --build build --config Debug` (expect success). Then:

```bash
git add src/main.cpp
git commit -m "Add gear button + settings window; move fullscreen/orbit/real-scale into it"
```

---

## Task 3: Frame cap, FPS overlay, FOV, and exposure (the light settings)

The settings that need no GPU-resource recreation. Frame cap sleeps the main loop; FPS overlay is ImGui text; FOV drives `camera.baseFov`; exposure goes to the post shader.

**Files:**
- Modify: `src/main.cpp`, `src/Camera.hpp`, `src/VulkanBase.hpp`, `shaders/post.frag`

**Interfaces:**
- Produces: `float Camera::baseFov` and FOV derivation from it
- Produces: exposure delivered to the post pass via push constant
- Consumes: `settings.frameCap`, `settings.showFps`, `settings.fovDegrees`, `settings.exposure`

- [ ] **Step 1: Base FOV in `src/Camera.hpp`**

Find:

```cpp
    float fov = 45.0f; 
```

Replace with:

```cpp
    float baseFov = 45.0f; // 설정값(줌 안 했을 때의 시야각)
    float fov = 45.0f;     // 실제 렌더에 쓰는 값 = baseFov에서 줌으로 좁혀진 결과
```

Then find the scroll-zoom handler (the block that does `fov -= 3.0f` / `fov += 3.0f` with the `< 45.0f` / `> 45.0f` clamps) and change the upper clamp from the literal `45.0f` to `baseFov`, so zoom-out returns to the configured base rather than a hardcoded 45. Show the before/after in your notes. (Zoom-in still narrows toward the existing 2.0f minimum.)

- [ ] **Step 2: Drive `baseFov` from settings each frame**

In `updateUniformBuffer` (near where the camera is updated), add:

```cpp
        camera.baseFov = settings.fovDegrees;
        if (camera.fov > camera.baseFov) camera.fov = camera.baseFov; // 설정을 낮추면 즉시 반영
```

- [ ] **Step 3: Exposure in the post shader**

`shaders/post.frag` currently has NO push-constant block. Add one and apply exposure.

Find:

```glsl
layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;
```

Replace with:

```glsl
layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push { float exposure; } push;
```

Find:

```glsl
    vec3 hdrColor = color + bright;
```

Replace with:

```glsl
    vec3 hdrColor = (color + bright) * push.exposure;
```

Recompile: `"/c/VulkanSDK/1.4.350.0/Bin/glslc.exe" shaders/post.frag -o shaders/post_frag.spv`

- [ ] **Step 4a: Give the post pipeline layout a push-constant range**

The post pipeline layout is currently created with NO push-constant range. Find:

```cpp
        VkPipelineLayoutCreateInfo pLayInfo{}; pLayInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pLayInfo.setLayoutCount = 1; pLayInfo.pSetLayouts = &postDescriptorSetLayout;
        vkCreatePipelineLayout(device, &pLayInfo, nullptr, &postPipelineLayout);
```

Replace with:

```cpp
        VkPushConstantRange postPcRange{}; postPcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; postPcRange.offset = 0; postPcRange.size = sizeof(float);
        VkPipelineLayoutCreateInfo pLayInfo{}; pLayInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pLayInfo.setLayoutCount = 1; pLayInfo.pSetLayouts = &postDescriptorSetLayout; pLayInfo.pushConstantRangeCount = 1; pLayInfo.pPushConstantRanges = &postPcRange;
        vkCreatePipelineLayout(device, &pLayInfo, nullptr, &postPipelineLayout);
```

- [ ] **Step 4b: Push exposure before the post draw**

In `recordCommandBuffer`, find:

```cpp
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipelineLayout, 0, 1, &postDescriptorSet, 0, nullptr);
        vkCmdDraw(cb, 3, 1, 0, 0); 
```

Replace with:

```cpp
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipelineLayout, 0, 1, &postDescriptorSet, 0, nullptr);
        float postExposure = settings.exposure;
        vkCmdPushConstants(cb, postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &postExposure);
        vkCmdDraw(cb, 3, 1, 0, 0); 
```

- [ ] **Step 5: Frame cap in the main loop**

In `src/VulkanBase.hpp`, add a virtual hook the app can use to throttle. Find:

```cpp
    void mainLoop()
    {
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            onFrameStart();
            drawFrame();
        }
        vkDeviceWaitIdle(device);
    }
```

Replace with:

```cpp
    void mainLoop()
    {
        while (!glfwWindowShouldClose(window))
        {
            double frameStart = glfwGetTime();
            glfwPollEvents();
            onFrameStart();
            drawFrame();
            throttleFrame(frameStart);
        }
        vkDeviceWaitIdle(device);
    }

    // 기본은 무제한. 앱이 프레임 상한을 걸고 싶으면 override 한다.
    virtual void throttleFrame(double /*frameStart*/) {}
```

In `src/main.cpp`, add the override:

```cpp
    void throttleFrame(double frameStart) override {
        if (settings.frameCap <= 0) return;
        double target = 1.0 / (double)settings.frameCap;
        double elapsed = glfwGetTime() - frameStart;
        while (elapsed < target) { // busy-wait의 대안으로 짧게 sleep
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            elapsed = glfwGetTime() - frameStart;
        }
    }
```

Add `#include <thread>` to `src/main.cpp` if not present.

- [ ] **Step 6: FPS overlay**

In the ImGui drawing (near the gear button), add:

```cpp
        if (settings.showFps) {
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
            ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
            ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
            ImGui::End();
        }
```

- [ ] **Step 7: Build (+ shader recompile already done), then commit**

Run: `"$CMAKE" --build build --config Debug` (expect success).

```bash
git add src/main.cpp src/Camera.hpp src/VulkanBase.hpp shaders/post.frag
git commit -m "Add frame cap, FPS overlay, FOV base, and exposure settings"
```

---

## Task 4: VSync + resolution (swapchain recreation via a pending flag)

Route resolution and VSync changes through `onFrameStart()`, reusing the fullscreen recreation pattern. Parameterize `createSwapChain()` to honor the chosen present mode and window size.

**Files:**
- Modify: `src/VulkanBase.hpp`, `src/main.cpp`

**Interfaces:**
- Produces: `VkPresentModeKHR VulkanBase::desiredPresentMode = VK_PRESENT_MODE_FIFO_KHR;`
- Produces: member `bool pendingSwapRecreate = false;`
- Consumes: `settings.vsync`, `settings.resolutionIndex`

- [ ] **Step 1: Parameterize present mode in `src/VulkanBase.hpp`**

Find:

```cpp
        createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
```

Replace with:

```cpp
        createInfo.presentMode = choosePresentMode(desiredPresentMode);
```

Add these members and helper to `VulkanBase` (near the other swapchain state):

```cpp
    VkPresentModeKHR desiredPresentMode = VK_PRESENT_MODE_FIFO_KHR;

    // 원하는 모드가 지원되면 그걸, 아니면 FIFO(항상 지원)로 폴백.
    VkPresentModeKHR choosePresentMode(VkPresentModeKHR wanted) {
        uint32_t n = 0; vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &n, nullptr);
        std::vector<VkPresentModeKHR> modes(n);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &n, modes.data());
        for (auto m : modes) if (m == wanted) return wanted;
        return VK_PRESENT_MODE_FIFO_KHR;
    }
```

(`surface` and `physicalDevice` are existing members. If `<vector>` isn't included in this header, add it.)

- [ ] **Step 2: Add the pending-recreate flag and apply in `onFrameStart`**

In `src/main.cpp`, find:

```cpp
    bool settingsOpen = false;
```

Add below:

```cpp
    bool pendingSwapRecreate = false;   // 해상도/VSync 변경 시
    int  appliedResolutionIndex = 2;
    bool appliedVsync = true;
```

In `onFrameStart()`, find:

```cpp
    void onFrameStart() override {
        if (!fullscreenToggleRequested) return;
        fullscreenToggleRequested = false;

        vkDeviceWaitIdle(device);
```

Replace with:

```cpp
    void onFrameStart() override {
        // VSync 변경은 present mode만 바꾸면 되므로 스왑체인 재생성으로 처리.
        if (settings.vsync != appliedVsync) {
            appliedVsync = settings.vsync;
            desiredPresentMode = settings.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
            pendingSwapRecreate = true;
        }
        // 해상도 변경(창모드에서만 창 크기 변경; 전체화면 중이면 다음에 창모드로 돌아올 때 반영).
        if (settings.resolutionIndex != appliedResolutionIndex) {
            appliedResolutionIndex = settings.resolutionIndex;
            if (!isFullscreen) {
                GLFWmonitor* mon = glfwGetPrimaryMonitor(); const GLFWvidmode* vm = glfwGetVideoMode(mon);
                int w = kResolutions[appliedResolutionIndex][0], h = kResolutions[appliedResolutionIndex][1];
                int x = std::max(0, (vm->width - w) / 2), y = std::max(0, (vm->height - h) / 2);
                glfwSetWindowMonitor(window, nullptr, x, y, w, h, 0);
            }
            pendingSwapRecreate = true;
        }

        if (pendingSwapRecreate) {
            pendingSwapRecreate = false;
            vkDeviceWaitIdle(device);
            recreateSwapChain();
        }

        if (!fullscreenToggleRequested) return;
        fullscreenToggleRequested = false;

        vkDeviceWaitIdle(device);
```

- [ ] **Step 3: Apply loaded resolution/vsync at first frame**

In `initApp()`, after `loadSettings();`, seed the applied-trackers so the first `onFrameStart` applies any non-default loaded values:

Find (added in Task 1 Step 4):

```cpp
        loadSettings();
        msaaSamples = clampMsaaLevel(settings.msaaLevel);
```

Replace with:

```cpp
        loadSettings();
        msaaSamples = clampMsaaLevel(settings.msaaLevel);
        appliedResolutionIndex = -1; // 강제로 첫 프레임에 해상도 적용
        appliedVsync = settings.vsync;
        desiredPresentMode = settings.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
```

(Setting `appliedResolutionIndex = -1` guarantees the first `onFrameStart` runs the resolution branch once, sizing the window to the loaded/default resolution. Initial fullscreen from a loaded file is handled by `applyLiveSettings()`, which requests the toggle on frame 1 when `settings.fullscreen != isFullscreen`.)

- [ ] **Step 4: Build + commit**

Run: `"$CMAKE" --build build --config Debug` (expect success).

```bash
git add src/VulkanBase.hpp src/main.cpp
git commit -m "Apply VSync and resolution changes via onFrameStart swapchain recreate"
```

---

## Task 5: Render scale (offscreen at scale x window, composite to window)

Decouple the offscreen/blur render size from the swapchain size. The offscreen 3D pass and blur run at `renderExtent = round(renderScale x swapChainExtent)`; the post/composite pass and UI stay at `swapChainExtent`.

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Produces: member `VkExtent2D renderExtent;`
- Produces: `void computeRenderExtent();`
- Consumes: `settings.renderScale`

- [ ] **Step 1: Add `renderExtent` and its computation**

In `src/main.cpp`, add a member near `msaaSamples`:

```cpp
    VkExtent2D renderExtent{}; // 오프스크린/블러 렌더 크기 = renderScale x swapChainExtent
    float appliedRenderScale = 1.0f;
    bool  pendingOffscreenRecreate = false; // 렌더스케일/MSAA 변경 시
```

Add the helper near `createOffscreenImages`:

```cpp
    void computeRenderExtent() {
        uint32_t w = std::max(1u, (uint32_t)std::lround(swapChainExtent.width  * settings.renderScale));
        uint32_t h = std::max(1u, (uint32_t)std::lround(swapChainExtent.height * settings.renderScale));
        renderExtent = {w, h};
    }
```

- [ ] **Step 2: Size offscreen + blur images by `renderExtent`**

In `createOffscreenImages()`, call `computeRenderExtent();` at the top, then replace every `swapChainExtent.width`/`swapChainExtent.height` used for the offscreen/MSAA image extents and the offscreen framebuffer dimensions with `renderExtent.width`/`renderExtent.height`. Do the same in `createBlurImages()` (blur images and blur framebuffers). Do NOT change the post pass, the swapchain, or ImGui — those stay `swapChainExtent`.

Show each changed line in your notes. (Search within these two functions only.)

- [ ] **Step 3: Use `renderExtent` for the offscreen + blur passes' viewport/scissor and render area**

First, add an extent-parameterized viewport helper. Find:

```cpp
    void setFullViewport(VkCommandBuffer cb) {
        VkViewport vp{}; vp.width = (float)swapChainExtent.width; vp.height = (float)swapChainExtent.height; vp.maxDepth = 1.0f;
        VkRect2D sc{}; sc.extent = swapChainExtent;
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
    }
```

Replace with:

```cpp
    void setViewport(VkCommandBuffer cb, VkExtent2D e) {
        VkViewport vp{}; vp.width = (float)e.width; vp.height = (float)e.height; vp.maxDepth = 1.0f;
        VkRect2D sc{}; sc.extent = e;
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
    }
    void setFullViewport(VkCommandBuffer cb) { setViewport(cb, swapChainExtent); } // 포스트/스왑체인 패스용
```

Offscreen pass render area — find:

```cpp
        offscreenPassInfo.renderArea.offset = {0, 0}; offscreenPassInfo.renderArea.extent = swapChainExtent;
```

Replace with:

```cpp
        offscreenPassInfo.renderArea.offset = {0, 0}; offscreenPassInfo.renderArea.extent = renderExtent;
```

Offscreen pass viewport — find (the offscreen pass's `setFullViewport(cb)` right after `vkCmdBeginRenderPass(cb, &offscreenPassInfo, ...)`):

```cpp
        vkCmdBeginRenderPass(cb, &offscreenPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        setFullViewport(cb);
```

Replace with:

```cpp
        vkCmdBeginRenderPass(cb, &offscreenPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        setViewport(cb, renderExtent);
```

Blur pass — find:

```cpp
            vkCmdBeginRenderPass(cb, &blurPassInfo, VK_SUBPASS_CONTENTS_INLINE); setFullViewport(cb); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline);
```

Replace with:

```cpp
            vkCmdBeginRenderPass(cb, &blurPassInfo, VK_SUBPASS_CONTENTS_INLINE); setViewport(cb, renderExtent); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline);
```

Also set the blur pass `renderArea.extent` to `renderExtent` (find the `blurPassInfo.renderArea` assignment near the blur begin and change `swapChainExtent` → `renderExtent`; show it in your notes). The POST pass (which calls `setFullViewport(cb)` and uses `renderPassInfo.renderArea.extent = swapChainExtent`) stays at `swapChainExtent` — do not touch it.

- [ ] **Step 4: Recreate offscreen on render-scale change (via onFrameStart)**

In `onFrameStart()`, before the fullscreen block, add:

```cpp
        if (settings.renderScale != appliedRenderScale) {
            appliedRenderScale = settings.renderScale;
            pendingOffscreenRecreate = true;
        }
        if (pendingOffscreenRecreate) {
            pendingOffscreenRecreate = false;
            vkDeviceWaitIdle(device);
            recreateOffscreenAndBlur();
        }
```

Add `recreateOffscreenAndBlur()` that tears down and rebuilds only the offscreen + blur images/framebuffers (mirror the teardown in `recreateSwapChain` / `cleanupSwapChain` for those resources, then call `createOffscreenImages(); createBlurImages(); updateBlurDescriptorSets(); updatePostDescriptorSets();`). Reuse the exact destroy calls already present. Show the function in your notes.

Also call `computeRenderExtent();` inside `recreateSwapChain()` (so a window resize keeps the offscreen sized to scale x new-window).

- [ ] **Step 5: Build + commit**

Run: `"$CMAKE" --build build --config Debug` (expect success). Verify visually (controller) that render scale 1.0 looks identical to before.

```bash
git add src/main.cpp
git commit -m "Add render scale: offscreen/blur at scale x window, composite to window"
```

---

## Task 6: Runtime MSAA switching

Let the MSAA dropdown recreate the offscreen render pass, MSAA images, and the two offscreen pipelines at the new sample count. Reuses Task 5's offscreen-recreate seam.

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `settings.msaaLevel`, `clampMsaaLevel()` (Task 1), `recreateOffscreenAndBlur()` (Task 5)
- Produces: `void recreateOffscreenForMsaa();`

- [ ] **Step 1: Detect MSAA change in `onFrameStart`**

In `onFrameStart()`, add near the render-scale check:

```cpp
        if (clampMsaaLevel(settings.msaaLevel) != msaaSamples) {
            vkDeviceWaitIdle(device);
            msaaSamples = clampMsaaLevel(settings.msaaLevel);
            recreateOffscreenForMsaa();
        }
```

- [ ] **Step 2: Add `recreateOffscreenForMsaa()`**

Only the offscreen render pass (sample count in its attachments) and the two offscreen pipelines
(`rasterizationSamples` baked at creation) depend on MSAA. **Blur and post do NOT** — they sample the
*resolved*, always-single-sample offscreen images, so their render passes/images/pipelines are left
untouched; we only rebind the two descriptor sets because the resolve-target image views were
recreated. `createGraphicsPipeline()` creates `graphicsPipeline` + `linePipeline` + `pipelineLayout`
(that layout is used only by those two pipelines). `createOffscreenResources()` creates the offscreen
render pass, the images (via `createOffscreenImages()`), AND the `offscreenSampler` — so the old
render pass and sampler must be destroyed first or they leak (they are otherwise freed only in
`cleanupApp`). `updatePostDescriptorSets` / `updateBlurDescriptorSets` both bind offscreen views with
`offscreenSampler`, so they correctly rebind the recreated views + sampler.

Do NOT use `destroyOffscreenAndBlurResources()` here — it also destroys the blur images (which we are
keeping) and does not destroy the offscreen render pass or sampler (which we must). Add:

```cpp
    void recreateOffscreenForMsaa() {
        // 오프스크린 파이프라인(graphics/line)과 렌더패스는 샘플 수가 생성 시 고정되므로 다시 만든다.
        // 블러/포스트는 resolve된 단일 샘플 이미지를 쓰므로 MSAA와 무관 — 렌더패스/이미지는 그대로 두고
        // resolve 대상 뷰만 새로 바인딩한다.
        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        vkDestroyPipeline(device, linePipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

        vkDestroyFramebuffer(device, offscreenFramebuffer, nullptr);
        vkDestroyImageView(device, offscreenColorView, nullptr); vkDestroyImage(device, offscreenColorImage, nullptr); vkFreeMemory(device, offscreenColorMem, nullptr);
        vkDestroyImageView(device, offscreenBrightView, nullptr); vkDestroyImage(device, offscreenBrightImage, nullptr); vkFreeMemory(device, offscreenBrightMem, nullptr);
        vkDestroyImageView(device, offscreenDepthView, nullptr); vkDestroyImage(device, offscreenDepthImage, nullptr); vkFreeMemory(device, offscreenDepthMem, nullptr);
        vkDestroyImageView(device, msaaColorView, nullptr); vkDestroyImage(device, msaaColorImage, nullptr); vkFreeMemory(device, msaaColorMem, nullptr);
        vkDestroyImageView(device, msaaBrightView, nullptr); vkDestroyImage(device, msaaBrightImage, nullptr); vkFreeMemory(device, msaaBrightMem, nullptr);
        vkDestroyRenderPass(device, offscreenRenderPass, nullptr);
        vkDestroySampler(device, offscreenSampler, nullptr);

        createOffscreenResources();  // 렌더패스 + 이미지 + 프레임버퍼 + 샘플러 (새 msaaSamples 반영)
        createGraphicsPipeline();    // graphics + line + pipelineLayout (새 msaaSamples 반영)

        updateBlurDescriptorSets();  // 새 offscreenBrightView + 새 샘플러 재바인딩
        updatePostDescriptorSets();  // 새 offscreenColorView + 새 샘플러 재바인딩
    }
```

- [ ] **Step 3: Build + commit**

Run: `"$CMAKE" --build build --config Debug` (expect success).

```bash
git add src/main.cpp
git commit -m "Runtime MSAA switching: rebuild offscreen pass + pipelines on change"
```

---

## Task 7: End-to-end verification (controller-run, no code)

- [ ] **Step 1: Clean rebuild**

Run: `"$CMAKE" --build build --config Debug --clean-first`
Expected: 0 errors, no new warnings.

- [ ] **Step 2: settings.ini round-trip via a seeded file**

Since mouse input cannot be injected, the controller writes a non-default `settings.ini` in the worktree root and launches, confirming the app boots with those values:

```
resolutionIndex=1
renderScale=2.0
msaaLevel=2
vsync=0
fovDegrees=70
exposure=1.6
frameCap=60
showFps=1
fullscreen=0
orbitLines=0
realScale=0
```

Launch, screenshot the lobby, confirm: app boots without crash, FPS overlay visible (showFps=1), window at 1600x900 (resolutionIndex=1), brighter image (exposure 1.6). Then delete the seeded file and relaunch to confirm defaults still work.

- [ ] **Step 3: Report what needs the user**

Interactive checks the controller cannot do (mouse injection blocked); the user confirms:

1. Gear button opens/closes the settings window on both lobby and simulation.
2. Each dropdown/slider changes the scene: resolution resizes the window; render scale visibly sharpens (2.0x) or speeds up (0.5x); MSAA Off↔8x changes edge smoothness; VSync Off uncaps FPS; FOV widens/narrows; brightness changes exposure; frame cap limits FPS; Show FPS toggles the overlay; fullscreen toggles.
3. Orbit Lines / Real Scale work from the panel in simulation and are disabled on the lobby.
4. Settings persist across a restart (change something, close, relaunch).
5. No crash when changing resolution/MSAA/VSync/render-scale repeatedly (these recreate GPU resources).
