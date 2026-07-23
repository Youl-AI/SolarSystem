#include "SolarSystemApp.hpp"

// 오프스크린/블러 이미지·프레임버퍼 전용 파괴 헬퍼. recreateSwapChain()과
// recreateOffscreenAndBlur() 양쪽에서 재사용한다(Task 6의 MSAA 재생성도 재사용 예정).
void SolarSystemApp::destroyOffscreenAndBlurResources() {
    for (int i = 0; i < bloomMipCount; i++) {
        vkDestroyFramebuffer(device, bloomFramebuffers[i], nullptr);
        vkDestroyImageView(device, bloomMipViews[i], nullptr);
    }
    vkDestroyImage(device, bloomImage, nullptr);
    vkFreeMemory(device, bloomMem, nullptr);
    vkDestroyFramebuffer(device, offscreenFramebuffer, nullptr);
    vkDestroyImageView(device, offscreenColorView, nullptr); vkDestroyImage(device, offscreenColorImage, nullptr); vkFreeMemory(device, offscreenColorMem, nullptr);
    vkDestroyImageView(device, offscreenBrightView, nullptr); vkDestroyImage(device, offscreenBrightImage, nullptr); vkFreeMemory(device, offscreenBrightMem, nullptr);
    vkDestroyImageView(device, offscreenDepthView, nullptr); vkDestroyImage(device, offscreenDepthImage, nullptr); vkFreeMemory(device, offscreenDepthMem, nullptr);
    vkDestroyImageView(device, msaaColorView, nullptr); vkDestroyImage(device, msaaColorImage, nullptr); vkFreeMemory(device, msaaColorMem, nullptr);
    vkDestroyImageView(device, msaaBrightView, nullptr); vkDestroyImage(device, msaaBrightImage, nullptr); vkFreeMemory(device, msaaBrightMem, nullptr);
}

void SolarSystemApp::recreateSwapChain() {
    vkDeviceWaitIdle(device);

    destroyOffscreenAndBlurResources();

    cleanupSwapChain();

    createSwapChain();
    createRenderFinishedSemaphores(); // 이미지 수가 달라질 수 있으므로 다시 만든다
    computeRenderExtent(); // 창 크기 변경 시 오프스크린도 scale x 새-창크기로 유지
    createImageViews();
    createDepthResources();
    createFramebuffers();

    createOffscreenImages();
    createBlurImages();
    updateBlurDescriptorSets();
    updatePostDescriptorSets();
}

// 렌더스케일(및 Task 6의 MSAA) 변경 시 오프스크린/블러만 다시 만든다. 스왑체인은 그대로.
void SolarSystemApp::recreateOffscreenAndBlur() {
    destroyOffscreenAndBlurResources();
    createOffscreenImages();
    createBlurImages();
    updateBlurDescriptorSets();
    updatePostDescriptorSets();
}

void SolarSystemApp::recreateOffscreenForMsaa() {
    // 오프스크린 파이프라인(graphics/line)과 렌더패스는 샘플 수가 생성 시 고정되므로 다시 만든다.
    // 블러/포스트는 resolve된 단일 샘플 이미지를 쓰므로 MSAA와 무관 — 렌더패스/이미지는 그대로 두고
    // resolve 대상 뷰만 새로 바인딩한다.
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipeline(device, linePipeline, nullptr);
    vkDestroyPipeline(device, constellationPipeline, nullptr);
    vkDestroyPipeline(device, atmospherePipeline, nullptr);
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

// 색상과 깊이 어태치먼트가 공통으로 지원하는 최대 샘플 수를 8x로 상한을 두고 고른다.
// (오프스크린 패스는 두 종류를 한 서브패스에서 쓰므로 교집합이어야 한다)
// 8x 미지원이면 4x, 2x, 1x 순으로 자동 강등된다.
VkSampleCountFlagBits SolarSystemApp::pickMsaaSamples() {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts
                              & props.limits.framebufferDepthSampleCounts;
    VkSampleCountFlagBits chosen = VK_SAMPLE_COUNT_1_BIT;
    if (counts & VK_SAMPLE_COUNT_8_BIT)      chosen = VK_SAMPLE_COUNT_8_BIT;
    else if (counts & VK_SAMPLE_COUNT_4_BIT) chosen = VK_SAMPLE_COUNT_4_BIT;
    else if (counts & VK_SAMPLE_COUNT_2_BIT) chosen = VK_SAMPLE_COUNT_2_BIT;
    fprintf(stderr, "[MSAA] selected %dx (device supports mask 0x%x)\n", (int)chosen, (unsigned)counts);
    return chosen;
}

// 설정의 MSAA 레벨(0/2/4/8)을 기기 지원으로 클램프해 VkSampleCountFlagBits로 변환한다.
VkSampleCountFlagBits SolarSystemApp::clampMsaaLevel(int level) {
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physicalDevice, &props);
    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    auto ok = [&](VkSampleCountFlagBits b){ return (counts & b) != 0; };
    if (level >= 8 && ok(VK_SAMPLE_COUNT_8_BIT)) return VK_SAMPLE_COUNT_8_BIT;
    if (level >= 4 && ok(VK_SAMPLE_COUNT_4_BIT)) return VK_SAMPLE_COUNT_4_BIT;
    if (level >= 2 && ok(VK_SAMPLE_COUNT_2_BIT)) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

void SolarSystemApp::createComputeResources() {
    VkDeviceSize inputSize = asteroidTransforms.size() * sizeof(AsteroidData);
    createBuffer(inputSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, computeInputBuffer, computeInputMem);
    void* inData; vkMapMemory(device, computeInputMem, 0, inputSize, 0, &inData);
    memcpy(inData, asteroidTransforms.data(), inputSize);
    vkUnmapMemory(device, computeInputMem);

    // 각 소행성은 12개 바구니 중 정확히 하나로 들어가지만, 어느 바구니에 몰릴지는
    // 시점에 따라 달라진다. 한 바구니가 전부를 받아도 넘치지 않도록 잡는다.
    // (예전엔 20000이 박혀 있어, 생성한 40000개 중 절반이 아예 그려지지 않았다)
    asteroidBucketCapacity = static_cast<uint32_t>(asteroidTransforms.size());
    VkDeviceSize outputSize = (VkDeviceSize)12 * asteroidBucketCapacity * sizeof(glm::mat4);
    createBuffer(outputSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, computeOutputBuffer, computeOutputMem);

    VkDeviceSize indirectSize = 12 * sizeof(VkDrawIndexedIndirectCommand);
    createBuffer(indirectSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indirectDrawBuffer, indirectDrawMem);
    vkMapMemory(device, indirectDrawMem, 0, indirectSize, 0, &indirectDrawMapped);

    for (int lod = 0; lod < 3; ++lod) {
        for (int type = 0; type < 4; ++type) {
            int bucket = lod * 4 + type;
            drawCmdTemplates[bucket].instanceCount = 0;
            drawCmdTemplates[bucket].firstInstance = bucket * asteroidBucketCapacity;
            if (lod == 0) { drawCmdTemplates[bucket].indexCount = highLodIndexCount[type]; drawCmdTemplates[bucket].firstIndex = highLodFirstIndex[type]; drawCmdTemplates[bucket].vertexOffset = highLodVertexOffset[type]; }
            else if (lod == 1) { drawCmdTemplates[bucket].indexCount = midLodIndexCount[type]; drawCmdTemplates[bucket].firstIndex = midLodFirstIndex[type]; drawCmdTemplates[bucket].vertexOffset = midLodVertexOffset[type]; }
            else { drawCmdTemplates[bucket].indexCount = lowLodIndexCount[type]; drawCmdTemplates[bucket].firstIndex = lowLodFirstIndex[type]; drawCmdTemplates[bucket].vertexOffset = lowLodVertexOffset[type]; }
        }
    }
    memcpy(indirectDrawMapped, drawCmdTemplates, indirectSize);

    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    bindings[0].binding = 0; bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; bindings[0].descriptorCount = 1; bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1; bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bindings[1].descriptorCount = 1; bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding = 2; bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bindings[2].descriptorCount = 1; bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[3].binding = 3; bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bindings[3].descriptorCount = 1; bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{}; layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; layoutInfo.bindingCount = 4; layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &computeDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{}; allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; allocInfo.descriptorPool = descriptorPool; allocInfo.descriptorSetCount = 1; allocInfo.pSetLayouts = &computeDescriptorSetLayout;
    vkAllocateDescriptorSets(device, &allocInfo, &computeDescriptorSet);

    VkDescriptorBufferInfo uboInfo{uniformBuffer, 0, sizeof(UniformBufferObject)};
    VkDescriptorBufferInfo inInfo{computeInputBuffer, 0, inputSize};
    VkDescriptorBufferInfo outInfo{computeOutputBuffer, 0, outputSize};
    VkDescriptorBufferInfo indInfo{indirectDrawBuffer, 0, indirectSize};

    std::array<VkWriteDescriptorSet, 4> writes{};
    for(int i=0; i<4; i++) { writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet = computeDescriptorSet; writes[i].dstBinding = i; writes[i].descriptorCount = 1; writes[i].descriptorType = (i==0) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; }
    writes[0].pBufferInfo = &uboInfo; writes[1].pBufferInfo = &inInfo; writes[2].pBufferInfo = &outInfo; writes[3].pBufferInfo = &indInfo;
    vkUpdateDescriptorSets(device, 4, writes.data(), 0, nullptr);

    auto compCode = readFile("shaders/asteroid_cull.spv"); 
    VkShaderModule compMod = createShaderModule(compCode);
    VkPipelineShaderStageCreateInfo stageInfo{}; stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT; stageInfo.module = compMod; stageInfo.pName = "main";
    
    // LOD 판정을 렌더 좌표계에서 하기 위한 벨트 행렬 + 실제 소행성 개수.
    VkPushConstantRange compPush{}; compPush.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; compPush.offset = 0; compPush.size = sizeof(AsteroidCullPush);
    VkPipelineLayoutCreateInfo pLayoutInfo{}; pLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pLayoutInfo.setLayoutCount = 1; pLayoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    pLayoutInfo.pushConstantRangeCount = 1; pLayoutInfo.pPushConstantRanges = &compPush;
    vkCreatePipelineLayout(device, &pLayoutInfo, nullptr, &computePipelineLayout);
    
    VkComputePipelineCreateInfo pipelineInfo{}; pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO; pipelineInfo.stage = stageInfo; pipelineInfo.layout = computePipelineLayout;
    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline);
    vkDestroyShaderModule(device, compMod, nullptr);
}

        // 오프스크린/블러 렌더 크기 = round(renderScale x swapChainExtent). 포스트/UI는 계속 swapChainExtent.
void SolarSystemApp::computeRenderExtent() {
    uint32_t w = std::max(1u, (uint32_t)std::lround(swapChainExtent.width  * settings.renderScale));
    uint32_t h = std::max(1u, (uint32_t)std::lround(swapChainExtent.height * settings.renderScale));
    renderExtent = {w, h};
    blurExtent = {std::max(1u, w / 2), std::max(1u, h / 2)};
}

void SolarSystemApp::createOffscreenImages() {
    computeRenderExtent();
    VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT; VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    auto makeImg = [&](VkFormat fmt, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem, VkImageView& view, VkImageAspectFlags aspect, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) {
        VkImageCreateInfo iInfo{}; iInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; iInfo.imageType = VK_IMAGE_TYPE_2D;
        iInfo.extent.width = renderExtent.width; iInfo.extent.height = renderExtent.height; iInfo.extent.depth = 1; iInfo.mipLevels = 1; iInfo.arrayLayers = 1; iInfo.format = fmt; iInfo.tiling = VK_IMAGE_TILING_OPTIMAL; iInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; iInfo.usage = usage; iInfo.samples = samples; iInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateImage(device, &iInfo, nullptr, &img);
        VkMemoryRequirements mReqs; vkGetImageMemoryRequirements(device, img, &mReqs);
        VkMemoryAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        aInfo.allocationSize = mReqs.size; aInfo.memoryTypeIndex = findMemoryType(mReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &aInfo, nullptr, &mem); vkBindImageMemory(device, img, mem, 0);
        VkImageViewCreateInfo vInfo{}; vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vInfo.image = img; vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vInfo.format = fmt; vInfo.subresourceRange.aspectMask = aspect; vInfo.subresourceRange.baseMipLevel = 0; vInfo.subresourceRange.levelCount = 1; vInfo.subresourceRange.baseArrayLayer = 0; vInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &vInfo, nullptr, &view);
    };
    // resolve 대상(단일 샘플, 이후 블러/포스트가 SAMPLED). MSAA 색상/bright가 여기로 resolve된다.
    makeImg(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, offscreenColorImage, offscreenColorMem, offscreenColorView, VK_IMAGE_ASPECT_COLOR_BIT);
    makeImg(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, offscreenBrightImage, offscreenBrightMem, offscreenBrightView, VK_IMAGE_ASPECT_COLOR_BIT);

    // 멀티샘플 렌더 타겟들. 색상/bright는 resolve되므로 SAMPLED 불필요. 깊이는 MSAA 렌더 타겟 겸용.
    makeImg(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, msaaColorImage, msaaColorMem, msaaColorView, VK_IMAGE_ASPECT_COLOR_BIT, msaaSamples);
    makeImg(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, msaaBrightImage, msaaBrightMem, msaaBrightView, VK_IMAGE_ASPECT_COLOR_BIT, msaaSamples);
    makeImg(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, offscreenDepthImage, offscreenDepthMem, offscreenDepthView, VK_IMAGE_ASPECT_DEPTH_BIT, msaaSamples);

    // 어태치먼트 순서는 렌더패스와 반드시 일치: 0=MSAA색상 1=MSAA-bright 2=MSAA깊이 3=resolve색상 4=resolve-bright
    std::array<VkImageView, 5> fbAtts = {msaaColorView, msaaBrightView, offscreenDepthView, offscreenColorView, offscreenBrightView};
    VkFramebufferCreateInfo fbInfo{}; fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fbInfo.renderPass = offscreenRenderPass; fbInfo.attachmentCount = 5; fbInfo.pAttachments = fbAtts.data(); fbInfo.width = renderExtent.width; fbInfo.height = renderExtent.height; fbInfo.layers = 1;
    vkCreateFramebuffer(device, &fbInfo, nullptr, &offscreenFramebuffer);
}

void SolarSystemApp::createOffscreenResources() {
    VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT; VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    // 어태치먼트 순서는 프레임버퍼와 반드시 일치:
    //   0=MSAA색상 1=MSAA-bright 2=MSAA깊이 (전부 msaaSamples, resolve/렌더용이라 storeOp=DONT_CARE)
    //   3=resolve색상 4=resolve-bright (단일 샘플, loadOp=DONT_CARE로 resolve가 전체를 덮어씀, STORE, 이후 SAMPLED)
    std::array<VkAttachmentDescription, 5> atts{};
    atts[0].format = colorFormat; atts[0].samples = msaaSamples; atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    atts[1] = atts[0];
    atts[2].format = depthFormat; atts[2].samples = msaaSamples; atts[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    atts[3].format = colorFormat; atts[3].samples = VK_SAMPLE_COUNT_1_BIT; atts[3].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE; atts[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[3].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atts[4] = atts[3];

    VkAttachmentReference colRefs[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkAttachmentReference depRef{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    // resolve 참조는 색상 참조와 인덱스가 1:1 대응한다. resolve[i]가 color[i]를 다운샘플한다.
    VkAttachmentReference resolveRefs[2] = {{3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 2; subpass.pColorAttachments = colRefs; subpass.pResolveAttachments = resolveRefs; subpass.pDepthStencilAttachment = &depRef;

    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0; deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL; deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpInfo{}; rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpInfo.attachmentCount = 5; rpInfo.pAttachments = atts.data(); rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass; rpInfo.dependencyCount = 2; rpInfo.pDependencies = deps.data();
    vkCreateRenderPass(device, &rpInfo, nullptr, &offscreenRenderPass);

    createOffscreenImages();

    VkSamplerCreateInfo sInfo{}; sInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO; sInfo.magFilter = VK_FILTER_LINEAR; sInfo.minFilter = VK_FILTER_LINEAR; sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &sInfo, nullptr, &offscreenSampler);
}

void SolarSystemApp::createBlurImages() {
    VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    // 밉 단계 수: 가장 작은 변이 8픽셀 밑으로 내려가지 않을 때까지.
    bloomMipCount = 1;
    while (bloomMipCount < BLOOM_MAX_MIPS
           && (blurExtent.width >> bloomMipCount) >= 8
           && (blurExtent.height >> bloomMipCount) >= 8) {
        bloomMipCount++;
    }
    for (int i = 0; i < bloomMipCount; ++i) {
        bloomMipExtents[i] = { std::max(1u, blurExtent.width >> i), std::max(1u, blurExtent.height >> i) };
    }

    VkImageCreateInfo iInfo{}; iInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; iInfo.imageType = VK_IMAGE_TYPE_2D;
    iInfo.extent = { blurExtent.width, blurExtent.height, 1 };
    iInfo.mipLevels = bloomMipCount; iInfo.arrayLayers = 1; iInfo.format = colorFormat;
    iInfo.tiling = VK_IMAGE_TILING_OPTIMAL; iInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    iInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    iInfo.samples = VK_SAMPLE_COUNT_1_BIT; iInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(device, &iInfo, nullptr, &bloomImage);

    VkMemoryRequirements mReqs; vkGetImageMemoryRequirements(device, bloomImage, &mReqs);
    VkMemoryAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; aInfo.allocationSize = mReqs.size;
    aInfo.memoryTypeIndex = findMemoryType(mReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device, &aInfo, nullptr, &bloomMem);
    vkBindImageMemory(device, bloomImage, bloomMem, 0);

    // 밉마다 '그 한 단계만' 보는 뷰를 만든다. 렌더 타겟으로도, 샘플링 소스로도 쓴다.
    // 뷰가 한 단계만 덮으므로 레이아웃 전환도 그 밉에만 적용되어, 같은 이미지의
    // 다른 밉을 읽으면서 이 밉에 그리는 것이 안전하다.
    for (int i = 0; i < bloomMipCount; ++i) {
        VkImageViewCreateInfo vInfo{}; vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vInfo.image = bloomImage; vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; vInfo.format = colorFormat;
        vInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vInfo.subresourceRange.baseMipLevel = i; vInfo.subresourceRange.levelCount = 1;
        vInfo.subresourceRange.baseArrayLayer = 0; vInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &vInfo, nullptr, &bloomMipViews[i]);

        VkFramebufferCreateInfo fbInfo{}; fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = blurRenderPass; fbInfo.attachmentCount = 1; fbInfo.pAttachments = &bloomMipViews[i];
        fbInfo.width = bloomMipExtents[i].width; fbInfo.height = bloomMipExtents[i].height; fbInfo.layers = 1;
        vkCreateFramebuffer(device, &fbInfo, nullptr, &bloomFramebuffers[i]);
    }
}

void SolarSystemApp::createBlurResources() {
    VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkAttachmentDescription att{}; att.format = colorFormat; att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &colorRef;

    std::array<VkSubpassDependency, 2> deps{};
    // 업샘플 패스는 loadOp=LOAD로 어태치먼트를 '읽으므로' 읽기 접근도 열어야 한다.
    // 렌더패스 호환성 판정에는 서브패스 의존성도 포함되므로, 프레임버퍼를 두 패스가
    // 공유하려면 양쪽 마스크가 같아야 한다. 다운샘플에는 읽기가 없지만 허용해도 무해하다.
    const VkAccessFlags kColorRW = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0; deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = kColorRW;
    deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL; deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[1].srcAccessMask = kColorRW; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpInfo{}; rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpInfo.attachmentCount = 1; rpInfo.pAttachments = &att; rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass; rpInfo.dependencyCount = 2; rpInfo.pDependencies = deps.data();
    vkCreateRenderPass(device, &rpInfo, nullptr, &blurRenderPass);

    // 업샘플 패스: 이미 그려진 밉 위에 '더해야' 하므로 기존 내용을 보존한다.
    // (다운샘플 패스와 어태치먼트 포맷·샘플수가 같아 프레임버퍼를 공유할 수 있다)
    VkAttachmentDescription attUp = att;
    attUp.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attUp.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // 의존성은 위에서 두 패스가 같도록 맞춰뒀다(프레임버퍼 공유 조건).
    VkRenderPassCreateInfo rpUp = rpInfo; rpUp.pAttachments = &attUp;
    vkCreateRenderPass(device, &rpUp, nullptr, &bloomUpPass);

    createBlurImages();
}

void SolarSystemApp::updateBlurDescriptorSets() {
    auto mI = [&](VkImageView v){ VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = offscreenSampler; return i; };

    std::vector<VkDescriptorImageInfo> infos;
    infos.reserve(bloomMipCount + 1);
    infos.push_back(mI(offscreenBrightView));                 // 체인의 입력
    for (int i = 0; i < bloomMipCount; ++i) infos.push_back(mI(bloomMipViews[i]));

    std::vector<VkWriteDescriptorSet> wr(infos.size());
    for (size_t i = 0; i < infos.size(); ++i) {
        wr[i] = {}; wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = (i == 0) ? bloomSrcSet : bloomMipSets[i - 1];
        wr[i].dstBinding = 0; wr[i].descriptorCount = 1;
        wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device, (uint32_t)wr.size(), wr.data(), 0, nullptr);
}

void SolarSystemApp::createBlurPipeline() {
    VkDescriptorSetLayoutBinding b{}; b.binding = 0; b.descriptorCount = 1; b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layInfo{}; layInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; layInfo.bindingCount = 1; layInfo.pBindings = &b;
    vkCreateDescriptorSetLayout(device, &layInfo, nullptr, &blurDescriptorSetLayout);

    // 체인 입력용 1개 + 밉 단계마다 1개. 밉 수는 해상도에 따라 변하므로 최대치로 잡아둔다.
    {
        std::vector<VkDescriptorSetLayout> layouts(BLOOM_MAX_MIPS + 1, blurDescriptorSetLayout);
        std::vector<VkDescriptorSet> sets(BLOOM_MAX_MIPS + 1);
        VkDescriptorSetAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        aInfo.descriptorPool = descriptorPool; aInfo.descriptorSetCount = (uint32_t)layouts.size(); aInfo.pSetLayouts = layouts.data();
        vkAllocateDescriptorSets(device, &aInfo, sets.data());
        bloomSrcSet = sets[0];
        for (int i = 0; i < BLOOM_MAX_MIPS; ++i) bloomMipSets[i] = sets[i + 1];
    }

    updateBlurDescriptorSets();

    VkPushConstantRange pc{}; pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; pc.offset = 0; pc.size = sizeof(BloomPush);
    VkPipelineLayoutCreateInfo pLayInfo{}; pLayInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pLayInfo.setLayoutCount = 1; pLayInfo.pSetLayouts = &blurDescriptorSetLayout; pLayInfo.pushConstantRangeCount = 1; pLayInfo.pPushConstantRanges = &pc;
    vkCreatePipelineLayout(device, &pLayInfo, nullptr, &blurPipelineLayout);

    auto vCode = readFile("shaders/post_vert.spv");
    auto fDownCode = readFile("shaders/bloom_down_frag.spv");
    auto fUpCode = readFile("shaders/bloom_up_frag.spv");
    VkShaderModule vMod = createShaderModule(vCode);
    VkShaderModule fMod = createShaderModule(fDownCode);
    VkShaderModule fUpMod = createShaderModule(fUpCode);
    VkPipelineShaderStageCreateInfo vS{}; vS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; vS.stage = VK_SHADER_STAGE_VERTEX_BIT; vS.module = vMod; vS.pName = "main";
    VkPipelineShaderStageCreateInfo fS{}; fS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; fS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fS.module = fMod; fS.pName = "main";
    VkPipelineShaderStageCreateInfo sS[] = {vS, fS};

    VkPipelineVertexInputStateCreateInfo vI{}; vI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo iA{}; iA.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; iA.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vp{}; vp.width = swapChainExtent.width; vp.height = swapChainExtent.height; vp.maxDepth = 1.0f; VkRect2D sc{}; sc.extent = swapChainExtent;
    VkPipelineViewportStateCreateInfo vpS{}; vpS.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vpS.viewportCount = 1; vpS.pViewports = &vp; vpS.scissorCount = 1; vpS.pScissors = &sc;
    std::array<VkDynamicState, 2> dynamicStatesBlur = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicStateBlur{}; dynamicStateBlur.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dynamicStateBlur.dynamicStateCount = static_cast<uint32_t>(dynamicStatesBlur.size()); dynamicStateBlur.pDynamicStates = dynamicStatesBlur.data();
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; ds.depthTestEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState cbA{}; cbA.colorWriteMask = 0xF; cbA.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cbS{}; cbS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cbS.attachmentCount = 1; cbS.pAttachments = &cbA;

    VkGraphicsPipelineCreateInfo pInfo{}; pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pInfo.stageCount = 2; pInfo.pStages = sS; pInfo.pVertexInputState = &vI; pInfo.pInputAssemblyState = &iA; pInfo.pViewportState = &vpS; pInfo.pRasterizationState = &rs; pInfo.pMultisampleState = &ms; pInfo.pDepthStencilState = &ds; pInfo.pColorBlendState = &cbS; pInfo.layout = blurPipelineLayout; pInfo.renderPass = blurRenderPass; pInfo.pDynamicState = &dynamicStateBlur;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &bloomDownPipeline);

    // 업샘플은 같은 상태에 가산 블렌딩(ONE/ONE)만 켜서, 확대한 결과를 아래 밉에 더한다.
    VkPipelineColorBlendAttachmentState cbAdd = cbA;
    cbAdd.blendEnable = VK_TRUE;
    cbAdd.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; cbAdd.dstColorBlendFactor = VK_BLEND_FACTOR_ONE; cbAdd.colorBlendOp = VK_BLEND_OP_ADD;
    cbAdd.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cbAdd.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cbAdd.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cbAddS = cbS; cbAddS.pAttachments = &cbAdd;

    VkPipelineShaderStageCreateInfo fUpS = fS; fUpS.module = fUpMod;
    VkPipelineShaderStageCreateInfo sUp[] = {vS, fUpS};
    VkGraphicsPipelineCreateInfo pUp = pInfo;
    pUp.pStages = sUp; pUp.pColorBlendState = &cbAddS; pUp.renderPass = bloomUpPass;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pUp, nullptr, &bloomUpPipeline);

    vkDestroyShaderModule(device, fUpMod, nullptr); vkDestroyShaderModule(device, fMod, nullptr); vkDestroyShaderModule(device, vMod, nullptr);
}

void SolarSystemApp::updatePostDescriptorSets() {
    auto mI = [&](VkImageView v){ VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = offscreenSampler; return i; };
    // 블룸 결과는 체인을 다 올라온 밉 0에 모여 있다.
    VkDescriptorImageInfo cI = mI(offscreenColorView); VkDescriptorImageInfo brI = mI(bloomMipViews[0]);
    std::array<VkWriteDescriptorSet, 2> wr{};
    wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr[0].dstSet = postDescriptorSet; wr[0].dstBinding = 0; wr[0].descriptorCount = 1; wr[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr[0].pImageInfo = &cI;
    wr[1] = wr[0]; wr[1].dstBinding = 1; wr[1].pImageInfo = &brI;
    vkUpdateDescriptorSets(device, 2, wr.data(), 0, nullptr);
}

void SolarSystemApp::createPostProcessPipeline() {
    std::array<VkDescriptorSetLayoutBinding, 2> b{};
    b[0].binding = 0; b[0].descriptorCount = 1; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    b[1].binding = 1; b[1].descriptorCount = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layInfo{}; layInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; layInfo.bindingCount = 2; layInfo.pBindings = b.data();
    vkCreateDescriptorSetLayout(device, &layInfo, nullptr, &postDescriptorSetLayout);

    VkDescriptorSetAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; aInfo.descriptorPool = descriptorPool; aInfo.descriptorSetCount = 1; aInfo.pSetLayouts = &postDescriptorSetLayout;
    vkAllocateDescriptorSets(device, &aInfo, &postDescriptorSet);

    updatePostDescriptorSets();

    VkPushConstantRange postPcRange{}; postPcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; postPcRange.offset = 0; postPcRange.size = sizeof(float);
    VkPipelineLayoutCreateInfo pLayInfo{}; pLayInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pLayInfo.setLayoutCount = 1; pLayInfo.pSetLayouts = &postDescriptorSetLayout; pLayInfo.pushConstantRangeCount = 1; pLayInfo.pPushConstantRanges = &postPcRange;
    vkCreatePipelineLayout(device, &pLayInfo, nullptr, &postPipelineLayout);

    auto vCode = readFile("shaders/post_vert.spv"); auto fCode = readFile("shaders/post_frag.spv");
    VkShaderModule vMod = createShaderModule(vCode); VkShaderModule fMod = createShaderModule(fCode);
    VkPipelineShaderStageCreateInfo vS{}; vS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; vS.stage = VK_SHADER_STAGE_VERTEX_BIT; vS.module = vMod; vS.pName = "main";
    VkPipelineShaderStageCreateInfo fS{}; fS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; fS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fS.module = fMod; fS.pName = "main";
    VkPipelineShaderStageCreateInfo sS[] = {vS, fS};

    VkPipelineVertexInputStateCreateInfo vI{}; vI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo iA{}; iA.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; iA.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vp{}; vp.width = swapChainExtent.width; vp.height = swapChainExtent.height; vp.maxDepth = 1.0f; VkRect2D sc{}; sc.extent = swapChainExtent;
    VkPipelineViewportStateCreateInfo vpS{}; vpS.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vpS.viewportCount = 1; vpS.pViewports = &vp; vpS.scissorCount = 1; vpS.pScissors = &sc;
    std::array<VkDynamicState, 2> dynamicStatesPost = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicStatePost{}; dynamicStatePost.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dynamicStatePost.dynamicStateCount = static_cast<uint32_t>(dynamicStatesPost.size()); dynamicStatePost.pDynamicStates = dynamicStatesPost.data();
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; ds.depthTestEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState cbA{}; cbA.colorWriteMask = 0xF; cbA.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cbS{}; cbS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cbS.attachmentCount = 1; cbS.pAttachments = &cbA;

    VkGraphicsPipelineCreateInfo pInfo{}; pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pInfo.stageCount = 2; pInfo.pStages = sS; pInfo.pVertexInputState = &vI; pInfo.pInputAssemblyState = &iA; pInfo.pViewportState = &vpS; pInfo.pRasterizationState = &rs; pInfo.pMultisampleState = &ms; pInfo.pDepthStencilState = &ds; pInfo.pColorBlendState = &cbS; pInfo.layout = postPipelineLayout; pInfo.renderPass = renderPass; pInfo.pDynamicState = &dynamicStatePost;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &postPipeline);
    vkDestroyShaderModule(device, fMod, nullptr); vkDestroyShaderModule(device, vMod, nullptr);
}

void SolarSystemApp::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 9> b{}; // 0=UBO, 1~6=텍스처, 7=소행성 SSBO, 8=은하수 층
    for (int i = 0; i < 9; i++) { b[i].binding = i; b[i].descriptorCount = 1; b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; }
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; b[0].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
    for (int i = 1; i < 7; i++) b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[4].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;

    // 7번 바인딩: 버텍스 셰이더가 읽는 소행성 인스턴스 버퍼(SSBO).
    // 셰도우 맵이 쓰던 자리를 물려받았다.
    b[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b[7].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // 8번: 은하수 확산광 큐브맵. 밝은 별(6번)과 따로 둬야 둘을 독립적으로 조절할 수 있다.
    b[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    VkDescriptorSetLayoutCreateInfo layoutInfo{}; layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; layoutInfo.bindingCount = 9; layoutInfo.pBindings = b.data();
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);
}

void SolarSystemApp::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    // 천체가 늘어날 것에 대비해 내부 슬롯 용량도 빵빵하게 늘려줍니다.
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; poolSizes[0].descriptorCount = 100; 
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; poolSizes[1].descriptorCount = 500; 
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; poolSizes[2].descriptorCount = 100;

    VkDescriptorPoolCreateInfo poolInfo{}; 
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; 
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size()); 
    poolInfo.pPoolSizes = poolSizes.data(); 
    
    // 🚀 [핵심 해결] 최대 세트 개수를 20개에서 100개로 대폭 확장합니다!
    poolInfo.maxSets = 100; 
    
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
}

void SolarSystemApp::createGraphicsPipeline() {
    auto vertShaderCode = readFile("shaders/vert.spv"); auto fragShaderCode = readFile("shaders/frag.spv");
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode); VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{}; vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT; vertShaderStageInfo.module = vertShaderModule; vertShaderStageInfo.pName = "main";
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{}; fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fragShaderStageInfo.module = fragShaderModule; fragShaderStageInfo.pName = "main";
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    auto bindingDescription = Vertex::getBindingDescription(); auto attributeDescriptions = Vertex::getAttributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{}; vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; vertexInputInfo.vertexBindingDescriptionCount = 1; vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()); vertexInputInfo.pVertexBindingDescriptions = &bindingDescription; vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{}; inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{}; viewport.width = (float)swapChainExtent.width; viewport.height = (float)swapChainExtent.height; viewport.maxDepth = 1.0f; VkRect2D scissor{}; scissor.extent = swapChainExtent;
    VkPipelineViewportStateCreateInfo viewportState{}; viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; viewportState.viewportCount = 1; viewportState.pViewports = &viewport; viewportState.scissorCount = 1; viewportState.pScissors = &scissor;
    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{}; dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()); dynamicState.pDynamicStates = dynamicStates.data();
    VkPipelineRasterizationStateCreateInfo rasterizer{}; rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rasterizer.polygonMode = VK_POLYGON_MODE_FILL; rasterizer.lineWidth = 1.0f; rasterizer.cullMode = VK_CULL_MODE_NONE; rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    // 오프스크린 패스가 MSAA이므로 여기 그리는 두 파이프라인(graphics/line)의 샘플 수도 일치시켜야 한다.
    VkPipelineMultisampleStateCreateInfo multisampling{}; multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; multisampling.rasterizationSamples = msaaSamples;
    VkPipelineDepthStencilStateCreateInfo depthStencil{}; depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; depthStencil.depthTestEnable = VK_TRUE; depthStencil.depthWriteEnable = VK_TRUE; depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment[2]{};
    colorBlendAttachment[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; colorBlendAttachment[0].blendEnable = VK_TRUE; colorBlendAttachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; colorBlendAttachment[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; colorBlendAttachment[0].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment[1] = colorBlendAttachment[0]; 

    VkPipelineColorBlendStateCreateInfo colorBlending{}; colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; colorBlending.attachmentCount = 2; colorBlending.pAttachments = colorBlendAttachment;

    VkPushConstantRange pushConstantRange{}; pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; pushConstantRange.offset = 0; pushConstantRange.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{}; pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pipelineLayoutInfo.setLayoutCount = 1; pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout; pipelineLayoutInfo.pushConstantRangeCount = 1; pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
    
    VkGraphicsPipelineCreateInfo pipelineInfo{}; pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pipelineInfo.stageCount = 2; pipelineInfo.pStages = shaderStages; pipelineInfo.pVertexInputState = &vertexInputInfo; pipelineInfo.pInputAssemblyState = &inputAssembly; pipelineInfo.pViewportState = &viewportState; pipelineInfo.pRasterizationState = &rasterizer; pipelineInfo.pMultisampleState = &multisampling; pipelineInfo.pDepthStencilState = &depthStencil; pipelineInfo.pColorBlendState = &colorBlending; pipelineInfo.layout = pipelineLayout; pipelineInfo.renderPass = offscreenRenderPass; pipelineInfo.pDynamicState = &dynamicState;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);

    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &linePipeline);

    // 별자리 선: 끊긴 간선 그래프라 LINE_LIST. 배경(먼 깊이)에 얹으므로 깊이 쓰기는 끈다
    // (행성이 이미 쓴 깊이로 LEQUAL 테스트만 하면 앞의 행성이 선을 가린다).
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    depthStencil.depthWriteEnable = VK_FALSE;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &constellationPipeline);
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // 뒤 파이프라인을 위해 원복
    depthStencil.depthWriteEnable = VK_TRUE;

    // 대기 껍질용. 두 가지만 다르다.
    // - 깊이를 쓰지 않는다: 껍질은 행성보다 커서, 깊이를 남기면 나중에 그리는 위성이나
    //   궤도선이 그 뒤로 사라져 버린다. 읽기는 그대로 해야 앞의 물체에 가려진다.
    // - 가산 블렌딩: 산란광은 뒤를 가리는 게 아니라 빛을 더하는 것이다.
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    depthStencil.depthWriteEnable = VK_FALSE;
    colorBlendAttachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment[1].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &atmospherePipeline);

    vkDestroyShaderModule(device, fragShaderModule, nullptr); vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void SolarSystemApp::createLockedOrbitBuffer() {
    VkDeviceSize bufferSize = sizeof(Vertex) * (LOCKED_ORBIT_SEGMENTS + 1);
    createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 lockedOrbitBuffer, lockedOrbitMemory);
    vkMapMemory(device, lockedOrbitMemory, 0, bufferSize, 0, &lockedOrbitMapped);
}

void SolarSystemApp::createVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexBuffer, vertexBufferMemory);
    void *data;
    vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(device, vertexBufferMemory);
}

void SolarSystemApp::createIndexBuffer() {
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
    createBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indexBuffer, indexBufferMemory);
    void *data;
    vkMapMemory(device, indexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vkUnmapMemory(device, indexBufferMemory);
}

void SolarSystemApp::createUniformBuffer() { VkDeviceSize bufferSize = sizeof(UniformBufferObject); createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffer, uniformBufferMemory); vkMapMemory(device, uniformBufferMemory, 0, bufferSize, 0, &uniformBufferMapped); }

void SolarSystemApp::createTextureSampler() {
    // 이방성 필터링은 디바이스 기능으로 이미 켜져 있다(createLogicalDevice의 samplerAnisotropy).
    // 구체에 감긴 텍스처는 시선이 비스듬히 닿는 가장자리에서 특히 뭉개지므로 효과가 크다.
    VkPhysicalDeviceProperties devProps{};
    vkGetPhysicalDeviceProperties(physicalDevice, &devProps);

    VkSamplerCreateInfo samplerInfo{}; samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR; samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT; samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    // 8x와 16x는 눈으로 구분이 거의 안 되는데 페치 비용은 두 배 차이다.
    // 성능이 아쉬우면 이 숫자를 4.0f로 내리거나 anisotropyEnable을 끄면 된다.
    samplerInfo.maxAnisotropy = std::min(8.0f, devProps.limits.maxSamplerAnisotropy);
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    // 밉 레벨이 1개뿐인 이미지(더미·UI 아이콘·스카이박스)는 뷰의 levelCount가 알아서
    // 클램프하므로, 여기서 상한을 풀어도 안전하다.
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler);
}
