#include "VulkanBase.hpp"
#include "Camera.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <chrono>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
#include <windows.h>
#endif

struct PushConstants {
    alignas(16) glm::mat4 model;
    int objectType; 
};

struct UniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
    alignas(16) glm::vec3 cameraPos;
    float time;
    float deltaTime;
};

struct Planet {
    std::string name; int typeId;
    float radius; float orbitRadius; float orbitSpeed; float eccentricity; float rotationSpeed; float axialTilt; bool hasClouds;
    glm::mat4 currentModelMat; glm::vec3 currentPosition; glm::mat4 cloudModelMat;
    VkImageView viewDiffuse, viewNight, viewSpecular, viewNormal, viewClouds;
    VkDescriptorSet descriptorSet;
};

class SolarSystemApp : public VulkanBase {
private:
    Camera camera;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    VkPipeline linePipeline;
    VkPipeline transparentPipeline; // 레이마칭 볼륨을 담을 투명 껍질용 파이프라인
    VkDescriptorPool descriptorPool;

    VkBuffer vertexBuffer; VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer; VkDeviceMemory indexBufferMemory;
    VkBuffer uniformBuffer; VkDeviceMemory uniformBufferMemory;
    void *uniformBufferMapped;

    VkSampler textureSampler;
    VkImage texSkybox, texDummyBlack, texDummyFlatNormal;
    VkDeviceMemory memSkybox, memDummyBlack, memDummyFlatNormal;
    VkImageView viewSkybox, viewDummyBlack, viewDummyFlatNormal;

    Planet sun, moon, saturnRing;
    std::vector<Planet> planets;

    uint32_t sphereIndexCount = 0, ringIndexCount = 0, orbitIndexCount = 0, orbitFirstIndex = 0;
    int32_t ringVertexOffset = 0, orbitVertexOffset = 0;

    int lockedTargetType = 1; int lockedPlanetIndex = 2;
    std::chrono::time_point<std::chrono::high_resolution_clock> lastClickTime;
    float currentAppTime = 0.0f;
    bool isPaused = false;

    std::vector<VkImage> allImages;
    std::vector<VkDeviceMemory> allMemories;
    std::vector<VkImageView> allViews;

    VkImage offscreenColorImage, offscreenBrightImage, offscreenDepthImage;
    VkDeviceMemory offscreenColorMem, offscreenBrightMem, offscreenDepthMem;
    VkImageView offscreenColorView, offscreenBrightView, offscreenDepthView;
    VkRenderPass offscreenRenderPass;
    VkFramebuffer offscreenFramebuffer;
    VkSampler offscreenSampler;

    VkImage blurImages[2];
    VkDeviceMemory blurMemories[2];
    VkImageView blurViews[2];
    VkRenderPass blurRenderPass;
    VkFramebuffer blurFramebuffers[2];
    VkDescriptorSetLayout blurDescriptorSetLayout;
    VkPipelineLayout blurPipelineLayout;
    VkPipeline blurPipeline;
    VkDescriptorSet blurDescriptorSets[3];

    VkDescriptorSetLayout postDescriptorSetLayout;
    VkPipelineLayout postPipelineLayout;
    VkPipeline postPipeline;
    VkDescriptorSet postDescriptorSet;

protected:
    void onMouseButton(int button, int action, int mods) override {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS) {
                camera.mousePressed = true;
                glfwGetCursorPos(window, &camera.lastX, &camera.lastY);
                auto now = std::chrono::high_resolution_clock::now();
                bool isDoubleClick = (std::chrono::duration<float>(now - lastClickTime).count() < 0.3f);
                handleRaycast(camera.lastX, camera.lastY, isDoubleClick);
                lastClickTime = now;
            } else if (action == GLFW_RELEASE) camera.mousePressed = false;
        }
    }
    void onMouseMove(double xpos, double ypos) override { camera.processMouseDrag(xpos, ypos); }
    void onMouseScroll(double xoffset, double yoffset) override { camera.processMouseScroll(yoffset); }

    void handleRaycast(double xpos, double ypos, bool isDoubleClick) {
        float ndcX = (2.0f * (float)xpos) / swapChainExtent.width - 1.0f;
        float ndcY = (2.0f * (float)ypos) / swapChainExtent.height - 1.0f;
        glm::mat4 proj = glm::perspective(glm::radians(camera.fov), swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 200.0f);
        proj[1][1] *= -1;
        glm::mat4 view = camera.getViewMatrix();
        glm::vec4 eyeCoords = glm::inverse(proj) * glm::vec4(ndcX, ndcY, 0.5f, 1.0f);
        glm::vec3 rayWorldDir = glm::normalize(glm::vec3(glm::inverse(view) * glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f)));
        glm::vec3 rayOrigin = camera.target + camera.pos;

        float minT = FLT_MAX; int hitType = -1; int hitIndex = -1;
        auto checkIntersect = [&](glm::vec3 center, float radius, int type, int idx) {
            glm::vec3 oc = rayOrigin - center;
            float a = glm::dot(rayWorldDir, rayWorldDir);
            float b = 2.0f * glm::dot(oc, rayWorldDir);
            float c = glm::dot(oc, oc) - radius * radius;
            float discriminant = b * b - 4 * a * c;
            if (discriminant > 0) {
                float t = (-b - sqrt(discriminant)) / (2.0f * a);
                if (t > 0 && t < minT) { minT = t; hitType = type; hitIndex = idx; }
            }
        };

        checkIntersect(sun.currentPosition, sun.radius, 0, -1);
        for (int i = 0; i < planets.size(); i++) checkIntersect(planets[i].currentPosition, planets[i].radius, 1, i);
        checkIntersect(moon.currentPosition, moon.radius, 2, -1);

        if (hitType != -1) { 
            if (isDoubleClick) { lockedTargetType = hitType; lockedPlanetIndex = hitIndex; camera.targetDistance = (hitType == 0 ? sun.radius : (hitType == 1 ? planets[hitIndex].radius : moon.radius)) * 6.0f; }
        }
    }

    void initApp() override {
        generateSphere(1.0f, 64, 64);
        sphereIndexCount = static_cast<uint32_t>(indices.size());
        ringVertexOffset = static_cast<int32_t>(vertices.size());
        generateRing(1.2f, 2.2f, 64);
        ringIndexCount = static_cast<uint32_t>(indices.size()) - sphereIndexCount;
        orbitVertexOffset = static_cast<int32_t>(vertices.size());
        orbitFirstIndex = static_cast<uint32_t>(indices.size());
        generateOrbit(128); 
        orbitIndexCount = static_cast<uint32_t>(indices.size()) - orbitFirstIndex;

        createCubeTextureImage();
        createTextureSampler();
        createColorTexture(0, 0, 0, 0, texDummyBlack, memDummyBlack, viewDummyBlack, VK_FORMAT_R8G8B8A8_UNORM);
        createColorTexture(128, 128, 255, 255, texDummyFlatNormal, memDummyFlatNormal, viewDummyFlatNormal, VK_FORMAT_R8G8B8A8_UNORM);

        createVertexBuffer(); createIndexBuffer(); createUniformBuffer();
        createDescriptorSetLayout(); createDescriptorPool();

        createOffscreenResources();
        createBlurResources();       
        createBlurPipeline();        
        createPostProcessPipeline(); 
        createGraphicsPipeline();

        sun = createPlanet("Sun", 4, 1.5f, 0.0f, 0.0f, 0.0f, 2.0f, 7.25f, false, "textures/sun.jpg", "", "", "", "");
        planets.push_back(createPlanet("Mercury", 0, 0.076f, 2.5f, 16.0f, 0.205f, 5.0f, 0.03f, false, "textures/mercury.jpg", "", "", "", ""));
        planets.push_back(createPlanet("Venus", 0, 0.19f, 3.5f, 12.0f, 0.006f, 2.0f, 177.36f, true, "textures/venus.jpg", "", "", "", "textures/venus_atmosphere.jpg"));
        planets.push_back(createPlanet("Earth", 0, 0.2f, 5.0f, 10.0f, 0.016f, 25.0f, 23.44f, true, "textures/earth.jpg", "textures/earth_night.jpg", "textures/earth_specular.jpg", "textures/earth_normal.jpg", "textures/earth_clouds.jpg"));
        planets.push_back(createPlanet("Mars", 0, 0.106f, 6.5f, 8.0f, 0.093f, 24.0f, 25.19f, false, "textures/mars.jpg", "", "", "", ""));
        planets.push_back(createPlanet("Jupiter", 0, 0.7f, 12.0f, 4.0f, 0.048f, 60.0f, 3.13f, false, "textures/jupiter.jpg", "", "", "", ""));
        planets.push_back(createPlanet("Saturn", 0, 0.58f, 18.0f, 3.0f, 0.056f, 55.0f, 26.73f, false, "textures/saturn.jpg", "", "", "", "")); 
        planets.push_back(createPlanet("Uranus", 0, 0.35f, 24.0f, 2.0f, 0.046f, 40.0f, 97.77f, false, "textures/uranus.jpg", "", "", "", ""));
        planets.push_back(createPlanet("Neptune", 0, 0.34f, 30.0f, 1.5f, 0.009f, 38.0f, 28.32f, false, "textures/neptune.jpg", "", "", "", ""));
        moon = createPlanet("Moon", 1, 0.055f, 0.0f, 0.0f, 0.054f, 0.0f, 6.68f, false, "textures/moon.jpg", "", "", "textures/moon_disp.jpg", "");
        saturnRing = createPlanet("SaturnRing", 5, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 26.73f, false, "textures/saturn_ring.png", "", "", "", "");
    }

    void createOffscreenResources() {
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT; VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        auto makeImg = [&](VkFormat fmt, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem, VkImageView& view, VkImageAspectFlags aspect) {
            VkImageCreateInfo iInfo{}; iInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; iInfo.imageType = VK_IMAGE_TYPE_2D; iInfo.extent.width = swapChainExtent.width; iInfo.extent.height = swapChainExtent.height; iInfo.extent.depth = 1; iInfo.mipLevels = 1; iInfo.arrayLayers = 1; iInfo.format = fmt; iInfo.tiling = VK_IMAGE_TILING_OPTIMAL; iInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; iInfo.usage = usage; iInfo.samples = VK_SAMPLE_COUNT_1_BIT; iInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateImage(device, &iInfo, nullptr, &img);
            VkMemoryRequirements mReqs; vkGetImageMemoryRequirements(device, img, &mReqs);
            VkMemoryAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; aInfo.allocationSize = mReqs.size; aInfo.memoryTypeIndex = findMemoryType(mReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device, &aInfo, nullptr, &mem); vkBindImageMemory(device, img, mem, 0);
            VkImageViewCreateInfo vInfo{}; vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vInfo.image = img; vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; vInfo.format = fmt; vInfo.subresourceRange.aspectMask = aspect; vInfo.subresourceRange.baseMipLevel = 0; vInfo.subresourceRange.levelCount = 1; vInfo.subresourceRange.baseArrayLayer = 0; vInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &vInfo, nullptr, &view);
        };
        makeImg(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, offscreenColorImage, offscreenColorMem, offscreenColorView, VK_IMAGE_ASPECT_COLOR_BIT);
        makeImg(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, offscreenBrightImage, offscreenBrightMem, offscreenBrightView, VK_IMAGE_ASPECT_COLOR_BIT);
        makeImg(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, offscreenDepthImage, offscreenDepthMem, offscreenDepthView, VK_IMAGE_ASPECT_DEPTH_BIT);

        std::array<VkAttachmentDescription, 3> atts{};
        atts[0].format = colorFormat; atts[0].samples = VK_SAMPLE_COUNT_1_BIT; atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE; atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        atts[1] = atts[0]; 
        atts[2].format = depthFormat; atts[2].samples = VK_SAMPLE_COUNT_1_BIT; atts[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colRefs[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}}; VkAttachmentReference depRef{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 2; subpass.pColorAttachments = colRefs; subpass.pDepthStencilAttachment = &depRef;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0; deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL; deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpInfo{}; rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpInfo.attachmentCount = 3; rpInfo.pAttachments = atts.data(); rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass; rpInfo.dependencyCount = 2; rpInfo.pDependencies = deps.data();
        vkCreateRenderPass(device, &rpInfo, nullptr, &offscreenRenderPass);

        std::array<VkImageView, 3> fbAtts = {offscreenColorView, offscreenBrightView, offscreenDepthView};
        VkFramebufferCreateInfo fbInfo{}; fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fbInfo.renderPass = offscreenRenderPass; fbInfo.attachmentCount = 3; fbInfo.pAttachments = fbAtts.data(); fbInfo.width = swapChainExtent.width; fbInfo.height = swapChainExtent.height; fbInfo.layers = 1;
        vkCreateFramebuffer(device, &fbInfo, nullptr, &offscreenFramebuffer);

        VkSamplerCreateInfo sInfo{}; sInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO; sInfo.magFilter = VK_FILTER_LINEAR; sInfo.minFilter = VK_FILTER_LINEAR; sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(device, &sInfo, nullptr, &offscreenSampler);
    }

    void createBlurResources() {
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        auto makeImg = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
            VkImageCreateInfo iInfo{}; iInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; iInfo.imageType = VK_IMAGE_TYPE_2D; iInfo.extent.width = swapChainExtent.width; iInfo.extent.height = swapChainExtent.height; iInfo.extent.depth = 1; iInfo.mipLevels = 1; iInfo.arrayLayers = 1; iInfo.format = colorFormat; iInfo.tiling = VK_IMAGE_TILING_OPTIMAL; iInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; iInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; iInfo.samples = VK_SAMPLE_COUNT_1_BIT; iInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; vkCreateImage(device, &iInfo, nullptr, &img);
            VkMemoryRequirements mReqs; vkGetImageMemoryRequirements(device, img, &mReqs); VkMemoryAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; aInfo.allocationSize = mReqs.size; aInfo.memoryTypeIndex = findMemoryType(mReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); vkAllocateMemory(device, &aInfo, nullptr, &mem); vkBindImageMemory(device, img, mem, 0);
            VkImageViewCreateInfo vInfo{}; vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vInfo.image = img; vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; vInfo.format = colorFormat; vInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vInfo.subresourceRange.baseMipLevel = 0; vInfo.subresourceRange.levelCount = 1; vInfo.subresourceRange.baseArrayLayer = 0; vInfo.subresourceRange.layerCount = 1; vkCreateImageView(device, &vInfo, nullptr, &view);
        };
        for(int i = 0; i < 2; i++) makeImg(blurImages[i], blurMemories[i], blurViews[i]);

        VkAttachmentDescription att{}; att.format = colorFormat; att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &colorRef;
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0; deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL; deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo rpInfo{}; rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpInfo.attachmentCount = 1; rpInfo.pAttachments = &att; rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass; rpInfo.dependencyCount = 2; rpInfo.pDependencies = deps.data(); vkCreateRenderPass(device, &rpInfo, nullptr, &blurRenderPass);
        for(int i = 0; i < 2; i++) { VkFramebufferCreateInfo fbInfo{}; fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fbInfo.renderPass = blurRenderPass; fbInfo.attachmentCount = 1; fbInfo.pAttachments = &blurViews[i]; fbInfo.width = swapChainExtent.width; fbInfo.height = swapChainExtent.height; fbInfo.layers = 1; vkCreateFramebuffer(device, &fbInfo, nullptr, &blurFramebuffers[i]); }
    }

    void createBlurPipeline() {
        VkDescriptorSetLayoutBinding b{}; b.binding = 0; b.descriptorCount = 1; b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layInfo{}; layInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; layInfo.bindingCount = 1; layInfo.pBindings = &b; vkCreateDescriptorSetLayout(device, &layInfo, nullptr, &blurDescriptorSetLayout);
        VkDescriptorSetAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; aInfo.descriptorPool = descriptorPool; aInfo.descriptorSetCount = 3; std::vector<VkDescriptorSetLayout> layouts(3, blurDescriptorSetLayout); aInfo.pSetLayouts = layouts.data(); vkAllocateDescriptorSets(device, &aInfo, blurDescriptorSets);
        auto mI = [&](VkImageView v){ VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = offscreenSampler; return i; };
        VkDescriptorImageInfo i0 = mI(offscreenBrightView); VkDescriptorImageInfo i1 = mI(blurViews[0]); VkDescriptorImageInfo i2 = mI(blurViews[1]);
        std::array<VkWriteDescriptorSet, 3> wr{}; for(int i=0; i<3; i++) { wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr[i].dstSet = blurDescriptorSets[i]; wr[i].dstBinding = 0; wr[i].descriptorCount = 1; wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; }
        wr[0].pImageInfo = &i0; wr[1].pImageInfo = &i1; wr[2].pImageInfo = &i2; vkUpdateDescriptorSets(device, 3, wr.data(), 0, nullptr);
        VkPushConstantRange pc{}; pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; pc.offset = 0; pc.size = sizeof(int);
        VkPipelineLayoutCreateInfo pLayInfo{}; pLayInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pLayInfo.setLayoutCount = 1; pLayInfo.pSetLayouts = &blurDescriptorSetLayout; pLayInfo.pushConstantRangeCount = 1; pLayInfo.pPushConstantRanges = &pc; vkCreatePipelineLayout(device, &pLayInfo, nullptr, &blurPipelineLayout);
        auto vCode = readFile("shaders/post_vert.spv"); auto fCode = readFile("shaders/blur_frag.spv");
        VkShaderModule vMod = createShaderModule(vCode); VkShaderModule fMod = createShaderModule(fCode);
        VkPipelineShaderStageCreateInfo vS{}; vS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; vS.stage = VK_SHADER_STAGE_VERTEX_BIT; vS.module = vMod; vS.pName = "main";
        VkPipelineShaderStageCreateInfo fS{}; fS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; fS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fS.module = fMod; fS.pName = "main"; VkPipelineShaderStageCreateInfo sS[] = {vS, fS};
        VkPipelineVertexInputStateCreateInfo vI{}; vI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo iA{}; iA.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; iA.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{}; vp.width = swapChainExtent.width; vp.height = swapChainExtent.height; vp.maxDepth = 1.0f; VkRect2D sc{}; sc.extent = swapChainExtent; VkPipelineViewportStateCreateInfo vpS{}; vpS.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vpS.viewportCount = 1; vpS.pViewports = &vp; vpS.scissorCount = 1; vpS.pScissors = &sc;
        VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; ds.depthTestEnable = VK_FALSE;
        VkPipelineColorBlendAttachmentState cbA{}; cbA.colorWriteMask = 0xF; cbA.blendEnable = VK_FALSE; VkPipelineColorBlendStateCreateInfo cbS{}; cbS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cbS.attachmentCount = 1; cbS.pAttachments = &cbA;
        VkGraphicsPipelineCreateInfo pInfo{}; pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pInfo.stageCount = 2; pInfo.pStages = sS; pInfo.pVertexInputState = &vI; pInfo.pInputAssemblyState = &iA; pInfo.pViewportState = &vpS; pInfo.pRasterizationState = &rs; pInfo.pMultisampleState = &ms; pInfo.pDepthStencilState = &ds; pInfo.pColorBlendState = &cbS; pInfo.layout = blurPipelineLayout; pInfo.renderPass = blurRenderPass; vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &blurPipeline);
        vkDestroyShaderModule(device, fMod, nullptr); vkDestroyShaderModule(device, vMod, nullptr);
    }

    void createPostProcessPipeline() {
        std::array<VkDescriptorSetLayoutBinding, 2> b{}; b[0].binding = 0; b[0].descriptorCount = 1; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; b[1].binding = 1; b[1].descriptorCount = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layInfo{}; layInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; layInfo.bindingCount = 2; layInfo.pBindings = b.data(); vkCreateDescriptorSetLayout(device, &layInfo, nullptr, &postDescriptorSetLayout);
        VkDescriptorSetAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; aInfo.descriptorPool = descriptorPool; aInfo.descriptorSetCount = 1; aInfo.pSetLayouts = &postDescriptorSetLayout; vkAllocateDescriptorSets(device, &aInfo, &postDescriptorSet);
        auto mI = [&](VkImageView v){ VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = offscreenSampler; return i; };
        VkDescriptorImageInfo cI = mI(offscreenColorView); VkDescriptorImageInfo brI = mI(blurViews[1]); 
        std::array<VkWriteDescriptorSet, 2> wr{}; wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr[0].dstSet = postDescriptorSet; wr[0].dstBinding = 0; wr[0].descriptorCount = 1; wr[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr[0].pImageInfo = &cI; wr[1] = wr[0]; wr[1].dstBinding = 1; wr[1].pImageInfo = &brI; vkUpdateDescriptorSets(device, 2, wr.data(), 0, nullptr);
        VkPipelineLayoutCreateInfo pLayInfo{}; pLayInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pLayInfo.setLayoutCount = 1; pLayInfo.pSetLayouts = &postDescriptorSetLayout; vkCreatePipelineLayout(device, &pLayInfo, nullptr, &postPipelineLayout);
        auto vCode = readFile("shaders/post_vert.spv"); auto fCode = readFile("shaders/post_frag.spv");
        VkShaderModule vMod = createShaderModule(vCode); VkShaderModule fMod = createShaderModule(fCode);
        VkPipelineShaderStageCreateInfo vS{}; vS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; vS.stage = VK_SHADER_STAGE_VERTEX_BIT; vS.module = vMod; vS.pName = "main";
        VkPipelineShaderStageCreateInfo fS{}; fS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; fS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fS.module = fMod; fS.pName = "main"; VkPipelineShaderStageCreateInfo sS[] = {vS, fS};
        VkPipelineVertexInputStateCreateInfo vI{}; vI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; VkPipelineInputAssemblyStateCreateInfo iA{}; iA.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; iA.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{}; vp.width = swapChainExtent.width; vp.height = swapChainExtent.height; vp.maxDepth = 1.0f; VkRect2D sc{}; sc.extent = swapChainExtent; VkPipelineViewportStateCreateInfo vpS{}; vpS.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vpS.viewportCount = 1; vpS.pViewports = &vp; vpS.scissorCount = 1; vpS.pScissors = &sc;
        VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE; VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; ds.depthTestEnable = VK_FALSE; VkPipelineColorBlendAttachmentState cbA{}; cbA.colorWriteMask = 0xF; cbA.blendEnable = VK_FALSE; VkPipelineColorBlendStateCreateInfo cbS{}; cbS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cbS.attachmentCount = 1; cbS.pAttachments = &cbA;
        VkGraphicsPipelineCreateInfo pInfo{}; pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pInfo.stageCount = 2; pInfo.pStages = sS; pInfo.pVertexInputState = &vI; pInfo.pInputAssemblyState = &iA; pInfo.pViewportState = &vpS; pInfo.pRasterizationState = &rs; pInfo.pMultisampleState = &ms; pInfo.pDepthStencilState = &ds; pInfo.pColorBlendState = &cbS; pInfo.layout = postPipelineLayout; pInfo.renderPass = renderPass; vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &postPipeline);
        vkDestroyShaderModule(device, fMod, nullptr); vkDestroyShaderModule(device, vMod, nullptr);
    }

    void createGraphicsPipeline() {
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
        VkPipelineRasterizationStateCreateInfo rasterizer{}; rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rasterizer.polygonMode = VK_POLYGON_MODE_FILL; rasterizer.lineWidth = 1.0f; rasterizer.cullMode = VK_CULL_MODE_NONE; rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        VkPipelineMultisampleStateCreateInfo multisampling{}; multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depthStencil{}; depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; depthStencil.depthTestEnable = VK_TRUE; depthStencil.depthWriteEnable = VK_TRUE; depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        
        VkPipelineColorBlendAttachmentState colorBlendAttachment[2]{};
        colorBlendAttachment[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; colorBlendAttachment[0].blendEnable = VK_TRUE; colorBlendAttachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; colorBlendAttachment[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; colorBlendAttachment[0].colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment[1] = colorBlendAttachment[0]; 

        VkPipelineColorBlendStateCreateInfo colorBlending{}; colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; colorBlending.attachmentCount = 2; colorBlending.pAttachments = colorBlendAttachment;

        VkPushConstantRange pushConstantRange{}; pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; pushConstantRange.offset = 0; pushConstantRange.size = sizeof(PushConstants);
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{}; pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pipelineLayoutInfo.setLayoutCount = 1; pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout; pipelineLayoutInfo.pushConstantRangeCount = 1; pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
        
        VkGraphicsPipelineCreateInfo pipelineInfo{}; pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pipelineInfo.stageCount = 2; pipelineInfo.pStages = shaderStages; pipelineInfo.pVertexInputState = &vertexInputInfo; pipelineInfo.pInputAssemblyState = &inputAssembly; pipelineInfo.pViewportState = &viewportState; pipelineInfo.pRasterizationState = &rasterizer; pipelineInfo.pMultisampleState = &multisampling; pipelineInfo.pDepthStencilState = &depthStencil; pipelineInfo.pColorBlendState = &colorBlending; pipelineInfo.layout = pipelineLayout; pipelineInfo.renderPass = offscreenRenderPass;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);

        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &linePipeline);

        // [핵심 보존] 볼류메트릭 레이마칭(3D 볼륨)이 다른 물체를 가리지 않게 투명 파이프라인으로 그립니다.
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        depthStencil.depthWriteEnable = VK_FALSE; 
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &transparentPipeline);

        vkDestroyShaderModule(device, fragShaderModule, nullptr); vkDestroyShaderModule(device, vertShaderModule, nullptr);
    }

    void updateUniformBuffer() override {
        static auto lastTimePoint = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTimePoint).count();
        lastTimePoint = currentTime;

        static bool spacePressedLastFrame = false;
        bool spacePressed = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spacePressed && !spacePressedLastFrame) isPaused = !isPaused;
        spacePressedLastFrame = spacePressed;
        if (!isPaused) currentAppTime += deltaTime;
        float time = currentAppTime; 

        sun.currentModelMat = glm::rotate(glm::mat4(1.0f), glm::radians(sun.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f)) * glm::rotate(glm::mat4(1.0f), time * glm::radians(sun.rotationSpeed), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(sun.radius));
        sun.currentPosition = glm::vec3(0.0f);

        for (int i = 0; i < planets.size(); i++) {
            auto &planet = planets[i];
            float a = planet.orbitRadius; float e = planet.eccentricity; float b = a * sqrt(1.0f - e * e); float c = a * e; float angle = time * glm::radians(planet.orbitSpeed);
            glm::vec3 localPos = glm::vec3(a * cos(angle) - c, 0.0f, b * sin(angle));
            glm::mat4 parentTrajectory = glm::translate(glm::mat4(1.0f), localPos);
            glm::mat4 selfRotationMat = glm::rotate(glm::mat4(1.0f), glm::radians(planet.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f)) * glm::rotate(glm::mat4(1.0f), time * glm::radians(planet.rotationSpeed), glm::vec3(0.0f, 1.0f, 0.0f));
            planet.currentModelMat = parentTrajectory * selfRotationMat * glm::scale(glm::mat4(1.0f), glm::vec3(planet.radius));
            planet.currentPosition = glm::vec3(parentTrajectory[3]);

            if (planet.hasClouds) {
                glm::mat4 cloudSelfRot = glm::rotate(glm::mat4(1.0f), glm::radians(planet.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f)) * glm::rotate(glm::mat4(1.0f), time * glm::radians(planet.rotationSpeed + 4.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                planet.cloudModelMat = parentTrajectory * cloudSelfRot * glm::scale(glm::mat4(1.0f), glm::vec3(planet.radius * 1.005f));
            }
            if (i == 2) { 
                float ma = 0.75f, me = moon.eccentricity, mb = ma * sqrt(1.0f - me * me), mc = ma * me, mAngle = time * glm::radians(45.0f);
                glm::mat4 moonBase = parentTrajectory * glm::translate(glm::mat4(1.0f), glm::vec3(ma * cos(mAngle) - mc, 0.0f, mb * sin(mAngle)));
                moon.currentModelMat = moonBase * glm::rotate(glm::mat4(1.0f), glm::radians(moon.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(moon.radius));
                moon.currentPosition = glm::vec3(moonBase[3]);
            }
        }

        glm::vec3 nextTarget = camera.target;
        if (lockedTargetType == 0) nextTarget = sun.currentPosition;
        else if (lockedTargetType == 1 && lockedPlanetIndex != -1) nextTarget = planets[lockedPlanetIndex].currentPosition; 
        else if (lockedTargetType == 2) nextTarget = moon.currentPosition;

        float targetRadius = 1.0f;
        if (lockedTargetType == 0) targetRadius = sun.radius;
        else if (lockedTargetType == 1 && lockedPlanetIndex != -1) targetRadius = planets[lockedPlanetIndex].radius;
        else if (lockedTargetType == 2) targetRadius = moon.radius;
        
        camera.minDistance = targetRadius + 0.2f;
        camera.smoothFollow(nextTarget, deltaTime);
        camera.update(deltaTime);
        
        UniformBufferObject ubo{};
        ubo.view = camera.getViewMatrix();
        ubo.proj = glm::perspective(glm::radians(camera.fov), swapChainExtent.width / (float)swapChainExtent.height, 0.01f, 200.0f);
        ubo.proj[1][1] *= -1;
        ubo.cameraPos = camera.target + camera.pos; 
        ubo.time = time;
        ubo.deltaTime = deltaTime;
        memcpy(uniformBufferMapped, &ubo, sizeof(ubo));
    }

    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex) override {
        VkCommandBufferBeginInfo beginInfo{}; beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cb, &beginInfo);

        VkRenderPassBeginInfo offscreenPassInfo{};
        offscreenPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        offscreenPassInfo.renderPass = offscreenRenderPass;
        offscreenPassInfo.framebuffer = offscreenFramebuffer;
        offscreenPassInfo.renderArea.offset = {0, 0}; offscreenPassInfo.renderArea.extent = swapChainExtent;

        std::array<VkClearValue, 3> clearValues{};
        clearValues[0].color = {{0.01f, 0.01f, 0.01f, 1.0f}}; 
        clearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};    
        clearValues[2].depthStencil = {1.0f, 0};

        offscreenPassInfo.clearValueCount = 3;
        offscreenPassInfo.pClearValues = clearValues.data();
        vkCmdBeginRenderPass(cb, &offscreenPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        VkBuffer vertexBuffers[] = {vertexBuffer}; VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cb, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        auto drawObject = [&](const Planet &p, glm::mat4 mat, int type) {
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &p.descriptorSet, 0, nullptr);
            PushConstants pc{mat, type};
            vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
            vkCmdDrawIndexed(cb, sphereIndexCount, 1, 0, 0, 0); 
        };

        drawObject(sun, sun.currentModelMat, sun.typeId);
        for (const auto &planet : planets) {
            drawObject(planet, planet.currentModelMat, planet.typeId);
            if (planet.hasClouds) drawObject(planet, planet.cloudModelMat, 2);
            if (planet.name == "Saturn") {
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &saturnRing.descriptorSet, 0, nullptr);
                PushConstants ringPush{planet.currentModelMat, 5}; 
                vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &ringPush);
                vkCmdDrawIndexed(cb, ringIndexCount, 1, sphereIndexCount, ringVertexOffset, 0);
            }
        }
        drawObject(moon, moon.currentModelMat, moon.typeId);
        drawObject(sun, glm::rotate(glm::mat4(1.0f), currentAppTime * glm::radians(0.5f), glm::vec3(0.0f, 1.0f, 0.0f)), 3);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sun.descriptorSet, 0, nullptr);
        for (const auto &planet : planets) {
            float a = planet.orbitRadius, e = planet.eccentricity, b = a * sqrt(1.0f - e * e), c = a * e; 
            PushConstants orbitPush{glm::translate(glm::mat4(1.0f), glm::vec3(-c, 0.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(a, 1.0f, b)), 6};
            vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &orbitPush);
            vkCmdDrawIndexed(cb, orbitIndexCount, 1, orbitFirstIndex, orbitVertexOffset, 0);
        }
        float ma = 0.75f, me = moon.eccentricity, mb = ma * sqrt(1.0f - me * me), mc = ma * me;
        PushConstants moonOrbitPush{glm::translate(glm::mat4(1.0f), planets[2].currentPosition) * glm::translate(glm::mat4(1.0f), glm::vec3(-mc, 0.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(ma, 1.0f, mb)), 6}; 
        vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &moonOrbitPush);
        vkCmdDrawIndexed(cb, orbitIndexCount, 1, orbitFirstIndex, orbitVertexOffset, 0);

        // =========================================================================
        // [핵심] 볼류메트릭 레이마칭 영역 그리기
        // 이제 파티클이 아니라, 태양의 1.5배 크기에 달하는 거대한 '보이지 않는 3D 렌더링 돔'을 설치합니다.
        // 빛(Ray)은 이 돔 안으로 들어가면서 플라즈마 밀도를 적분 계산하게 됩니다.
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, transparentPipeline);
        glm::mat4 haloModel = glm::translate(glm::mat4(1.0f), sun.currentPosition) * glm::scale(glm::mat4(1.0f), glm::vec3(sun.radius * 1.5f));
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sun.descriptorSet, 0, nullptr);
        PushConstants haloPush{haloModel, 7};
        vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &haloPush);
        vkCmdDrawIndexed(cb, sphereIndexCount, 1, 0, 0, 0);
        // =========================================================================

        vkCmdEndRenderPass(cb); 

        bool horizontal = true;
        int blurAmount = 6; 
        
        for (int i = 0; i < blurAmount; i++) {
            VkRenderPassBeginInfo blurPassInfo{};
            blurPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            blurPassInfo.renderPass = blurRenderPass;
            blurPassInfo.framebuffer = blurFramebuffers[horizontal ? 0 : 1]; 
            blurPassInfo.renderArea.offset = {0, 0}; blurPassInfo.renderArea.extent = swapChainExtent;

            VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
            blurPassInfo.clearValueCount = 1; blurPassInfo.pClearValues = &clearColor;

            vkCmdBeginRenderPass(cb, &blurPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline);

            int setIndex = (i == 0) ? 0 : (horizontal ? 2 : 1);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipelineLayout, 0, 1, &blurDescriptorSets[setIndex], 0, nullptr);
            
            int horizInt = horizontal ? 1 : 0; 
            vkCmdPushConstants(cb, blurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &horizInt);
            vkCmdDraw(cb, 3, 1, 0, 0);
            vkCmdEndRenderPass(cb);

            horizontal = !horizontal; 
        }

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass; 
        renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0}; renderPassInfo.renderArea.extent = swapChainExtent;

        std::array<VkClearValue, 2> swapClear{};
        swapClear[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        swapClear[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = 2; renderPassInfo.pClearValues = swapClear.data();

        vkCmdBeginRenderPass(cb, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipelineLayout, 0, 1, &postDescriptorSet, 0, nullptr);
        vkCmdDraw(cb, 3, 1, 0, 0); 

        vkCmdEndRenderPass(cb);
        vkEndCommandBuffer(cb);
    }

    void cleanupApp() override {
        vkDestroySampler(device, textureSampler, nullptr);
        for (auto view : allViews) vkDestroyImageView(device, view, nullptr);
        for (auto img : allImages) vkDestroyImage(device, img, nullptr);
        for (auto mem : allMemories) vkFreeMemory(device, mem, nullptr);

        vkDestroyPipeline(device, transparentPipeline, nullptr);
        
        vkDestroyPipeline(device, blurPipeline, nullptr); vkDestroyPipelineLayout(device, blurPipelineLayout, nullptr); vkDestroyDescriptorSetLayout(device, blurDescriptorSetLayout, nullptr); vkDestroyRenderPass(device, blurRenderPass, nullptr);
        for(int i=0; i<2; i++) { vkDestroyFramebuffer(device, blurFramebuffers[i], nullptr); vkDestroyImageView(device, blurViews[i], nullptr); vkDestroyImage(device, blurImages[i], nullptr); vkFreeMemory(device, blurMemories[i], nullptr); }
        
        vkDestroyPipeline(device, postPipeline, nullptr); vkDestroyPipelineLayout(device, postPipelineLayout, nullptr); vkDestroyDescriptorSetLayout(device, postDescriptorSetLayout, nullptr); vkDestroyFramebuffer(device, offscreenFramebuffer, nullptr); vkDestroyRenderPass(device, offscreenRenderPass, nullptr); vkDestroySampler(device, offscreenSampler, nullptr);
        vkDestroyImageView(device, offscreenColorView, nullptr); vkDestroyImage(device, offscreenColorImage, nullptr); vkFreeMemory(device, offscreenColorMem, nullptr);
        vkDestroyImageView(device, offscreenBrightView, nullptr); vkDestroyImage(device, offscreenBrightImage, nullptr); vkFreeMemory(device, offscreenBrightMem, nullptr);
        vkDestroyImageView(device, offscreenDepthView, nullptr); vkDestroyImage(device, offscreenDepthImage, nullptr); vkFreeMemory(device, offscreenDepthMem, nullptr);

        vkDestroyImageView(device, viewSkybox, nullptr); vkDestroyImage(device, texSkybox, nullptr); vkFreeMemory(device, memSkybox, nullptr);
        vkDestroyImageView(device, viewDummyBlack, nullptr); vkDestroyImage(device, texDummyBlack, nullptr); vkFreeMemory(device, memDummyBlack, nullptr);
        vkDestroyImageView(device, viewDummyFlatNormal, nullptr); vkDestroyImage(device, texDummyFlatNormal, nullptr); vkFreeMemory(device, memDummyFlatNormal, nullptr);
        vkDestroyBuffer(device, indexBuffer, nullptr); vkFreeMemory(device, indexBufferMemory, nullptr);
        vkDestroyBuffer(device, vertexBuffer, nullptr); vkFreeMemory(device, vertexBufferMemory, nullptr);
        vkDestroyBuffer(device, uniformBuffer, nullptr); vkFreeMemory(device, uniformBufferMemory, nullptr);
        vkDestroyDescriptorPool(device, descriptorPool, nullptr); vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        vkDestroyPipeline(device, graphicsPipeline, nullptr); vkDestroyPipeline(device, linePipeline, nullptr); vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }

private:
    void createVertexBuffer() {
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
        createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexBuffer, vertexBufferMemory);
        void *data; vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), (size_t)bufferSize); vkUnmapMemory(device, vertexBufferMemory);
    }
    void createIndexBuffer() {
        VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
        createBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indexBuffer, indexBufferMemory);
        void *data; vkMapMemory(device, indexBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, indices.data(), (size_t)bufferSize); vkUnmapMemory(device, indexBufferMemory);
    }
    void createColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, VkImage &image, VkDeviceMemory &imageMemory, VkImageView &imageView, VkFormat format) {
        uint8_t pixels[4] = {r, g, b, a}; VkDeviceSize imageSize = 4; VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory; createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory); void *data; vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data); memcpy(data, pixels, static_cast<size_t>(imageSize)); vkUnmapMemory(device, stagingBufferMemory); createImage(1, 1, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, imageMemory); transitionImageLayout(image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL); copyBufferToImage(stagingBuffer, image, 1, 1); transitionImageLayout(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); vkDestroyBuffer(device, stagingBuffer, nullptr); vkFreeMemory(device, stagingBufferMemory, nullptr); imageView = createImageView(image, format, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    VkImageView loadTexture(const std::string &path, VkFormat format) {
        if (path.empty()) return (format == VK_FORMAT_R8G8B8A8_UNORM) ? viewDummyFlatNormal : viewDummyBlack;
        VkImage img; VkDeviceMemory mem; VkImageView view; int texWidth, texHeight, texChannels; stbi_uc *pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels) { std::cerr << "텍스처 없음, 더미 사용: " << path << "\n"; return viewDummyBlack; }
        VkDeviceSize imageSize = texWidth * texHeight * 4; VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory; createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory); void *data; vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data); memcpy(data, pixels, static_cast<size_t>(imageSize)); vkUnmapMemory(device, stagingBufferMemory); stbi_image_free(pixels);
        createImage(texWidth, texHeight, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, img, mem); transitionImageLayout(img, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL); copyBufferToImage(stagingBuffer, img, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight)); transitionImageLayout(img, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); vkDestroyBuffer(device, stagingBuffer, nullptr); vkFreeMemory(device, stagingBufferMemory, nullptr);
        view = createImageView(img, format, VK_IMAGE_ASPECT_COLOR_BIT); allImages.push_back(img); allMemories.push_back(mem); allViews.push_back(view); return view;
    }
    Planet createPlanet(std::string name, int typeId, float radius, float orbitRadius, float orbitSpeed, float eccentricity, float rotSpeed, float axialTilt, bool hasClouds, std::string diffuse, std::string night, std::string spec, std::string normal, std::string clouds) {
        Planet p; p.name = name; p.typeId = typeId; p.radius = radius; p.orbitRadius = orbitRadius; p.orbitSpeed = orbitSpeed; p.eccentricity = eccentricity; p.rotationSpeed = rotSpeed; p.axialTilt = axialTilt; p.hasClouds = hasClouds;
        p.viewDiffuse = loadTexture(diffuse, VK_FORMAT_R8G8B8A8_SRGB); p.viewNight = loadTexture(night, VK_FORMAT_R8G8B8A8_SRGB); p.viewSpecular = loadTexture(spec, VK_FORMAT_R8G8B8A8_UNORM); p.viewNormal = loadTexture(normal, VK_FORMAT_R8G8B8A8_UNORM); p.viewClouds = loadTexture(clouds, VK_FORMAT_R8G8B8A8_SRGB);
        VkDescriptorSetAllocateInfo allocInfo{}; allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; allocInfo.descriptorPool = descriptorPool; allocInfo.descriptorSetCount = 1; allocInfo.pSetLayouts = &descriptorSetLayout; vkAllocateDescriptorSets(device, &allocInfo, &p.descriptorSet);
        VkDescriptorBufferInfo bInfo{}; bInfo.buffer = uniformBuffer; bInfo.offset = 0; bInfo.range = sizeof(UniformBufferObject);
        auto mkImgInfo = [&](VkImageView v) { VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = textureSampler; return i; };
        VkDescriptorImageInfo iDiff = mkImgInfo(p.viewDiffuse), iNight = mkImgInfo(p.viewNight), iSpec = mkImgInfo(p.viewSpecular), iNorm = mkImgInfo(p.viewNormal), iCloud = mkImgInfo(p.viewClouds), iSky = mkImgInfo(viewSkybox);
        std::array<VkWriteDescriptorSet, 7> writes{}; for (int i = 0; i < 7; i++) { writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet = p.descriptorSet; writes[i].dstBinding = i; writes[i].descriptorCount = 1; }
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[0].pBufferInfo = &bInfo; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[1].pImageInfo = &iDiff; writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[2].pImageInfo = &iNight; writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[3].pImageInfo = &iSpec; writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[4].pImageInfo = &iNorm; writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[5].pImageInfo = &iCloud; writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[6].pImageInfo = &iSky;
        vkUpdateDescriptorSets(device, 7, writes.data(), 0, nullptr); return p;
    }
    
    // [RESTORED] 누락되었던 뼈대 함수들 완벽 복구
    void createDescriptorSetLayout() {
        std::array<VkDescriptorSetLayoutBinding, 7> b{};
        for (int i = 0; i < 7; i++) { b[i].binding = i; b[i].descriptorCount = 1; b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; }
        b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; b[0].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
        for (int i = 1; i < 7; i++) b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[4].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo{}; layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; layoutInfo.bindingCount = 7; layoutInfo.pBindings = b.data();
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);
    }
    void createDescriptorPool() {
        std::array<VkDescriptorPoolSize, 2> poolSizes{}; 
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; poolSizes[0].descriptorCount = 20; 
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; poolSizes[1].descriptorCount = 120;
        VkDescriptorPoolCreateInfo poolInfo{}; poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; 
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size()); poolInfo.pPoolSizes = poolSizes.data(); poolInfo.maxSets = 20;
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
    }
    void generateSphere(float radius, int sectorCount, int stackCount) {
        float x, y, z, xy; float sectorStep = 2 * M_PI / sectorCount; float stackStep = M_PI / stackCount; float sectorAngle, stackAngle;
        for (int i = 0; i <= stackCount; ++i) {
            stackAngle = M_PI / 2 - i * stackStep; xy = radius * cosf(stackAngle); z = radius * sinf(stackAngle);
            for (int j = 0; j <= sectorCount; ++j) {
                sectorAngle = j * sectorStep; x = xy * cosf(sectorAngle); y = xy * sinf(sectorAngle);
                vertices.push_back({glm::vec3(x, y, z), glm::vec3(1.0f), glm::vec2((float)j / sectorCount, (float)i / stackCount), glm::normalize(glm::vec3(x, y, z))});
            }
        }
        for (int i = 0; i < stackCount; ++i) {
            int k1 = i * (sectorCount + 1); int k2 = k1 + sectorCount + 1;
            for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
                if (i != 0) { indices.push_back(k1); indices.push_back(k2); indices.push_back(k1 + 1); }
                if (i != (stackCount - 1)) { indices.push_back(k1 + 1); indices.push_back(k2); indices.push_back(k2 + 1); }
            }
        }
    }
    void generateRing(float innerRadius, float outerRadius, int sectorCount) {
        for (int i = 0; i <= sectorCount; ++i) {
            float angle = (float)i / sectorCount * 2.0f * M_PI; float cosA = cosf(angle); float sinA = sinf(angle);
            vertices.push_back({glm::vec3(innerRadius * cosA, 0.0f, innerRadius * sinA), glm::vec3(1.0f), glm::vec2(0.0f, (float)i / sectorCount), glm::vec3(0, 1, 0)});
            vertices.push_back({glm::vec3(outerRadius * cosA, 0.0f, outerRadius * sinA), glm::vec3(1.0f), glm::vec2(1.0f, (float)i / sectorCount), glm::vec3(0, 1, 0)});
        }
        for (int i = 0; i < sectorCount; ++i) {
            int k1 = i * 2; int k2 = k1 + 1; int k3 = (i + 1) * 2; int k4 = k3 + 1;
            indices.push_back(k1); indices.push_back(k3); indices.push_back(k2); indices.push_back(k2); indices.push_back(k3); indices.push_back(k4);
        }
    }
    void generateOrbit(int segmentCount) {
        for (int i = 0; i <= segmentCount; ++i) {
            float angle = (float)i / segmentCount * 2.0f * M_PI;
            vertices.push_back({glm::vec3(cosf(angle), 0.0f, sinf(angle)), glm::vec3(1.0f), glm::vec2(0.0f), glm::vec3(0, 1, 0)});
            indices.push_back(i);
        }
    }
    void createCubeTextureImage() {
        std::vector<std::string> cubeFaces = {"textures/right.png", "textures/left.png", "textures/top.png", "textures/bottom.png", "textures/front.png", "textures/back.png"};
        int texWidth, texHeight, texChannels; stbi_uc *pixels[6];
        for (int i = 0; i < 6; i++) { pixels[i] = stbi_load(cubeFaces[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha); if (!pixels[i]) throw std::runtime_error("큐브맵 로드 실패"); }
        VkDeviceSize layerSize = texWidth * texHeight * 4; VkDeviceSize imageSize = layerSize * 6; VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
        void *data; vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        for (int i = 0; i < 6; i++) { memcpy(static_cast<char *>(data) + (layerSize * i), pixels[i], static_cast<size_t>(layerSize)); stbi_image_free(pixels[i]); }
        vkUnmapMemory(device, stagingBufferMemory);
        VkImageCreateInfo imageInfo{}; imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.extent.width = texWidth; imageInfo.extent.height = texHeight; imageInfo.extent.depth = 1; imageInfo.mipLevels = 1; imageInfo.arrayLayers = 6; imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB; imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL; imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateImage(device, &imageInfo, nullptr, &texSkybox);
        VkMemoryRequirements memReqs; vkGetImageMemoryRequirements(device, texSkybox, &memReqs); VkMemoryAllocateInfo allocInfo{}; allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocInfo.allocationSize = memReqs.size; allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &allocInfo, nullptr, &memSkybox); vkBindImageMemory(device, texSkybox, memSkybox, 0);
        transitionImageLayout(texSkybox, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6);
        VkCommandBuffer cb = beginSingleTimeCommands();
        std::vector<VkBufferImageCopy> bufferCopyRegions;
        for (uint32_t face = 0; face < 6; face++) { VkBufferImageCopy region{}; region.bufferOffset = layerSize * face; region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; region.imageSubresource.mipLevel = 0; region.imageSubresource.baseArrayLayer = face; region.imageSubresource.layerCount = 1; region.imageExtent = {(uint32_t)texWidth, (uint32_t)texHeight, 1}; bufferCopyRegions.push_back(region); }
        vkCmdCopyBufferToImage(cb, stagingBuffer, texSkybox, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bufferCopyRegions.size(), bufferCopyRegions.data());
        endSingleTimeCommands(cb);
        transitionImageLayout(texSkybox, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 6);
        vkDestroyBuffer(device, stagingBuffer, nullptr); vkFreeMemory(device, stagingBufferMemory, nullptr);
        viewSkybox = createImageView(texSkybox, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_CUBE, 6);
    }

    void createUniformBuffer() { VkDeviceSize bufferSize = sizeof(UniformBufferObject); createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffer, uniformBufferMemory); vkMapMemory(device, uniformBufferMemory, 0, bufferSize, 0, &uniformBufferMapped); }
    void createTextureSampler() { VkSamplerCreateInfo samplerInfo{}; samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO; samplerInfo.magFilter = VK_FILTER_LINEAR; samplerInfo.minFilter = VK_FILTER_LINEAR; samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT; samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT; samplerInfo.anisotropyEnable = VK_FALSE; samplerInfo.compareEnable = VK_FALSE; samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler); }
};

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    SolarSystemApp app;
    try { app.run(); } catch (const std::exception &e) { std::cerr << e.what() << std::endl; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}