#include "SolarSystemApp.hpp"

void SolarSystemApp::recordCompute(VkCommandBuffer cb, float easeScale, glm::mat4 astRot, VkExtent2D renderExtent) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet, 0, nullptr);
    // 정점 셰이더가 쓰는 것과 동일한 행렬을 넘겨, 컴퓨트가 렌더 좌표계에서 거리를 재게 한다.
    AsteroidCullPush cullPush{astRot, static_cast<uint32_t>(asteroidTransforms.size()), easeScale};
    vkCmdPushConstants(cb, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(cullPush), &cullPush);

    uint32_t groupCount = (static_cast<uint32_t>(asteroidTransforms.size()) + 255) / 256;
    vkCmdDispatch(cb, groupCount, 1, 1);

    VkMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; 
    // HOST_READ는 계측이 인스턴스 수를 CPU에서 읽기 위해 필요하다(HOST_COHERENT 메모리라도
    // 배리어 없이는 가시성이 보장되지 않는다). 계측이 꺼져 있으면 비용은 무시할 수준이다.
    barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    VkBuffer vertexBuffers[] = {vertexBuffer}; VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets); vkCmdBindIndexBuffer(cb, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
}

void SolarSystemApp::recordAsteroids(VkCommandBuffer cb, float easeScale, glm::mat4 astRot, VkExtent2D renderExtent) {
    // vertexBuffers/offsets: recordCompute에서 만든 것과 같은 값. 지역 배열이라 함수 경계를 못 넘으므로 다시 선언한다.
    VkBuffer vertexBuffers[] = {vertexBuffer}; VkDeviceSize offsets[] = {0};
    setViewport(cb, renderExtent);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets); vkCmdBindIndexBuffer(cb, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // easeScale / beltScale / astRot은 컴퓨트 디스패치 직전에 이미 만들어 두었다(위 참조).
    PushConstants pcAst{astRot, 8};
    vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pcAst);

    if (settings.asteroids) {
        for (int type = 0; type < 4; ++type) {
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &asteroidTypes[type].descriptorSet, 0, nullptr);
            for (int lod = 0; lod < 3; ++lod) {
                int bucketIndex = lod * 4 + type;
                VkDeviceSize offset = bucketIndex * sizeof(VkDrawIndexedIndirectCommand);
                vkCmdDrawIndexedIndirect(cb, indirectDrawBuffer, offset, 1, sizeof(VkDrawIndexedIndirectCommand));
            }
        }
    }

}

void SolarSystemApp::recordBodies(VkCommandBuffer cb) {
    auto drawObject = [&](const Planet &p, glm::mat4 mat, int type) {
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &p.descriptorSet, 0, nullptr);
        PushConstants pc{mat, type, p.normalAmp}; vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdDrawIndexed(cb, sphereIndexCount, 1, 0, 0, 0); 
    };

    glm::vec3 currentCameraPos = glm::vec3(camera.getEyeWorld());
    float distToSun = glm::length(currentCameraPos - glm::vec3(sun.currentPosition));
    bool shouldRenderSun = distToSun > (sun.radius * 2.6f);

    if (shouldRenderSun) {
        drawObject(sun, sun.currentModelMat, sun.typeId); 
    }

    // 카메라가 천체 안에 들어갔을 때만 건너뛴다.
    //
    // 예전에는 기준이 planet.radius * 1.2f 였는데, 카메라의 최소 거리도 같은 식
    // (targetRadius * 1.2f)이라 기본 축척에서는 두 값이 정확히 같아졌다. 확대해서
    // 최소 거리에 도달하면 부동소수점 잡음만으로 판정이 매 프레임 뒤집혀 행성이
    // 통째로 명멸했다. 실제 축척에서는 최소 거리가 realRadius 기준이라 증상이 없었다.
    // 이제는 실제로 그려지는 반지름을 쓰고, 최소 거리보다 20% 안쪽에서만 걸린다.
    for (const auto &planet : planets) {
        float distToPlanet = glm::length(currentCameraPos - glm::vec3(planet.currentPosition));
        if (distToPlanet > planet.currentRenderRadius) {
            drawObject(planet, planet.currentModelMat, planet.typeId);
            if (planet.hasClouds) drawObject(planet, planet.cloudModelMat, 2);
            if (planet.name == "Saturn") {
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &saturnRing.descriptorSet, 0, nullptr);
                PushConstants ringPush{planet.currentModelMat, 5}; 
                vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &ringPush);
                vkCmdDrawIndexed(cb, ringIndexCount, 1, sphereIndexCount, ringVertexOffset, 0);
            }
        }
    }

    float distToMoon = glm::length(currentCameraPos - glm::vec3(moon.currentPosition));
    if (distToMoon > moon.currentRenderRadius) {
        drawObject(moon, moon.currentModelMat, moon.typeId);
    }
}

void SolarSystemApp::recordSky(VkCommandBuffer cb, float easeScale) {
    // 스카이박스는 태양의 디스크립터 세트를 빌려 쓴다(큐브맵은 어느 세트에나 같이 들어 있다).
    // drawObject를 쓰지 않고 푸시 상수를 직접 만드는 이유: customData에 하늘 전환값을
    // 실어야 하기 때문이다. 타입 3 분기는 customData를 달리 쓰지 않으므로 자리가 비어 있다.
    // 이 값으로 셰이더가 '사진 같은 하늘'과 '맨눈으로 본 하늘' 사이를 오간다.
    //
    // 축척(easeScale)이 아니라 별도의 Real Stars 토글을 쓴다. 하늘의 사실성과 거리
    // 축척은 서로 다른 축이라, 압축 축척으로 태양계 전체를 보면서도 실제 별하늘을
    // 볼 수 있어야 한다.
    {
        float easeStars = starsLerp * starsLerp * (3.0f - 2.0f * starsLerp);
        glm::mat4 skyMat = glm::rotate(glm::mat4(1.0f), currentAppTime * glm::radians(0.5f), glm::vec3(0.0f, 1.0f, 0.0f));
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sun.descriptorSet, 0, nullptr);
        PushConstants skyPush{skyMat, 3, easeStars};
        vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &skyPush);
        vkCmdDrawIndexed(cb, sphereIndexCount, 1, 0, 0, 0);

        // 별자리 선: 스카이박스와 같은 skyMat(단위구, 카메라 회전만 따라감)을 모델로 쓴다.
        // vertexBuffers/offsets: recordOrbits 패턴과 동일하게, 그리고 나면 원래 버텍스 버퍼로 복구한다.
        if (constLineVertexCount > 0) {
            VkBuffer vertexBuffers[] = {vertexBuffer}; VkDeviceSize offsets[] = {0};
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, constellationPipeline);
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &constLineBuffer, &off);
            // 배경 하늘 큐브맵을 raDecToDir 규약과 일치하게 다시 구웠으므로(skybox-cube-convention),
            // 별자리 정점(raDecToDir)이 스카이박스와 같은 skyMat만으로 별 위에 정확히 얹힌다.
            PushConstants cp{skyMat, 12, 1.0f}; // customData=1.0 (완전 불투명)
            vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &cp);
            vkCmdDraw(cb, constLineVertexCount, 1, 0, 0);
            // 뒤(대기 등)가 기존 버텍스 버퍼를 기대하므로 복구
            vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        }
    }
}

void SolarSystemApp::recordAtmosphere(VkCommandBuffer cb) {
    // drawObject/currentCameraPos: recordBodies에 있던 것과 동일. cb가 함수마다 별도 매개변수라 공유할 수 없어 다시 선언한다.
    auto drawObject = [&](const Planet &p, glm::mat4 mat, int type) {
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &p.descriptorSet, 0, nullptr);
        PushConstants pc{mat, type, p.normalAmp}; vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdDrawIndexed(cb, sphereIndexCount, 1, 0, 0, 0); 
    };
    glm::vec3 currentCameraPos = glm::vec3(camera.getEyeWorld());
    // 여기에는 멀리 축소했을 때 떠오르던 은하 원반(Milkyway.png를 입힌 판)이 있었다.
    // 하늘 큐브맵이 은하 안에서 밖을 본 모습인데 원반은 은하를 밖에서 본 모습이라,
    // 둘이 동시에 보이면 관측자가 은하 안에도 밖에도 있는 셈이 된다. 게다가 원반은
    // 1229x1229짜리 그림 한 장이라 실측 성도와 나란히 두기에 근거가 약했다.

    // 대기 껍질은 배경이 전부 깔린 뒤에 그려야 한다.
    //
    // 껍질은 일부러 깊이를 쓰지 않는다(자기보다 작은 행성 뒤의 것들을 삼키지 않으려고).
    // 그런데 스카이박스는 천체보다 나중에, 원거리 깊이로, 알파 1로 그려진다. 그래서
    // 행성 실루엣 바깥의 발광 영역은 깊이가 비어 있는 탓에 스카이박스가 통과해 덮어버렸다.
    // 껍질을 만든 이유가 바로 그 바깥 발광인데 그게 통째로 지워지고 있었다.
    //
    // 유일하게 살아남던 곳이 소행성 위였다. 소행성은 깊이를 쓰므로 스카이박스가 거기서만
    // 깊이 테스트에 걸렸고, 그 결과 소행성만 빛나는 점으로 남았다.
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, atmospherePipeline);
    for (const auto &planet : planets) {
        if (!planet.hasAtmosphere) continue;
        float d = glm::length(currentCameraPos - glm::vec3(planet.currentPosition));
        // 껍질 안쪽에 들어갔을 때만 건너뛴다(본체와 같은 이유로 기준을 바꿨다).
        if (d > planet.currentRenderRadius * kAtmosphereShellScale) drawObject(planet, planet.atmosphereModelMat, 11);
    }
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
}

void SolarSystemApp::recordOrbits(VkCommandBuffer cb) {
    // vertexBuffers/offsets: 고정 궤도선을 그린 뒤 원래 버텍스 버퍼로 복구하는 데 쓰인다. 지역 배열이라 다시 선언한다.
    VkBuffer vertexBuffers[] = {vertexBuffer}; VkDeviceSize offsets[] = {0};
    if (showOrbits) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sun.descriptorSet, 0, nullptr);
        for (int pi = 0; pi < (int)planets.size(); ++pi) {
            const auto &planet = planets[pi];
            // 고정된 천체의 궤도는 아래에서 고정밀 버퍼로 따로 그린다.
            if (lockedOrbitValid && lockedTargetType == 1 && pi == lockedPlanetIndex) continue;
            float curOrbit = glm::mix(planet.orbitRadius, planet.realOrbit, scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp));
            float a = curOrbit, e = planet.eccentricity, b = a * sqrt(1.0f - e * e), c = a * e; 
            glm::mat4 orbitTilt = glm::rotate(glm::mat4(1.0f), glm::radians(planet.ascendingNode), glm::vec3(0.0f, 1.0f, 0.0f))
                                * glm::rotate(glm::mat4(1.0f), glm::radians(planet.orbitalInclination), glm::vec3(0.0f, 0.0f, 1.0f))
                                * glm::rotate(glm::mat4(1.0f), glm::radians(planet.periapsisAngle), glm::vec3(0.0f, 1.0f, 0.0f));
                                
            glm::dvec3 orbitCenterWorld = glm::dvec3(0.0);
            if (planet.parentIndex != -1) orbitCenterWorld = planets[planet.parentIndex].currentPosition;
            glm::mat4 orbitCenter = glm::translate(glm::mat4(1.0f), relativeToCamera(orbitCenterWorld));
                                
            glm::mat4 orbitModel = orbitCenter * orbitTilt * glm::translate(glm::mat4(1.0f), glm::vec3(-c, 0.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(a, 1.0f, b));
            PushConstants orbitPush{orbitModel, 6}; vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &orbitPush);
            vkCmdDrawIndexed(cb, orbitIndexCount, 1, orbitFirstIndex, orbitVertexOffset, 0);
        }

        // 고정된 천체의 궤도선: 이미 카메라 상대 좌표로 구워져 있으므로 모델 행렬은 단위 행렬.
        if (lockedOrbitValid) {
            VkDeviceSize lockedOffset = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &lockedOrbitBuffer, &lockedOffset);
            PushConstants lockedPush{glm::mat4(1.0f), 6};
            vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &lockedPush);
            vkCmdDraw(cb, LOCKED_ORBIT_SEGMENTS + 1, 1, 0, 0);
            vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets);
        }
    
        float curMoonOrbit = glm::mix(0.75f, moon.realOrbit, scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp));
        float ma = curMoonOrbit, me = moon.eccentricity, mb = ma * sqrt(1.0f - me * me), mc = ma * me;
        glm::mat4 mOrbitTilt = glm::rotate(glm::mat4(1.0f), glm::radians(moon.ascendingNode), glm::vec3(0.0f, 1.0f, 0.0f))
                             * glm::rotate(glm::mat4(1.0f), glm::radians(moon.orbitalInclination), glm::vec3(0.0f, 0.0f, 1.0f))
                             * glm::rotate(glm::mat4(1.0f), glm::radians(moon.periapsisAngle), glm::vec3(0.0f, 1.0f, 0.0f));
                             
        glm::mat4 moonOrbitModel = glm::translate(glm::mat4(1.0f), relativeToCamera(planets[2].currentPosition)) * mOrbitTilt * glm::translate(glm::mat4(1.0f), glm::vec3(-mc, 0.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(ma, 1.0f, mb));
        
        PushConstants moonOrbitPush{moonOrbitModel, 6}; 
        vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &moonOrbitPush);
        vkCmdDrawIndexed(cb, orbitIndexCount, 1, orbitFirstIndex, orbitVertexOffset, 0);
    }

    // =========================================================
    // 🚀 [중요] 궤도선을 모두 그린 후, 태양을 그리기 위해 면(Triangle) 파이프라인으로 완벽 복구
    // =========================================================
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
}

void SolarSystemApp::recordSun(VkCommandBuffer cb) {
    // currentCameraPos/distToSun/shouldRenderSun: recordBodies에서 이미 계산한 것과 동일한 값. cb가 함수마다
    // 별도 매개변수라 공유할 수 없어 재계산한다(카메라/태양 상태는 이번 프레임 동안 바뀌지 않는다).
    glm::vec3 currentCameraPos = glm::vec3(camera.getEyeWorld());
    float distToSun = glm::length(currentCameraPos - glm::vec3(sun.currentPosition));
    bool shouldRenderSun = distToSun > (sun.radius * 2.6f);
    // 3. 거대한 태양 홍염 (Type 7) - 가까우면 숨김
    if (shouldRenderSun) {
        // 🚀 [수정됨] 태양 홍염도 리얼 스케일에 맞춰 거대하게 팽창합니다.
        float easeScale = scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp); 
        float curSunRadius = glm::mix(sun.radius, sun.realRadius, easeScale);
        
        // 돔 배율 2.5. 홍염 자체는 r <= 1.48까지만 존재하지만, 돔을 1.7로 줄여봤을 때
        // 성능 차이가 측정 노이즈 수준이었다(셰이더의 r > 1.6 조기 discard가 이미
        // 바깥 픽셀을 몇 연산 만에 버리기 때문). 그래서 원래 값을 유지한다.
        // 이 값을 바꾸면 shader.frag의 나눗셈 상수도 같이 맞춰야 한다.
        glm::mat4 haloModel = glm::translate(glm::mat4(1.0f), relativeToCamera(sun.currentPosition)) * glm::scale(glm::mat4(1.0f), glm::vec3(curSunRadius * 2.5f));
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sun.descriptorSet, 0, nullptr);
        PushConstants haloPush{haloModel, 7}; vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &haloPush);
        vkCmdDrawIndexed(cb, sphereIndexCount, 1, 0, 0, 0);
    }
}

void SolarSystemApp::recordBloom(VkCommandBuffer cb, VkExtent2D renderExtent) {
    // ── 밉체인 블룸 ──────────────────────────────────────────────────
    // 한 번의 draw = 전체화면 삼각형 하나. 렌더 타겟과 소스는 같은 이미지의 다른 밉이며,
    // 각 뷰가 한 밉만 덮으므로 레이아웃 전환이 서로 침범하지 않는다.
    auto bloomDraw = [&](VkRenderPass pass, VkPipeline pipe, int dstMip, VkDescriptorSet srcSet,
                         VkExtent2D srcExtent, float radius) {
        VkRenderPassBeginInfo bp{}; bp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        bp.renderPass = pass; bp.framebuffer = bloomFramebuffers[dstMip];
        bp.renderArea.offset = {0, 0}; bp.renderArea.extent = bloomMipExtents[dstMip];
        VkClearValue clear = {{{0.0f, 0.0f, 0.0f, 1.0f}}}; bp.clearValueCount = 1; bp.pClearValues = &clear;

        vkCmdBeginRenderPass(cb, &bp, VK_SUBPASS_CONTENTS_INLINE);
        setViewport(cb, bloomMipExtents[dstMip]);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipelineLayout, 0, 1, &srcSet, 0, nullptr);
        BloomPush bpush{ glm::vec2(1.0f / (float)srcExtent.width, 1.0f / (float)srcExtent.height), radius };
        vkCmdPushConstants(cb, blurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(bpush), &bpush);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRenderPass(cb);
    };

    // 1) 내려가며 흐린다: 밝기 버퍼 -> 밉0, 밉0 -> 밉1, ...
    bloomDraw(blurRenderPass, bloomDownPipeline, 0, bloomSrcSet, renderExtent, 0.0f);
    for (int i = 1; i < bloomMipCount; ++i)
        bloomDraw(blurRenderPass, bloomDownPipeline, i, bloomMipSets[i - 1], bloomMipExtents[i - 1], 0.0f);

    // 2) 올라오며 더한다: 밉N-1 -> 밉N-2, ... -> 밉0.
    // 여러 해상도의 번짐이 누적되어 넓고 부드러운 글로우가 만들어진다.
    const float kBloomFilterRadius = 1.0f;
    for (int i = bloomMipCount - 1; i > 0; --i)
        bloomDraw(bloomUpPass, bloomUpPipeline, i - 1, bloomMipSets[i], bloomMipExtents[i], kBloomFilterRadius);
}

void SolarSystemApp::recordPost(VkCommandBuffer cb, uint32_t imageIndex) {
    VkRenderPassBeginInfo renderPassInfo{}; renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; renderPassInfo.renderPass = renderPass; renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0}; renderPassInfo.renderArea.extent = swapChainExtent;
    std::array<VkClearValue, 2> swapClear{}; swapClear[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; swapClear[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = 2; renderPassInfo.pClearValues = swapClear.data();
    vkCmdBeginRenderPass(cb, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    setFullViewport(cb);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipelineLayout, 0, 1, &postDescriptorSet, 0, nullptr);
    float postExposure = settings.exposure;
    vkCmdPushConstants(cb, postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &postExposure);
    vkCmdDraw(cb, 3, 1, 0, 0);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
    vkCmdEndRenderPass(cb);
}
