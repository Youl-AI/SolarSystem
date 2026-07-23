#include "SolarSystemApp.hpp"

bool SolarSystemApp::worldToScreen(glm::vec3 worldPos, glm::mat4 view, glm::mat4 proj, float screenWidth, float screenHeight, glm::vec2& outScreenPos) {
    glm::vec4 clipSpacePos = proj * view * glm::vec4(worldPos, 1.0f);
    if (clipSpacePos.w < 0.1f) return false;

    glm::vec3 ndcSpacePos = glm::vec3(clipSpacePos) / clipSpacePos.w;
    outScreenPos.x = (ndcSpacePos.x + 1.0f) * 0.5f * screenWidth;
    outScreenPos.y = (ndcSpacePos.y + 1.0f) * 0.5f * screenHeight; 
    return true;
}

void SolarSystemApp::TextCentered(const char* text) {
    float windowWidth = ImGui::GetWindowSize().x;
    float textWidth = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
    ImGui::Text("%s", text);
}

void SolarSystemApp::initImGui() {
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 }
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 100;
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = nullptr;

    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = 0;
    init_info.Queue = graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = imguiPool;
    init_info.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = 2;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.RenderPass = renderPass;
    ImGui_ImplVulkan_Init(&init_info);

    // 기어 버튼용 아이콘. loadTexture가 이미지/뷰/메모리를 allImages 등에 등록해 수명을 관리하고,
    // AddTexture로 얻은 디스크립터셋은 imguiPool과 함께 해제된다. 로드 실패 시 gearTexId는 NULL로
    // 남아, 렌더링 쪽에서 기존 "[=]" 텍스트 버튼으로 폴백한다.
    VkImageView gearView = loadGrayIcon("assets/settings.png", 175);  // 어두운 배경에서도 보이는 밝은 회색
    if (gearView != VK_NULL_HANDLE)
        gearTexId = ImGui_ImplVulkan_AddTexture(textureSampler, gearView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// 이번 프레임의 ImGui를 전부 구성한다. updateUniformBuffer의 뒤쪽 254줄이었다.
void SolarSystemApp::drawUi() {
    ImGui_ImplVulkan_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

    // =========================================================
    // 0. 우측 상단 설정(기어) 버튼 (LOBBY/SIMULATION 공통) + 설정 창
    // =========================================================
    ImGui::SetNextWindowPos(ImVec2(swapChainExtent.width - 84.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(64.0f, 64.0f), ImGuiCond_Always);
    ImGui::Begin("Gear", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));   // 배경 완전 투명 (기어만 보이도록)
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));   // 호버 하이라이트 없음
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
    bool gearClicked = gearTexId
        ? ImGui::ImageButton("gear", gearTexId, ImVec2(40.0f, 40.0f))   // assets/settings.png 아이콘
        : ImGui::Button("[=]", ImVec2(50.0f, 40.0f));                   // 로드 실패 시 텍스트 폴백
    if (gearClicked) settingsOpen = !settingsOpen;
    ImGui::PopStyleColor(3);
    ImGui::End();

    // =========================================================
    // 0-b. 우측 하단 EXIT 버튼 (LOBBY/SIMULATION 공통) — 앱 자체를 종료한다.
    //      메뉴의 플랫 사이파이 스타일을 따르되, 종료 의미로 붉은 톤을 쓴다.
    // =========================================================
    {
        const float exitW = 96.0f, exitH = 38.0f;
        ImGui::SetNextWindowPos(ImVec2(swapChainExtent.width - exitW - 30.0f, swapChainExtent.height - exitH - 26.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(exitW + 20.0f, exitH + 20.0f), ImGuiCond_Always);
        ImGui::Begin("ExitBtn", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.15f, 0.15f, 0.22f)); // 평소 투명한 붉은 톤
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.22f, 0.22f, 0.60f)); // 호버 시 채워짐
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f, 0.30f, 0.30f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.90f, 0.40f, 0.40f, 0.85f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("EXIT", ImVec2(exitW, exitH))) glfwSetWindowShouldClose(window, GLFW_TRUE);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        ImGui::End();
    }

    applyLiveSettings();            // settings -> 기존 상태 동기화 (창이 닫혀 있어도 매 프레임)
    if (settingsOpen) drawSettingsWindow();

    if (settings.showFps) {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
        ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
        ImGui::End();
    }

    if (settings.showGpuTimes && profiler.enabled()) {
        // FPS만으로는 '느리다'는 것만 알 뿐 어디가 느린지 알 수 없다. 패스별로 나눠
        // 보여줘야 MSAA, 소행성 수, 대기 셰이더 중 무엇을 손댈지 정할 수 있다.
        //
        // 좌하단에 둔다. 좌상단은 천체 정보 패널, 우상단은 기어, 우하단은 EXIT가 쓴다.
        // 배경도 깔아 준다 — 별밭 위에 흰 글씨만 얹으면 읽을 수가 없다.
        ImGui::SetNextWindowPos(ImVec2(10.0f, swapChainExtent.height - 10.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::Begin("GPU Times", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
        double total = 0.0;
        for (int i = 0; i < 10; ++i) total += profiler.smoothMs(i);
        // 라벨은 반드시 ASCII로 쓴다. ImGui 기본 폰트에는 한글 글리프가 없어 전부 '?'가 된다.
        ImGui::Text("GPU %.2f ms  (max %.0f FPS)", total, total > 0.0 ? 1000.0 / total : 0.0);
        ImGui::Separator();
        for (int i = 0; i < 10; ++i) {
            if (profiler.smoothMs(i) < 0.005) continue;   // 눈금 이하인 패스는 줄만 차지한다
            ImGui::Text("%-12s %5.2f ms  %4.1f%%", GpuProfiler::NAMES[i], profiler.smoothMs(i),
                        total > 0.0 ? profiler.smoothMs(i) / total * 100.0 : 0.0);
        }

        // 소행성이 비싸 보일 때 개수 탓인지 LOD 탓인지 가르려면 이 줄이 필요하다.
        // 콘솔에만 찍히고 있었는데, 창으로 실행하면 콘솔이 없어 볼 방법이 없었다.
        uint32_t perLod[3] = {}, tris = 0;
        for (int lod = 0; lod < 3; ++lod)
            for (int type = 0; type < 4; ++type) {
                const auto &c = profiler.drawCmds()[lod * 4 + type];
                perLod[lod] += c.instanceCount;
                tris += c.instanceCount * (c.indexCount / 3);
            }
        ImGui::Separator();
        ImGui::Text("asteroids drawn %u / %zu", perLod[0] + perLod[1] + perLod[2], asteroidTransforms.size());
        ImGui::Text("  LOD  high %u   mid %u   low %u", perLod[0], perLod[1], perLod[2]);
        ImGui::Text("  triangles %.2f M", tris / 1.0e6);

        // MSAA 렌더 타겟은 샘플 수에 정비례해 커진다. 8x에서는 색상/bright/깊이가
        // 합쳐서 수백 MiB인데, 프레임 시간에는 거의 안 잡혀서 눈치채기 어렵다.
        {
            double px = (double)renderExtent.width * renderExtent.height;
            int s = (int)msaaSamples;
            double msaaMiB = px * s * (8.0 + 8.0 + 4.0) / 1048576.0;   // 색상 + bright + 깊이(D32)
            ImGui::Separator();
            ImGui::Text("render %ux%u  MSAA %dx  targets %.0f MiB",
                        renderExtent.width, renderExtent.height, s, msaaMiB);
        }
        ImGui::End();
    }

    if (currentAppState == AppState::LOBBY) {
        // ---------------------------------------------------------
        // ① 대기 화면(LOBBY): 우주 시뮬레이션 스타일 HUD
        // 박스형 카드 대신, 살아있는 씬 위에 은은한 그라데이션 스크림 띠 +
        // 얇은 HUD 액센트 라인 + 플랫한 사이파이 버튼으로 구성한다.
        // ---------------------------------------------------------
        float cx = swapChainExtent.width * 0.5f;
        float cy = swapChainExtent.height * 0.5f;

        // 1) 가독성용 수평 그라데이션 스크림 (배경 드로리스트, 위아래로 부드럽게 페이드 → 하드 엣지 없음)
        ImDrawList* bgList = ImGui::GetBackgroundDrawList();
        const float bandH = 175.0f;
        const ImU32 scrimDark = IM_COL32(3, 6, 12, 150);
        const ImU32 scrimClear = IM_COL32(3, 6, 12, 0);
        bgList->AddRectFilledMultiColor(ImVec2(0.0f, cy - bandH), ImVec2((float)swapChainExtent.width, cy), scrimClear, scrimClear, scrimDark, scrimDark);
        bgList->AddRectFilledMultiColor(ImVec2(0.0f, cy), ImVec2((float)swapChainExtent.width, cy + bandH), scrimDark, scrimDark, scrimClear, scrimClear);

        // 2) 투명·오토리사이즈 창을 화면 중앙(피벗 0.5,0.5)에 → 빈 여백 없이 콘텐츠만
        ImGui::SetNextWindowPos(ImVec2(cx, cy), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::Begin("Main Menu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize);

        // 타이틀
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.93f, 1.0f, 1.0f));
        ImGui::SetWindowFontScale(2.3f);
        TextCentered("E P H E M E R I S");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        // 타이틀 밑 HUD 액센트 라인 (타이틀 폭에 맞춰 가운데, 시안 + 양 끝 캡)
        {
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImVec2 rmax = ImGui::GetItemRectMax();
            float lineY = rmax.y + 5.0f;
            float lineCx = (rmin.x + rmax.x) * 0.5f;
            float lineHalf = (rmax.x - rmin.x) * 0.5f;
            ImDrawList* wl = ImGui::GetWindowDrawList();
            wl->AddLine(ImVec2(lineCx - lineHalf, lineY), ImVec2(lineCx + lineHalf, lineY), IM_COL32(70, 150, 230, 200), 1.5f);
            wl->AddCircleFilled(ImVec2(lineCx - lineHalf, lineY), 2.5f, IM_COL32(120, 200, 255, 230));
            wl->AddCircleFilled(ImVec2(lineCx + lineHalf, lineY), 2.5f, IM_COL32(120, 200, 255, 230));
        }

        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        // 태그라인 (자간을 넓힌 HUD 느낌으로 공백 삽입)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.62f, 0.72f, 1.0f));
        ImGui::SetWindowFontScale(0.95f);
        TextCentered("A   M E A S U R E D   S O L A R   S Y S T E M");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 24.0f));

        // 플랫 사이파이 버튼: 평소 투명한 시안 + 테두리, 호버 시 채워짐
        const float btnW = 270.0f, btnH = 46.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - btnW) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.45f, 0.75f, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.60f, 0.95f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.30f, 0.70f, 1.00f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.45f, 0.75f, 1.0f, 0.9f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::SetWindowFontScale(1.15f);
        if (ImGui::Button("LAUNCH  MISSION", ImVec2(btnW, btnH))) {
            currentAppState = AppState::SIMULATION; // 시뮬레이션 진입
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.5f, 0.58f, 1.0f));
        ImGui::SetWindowFontScale(0.85f);
        TextCentered("Press LAUNCH to enter the simulation");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::End();
    }
    else if (currentAppState == AppState::SIMULATION) {
        // ---------------------------------------------------------
        // ② 인게임 시뮬레이션(SIMULATION) 상태의 UI 연출
        // ---------------------------------------------------------
        ImGui::SetNextWindowPos(ImVec2(0, 0)); 
        ImGui::SetNextWindowSize(ImVec2((float)swapChainExtent.width, (float)swapChainExtent.height));
        
        // 1. 기존 HUD 오버레이 (행성 이름표)
        ImGui::Begin("Overlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
        ImDrawList* drawList = ImGui::GetWindowDrawList(); glm::vec2 screenPos;
        glm::mat4 viewMat = camera.getViewMatrix();
        glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), swapChainExtent.width / (float)swapChainExtent.height, 0.01f, 1000000.0f); projMat[1][1] *= -1;

        auto drawTagBox = [&](glm::vec3 pos, const std::string& text, ImU32 col, float yOffset, bool isSelected) {
            if (worldToScreen(pos, viewMat, projMat, swapChainExtent.width, swapChainExtent.height, screenPos)) {
                // 🚀 [수정됨] 텍스트 크기를 정밀하게 측정하고 패딩(여백)을 부여합니다.
                ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
                ImVec2 padding = ImVec2(8.0f, 4.0f); // 좌우 8px, 상하 4px 여백
                
                float w = textSize.x + padding.x * 2.0f;
                float h = textSize.y + padding.y * 2.0f;
                
                // 중앙 정렬: 렌더링 시작점(X)을 전체 박스 길이의 절반만큼 왼쪽으로 옮김
                ImVec2 pMin = ImVec2(screenPos.x - (w * 0.5f), screenPos.y - yOffset);
                ImVec2 pMax = ImVec2(pMin.x + w, pMin.y + h);
                
                // 🚀 [디자인] SF 레이더 스타일의 형광 초록(Neon Green) 테마 적용
                // 선택됨: 밝고 쨍한 형광 초록 테두리 + 짙은 초록색 반투명 배경
                // 미선택: 어둡고 탁한 초록 테두리 + 검은색 반투명 배경
                ImU32 bgCol = isSelected ? IM_COL32(10, 50, 20, 220) : IM_COL32(0, 0, 0, 150);
                ImU32 borderCol = isSelected ? IM_COL32(57, 255, 20, 255) : IM_COL32(30, 120, 30, 150);
                
                // 선택 시 글자색도 흰색/회색에서 밝은 형광 초록으로 오버라이드
                ImU32 textCol = isSelected ? IM_COL32(57, 255, 20, 255) : col;
                
                drawList->AddRectFilled(pMin, pMax, bgCol, 4.0f);
                drawList->AddRect(pMin, pMax, borderCol, 4.0f, 0, isSelected ? 2.0f : 1.0f);
                
                // 계산된 패딩 위치에 텍스트를 그리면 소름 돋도록 완벽한 중앙 정렬 완성!
                drawList->AddText(ImVec2(pMin.x + padding.x, pMin.y + padding.y), textCol, text.c_str());
            }
        };

        drawTagBox(relativeToCamera(sun.currentPosition), "Sun", IM_COL32(255, 200, 0, 255), 35.0f, (selectedTargetType == 0));
        for (int i = 0; i < planets.size(); i++) {
            drawTagBox(relativeToCamera(planets[i].currentPosition), planets[i].name, IM_COL32(255, 255, 255, 200), 35.0f, (selectedTargetType == 1 && selectedPlanetIndex == i));
        }
        drawTagBox(relativeToCamera(moon.currentPosition), "Moon", IM_COL32(200, 200, 200, 200), 25.0f, (selectedTargetType == 2));
        ImGui::End();

        // =========================================================
        // 2. 좌측 상단 정보 패널 (단일 클릭 시 등장)
        // =========================================================
        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Information Panel", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);

        if (selectedTargetType != -1) {
            std::string targetName = "";
            if (selectedTargetType == 0) targetName = "Sun";
            else if (selectedTargetType == 1) targetName = planets[selectedPlanetIndex].name;
            else if (selectedTargetType == 2) targetName = "Moon";

            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.8f, 1.0f), ">> TARGET : %s", targetName.c_str());
            ImGui::Spacing();
            ImGui::Text("%s", getCelestialInfo(targetName).c_str());
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (selectedTargetType == 2) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.8f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.9f, 0.9f));
                if (ImGui::Button(isEclipseEvent ? "EXIT ECLIPSE MODE" : "EXPERIENCE TOTAL ECLIPSE", ImVec2(240.0f, 40.0f))) {
                    isEclipseEvent = !isEclipseEvent; 
                }
                ImGui::PopStyleColor(2);
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select a celestial body\nor name tag to view info.");
        }
        ImGui::End();
    }

    ImGui::Render();
}

// 직전 위젯에 설명을 붙인다.
//
// 라벨과 마찬가지로 영문으로만 쓴다 — ImGui 기본 폰트에 한글 글리프가 없어
// 한글을 넣으면 전부 '?'로 나온다.
// AllowWhenDisabled를 주는 이유: VIEW 항목들은 로비에서 비활성인데, 그때야말로
// "이게 뭐 하는 건지" 궁금해서 마우스를 올려 보게 된다.
void SolarSystemApp::helpTip(const char *text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
}

void SolarSystemApp::drawSettingsWindow() {
    // 설정 창은 해상도가 바뀌어도 항상 화면 중앙에 고정한다(같은 상대 위치 + 잘림 방지).
    // 크기는 고정: 내용이 고정된 컨트롤 목록이라 해상도에 맞춰 키우면 여백만 늘어 비어 보인다.
    // 440x480은 최소 해상도(1280x720)에도 여유롭게 들어간다. NoMove로 드래그 스냅백도 방지.
    const ImVec2 settingsSize(440.0f, 480.0f);
    ImGui::SetNextWindowPos(ImVec2(swapChainExtent.width * 0.5f - settingsSize.x * 0.5f,
                                   swapChainExtent.height * 0.5f - settingsSize.y * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(settingsSize, ImGuiCond_Always);
    ImGui::Begin("SETTINGS", &settingsOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

    ImGui::SeparatorText("GRAPHICS");

    // Resolution / Render Scale / MSAA / VSync: 값은 여기서 고르고, 실제 적용은 Task 3-5에서
    // pendingRecreate 경로가 담당한다. 이 태스크에서는 위젯만 배치하고 settings에 반영한다.
    {
        // Display: 창모드 해상도 목록 + 마지막 항목 "Fullscreen"을 하나의 셀렉터로 통합.
        // 전체화면에서는 해상도 조정이 무의미하므로(항상 모니터 네이티브로 렌더) 합쳤다.
        // 저해상도로 "다운스케일"된 느낌이 필요하면 아래 Render Scale이 그 역할을 한다.
        const char* displayLabels[] = {"1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Fullscreen"};
        int displayIdx = settings.fullscreen ? RESOLUTION_COUNT : settings.resolutionIndex;
        if (ImGui::Combo("Display", &displayIdx, displayLabels, RESOLUTION_COUNT + 1)) {
            if (displayIdx == RESOLUTION_COUNT) {
                settings.fullscreen = true;              // resolutionIndex는 유지 → 해제 시 복귀
            } else {
                settings.fullscreen = false;
                settings.resolutionIndex = displayIdx;
            }
        }
        helpTip("Window size. 'Fullscreen' always renders at the monitor's\n"
                "native resolution, so the size list does not apply there.\n"
                "To render fewer pixels without shrinking the window, use\n"
                "Render Scale below instead.");

        const char* scaleLabels[] = {"0.5x", "0.75x", "1.0x", "1.5x", "2.0x"};
        static const float scaleVals[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
        int scaleIdx = 2; for (int i = 0; i < 5; ++i) if (scaleVals[i] == settings.renderScale) scaleIdx = i;
        if (ImGui::Combo("Render Scale", &scaleIdx, scaleLabels, 5)) settings.renderScale = scaleVals[scaleIdx];
        helpTip("Renders at this multiple of the window size, then resamples\n"
                "to fit. Cost scales with the square: 2.0x draws 4x the pixels.\n"
                "\n"
                "Above 1.0x this is supersampling - it cleans up everything,\n"
                "including texture and shader detail that MSAA cannot touch.\n"
                "Below 1.0x it is the cheapest way to gain frame rate.");

        const char* msaaLabels[] = {"Off", "2x", "4x", "8x"};
        static const int msaaVals[] = {0, 2, 4, 8};
        int msaaIdx = 3; for (int i = 0; i < 4; ++i) if (msaaVals[i] == settings.msaaLevel) msaaIdx = i;
        if (ImGui::Combo("Anti-aliasing", &msaaIdx, msaaLabels, 4)) settings.msaaLevel = msaaVals[msaaIdx];
        helpTip("Multisampling. Smooths geometry edges - planet limbs and\n"
                "orbit lines - but not texture or shader detail.\n"
                "\n"
                "Costs little frame time here, but the render targets grow in\n"
                "direct proportion to the sample count. At 3200x1800 they take\n"
                "879 MiB at 8x versus 220 MiB at 2x. Lower this first if you\n"
                "ever run short of video memory.");

        int vsyncIdx = settings.vsync ? 0 : 1;
        const char* vsyncLabels[] = {"On", "Off"};
        if (ImGui::Combo("VSync", &vsyncIdx, vsyncLabels, 2)) settings.vsync = (vsyncIdx == 0);
        helpTip("On: waits for the monitor to refresh before presenting, so\n"
                "the image never tears. Caps the frame rate at the refresh rate.\n"
                "Off: presents as soon as a frame is ready. Higher frame rate,\n"
                "but a frame can be swapped mid-scan and show a torn seam.");
    }

    ImGui::SliderFloat("Field of View", &settings.fovDegrees, 30.0f, 90.0f, "%.0f deg");
    helpTip("Vertical viewing angle. Wider fits more in but stretches the\n"
            "edges; narrower feels like a telephoto lens.\n"
            "This is the resting value - the scroll wheel zooms in from here,\n"
            "down to 2 degrees.");

    ImGui::SliderFloat("Brightness",    &settings.exposure,   0.3f, 3.0f, "%.2f");
    helpTip("Exposure multiplier applied before tone mapping, like the\n"
            "exposure dial on a camera. Raise it to lift dim detail out of\n"
            "shadow; the brightest highlights stay white either way.");

    {
        const char* capLabels[] = {"Unlimited", "30", "60", "120", "144"};
        static const int capVals[] = {0, 30, 60, 120, 144};
        int capIdx = 0; for (int i = 0; i < 5; ++i) if (capVals[i] == settings.frameCap) capIdx = i;
        if (ImGui::Combo("Frame Cap", &capIdx, capLabels, 5)) settings.frameCap = capVals[capIdx];
    }
    helpTip("Holds the frame rate at a target by sleeping between frames.\n"
            "Useful for keeping the GPU quiet and cool when the scene is\n"
            "running far faster than the display can show.");

    ImGui::Checkbox("Show FPS", &settings.showFps);
    helpTip("Frame rate counter in the top-left corner.");

    // 계측 항목은 개발자 세션에서만 보인다(SOLAR_PROFILE=1). 배포 실행에서는
    // 항목 자체가 없다 — 최종 사용자에게 필요한 정보가 아니고, 잘못 읽기도 쉽다.
    if (profiler.devTools()) {
        if (!profiler.enabled()) ImGui::BeginDisabled();
        ImGui::Checkbox("Show GPU Times", &settings.showGpuTimes);
        if (!profiler.enabled()) ImGui::EndDisabled();
        helpTip(profiler.enabled()
                ? "Per-pass GPU timings in the bottom-left corner, so you can see\n"
                  "which pass a frame is actually spent in rather than guessing.\n"
                  "Also reports how many asteroids are drawn at each level of\n"
                  "detail, and how much memory the render targets occupy.\n"
                  "\n"
                  "Read the milliseconds, not the percentages - a pass can reach\n"
                  "a large share simply because the others got cheaper."
                : "This GPU does not support timestamp queries.");
    }

    ImGui::SeparatorText("VIEW");
    bool inSim = (currentAppState == AppState::SIMULATION);
    if (!inSim) ImGui::BeginDisabled();

    ImGui::Checkbox("Orbit Lines", &settings.orbitLines);
    helpTip("Draws each body's orbital ellipse, tilted and oriented to its\n"
            "real inclination and ascending node.");

    ImGui::Checkbox("Asteroids", &settings.asteroids);
    helpTip("Draws the main belt and the Kuiper belt - 12,400 bodies whose\n"
            "orbits are sampled from the real populations. Turning this off\n"
            "skips them entirely, though they are cheap: even with every one\n"
            "on screen they cost well under a millisecond.");

    ImGui::Checkbox("Real Scale", &settings.realScale);
    helpTip("Moves every body to its true distance and size instead of the\n"
            "compressed layout, at 1 AU = 500 units.\n"
            "\n"
            "The solar system is mostly empty, so expect the planets to\n"
            "become specks scattered across a great deal of nothing. That\n"
            "emptiness is the point.");

    ImGui::Checkbox("Real Stars", &settings.realStars);
    helpTip("Show the sky as the naked eye would see it from space:\n"
            "about 9,100 stars (magnitude 6.5), a faint grey Milky Way,\n"
            "and no twinkling - starlight only shimmers when it passes\n"
            "through air, so in vacuum the stars sit perfectly still.\n"
            "\n"
            "Off is the long-exposure photograph look: millions of stars\n"
            "and a bright, colourful Milky Way. Independent of Real Scale.");

    if (!inSim) ImGui::EndDisabled();

    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120.0f, 30.0f))) { settingsOpen = false; saveSettings(settings); }

    ImGui::End();
}

// settings -> 기존 파생 상태로 동기화. 설정 창이 닫혀 있어도 매 프레임 호출된다.
void SolarSystemApp::applyLiveSettings() {
    showOrbits      = settings.orbitLines;
    isRealScaleMode = settings.realScale;   // scaleLerp 타이머가 이 값을 향해 전진한다(기존 로직)
    if (settings.fullscreen != isFullscreen) fullscreenToggleRequested = true;
}
