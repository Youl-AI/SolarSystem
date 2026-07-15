# Fullscreen/Windowed Toggle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a borderless fullscreen⇄1024x768-windowed toggle button, visible in both the LOBBY and SIMULATION screens, to the Vulkan solar system app.

**Architecture:** The engine currently has zero resize support (fixed-size swapchain, static pipeline viewports). This plan first builds a minimal, safe swapchain-recreation path (dynamic viewport/scissor state on the 4 resolution-dependent pipelines instead of pipeline recreation; a `cleanupSwapChain()`/`recreateSwapChain()` pair; a frame-boundary hook so recreation never happens mid-frame), then wires a GLFW borderless-fullscreen toggle and an ImGui button on top of it.

**Tech Stack:** C++17, Vulkan 1.2, GLFW 3.3.8, Dear ImGui 1.90.4, CMake (MSVC/Windows).

## Global Constraints

- Fullscreen style: borderless windowed fullscreen (no OS exclusive fullscreen mode switch), matching the primary monitor's current video mode.
- Windowed size is always fixed at `WIDTH`x`HEIGHT` (1024x768). No user drag-resize.
- Only two states: fullscreen ↔ 1024x768 windowed. No keyboard shortcut (button only).
- No automated test suite exists for this project (it's a Vulkan rendering app). "Testing" in this plan means: (a) the project builds cleanly with `cmake --build build --config Debug`, and (b) manual runtime verification by running `build\Debug\VulkanApp.exe` and observing the described behavior. Every task must leave the app in a working, launchable state.
- Design reference: `docs/superpowers/specs/2026-07-16-fullscreen-toggle-design.md`

---

## Task 1: VulkanBase — resolution-aware swapchain + frame-start hook

**Files:**
- Modify: `src/VulkanBase.hpp:96-99` (virtual hooks section)
- Modify: `src/VulkanBase.hpp:144-152` (`mainLoop`)
- Modify: `src/VulkanBase.hpp:393-416` (`createSwapChain`)
- Modify: `src/VulkanBase.hpp:515-535` (`cleanupCore`)

**Interfaces:**
- Produces: `virtual void onFrameStart() {}` (empty default, overridable by derived classes)
- Produces: `void cleanupSwapChain()` (protected, destroys only swapchain/depth/framebuffers, not device/renderPass/commandPool)
- Produces: `createSwapChain()` now derives `swapChainExtent` from the live GLFW framebuffer size instead of the `WIDTH`/`HEIGHT` constants

- [ ] **Step 1: Add the `onFrameStart()` virtual hook**

In `src/VulkanBase.hpp`, find:

```cpp
    // [가상 함수] 마우스 이벤트 오버라이딩용
    virtual void onMouseButton(int button, int action, int mods) {}
    virtual void onMouseMove(double x, double y) {}
    virtual void onMouseScroll(double xoffset, double yoffset) {}
```

Replace with:

```cpp
    // [가상 함수] 마우스 이벤트 오버라이딩용
    virtual void onMouseButton(int button, int action, int mods) {}
    virtual void onMouseMove(double x, double y) {}
    virtual void onMouseScroll(double xoffset, double yoffset) {}

    // [가상 함수] 매 프레임 시작 시(glfwPollEvents 직후, drawFrame 이전) 호출되는 훅.
    // 스왑체인 재생성처럼 프레임 경계 밖에서만 안전한 작업을 여기서 처리한다.
    virtual void onFrameStart() {}
```

- [ ] **Step 2: Call the hook from `mainLoop()`**

Find:

```cpp
    void mainLoop()
    {
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
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
            glfwPollEvents();
            onFrameStart();
            drawFrame();
        }
        vkDeviceWaitIdle(device);
    }
```

- [ ] **Step 3: Make `createSwapChain()` use the live framebuffer size**

Find:

```cpp
    void createSwapChain()
    {
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = 2;
        swapChainImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
        swapChainExtent = {WIDTH, HEIGHT};
        createInfo.imageFormat = swapChainImageFormat;
```

Replace with:

```cpp
    void createSwapChain()
    {
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = 2;
        swapChainImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        swapChainExtent = {static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight)};
        createInfo.imageFormat = swapChainImageFormat;
```

- [ ] **Step 4: Extract `cleanupSwapChain()` and reuse it from `cleanupCore()`**

Find:

```cpp
    void cleanupCore()
    {
        vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
        vkDestroyFence(device, inFlightFence, nullptr);
        vkDestroyCommandPool(device, commandPool, nullptr);
        vkDestroyImageView(device, depthImageView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthImageMemory, nullptr);
        for (auto fb : swapChainFramebuffers)
            vkDestroyFramebuffer(device, fb, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);
        for (auto iv : swapChainImageViews)
            vkDestroyImageView(device, iv, nullptr);
        vkDestroySwapchainKHR(device, swapChain, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    }
```

Replace with:

```cpp
    void cleanupSwapChain()
    {
        for (auto fb : swapChainFramebuffers)
            vkDestroyFramebuffer(device, fb, nullptr);
        swapChainFramebuffers.clear();
        vkDestroyImageView(device, depthImageView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthImageMemory, nullptr);
        for (auto iv : swapChainImageViews)
            vkDestroyImageView(device, iv, nullptr);
        swapChainImageViews.clear();
        vkDestroySwapchainKHR(device, swapChain, nullptr);
    }

    void cleanupCore()
    {
        vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
        vkDestroyFence(device, inFlightFence, nullptr);
        vkDestroyCommandPool(device, commandPool, nullptr);
        cleanupSwapChain();
        vkDestroyRenderPass(device, renderPass, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    }
```

- [ ] **Step 5: Build and verify no compile errors**

Run: `cmake --build build --config Debug`
Expected: Build succeeds (`VulkanApp.vcxproj -> ...\build\Debug\VulkanApp.exe`), no errors.

- [ ] **Step 6: Smoke-test — app still launches and renders normally**

Run `build\Debug\VulkanApp.exe`. Confirm the LOBBY screen appears at the normal 1024x768 windowed size, click "LAUNCH MISSION", confirm the solar system renders as before (planets, bloom glow, shadows). This confirms the framebuffer-size-based swapchain extent still equals 1024x768 at startup and the refactored `cleanupCore`/new `cleanupSwapChain` didn't break shutdown (close the window and confirm it exits cleanly, no crash).

- [ ] **Step 7: Commit**

```bash
git add src/VulkanBase.hpp
git commit -m "Add swapchain resize infrastructure to VulkanBase (frame-start hook, resolution-aware swapchain, cleanupSwapChain)"
```

---

## Task 2: Dynamic viewport/scissor on the 4 resolution-dependent pipelines

**Files:**
- Modify: `src/main.cpp` (`createGraphicsPipeline`, `createBlurPipeline`, `createPostProcessPipeline`, `recordCommandBuffer`)

**Interfaces:**
- Consumes: nothing new from Task 1 directly (independent of the resize hook, but required before `recreateSwapChain` can safely skip pipeline recreation)
- Produces: `void setFullViewport(VkCommandBuffer cb)` (private helper, consumed by `recordCommandBuffer`)
- Produces: `graphicsPipeline`, `linePipeline`, `blurPipeline`, `postPipeline` now use `VK_DYNAMIC_STATE_VIEWPORT`/`VK_DYNAMIC_STATE_SCISSOR` instead of baked-in viewport/scissor

- [ ] **Step 1: Add dynamic state to `createGraphicsPipeline()` (covers `graphicsPipeline` and `linePipeline`)**

In `src/main.cpp`, find:

```cpp
        VkViewport viewport{}; viewport.width = (float)swapChainExtent.width; viewport.height = (float)swapChainExtent.height; viewport.maxDepth = 1.0f; VkRect2D scissor{}; scissor.extent = swapChainExtent;
        VkPipelineViewportStateCreateInfo viewportState{}; viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; viewportState.viewportCount = 1; viewportState.pViewports = &viewport; viewportState.scissorCount = 1; viewportState.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo rasterizer{}; rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rasterizer.polygonMode = VK_POLYGON_MODE_FILL; rasterizer.lineWidth = 1.0f; rasterizer.cullMode = VK_CULL_MODE_NONE; rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
```

Replace with:

```cpp
        VkViewport viewport{}; viewport.width = (float)swapChainExtent.width; viewport.height = (float)swapChainExtent.height; viewport.maxDepth = 1.0f; VkRect2D scissor{}; scissor.extent = swapChainExtent;
        VkPipelineViewportStateCreateInfo viewportState{}; viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; viewportState.viewportCount = 1; viewportState.pViewports = &viewport; viewportState.scissorCount = 1; viewportState.pScissors = &scissor;
        std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{}; dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()); dynamicState.pDynamicStates = dynamicStates.data();
        VkPipelineRasterizationStateCreateInfo rasterizer{}; rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rasterizer.polygonMode = VK_POLYGON_MODE_FILL; rasterizer.lineWidth = 1.0f; rasterizer.cullMode = VK_CULL_MODE_NONE; rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
```

Then find:

```cpp
        VkGraphicsPipelineCreateInfo pipelineInfo{}; pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pipelineInfo.stageCount = 2; pipelineInfo.pStages = shaderStages; pipelineInfo.pVertexInputState = &vertexInputInfo; pipelineInfo.pInputAssemblyState = &inputAssembly; pipelineInfo.pViewportState = &viewportState; pipelineInfo.pRasterizationState = &rasterizer; pipelineInfo.pMultisampleState = &multisampling; pipelineInfo.pDepthStencilState = &depthStencil; pipelineInfo.pColorBlendState = &colorBlending; pipelineInfo.layout = pipelineLayout; pipelineInfo.renderPass = offscreenRenderPass;
```

Replace with:

```cpp
        VkGraphicsPipelineCreateInfo pipelineInfo{}; pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pipelineInfo.stageCount = 2; pipelineInfo.pStages = shaderStages; pipelineInfo.pVertexInputState = &vertexInputInfo; pipelineInfo.pInputAssemblyState = &inputAssembly; pipelineInfo.pViewportState = &viewportState; pipelineInfo.pRasterizationState = &rasterizer; pipelineInfo.pMultisampleState = &multisampling; pipelineInfo.pDepthStencilState = &depthStencil; pipelineInfo.pColorBlendState = &colorBlending; pipelineInfo.layout = pipelineLayout; pipelineInfo.renderPass = offscreenRenderPass; pipelineInfo.pDynamicState = &dynamicState;
```

(This `pipelineInfo` struct is reused for `linePipeline` right below via `vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &linePipeline);`, so both pipelines pick up the dynamic state automatically.)

- [ ] **Step 2: Add dynamic state to `createBlurPipeline()`**

Find:

```cpp
        VkViewport vp{}; vp.width = swapChainExtent.width; vp.height = swapChainExtent.height; vp.maxDepth = 1.0f; VkRect2D sc{}; sc.extent = swapChainExtent;
        VkPipelineViewportStateCreateInfo vpS{}; vpS.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vpS.viewportCount = 1; vpS.pViewports = &vp; vpS.scissorCount = 1; vpS.pScissors = &sc;
        VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; ds.depthTestEnable = VK_FALSE;
        VkPipelineColorBlendAttachmentState cbA{}; cbA.colorWriteMask = 0xF; cbA.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo cbS{}; cbS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cbS.attachmentCount = 1; cbS.pAttachments = &cbA;
        
        VkGraphicsPipelineCreateInfo pInfo{}; pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pInfo.stageCount = 2; pInfo.pStages = sS; pInfo.pVertexInputState = &vI; pInfo.pInputAssemblyState = &iA; pInfo.pViewportState = &vpS; pInfo.pRasterizationState = &rs; pInfo.pMultisampleState = &ms; pInfo.pDepthStencilState = &ds; pInfo.pColorBlendState = &cbS; pInfo.layout = blurPipelineLayout; pInfo.renderPass = blurRenderPass;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &blurPipeline);
        vkDestroyShaderModule(device, fMod, nullptr); vkDestroyShaderModule(device, vMod, nullptr);
    }

    void createPostProcessPipeline() {
```

Replace with:

```cpp
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
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &blurPipeline);
        vkDestroyShaderModule(device, fMod, nullptr); vkDestroyShaderModule(device, vMod, nullptr);
    }

    void createPostProcessPipeline() {
```

- [ ] **Step 3: Add dynamic state to `createPostProcessPipeline()`**

Find:

```cpp
        VkViewport vp{}; vp.width = swapChainExtent.width; vp.height = swapChainExtent.height; vp.maxDepth = 1.0f; VkRect2D sc{}; sc.extent = swapChainExtent;
        VkPipelineViewportStateCreateInfo vpS{}; vpS.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vpS.viewportCount = 1; vpS.pViewports = &vp; vpS.scissorCount = 1; vpS.pScissors = &sc;
        VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; ds.depthTestEnable = VK_FALSE;
        VkPipelineColorBlendAttachmentState cbA{}; cbA.colorWriteMask = 0xF; cbA.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo cbS{}; cbS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cbS.attachmentCount = 1; cbS.pAttachments = &cbA;
        
        VkGraphicsPipelineCreateInfo pInfo{}; pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pInfo.stageCount = 2; pInfo.pStages = sS; pInfo.pVertexInputState = &vI; pInfo.pInputAssemblyState = &iA; pInfo.pViewportState = &vpS; pInfo.pRasterizationState = &rs; pInfo.pMultisampleState = &ms; pInfo.pDepthStencilState = &ds; pInfo.pColorBlendState = &cbS; pInfo.layout = postPipelineLayout; pInfo.renderPass = renderPass; 
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &postPipeline);
```

Replace with:

```cpp
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
```

- [ ] **Step 4: Add the `setFullViewport` helper**

Find (this is the start of `recordCommandBuffer`, right after the class's `updateUniformBuffer` override ends — locate by the `void recordCommandBuffer` signature):

```cpp
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex) override {
        VkCommandBufferBeginInfo beginInfo{}; beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cb, &beginInfo);
```

Replace with:

```cpp
    void setFullViewport(VkCommandBuffer cb) {
        VkViewport vp{}; vp.width = (float)swapChainExtent.width; vp.height = (float)swapChainExtent.height; vp.maxDepth = 1.0f;
        VkRect2D sc{}; sc.extent = swapChainExtent;
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
    }

    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex) override {
        VkCommandBufferBeginInfo beginInfo{}; beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cb, &beginInfo);
```

- [ ] **Step 5: Call `setFullViewport` in the offscreen pass**

Find:

```cpp
        vkCmdBeginRenderPass(cb, &offscreenPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
```

Replace with:

```cpp
        vkCmdBeginRenderPass(cb, &offscreenPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        setFullViewport(cb);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
```

- [ ] **Step 6: Call `setFullViewport` in the blur loop**

Find:

```cpp
            vkCmdBeginRenderPass(cb, &blurPassInfo, VK_SUBPASS_CONTENTS_INLINE); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline);
```

Replace with:

```cpp
            vkCmdBeginRenderPass(cb, &blurPassInfo, VK_SUBPASS_CONTENTS_INLINE); setFullViewport(cb); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline);
```

- [ ] **Step 7: Call `setFullViewport` in the final swapchain pass**

Find:

```cpp
        vkCmdBeginRenderPass(cb, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
```

Replace with:

```cpp
        vkCmdBeginRenderPass(cb, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        setFullViewport(cb);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
```

- [ ] **Step 8: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds with no errors.

- [ ] **Step 9: Smoke-test — rendering is pixel-identical at the default resolution**

Run `build\Debug\VulkanApp.exe`, launch the simulation, and confirm planets, bloom/glow, and shadows still render correctly at the default 1024x768 window (this task doesn't change any pixel output — it only switches viewport/scissor from static to dynamic-but-identical, so a visual regression here means a mistake in Steps 1-7).

- [ ] **Step 10: Commit**

```bash
git add src/main.cpp
git commit -m "Use dynamic viewport/scissor state on offscreen, blur, and post pipelines"
```

---

## Task 3: Split offscreen/blur resource creation into resize-safe pieces

**Files:**
- Modify: `src/main.cpp` (`createOffscreenResources`, `createBlurResources`, `createBlurPipeline`, `createPostProcessPipeline`)

**Interfaces:**
- Produces: `void createOffscreenImages()` (creates offscreen color/bright/depth images+views+framebuffer only; assumes `offscreenRenderPass` already exists)
- Produces: `void createBlurImages()` (creates blur images+views+framebuffers only; assumes `blurRenderPass` already exists)
- Produces: `void updateBlurDescriptorSets()` (rewrites the 3 blur descriptor sets to point at the current `offscreenBrightView`/`blurViews`)
- Produces: `void updatePostDescriptorSets()` (rewrites the post descriptor set to point at the current `offscreenColorView`/`blurViews[1]`)
- Consumed by: Task 4's `recreateSwapChain()`

- [ ] **Step 1: Split `createOffscreenResources()` into render-pass setup + `createOffscreenImages()`**

Find (the full existing function):

```cpp
    void createOffscreenResources() {
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT; VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        auto makeImg = [&](VkFormat fmt, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem, VkImageView& view, VkImageAspectFlags aspect) {
            VkImageCreateInfo iInfo{}; iInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; iInfo.imageType = VK_IMAGE_TYPE_2D;
            iInfo.extent.width = swapChainExtent.width; iInfo.extent.height = swapChainExtent.height; iInfo.extent.depth = 1; iInfo.mipLevels = 1; iInfo.arrayLayers = 1; iInfo.format = fmt; iInfo.tiling = VK_IMAGE_TILING_OPTIMAL; iInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; iInfo.usage = usage; iInfo.samples = VK_SAMPLE_COUNT_1_BIT; iInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateImage(device, &iInfo, nullptr, &img);
            VkMemoryRequirements mReqs; vkGetImageMemoryRequirements(device, img, &mReqs);
            VkMemoryAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            aInfo.allocationSize = mReqs.size; aInfo.memoryTypeIndex = findMemoryType(mReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device, &aInfo, nullptr, &mem); vkBindImageMemory(device, img, mem, 0);
            VkImageViewCreateInfo vInfo{}; vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vInfo.image = img; vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vInfo.format = fmt; vInfo.subresourceRange.aspectMask = aspect; vInfo.subresourceRange.baseMipLevel = 0; vInfo.subresourceRange.levelCount = 1; vInfo.subresourceRange.baseArrayLayer = 0; vInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &vInfo, nullptr, &view);
        };
        makeImg(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, offscreenColorImage, offscreenColorMem, offscreenColorView, VK_IMAGE_ASPECT_COLOR_BIT);
        makeImg(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, offscreenBrightImage, offscreenBrightMem, offscreenBrightView, VK_IMAGE_ASPECT_COLOR_BIT);
        makeImg(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, offscreenDepthImage, offscreenDepthMem, offscreenDepthView, VK_IMAGE_ASPECT_DEPTH_BIT);

        std::array<VkAttachmentDescription, 3> atts{};
        atts[0].format = colorFormat; atts[0].samples = VK_SAMPLE_COUNT_1_BIT; atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE; atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        atts[1] = atts[0]; 
        atts[2].format = depthFormat; atts[2].samples = VK_SAMPLE_COUNT_1_BIT; atts[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colRefs[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        VkAttachmentReference depRef{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
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
```

Replace with:

```cpp
    void createOffscreenImages() {
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT; VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        auto makeImg = [&](VkFormat fmt, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem, VkImageView& view, VkImageAspectFlags aspect) {
            VkImageCreateInfo iInfo{}; iInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; iInfo.imageType = VK_IMAGE_TYPE_2D;
            iInfo.extent.width = swapChainExtent.width; iInfo.extent.height = swapChainExtent.height; iInfo.extent.depth = 1; iInfo.mipLevels = 1; iInfo.arrayLayers = 1; iInfo.format = fmt; iInfo.tiling = VK_IMAGE_TILING_OPTIMAL; iInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; iInfo.usage = usage; iInfo.samples = VK_SAMPLE_COUNT_1_BIT; iInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateImage(device, &iInfo, nullptr, &img);
            VkMemoryRequirements mReqs; vkGetImageMemoryRequirements(device, img, &mReqs);
            VkMemoryAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            aInfo.allocationSize = mReqs.size; aInfo.memoryTypeIndex = findMemoryType(mReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device, &aInfo, nullptr, &mem); vkBindImageMemory(device, img, mem, 0);
            VkImageViewCreateInfo vInfo{}; vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vInfo.image = img; vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vInfo.format = fmt; vInfo.subresourceRange.aspectMask = aspect; vInfo.subresourceRange.baseMipLevel = 0; vInfo.subresourceRange.levelCount = 1; vInfo.subresourceRange.baseArrayLayer = 0; vInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &vInfo, nullptr, &view);
        };
        makeImg(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, offscreenColorImage, offscreenColorMem, offscreenColorView, VK_IMAGE_ASPECT_COLOR_BIT);
        makeImg(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, offscreenBrightImage, offscreenBrightMem, offscreenBrightView, VK_IMAGE_ASPECT_COLOR_BIT);
        makeImg(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, offscreenDepthImage, offscreenDepthMem, offscreenDepthView, VK_IMAGE_ASPECT_DEPTH_BIT);

        std::array<VkImageView, 3> fbAtts = {offscreenColorView, offscreenBrightView, offscreenDepthView};
        VkFramebufferCreateInfo fbInfo{}; fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fbInfo.renderPass = offscreenRenderPass; fbInfo.attachmentCount = 3; fbInfo.pAttachments = fbAtts.data(); fbInfo.width = swapChainExtent.width; fbInfo.height = swapChainExtent.height; fbInfo.layers = 1;
        vkCreateFramebuffer(device, &fbInfo, nullptr, &offscreenFramebuffer);
    }

    void createOffscreenResources() {
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT; VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

        std::array<VkAttachmentDescription, 3> atts{};
        atts[0].format = colorFormat; atts[0].samples = VK_SAMPLE_COUNT_1_BIT; atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE; atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        atts[1] = atts[0]; 
        atts[2].format = depthFormat; atts[2].samples = VK_SAMPLE_COUNT_1_BIT; atts[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; atts[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colRefs[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        VkAttachmentReference depRef{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 2; subpass.pColorAttachments = colRefs; subpass.pDepthStencilAttachment = &depRef;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0; deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL; deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpInfo{}; rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpInfo.attachmentCount = 3; rpInfo.pAttachments = atts.data(); rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass; rpInfo.dependencyCount = 2; rpInfo.pDependencies = deps.data();
        vkCreateRenderPass(device, &rpInfo, nullptr, &offscreenRenderPass);

        createOffscreenImages();

        VkSamplerCreateInfo sInfo{}; sInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO; sInfo.magFilter = VK_FILTER_LINEAR; sInfo.minFilter = VK_FILTER_LINEAR; sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(device, &sInfo, nullptr, &offscreenSampler);
    }
```

- [ ] **Step 2: Split `createBlurResources()` into render-pass setup + `createBlurImages()`**

Find (the full existing function):

```cpp
    void createBlurResources() {
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        auto makeImg = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
            VkImageCreateInfo iInfo{}; iInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; iInfo.imageType = VK_IMAGE_TYPE_2D; iInfo.extent.width = swapChainExtent.width; iInfo.extent.height = swapChainExtent.height; iInfo.extent.depth = 1; iInfo.mipLevels = 1; iInfo.arrayLayers = 1; iInfo.format = colorFormat; iInfo.tiling = VK_IMAGE_TILING_OPTIMAL; iInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; iInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; iInfo.samples = VK_SAMPLE_COUNT_1_BIT; iInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateImage(device, &iInfo, nullptr, &img);
            VkMemoryRequirements mReqs; vkGetImageMemoryRequirements(device, img, &mReqs);
            VkMemoryAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; aInfo.allocationSize = mReqs.size; aInfo.memoryTypeIndex = findMemoryType(mReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device, &aInfo, nullptr, &mem); vkBindImageMemory(device, img, mem, 0);
            VkImageViewCreateInfo vInfo{}; vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vInfo.image = img; vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; vInfo.format = colorFormat; vInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vInfo.subresourceRange.baseMipLevel = 0; vInfo.subresourceRange.levelCount = 1; vInfo.subresourceRange.baseArrayLayer = 0; vInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &vInfo, nullptr, &view);
        };

        for(int i = 0; i < 2; i++) makeImg(blurImages[i], blurMemories[i], blurViews[i]);

        VkAttachmentDescription att{}; att.format = colorFormat; att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &colorRef;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0; deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL; deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpInfo{}; rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpInfo.attachmentCount = 1; rpInfo.pAttachments = &att; rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass; rpInfo.dependencyCount = 2; rpInfo.pDependencies = deps.data();
        vkCreateRenderPass(device, &rpInfo, nullptr, &blurRenderPass);

        for(int i = 0; i < 2; i++) {
            VkFramebufferCreateInfo fbInfo{}; fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fbInfo.renderPass = blurRenderPass; fbInfo.attachmentCount = 1; fbInfo.pAttachments = &blurViews[i]; fbInfo.width = swapChainExtent.width; fbInfo.height = swapChainExtent.height; fbInfo.layers = 1;
            vkCreateFramebuffer(device, &fbInfo, nullptr, &blurFramebuffers[i]);
        }
    }
```

Replace with:

```cpp
    void createBlurImages() {
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        auto makeImg = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
            VkImageCreateInfo iInfo{}; iInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; iInfo.imageType = VK_IMAGE_TYPE_2D; iInfo.extent.width = swapChainExtent.width; iInfo.extent.height = swapChainExtent.height; iInfo.extent.depth = 1; iInfo.mipLevels = 1; iInfo.arrayLayers = 1; iInfo.format = colorFormat; iInfo.tiling = VK_IMAGE_TILING_OPTIMAL; iInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; iInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; iInfo.samples = VK_SAMPLE_COUNT_1_BIT; iInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateImage(device, &iInfo, nullptr, &img);
            VkMemoryRequirements mReqs; vkGetImageMemoryRequirements(device, img, &mReqs);
            VkMemoryAllocateInfo aInfo{}; aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; aInfo.allocationSize = mReqs.size; aInfo.memoryTypeIndex = findMemoryType(mReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device, &aInfo, nullptr, &mem); vkBindImageMemory(device, img, mem, 0);
            VkImageViewCreateInfo vInfo{}; vInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vInfo.image = img; vInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; vInfo.format = colorFormat; vInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vInfo.subresourceRange.baseMipLevel = 0; vInfo.subresourceRange.levelCount = 1; vInfo.subresourceRange.baseArrayLayer = 0; vInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &vInfo, nullptr, &view);
        };

        for(int i = 0; i < 2; i++) makeImg(blurImages[i], blurMemories[i], blurViews[i]);

        for(int i = 0; i < 2; i++) {
            VkFramebufferCreateInfo fbInfo{}; fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fbInfo.renderPass = blurRenderPass; fbInfo.attachmentCount = 1; fbInfo.pAttachments = &blurViews[i]; fbInfo.width = swapChainExtent.width; fbInfo.height = swapChainExtent.height; fbInfo.layers = 1;
            vkCreateFramebuffer(device, &fbInfo, nullptr, &blurFramebuffers[i]);
        }
    }

    void createBlurResources() {
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkAttachmentDescription att{}; att.format = colorFormat; att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &colorRef;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0; deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL; deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpInfo{}; rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpInfo.attachmentCount = 1; rpInfo.pAttachments = &att; rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass; rpInfo.dependencyCount = 2; rpInfo.pDependencies = deps.data();
        vkCreateRenderPass(device, &rpInfo, nullptr, &blurRenderPass);

        createBlurImages();
    }
```

- [ ] **Step 3: Extract `updateBlurDescriptorSets()` from `createBlurPipeline()`**

Find:

```cpp
        auto mI = [&](VkImageView v){ VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = offscreenSampler; return i; };
        VkDescriptorImageInfo i0 = mI(offscreenBrightView); VkDescriptorImageInfo i1 = mI(blurViews[0]); VkDescriptorImageInfo i2 = mI(blurViews[1]);
        std::array<VkWriteDescriptorSet, 3> wr{};
        for(int i=0; i<3; i++) { wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr[i].dstSet = blurDescriptorSets[i]; wr[i].dstBinding = 0; wr[i].descriptorCount = 1; wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; }
        wr[0].pImageInfo = &i0; wr[1].pImageInfo = &i1; wr[2].pImageInfo = &i2;
        vkUpdateDescriptorSets(device, 3, wr.data(), 0, nullptr);

        VkPushConstantRange pc{}; pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; pc.offset = 0; pc.size = sizeof(int);
```

Replace with:

```cpp
        updateBlurDescriptorSets();

        VkPushConstantRange pc{}; pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; pc.offset = 0; pc.size = sizeof(int);
```

Then, immediately above `void createBlurPipeline() {`, add the new method:

```cpp
    void updateBlurDescriptorSets() {
        auto mI = [&](VkImageView v){ VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = offscreenSampler; return i; };
        VkDescriptorImageInfo i0 = mI(offscreenBrightView); VkDescriptorImageInfo i1 = mI(blurViews[0]); VkDescriptorImageInfo i2 = mI(blurViews[1]);
        std::array<VkWriteDescriptorSet, 3> wr{};
        for(int i=0; i<3; i++) { wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr[i].dstSet = blurDescriptorSets[i]; wr[i].dstBinding = 0; wr[i].descriptorCount = 1; wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; }
        wr[0].pImageInfo = &i0; wr[1].pImageInfo = &i1; wr[2].pImageInfo = &i2;
        vkUpdateDescriptorSets(device, 3, wr.data(), 0, nullptr);
    }

    void createBlurPipeline() {
```

- [ ] **Step 4: Extract `updatePostDescriptorSets()` from `createPostProcessPipeline()`**

Find:

```cpp
        auto mI = [&](VkImageView v){ VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = offscreenSampler; return i; };
        VkDescriptorImageInfo cI = mI(offscreenColorView); VkDescriptorImageInfo brI = mI(blurViews[1]); 

        std::array<VkWriteDescriptorSet, 2> wr{};
        wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr[0].dstSet = postDescriptorSet; wr[0].dstBinding = 0; wr[0].descriptorCount = 1; wr[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr[0].pImageInfo = &cI;
        wr[1] = wr[0]; wr[1].dstBinding = 1; wr[1].pImageInfo = &brI;
        vkUpdateDescriptorSets(device, 2, wr.data(), 0, nullptr);

        VkPipelineLayoutCreateInfo pLayInfo{}; pLayInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pLayInfo.setLayoutCount = 1; pLayInfo.pSetLayouts = &postDescriptorSetLayout;
        vkCreatePipelineLayout(device, &pLayInfo, nullptr, &postPipelineLayout);
```

Replace with:

```cpp
        updatePostDescriptorSets();

        VkPipelineLayoutCreateInfo pLayInfo{}; pLayInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pLayInfo.setLayoutCount = 1; pLayInfo.pSetLayouts = &postDescriptorSetLayout;
        vkCreatePipelineLayout(device, &pLayInfo, nullptr, &postPipelineLayout);
```

Then, immediately above `void createPostProcessPipeline() {`, add the new method:

```cpp
    void updatePostDescriptorSets() {
        auto mI = [&](VkImageView v){ VkDescriptorImageInfo i{}; i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; i.imageView = v; i.sampler = offscreenSampler; return i; };
        VkDescriptorImageInfo cI = mI(offscreenColorView); VkDescriptorImageInfo brI = mI(blurViews[1]);
        std::array<VkWriteDescriptorSet, 2> wr{};
        wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr[0].dstSet = postDescriptorSet; wr[0].dstBinding = 0; wr[0].descriptorCount = 1; wr[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr[0].pImageInfo = &cI;
        wr[1] = wr[0]; wr[1].dstBinding = 1; wr[1].pImageInfo = &brI;
        vkUpdateDescriptorSets(device, 2, wr.data(), 0, nullptr);
    }

    void createPostProcessPipeline() {
```

- [ ] **Step 5: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds with no errors.

- [ ] **Step 6: Smoke-test — bloom/post-process still renders correctly**

Run `build\Debug\VulkanApp.exe`, launch the simulation, and confirm the sun's bloom glow and the overall post-processed image still look correct (this task only reorders creation code and extracts descriptor-writing into named functions — pixel output must be unchanged).

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp
git commit -m "Split offscreen/blur resource creation into resize-safe image/framebuffer functions"
```

---

## Task 4: `recreateSwapChain()`

**Files:**
- Modify: `src/main.cpp` (add new private method, placed near `initApp`)

**Interfaces:**
- Consumes: `cleanupSwapChain()`, `createSwapChain()`, `createImageViews()`, `createDepthResources()`, `createFramebuffers()` (from `VulkanBase`, Task 1); `createOffscreenImages()`, `createBlurImages()`, `updateBlurDescriptorSets()`, `updatePostDescriptorSets()` (Task 3)
- Produces: `void recreateSwapChain()`, consumed by Task 5's `onFrameStart()` override

- [ ] **Step 1: Add `recreateSwapChain()`**

In `src/main.cpp`, find the start of `initApp()`:

```cpp
    void initApp() override {
        generateSphere(1.0f, 64, 64);
```

Replace with:

```cpp
    void recreateSwapChain() {
        vkDeviceWaitIdle(device);

        for (int i = 0; i < 2; i++) {
            vkDestroyFramebuffer(device, blurFramebuffers[i], nullptr);
            vkDestroyImageView(device, blurViews[i], nullptr);
            vkDestroyImage(device, blurImages[i], nullptr);
            vkFreeMemory(device, blurMemories[i], nullptr);
        }
        vkDestroyFramebuffer(device, offscreenFramebuffer, nullptr);
        vkDestroyImageView(device, offscreenColorView, nullptr); vkDestroyImage(device, offscreenColorImage, nullptr); vkFreeMemory(device, offscreenColorMem, nullptr);
        vkDestroyImageView(device, offscreenBrightView, nullptr); vkDestroyImage(device, offscreenBrightImage, nullptr); vkFreeMemory(device, offscreenBrightMem, nullptr);
        vkDestroyImageView(device, offscreenDepthView, nullptr); vkDestroyImage(device, offscreenDepthImage, nullptr); vkFreeMemory(device, offscreenDepthMem, nullptr);

        cleanupSwapChain();

        createSwapChain();
        createImageViews();
        createDepthResources();
        createFramebuffers();

        createOffscreenImages();
        createBlurImages();
        updateBlurDescriptorSets();
        updatePostDescriptorSets();
    }

    void initApp() override {
        generateSphere(1.0f, 64, 64);
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds with no errors. `recreateSwapChain()` is not called from anywhere yet, so this task only needs to compile — no runtime behavior change yet.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "Add recreateSwapChain() to rebuild swapchain and offscreen/blur images at the current window size"
```

---

## Task 5: Fullscreen toggle state and window-mode switching

**Files:**
- Modify: `src/main.cpp` (member variables, `onFrameStart()` override)

**Interfaces:**
- Consumes: `virtual void onFrameStart()` (Task 1), `recreateSwapChain()` (Task 4)
- Produces: `bool isFullscreen`, `bool fullscreenToggleRequested` (consumed by Task 6's UI button)

- [ ] **Step 1: Add the state members**

In `src/main.cpp`, find:

```cpp
    bool isRealScaleMode = false;
    bool showOrbits = false; // 디폴트는 OFF
    float scaleLerp = 0.0f;  // 0.0(시각 비율) ~ 1.0(리얼 비율) 사이를 스르륵 오가는 타이머
```

Replace with:

```cpp
    bool isRealScaleMode = false;
    bool showOrbits = false; // 디폴트는 OFF
    float scaleLerp = 0.0f;  // 0.0(시각 비율) ~ 1.0(리얼 비율) 사이를 스르륵 오가는 타이머

    bool isFullscreen = false;
    bool fullscreenToggleRequested = false;
```

- [ ] **Step 2: Add the `onFrameStart()` override**

Find:

```cpp
protected:
    void onMouseButton(int button, int action, int mods) override {
```

Replace with:

```cpp
protected:
    void onFrameStart() override {
        if (!fullscreenToggleRequested) return;
        fullscreenToggleRequested = false;

        vkDeviceWaitIdle(device);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);

        if (!isFullscreen) {
            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowMonitor(window, nullptr, 0, 0, mode->width, mode->height, mode->refreshRate);
        } else {
            int xpos = (mode->width - static_cast<int>(WIDTH)) / 2;
            int ypos = (mode->height - static_cast<int>(HEIGHT)) / 2;
            glfwSetWindowMonitor(window, nullptr, xpos, ypos, static_cast<int>(WIDTH), static_cast<int>(HEIGHT), 0);
            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
        }
        isFullscreen = !isFullscreen;

        recreateSwapChain();
    }

    void onMouseButton(int button, int action, int mods) override {
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds with no errors. `fullscreenToggleRequested` is never set to `true` yet (no UI wired up), so this task only needs to compile.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "Add fullscreen toggle state and onFrameStart handler for borderless fullscreen switching"
```

---

## Task 6: Fullscreen toggle button (UI)

**Files:**
- Modify: `src/main.cpp` (`updateUniformBuffer`, the ImGui section)

**Interfaces:**
- Consumes: `isFullscreen`, `fullscreenToggleRequested` (Task 5)

- [ ] **Step 1: Add the always-visible "Fullscreen Panel" and shift "Settings Panel" down**

Find:

```cpp
        ImGui_ImplVulkan_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

        if (currentAppState == AppState::LOBBY) {
```

Replace with:

```cpp
        ImGui_ImplVulkan_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

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

        if (currentAppState == AppState::LOBBY) {
```

- [ ] **Step 2: Move the existing "Settings Panel" down so it doesn't overlap**

Find:

```cpp
            ImGui::SetNextWindowPos(ImVec2(swapChainExtent.width - 260.0f, 20.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(240.0f, 120.0f), ImGuiCond_Always);
            ImGui::Begin("Settings Panel", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
```

Replace with:

```cpp
            ImGui::SetNextWindowPos(ImVec2(swapChainExtent.width - 260.0f, 100.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(240.0f, 120.0f), ImGuiCond_Always);
            ImGui::Begin("Settings Panel", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds with no errors.

- [ ] **Step 4: Manual runtime verification**

Run `build\Debug\VulkanApp.exe`:
1. On the LOBBY screen, confirm the "ENTER FULLSCREEN" button is visible in the top-right corner.
2. Click it. Confirm the window becomes borderless and fills the primary monitor, and the LOBBY UI (main menu) re-centers and renders correctly at the new resolution.
3. Click "LAUNCH MISSION" while still in fullscreen. Confirm the simulation renders correctly (planets, bloom, shadows, asteroid belt) at full monitor resolution, with no stretched/cropped/black-screen artifacts.
4. Confirm the button now reads "EXIT FULLSCREEN" (orange) and the "Settings Panel" (Real Scale / Orbit Lines) sits directly below it without overlapping.
5. Click "EXIT FULLSCREEN". Confirm the window returns to a 1024x768 decorated window, centered on the screen, and the simulation keeps rendering correctly.
6. Repeat the fullscreen/windowed toggle 3-4 times in a row while in SIMULATION state. Confirm no crash, no validation errors in the console/debugger output, and rendering stays correct each time.
7. Return to LOBBY via "RETURN TO MAIN MENU", then toggle fullscreen again from LOBBY to confirm the button still works outside SIMULATION state.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "Add fullscreen toggle button visible on both LOBBY and SIMULATION screens"
```

---

## Task 7: Final verification pass

**Files:** None (verification only)

- [ ] **Step 1: Full rebuild from clean**

Run: `cmake --build build --config Debug --clean-first`
Expected: Build succeeds with no errors or new warnings related to the changed files.

- [ ] **Step 2: Run the full manual test plan from the design spec**

Run `build\Debug\VulkanApp.exe` and verify every item from `docs/superpowers/specs/2026-07-16-fullscreen-toggle-design.md`'s "테스트 계획" section:
- LOBBY shows the fullscreen button; clicking it enters borderless fullscreen.
- "LAUNCH MISSION" works correctly from fullscreen; rendering (planets, bloom, shadows, asteroid belt) is intact.
- Toggling fullscreen ↔ windowed repeatedly in SIMULATION causes no crash, descriptor errors, or viewport misalignment.
- Returning to windowed mode restores exactly 1024x768, centered on the screen.
- Real Scale / Orbit Lines buttons and the rest of the right-side panels don't overlap the new fullscreen panel.

- [ ] **Step 3: Report results**

Summarize pass/fail for each bullet above. If anything fails, file it as a follow-up rather than silently patching outside this plan's tasks.
