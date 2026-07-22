#include "SolarSystemApp.hpp"

void SolarSystemApp::initApp() {
    loadSettings(settings);
    profiler.init(physicalDevice, device);
    // 진단용: 시작 시 특정 행성에 잠근다(SOLAR_LOCK=5 → 목성). 카메라를 직접 몰지 않고
    // 셰도우 맵 같은 시점 의존 기능을 재현하려고 둔 것이다.
    if (const char* lk = std::getenv("SOLAR_LOCK")) {
        lockedTargetType = 1; lockedPlanetIndex = std::atoi(lk);
        std::cout << "[lock] planets[" << lockedPlanetIndex << "]\n";
    }
    msaaSamples = clampMsaaLevel(settings.msaaLevel);
    appliedResolutionIndex = -1; // 강제로 첫 프레임에 해상도 적용
    appliedVsync = settings.vsync;
    desiredPresentMode = settings.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;

    generateSphere(vertices, indices, 1.0f, 64, 64);
    sphereIndexCount = static_cast<uint32_t>(indices.size());

    ringVertexOffset = static_cast<int32_t>(vertices.size());
    uint32_t ringFirstIdx = static_cast<uint32_t>(indices.size());
    generateRing(vertices, indices, 1.2f, 2.2f, 64);
    ringIndexCount = static_cast<uint32_t>(indices.size()) - ringFirstIdx;

    orbitVertexOffset = static_cast<int32_t>(vertices.size());
    orbitFirstIndex = static_cast<uint32_t>(indices.size());
    // 모든 궤도가 공유하는 단위 원이라 가장 빡센 궤도(리얼 스케일 Eris, 반지름 34000)에 맞춰야 한다.
    // 다각형의 현이 진짜 타원 안쪽으로 파고드는 오차가 궤도반경 x (1 - cos(pi/N)) 이므로,
    // 128분할이면 Eris 기준 10.2 유닛(= 그 천체 반지름의 113배)이 어긋나 궤도선이 천체를 완전히 빗나간다.
    generateOrbit(vertices, indices, 4096);

    orbitIndexCount = static_cast<uint32_t>(indices.size()) - orbitFirstIndex;

    // 1. 4종류의 소행성 모델 순차적 로딩 (대소문자 완벽 일치)
    std::string objPaths[4] = {
        "textures/asteroids/Bennu_Radar.obj",
        "textures/asteroids/gaspra_stooke.obj",
        "textures/asteroids/Ida_Stooke.obj",
        "textures/asteroids/Itokawa_Radar.obj"
    };
    for (int i = 0; i < 4; i++) {
        loadObjModel(vertices, indices, objPaths[i], highLodIndexCount[i], highLodFirstIndex[i], highLodVertexOffset[i]);
    }

    // 2. 중간 화질(Mid LOD): 타입별 원본을 정점 클러스터링으로 단순화한다.
    //    실루엣이 바위로 남아 LOD 0에서 넘어올 때 형태가 튀지 않는다.
    for (int i = 0; i < 4; i++) {
        buildSimplifiedLod(vertices, indices, highLodFirstIndex[i], highLodIndexCount[i], highLodVertexOffset[i], 12,
                           midLodIndexCount[i], midLodFirstIndex[i], midLodVertexOffset[i]);
    }

    // 3. 최하 화질(Low LOD): 같은 방식으로 훨씬 거칠게 줄인다.
    for (int i = 0; i < 4; i++) {
        buildSimplifiedLod(vertices, indices, highLodFirstIndex[i], highLodIndexCount[i], highLodVertexOffset[i], 5,
                           lowLodIndexCount[i], lowLodFirstIndex[i], lowLodVertexOffset[i]);
    }

    // 엔진 필수 리소스 초기화 (삭제 불가)
    createCubeTextureImage();
    createTextureSampler();
    createColorTexture(0, 0, 0, 0, texDummyBlack, memDummyBlack, viewDummyBlack, VK_FORMAT_R8G8B8A8_UNORM);
    createColorTexture(128, 128, 255, 255, texDummyFlatNormal, memDummyFlatNormal, viewDummyFlatNormal, VK_FORMAT_R8G8B8A8_UNORM);

    // 4. 2만 개의 소행성 데이터 수학적 생성 (다중 모델 부여 포함) 및 SSBO 업로드
    // 반지름은 기본 축척 기준이고, 마지막 인자가 실제 축척으로 갈 때의 위치 배율이다.
    // 실제 축척은 1 AU = 500 유닛이므로 소행성대 2.06~3.28 AU = 1030~1640,
    // 카이퍼벨트 30~50 AU = 15000~25000이 되도록 배율을 각각 118, 242로 잡았다.
    // 기본 축척 반지름은 그 배율로 나눈 값이라 두 모드가 동시에 맞는다.
    //
    // 크기 0.0039~0.025는 실제 축척에서 지름 100~637 km다. 거듭제곱 분포로 뽑으므로
    // 중앙값은 약 130 km이고 베스타급(525 km)은 드물게 나온다.
    asteroidTransforms = generateAsteroidBelt(2400, 8.73f, 13.90f, 0.175f, 0.0039f, 0.025f, 118.0f);

    // 카이퍼벨트는 궤도 경사가 두 갈래다. 5도 미만의 얇은 무리(cold)와 20도 안팎까지
    // 퍼진 무리(hot)가 섞여 있어, 옆에서 보면 얇은 심 위에 두꺼운 후광이 얹힌 모양이다.
    // 하나의 분포로 뽑으면 그 구조가 사라진다. 섞은 평균이 실제값 12도에 맞도록 나눴다.
    // 크기 0.0118~0.08은 지름 300~2040 km다. 중앙값은 약 394 km이고 에리스급(2326 km)은
    // 드물게 나온다. 개수를 10,000으로 잡아도 부피가 소행성대의 4,428배라 훨씬 성기다.
    for (auto [count, incl] : { std::pair<int, float>{4000, 0.035f}, {6000, 0.31f} }) {
        auto kuiper = generateAsteroidBelt(count, 62.0f, 103.3f, incl, 0.0118f, 0.08f, 242.0f);
        asteroidTransforms.insert(asteroidTransforms.end(), kuiper.begin(), kuiper.end());
    }
    
    // 파이프라인 및 버퍼 초기화 (삭제 불가)
    createVertexBuffer(); createIndexBuffer(); createUniformBuffer();
    createLockedOrbitBuffer();
    createDescriptorSetLayout(); createDescriptorPool();

    // 🚀 [수정] 반드시 풀(Pool)과 유니폼 버퍼가 생성된 "다음"에 Compute 자원을 만들어야 합니다!
    createComputeResources();
    createOffscreenResources();
    createBlurResources();
    createBlurPipeline();
    createPostProcessPipeline();

    // =========================================================
    // 🚀 [태양계 전면 재설계] 영역(Domain) 침범 방지 및 연쇄 확장 시스템
    // 1. 목성을 22.0으로 밀어내어 위성-소행성대 충돌 원천 차단
    // 2. 외행성들의 거리를 비례에 맞춰 확장 (해왕성 62.0)
    // 3. 카이퍼벨트(왜소행성)를 해왕성 밖(75.0 ~ 100.0)으로 완벽히 밀어냄
    // =========================================================
    
    sun = createPlanet("Sun", 4, 1.80f, 0.0f, 0.0f, 0.0f, 0.9f, 7.25f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, "textures/sun.jpg", "", "", "", "");
    
    // 1. 내행성 (수~화)
    planets.push_back(createPlanet("Mercury", 0, 0.11f, 3.50f, 15.00f, 0.205f, 0.43f, 0.03f, 7.0f, 77.0f, 48.0f, 120.0f, 45.0f, false, "textures/planets/8k_mercury.jpg", "", "", "textures/planets/mercury_normal.png", ""));  // index 0
    planets.push_back(createPlanet("Venus", 0, 0.19f, 4.80f, 11.00f, 0.006f, 0.1f, 177.36f, 3.4f, 131.0f, 76.0f, 30.0f, 100.0f, true, "textures/planets/8k_venus_surface.jpg", "", "", "textures/planets/venus_normal.png", "textures/planets/venus_atmosphere.jpg"));  // index 1
    planets.push_back(createPlanet("Earth", 0, 0.20f, 6.00f, 10.00f, 0.016f, 25.0f, 23.44f, 0.0f, 102.0f, 0.0f, 200.0f, 0.0f, true, "textures/planets/earth_diffuse.jpg", "textures/planets/earth_night.jpg", "textures/planets/8k_earth_specular.png", "textures/planets/earth_normal.png", "textures/planets/earth_clouds.jpg"));  // index 2
    planets.push_back(createPlanet("Mars", 0, 0.14f, 7.50f, 8.50f, 0.093f, 24.3f, 25.19f, 1.85f, 336.0f, 49.0f, 310.0f, 210.0f, false, "textures/planets/8k_mars.jpg", "", "", "textures/planets/mars_normal.png", ""));  // index 3
    
    // 2. 소행성대 (세레스가 11.0에서 기준선 역할을 합니다)
    planets.push_back(createPlanet("Ceres", 1, 0.04f, 11.00f, 6.50f, 0.076f, 66.6f, 4.0f, 10.6f, 73.0f, 80.0f, 0.0f, 0.0f, false, "textures/asteroids/ceres.jpg", "", "", "", ""));  // index 4
    
    // 3. 외행성 (목성계 확장에 따른 연쇄 이동 적용)
    planets.push_back(createPlanet("Jupiter", 9, 0.80f, 22.00f, 4.50f, 0.048f, 60.6f, 3.13f, 1.3f, 14.0f, 100.0f, 45.0f, 30.0f, false, "textures/planets/jupiter.jpg", "", "", "", "")); // index 5 (위성 부모)
    // 마지막 인자(clouds 슬롯)에 고리 텍스처를 넣는다. 가스 행성은 구름 맵을 쓰지 않으므로
    // 이 슬롯이 비어 있고, 셰이더가 여기서 고리 밀도를 읽어 본체에 그림자를 드리운다.
    planets.push_back(createPlanet("Saturn", 9, 0.70f, 34.00f, 3.30f, 0.056f, 56.0f, 26.73f, 2.49f, 92.0f, 113.0f, 150.0f, 120.0f, false, "textures/planets/8k_saturn.jpg", "", "", "", "textures/planets/8k_saturn_ring_alpha.png"));  // index 6 (타이탄 부모)
    planets.push_back(createPlanet("Uranus", 9, 0.40f, 48.00f, 2.50f, 0.046f, 34.8f, 97.77f, 0.77f, 170.0f, 74.0f, 280.0f, 250.0f, false, "textures/planets/uranus.jpg", "", "", "", ""));  // index 7
    planets.push_back(createPlanet("Neptune", 9, 0.39f, 62.00f, 2.00f, 0.009f, 37.2f, 28.32f, 1.77f, 44.0f, 131.0f, 90.0f, 80.0f, false, "textures/planets/4k_neptune.jpg", "", "", "", ""));  // index 8
    
    // 4. 달 및 토성 고리
    // 노말맵을 LOLA 실측 DEM(118m)에서 구운 것으로 교체. 예전 moon_normal.jpg는 알베도에서
    // 뽑은 가짜라 크레이터 바닥의 어둠을 요철로 착각했다. normalAmp는 아래 realScale 블록에서 설정.
    moon = createPlanet("Moon", 1, 0.08f, 0.75f, 45.0f, 0.054f, 45.0f, 6.68f, 5.14f, 318.0f, 125.0f, 45.0f, 0.0f, false, "textures/moons/8k_moon.jpg", "", "", "textures/moons/moon_normal.png", "");
    saturnRing = createPlanet("SaturnRing", 5, 1.60f, 0.0f, 0.0f, 0.0f, 0.0f, 26.73f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, "textures/planets/8k_saturn_ring_alpha.png", "", "", "", "");
    
    // 5. 카이퍼 벨트 및 왜소행성 (외행성들이 밀려난 만큼, 바깥쪽 영역(75~100)으로 완벽하게 밀려납니다)
    planets.push_back(createPlanet("Pluto", 1, 0.07f, 75.00f, 1.70f, 0.248f, 3.9f, 122.5f, 17.1f, 113.8f, 110.0f, 90.0f, 0.0f, false, "textures/asteroids/pluto.jpg", "", "", "", "")); // index 9
    planets.push_back(createPlanet("Haumea", 1, 0.06f, 80.00f, 1.60f, 0.196f, 153.8f, 28.0f, 28.2f, 239.0f, 122.0f, 45.0f, 0.0f, false, "textures/asteroids/haumea.jpg", "", "", "", "")); // index 10
    planets.push_back(createPlanet("Makemake", 1, 0.05f, 85.00f, 1.50f, 0.161f, 26.3f, 29.0f, 29.0f, 295.0f, 79.0f, 120.0f, 0.0f, false, "textures/asteroids/makemake.jpg", "", "", "", "")); // index 11
    planets.push_back(createPlanet("Eris", 1, 0.07f, 100.00f, 1.30f, 0.436f, 23.1f, 78.0f, 44.0f, 44.0f, 36.0f, 200.0f, 0.0f, false, "textures/asteroids/eris.jpg", "", "", "", "")); // index 12

    // 6. 목성 & 토성 위성 (목성계 안전 반경 내 공전)
    Planet io = createPlanet("Io", 1, 0.09f, 1.2f, 40.0f, 0.004f, 40.0f, 0.0f, 0.04f, 0.0f, 0.0f, 0.0f, 0.0f, false, "textures/moons/4k_io.jpg", "textures/moons/io_glow.png", "", "", ""); 
    io.parentIndex = 5; planets.push_back(io); 
    
    Planet europa = createPlanet("Europa", 1, 0.08f, 1.9f, 20.0f, 0.009f, 20.0f, 0.0f, 0.47f, 0.0f, 0.0f, 45.0f, 0.0f, false, "textures/moons/4k_europa.jpg", "", "", "", ""); 
    europa.parentIndex = 5; planets.push_back(europa);
    
    Planet ganymede = createPlanet("Ganymede", 1, 0.12f, 3.0f, 10.0f, 0.001f, 10.0f, 0.0f, 0.2f, 0.0f, 0.0f, 90.0f, 0.0f, false, "textures/moons/4k_ganymede.jpg", "", "", "", ""); 
    ganymede.parentIndex = 5; planets.push_back(ganymede);
    
    Planet callisto = createPlanet("Callisto", 1, 0.11f, 5.4f, 4.2f, 0.007f, 4.2f, 0.0f, 0.28f, 0.0f, 0.0f, 135.0f, 0.0f, false, "textures/moons/4k_callisto.jpg", "", "", "", ""); 
    callisto.parentIndex = 5; planets.push_back(callisto);

    Planet titan = createPlanet("Titan", 1, 0.11f, 2.0f, 5.0f, 0.028f, 5.0f, 0.0f, 0.33f, 0.0f, 0.0f, 0.0f, 0.0f, true, "textures/moons/4k_titan.jpg", "", "", "", "textures/moons/titan_atmosphere.jpg"); 
    titan.parentIndex = 6; planets.push_back(titan);
    
    // 6. 4가지 소행성 개별 텍스처 로딩 (기존 asteroidBelt 대체)
    std::string texPaths[4] = {
        "textures/asteroids/Bennu.jpg",
        "textures/asteroids/Gaspra.jpg",
        "textures/asteroids/Ida.jpg",
        "textures/asteroids/Itokawa.jpg"
    };
    for (int i = 0; i < 4; i++) {
        // 🚀 [수정됨] 소행성은 궤도 연산을 SSBO에서 따로 하므로, 추가된 5대 요소 자리에 0.0f 5개를 빵꾸울워 줍니다!
        asteroidTypes[i] = createPlanet("AsteroidType" + std::to_string(i), 8, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, texPaths[i], "", "", "", "");
    }

    // =========================================================
    // 🚀 [NEW] 리얼 스케일(Real Scale) 타겟 데이터 주입
    // =========================================================
    sun.realRadius = 54.0f; sun.realOrbit = 0.0f;
    moon.realRadius = 0.13f; moon.realOrbit = 1.5f;
    moon.shadowSunShrink = SHADOW_SUN_SHRINK_MOON;
    moon.normalAmp = 5.0f / 4.0f;  // LOLA DEM 맵을 4배 증폭해 구웠으므로(다른 실측 맵과 동일)
    for (auto& p : planets) {
        // 실측 DEM에서 구운 normal 맵은 4배(금성은 7배) 미리 증폭해 두었으므로 5/S를 넘긴다.
        if (p.name == "Mercury") { p.realRadius = 0.19f; p.realOrbit = 195.0f; p.normalAmp = 5.0f / 4.0f; }
        // 금성은 7배로 굽고 나서 마젤란 격자 잡음을 걷어내려 1픽셀 평활화를 했고,
        // 그때 좁아진 8비트 범위를 1.6배로 되돌려 채웠다. 그래서 총 증폭이 7 x 1.6 = 11.2다.
        else if (p.name == "Venus") { p.realRadius = 0.47f; p.realOrbit = 360.0f; p.normalAmp = 5.0f / 11.2f; }
        else if (p.name == "Earth") { p.realRadius = 0.50f; p.realOrbit = 500.0f; p.hasAtmosphere = true; p.normalAmp = 5.0f / 4.0f; }
        else if (p.name == "Mars") { p.realRadius = 0.26f; p.realOrbit = 760.0f; p.normalAmp = 5.0f / 4.0f; }
        else if (p.name == "Ceres") { p.realRadius = 0.03f; p.realOrbit = 1385.0f; }
        else if (p.name == "Jupiter") { p.realRadius = 5.60f; p.realOrbit = 2600.0f; }
        else if (p.name == "Saturn") { p.realRadius = 4.72f; p.realOrbit = 4790.0f; }
        else if (p.name == "Uranus") { p.realRadius = 2.00f; p.realOrbit = 9600.0f; }
        else if (p.name == "Neptune") { p.realRadius = 1.94f; p.realOrbit = 15050.0f; }
        else if (p.name == "Pluto") { p.realRadius = 0.09f; p.realOrbit = 19750.0f; }
        else if (p.name == "Haumea") { p.realRadius = 0.06f; p.realOrbit = 21550.0f; }
        else if (p.name == "Makemake") { p.realRadius = 0.05f; p.realOrbit = 22650.0f; }
        else if (p.name == "Eris") { p.realRadius = 0.09f; p.realOrbit = 34000.0f; }
        else if (p.name == "Io") { p.realRadius = 0.14f; p.realOrbit = 6.2f; p.shadowSunShrink = 2.1f; }  // 실제 비 5.82 
        else if (p.name == "Europa") { p.realRadius = 0.12f; p.realOrbit = 9.8f; p.shadowSunShrink = 3.3f; }  // 실제 비 2.91
        else if (p.name == "Ganymede") { p.realRadius = 0.20f; p.realOrbit = 15.7f; p.shadowSunShrink = 4.4f; }  // 실제 비 2.95
        else if (p.name == "Callisto") { p.realRadius = 0.18f; p.realOrbit = 27.6f; p.shadowSunShrink = 5.1f; }  // 실제 비 1.49 - 본영이 목성에 겨우 닿는다
        else if (p.name == "Titan") { p.realRadius = 0.20f; p.realOrbit = 12.2f; p.shadowSunShrink = 2.9f; }  // 실제 비 4.57
    }

    createGraphicsPipeline();
    initImGui();
}

Planet SolarSystemApp::createPlanet(std::string name, int typeId, float radius, float orbitRadius, float orbitSpeed, float eccentricity, float rotSpeed, float axialTilt, float orbIncl, float periAngle, float ascNode, float initAngle, float axisDir, bool hasClouds, std::string diffuse, std::string night, std::string spec, std::string normal, std::string clouds) {
    Planet p; p.name = name; p.typeId = typeId; p.radius = radius; p.orbitRadius = orbitRadius; p.orbitSpeed = orbitSpeed; p.eccentricity = eccentricity; p.rotationSpeed = rotSpeed; p.axialTilt = axialTilt; p.hasClouds = hasClouds;
    
    // 새로 추가된 5대 요소 맵핑
    p.orbitalInclination = orbIncl; p.periapsisAngle = periAngle; p.ascendingNode = ascNode; p.initialAngle = initAngle; p.axisDirection = axisDir;

    p.viewDiffuse = loadTexture(diffuse, VK_FORMAT_R8G8B8A8_SRGB); p.viewNight = loadTexture(night, VK_FORMAT_R8G8B8A8_SRGB); p.viewSpecular = loadTexture(spec, VK_FORMAT_R8G8B8A8_UNORM); p.viewNormal = loadTexture(normal, VK_FORMAT_R8G8B8A8_UNORM); p.viewClouds = loadTexture(clouds, VK_FORMAT_R8G8B8A8_SRGB);
    VkDescriptorSetAllocateInfo allocInfo{}; allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; allocInfo.descriptorPool = descriptorPool; allocInfo.descriptorSetCount = 1; allocInfo.pSetLayouts = &descriptorSetLayout;
    vkAllocateDescriptorSets(device, &allocInfo, &p.descriptorSet);
    
    VkDescriptorBufferInfo bInfo{}; bInfo.buffer = uniformBuffer; bInfo.offset = 0; bInfo.range = sizeof(UniformBufferObject);
    auto mkImgInfo = [&](VkImageView v) { VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = textureSampler; return i; };
    
    VkDescriptorImageInfo iDiff = mkImgInfo(p.viewDiffuse), iNight = mkImgInfo(p.viewNight), iSpec = mkImgInfo(p.viewSpecular), iNorm = mkImgInfo(p.viewNormal), iCloud = mkImgInfo(p.viewClouds), iSky = mkImgInfo(viewSkybox);
    // 은하수 층이 없으면(EXR 스카이박스로 물러난 경우) 검은 더미를 넣는다.
    // 셰이더는 이 층을 더하기만 하므로 0이면 예전과 같은 결과가 나온다.
    VkDescriptorImageInfo iBand = mkImgInfo(viewSkyBand != VK_NULL_HANDLE ? viewSkyBand : viewDummyBlack);

    VkDescriptorBufferInfo ssboInfo{}; ssboInfo.buffer = computeOutputBuffer; ssboInfo.offset = 0; ssboInfo.range = VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet, 9> writes{};
    for (int i = 0; i < 9; i++) { writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet = p.descriptorSet; writes[i].dstBinding = i; writes[i].descriptorCount = 1; }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[0].pBufferInfo = &bInfo;
    for (int i = 1; i < 7; i++) writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &iDiff; writes[2].pImageInfo = &iNight; writes[3].pImageInfo = &iSpec; writes[4].pImageInfo = &iNorm; writes[5].pImageInfo = &iCloud; writes[6].pImageInfo = &iSky;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[7].pBufferInfo = &ssboInfo;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[8].pImageInfo = &iBand;

    vkUpdateDescriptorSets(device, 9, writes.data(), 0, nullptr); return p;
}

void SolarSystemApp::cleanupApp() {
    ImGui_ImplVulkan_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext(); vkDestroyDescriptorPool(device, imguiPool, nullptr);
    
    // [NEW] SSBO 버퍼 반환 
    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
    vkDestroyBuffer(device, computeInputBuffer, nullptr); vkFreeMemory(device, computeInputMem, nullptr);
    vkDestroyBuffer(device, computeOutputBuffer, nullptr); vkFreeMemory(device, computeOutputMem, nullptr);
    vkDestroyBuffer(device, indirectDrawBuffer, nullptr); vkFreeMemory(device, indirectDrawMem, nullptr);


    vkDestroySampler(device, textureSampler, nullptr);
    for (auto view : allViews) vkDestroyImageView(device, view, nullptr);
    for (auto img : allImages) vkDestroyImage(device, img, nullptr);
    for (auto mem : allMemories) vkFreeMemory(device, mem, nullptr);

    vkDestroyPipeline(device, bloomDownPipeline, nullptr); vkDestroyPipeline(device, bloomUpPipeline, nullptr);
    vkDestroyPipelineLayout(device, blurPipelineLayout, nullptr); vkDestroyDescriptorSetLayout(device, blurDescriptorSetLayout, nullptr);
    vkDestroyRenderPass(device, blurRenderPass, nullptr); vkDestroyRenderPass(device, bloomUpPass, nullptr);
    for (int i = 0; i < bloomMipCount; i++) { vkDestroyFramebuffer(device, bloomFramebuffers[i], nullptr); vkDestroyImageView(device, bloomMipViews[i], nullptr); }
    vkDestroyImage(device, bloomImage, nullptr); vkFreeMemory(device, bloomMem, nullptr);
    
    vkDestroyPipeline(device, postPipeline, nullptr); vkDestroyPipelineLayout(device, postPipelineLayout, nullptr); vkDestroyDescriptorSetLayout(device, postDescriptorSetLayout, nullptr); vkDestroyFramebuffer(device, offscreenFramebuffer, nullptr); vkDestroyRenderPass(device, offscreenRenderPass, nullptr); vkDestroySampler(device, offscreenSampler, nullptr);
    vkDestroyImageView(device, offscreenColorView, nullptr); vkDestroyImage(device, offscreenColorImage, nullptr); vkFreeMemory(device, offscreenColorMem, nullptr);
    vkDestroyImageView(device, offscreenBrightView, nullptr); vkDestroyImage(device, offscreenBrightImage, nullptr); vkFreeMemory(device, offscreenBrightMem, nullptr);
    vkDestroyImageView(device, offscreenDepthView, nullptr); vkDestroyImage(device, offscreenDepthImage, nullptr); vkFreeMemory(device, offscreenDepthMem, nullptr);
    vkDestroyImageView(device, msaaColorView, nullptr); vkDestroyImage(device, msaaColorImage, nullptr); vkFreeMemory(device, msaaColorMem, nullptr);
    vkDestroyImageView(device, msaaBrightView, nullptr); vkDestroyImage(device, msaaBrightImage, nullptr); vkFreeMemory(device, msaaBrightMem, nullptr);

    profiler.destroy(device);

    vkDestroyImageView(device, viewSkybox, nullptr); vkDestroyImage(device, texSkybox, nullptr); vkFreeMemory(device, memSkybox, nullptr);
    if (viewSkyBand != VK_NULL_HANDLE) {
        vkDestroyImageView(device, viewSkyBand, nullptr); vkDestroyImage(device, texSkyBand, nullptr); vkFreeMemory(device, memSkyBand, nullptr);
    }
    vkDestroyImageView(device, viewDummyBlack, nullptr); vkDestroyImage(device, texDummyBlack, nullptr); vkFreeMemory(device, memDummyBlack, nullptr);
    vkDestroyImageView(device, viewDummyFlatNormal, nullptr); vkDestroyImage(device, texDummyFlatNormal, nullptr); vkFreeMemory(device, memDummyFlatNormal, nullptr);
    vkDestroyBuffer(device, indexBuffer, nullptr); vkFreeMemory(device, indexBufferMemory, nullptr);
    vkDestroyBuffer(device, vertexBuffer, nullptr); vkFreeMemory(device, vertexBufferMemory, nullptr);
    vkDestroyBuffer(device, uniformBuffer, nullptr); vkFreeMemory(device, uniformBufferMemory, nullptr);
    vkDestroyBuffer(device, lockedOrbitBuffer, nullptr); vkFreeMemory(device, lockedOrbitMemory, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr); vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    vkDestroyPipeline(device, graphicsPipeline, nullptr); vkDestroyPipeline(device, linePipeline, nullptr); vkDestroyPipeline(device, atmospherePipeline, nullptr); vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
}
