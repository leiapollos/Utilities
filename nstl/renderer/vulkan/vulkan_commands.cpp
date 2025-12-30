//
// Created by André Leite on 03/11/2025.
//

// ////////////////////////
// Command Buffer Helpers

static VkCommandBufferBeginInfo vulkan_command_buffer_begin_info(VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;
    return beginInfo;
}

static VkSemaphoreSubmitInfo vulkan_semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore) {
    VkSemaphoreSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    submitInfo.pNext = 0;
    submitInfo.semaphore = semaphore;
    submitInfo.stageMask = stageMask;
    submitInfo.deviceIndex = 0;
    submitInfo.value = 1;
    return submitInfo;
}

static VkCommandBufferSubmitInfo vulkan_command_buffer_submit_info(VkCommandBuffer cmd) {
    VkCommandBufferSubmitInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    info.pNext = 0;
    info.commandBuffer = cmd;
    info.deviceMask = 0;
    return info;
}

static VkSubmitInfo2 vulkan_submit_info2(VkCommandBufferSubmitInfo* cmd,
                                         VkSemaphoreSubmitInfo* signalSemaphoreInfo,
                                         VkSemaphoreSubmitInfo* waitSemaphoreInfo) {
    VkSubmitInfo2 info = {};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    info.pNext = 0;
    info.waitSemaphoreInfoCount = (waitSemaphoreInfo == 0) ? 0 : 1;
    info.pWaitSemaphoreInfos = waitSemaphoreInfo;
    info.signalSemaphoreInfoCount = (signalSemaphoreInfo == 0) ? 0 : 1;
    info.pSignalSemaphoreInfos = signalSemaphoreInfo;
    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos = cmd;
    return info;
}

// ////////////////////////
// Sync Primitives Helpers

static VkSemaphoreCreateInfo vulkan_semaphore_create_info(VkSemaphoreCreateFlags flags) {
    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.flags = flags;
    return createInfo;
}

static VkFenceCreateInfo vulkan_fence_create_info(VkFenceCreateFlags flags) {
    VkFenceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    createInfo.flags = flags;
    return createInfo;
}

// ////////////////////////
// Initialization

static B32 vulkan_create_sync_structures(RendererVulkan* vulkan, VulkanCommands* commands) {
    if (!vulkan || vulkan->device.device == VK_NULL_HANDLE || !commands) {
        return 0;
    }

    VkSemaphoreCreateInfo semaphoreInfo = vulkan_semaphore_create_info(0);
    VkFenceCreateInfo fenceInfo = vulkan_fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);

    for (U32 i = 0; i < VULKAN_FRAME_OVERLAP; ++i) {
        RendererVulkanFrame* frame = &commands->frames[i];

        VK_CHECK(vkCreateSemaphore(vulkan->device.device, &semaphoreInfo, 0, &frame->swapchainSemaphore));
        VK_CHECK(vkCreateFence(vulkan->device.device, &fenceInfo, 0, &frame->renderFence));

        // Register for defer shutdown
        vkdefer_destroy_VkSemaphore(&vulkan->deferCtx.globalBuf, frame->swapchainSemaphore);
        vkdefer_destroy_VkFence(&vulkan->deferCtx.globalBuf, frame->renderFence);

        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Created sync structures for frame {}", i);
    }

    return 1;
}

static B32 vulkan_commands_init(RendererVulkan* vulkan, VulkanCommands* commands) {
    if (!vulkan || vulkan->device.device == VK_NULL_HANDLE || !commands) {
        return 0;
    }

    for (U32 i = 0; i < VULKAN_FRAME_OVERLAP; ++i) {
        RendererVulkanFrame* frame = &commands->frames[i];

        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = vulkan->device.graphicsQueueFamilyIndex;

        VK_CHECK(vkCreateCommandPool(vulkan->device.device, &poolInfo, 0, &frame->commandPool));
        
        // Register for defer shutdown
        vkdefer_destroy_VkCommandPool(&vulkan->deferCtx.globalBuf, frame->commandPool);

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frame->commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VK_CHECK(vkAllocateCommandBuffers(vulkan->device.device, &allocInfo, &frame->commandBuffer));

        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Created frame {} command pool and command buffer", i);
    }

    if (!vulkan_create_sync_structures(vulkan, commands)) {
        return 0;
    }

    commands->currentFrameIndex = 0;

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Vulkan commands initialized");
    return 1;
}

// ////////////////////////
// Immediate Submit

static B32 vulkan_init_immediate_submit(RendererVulkan* vulkan, ImmediateSubmitContext* ctx) {
    if (!vulkan || vulkan->device.device == VK_NULL_HANDLE || !ctx) {
        return 0;
    }

    VkFenceCreateInfo fenceInfo = vulkan_fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VK_CHECK(vkCreateFence(vulkan->device.device, &fenceInfo, 0, &ctx->fence));

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = vulkan->device.graphicsQueueFamilyIndex;
    VK_CHECK(vkCreateCommandPool(vulkan->device.device, &poolInfo, 0, &ctx->commandPool));

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = ctx->commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(vulkan->device.device, &allocInfo, &ctx->commandBuffer));

    return 1;
}

static void vulkan_destroy_immediate_submit(RendererVulkan* vulkan, ImmediateSubmitContext* ctx) {
    if (!vulkan || vulkan->device.device == VK_NULL_HANDLE || !ctx) {
        return;
    }
    if (ctx->commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vulkan->device.device, ctx->commandPool, 0);
        ctx->commandPool = VK_NULL_HANDLE;
    }
    if (ctx->fence != VK_NULL_HANDLE) {
        vkDestroyFence(vulkan->device.device, ctx->fence, 0);
        ctx->fence = VK_NULL_HANDLE;
    }
}

static VkCommandBuffer vulkan_immediate_begin(RendererVulkan* vulkan, ImmediateSubmitContext* ctx) {
    if (!vulkan || vulkan->device.device == VK_NULL_HANDLE || !ctx) {
        return VK_NULL_HANDLE;
    }

    VK_CHECK(vkResetFences(vulkan->device.device, 1, &ctx->fence));
    VK_CHECK(vkResetCommandBuffer(ctx->commandBuffer, 0));

    VkCommandBufferBeginInfo beginInfo = vulkan_command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(ctx->commandBuffer, &beginInfo));

    return ctx->commandBuffer;
}

static void vulkan_immediate_end(RendererVulkan* vulkan, ImmediateSubmitContext* ctx) {
    if (!vulkan || vulkan->device.device == VK_NULL_HANDLE || !ctx) {
        return;
    }

    VK_CHECK(vkEndCommandBuffer(ctx->commandBuffer));

    VkCommandBufferSubmitInfo cmdInfo = vulkan_command_buffer_submit_info(ctx->commandBuffer);
    VkSubmitInfo2 submit = vulkan_submit_info2(&cmdInfo, 0, 0);

    VK_CHECK(vkQueueSubmit2(vulkan->device.graphicsQueue, 1, &submit, ctx->fence));
    VK_CHECK(vkWaitForFences(vulkan->device.device, 1, &ctx->fence, VK_TRUE, SECONDS_TO_NANOSECONDS(10)));
}

// ////////////////////////
// Frame Logic

static B32 vulkan_begin_frame(RendererVulkan* vulkan, VkCommandBuffer* outCmd) {
    if (!vulkan || !outCmd) return 0;

    RendererVulkanFrame* frame = &vulkan->commands.frames[vulkan->commands.currentFrameIndex];

    VK_CHECK(vkWaitForFences(vulkan->device.device, 1, &frame->renderFence, VK_TRUE, SECONDS_TO_NANOSECONDS(1)));
    VK_CHECK(vkResetFences(vulkan->device.device, 1, &frame->renderFence));

    vkdefer_begin_frame(&vulkan->deferCtx, vulkan->commands.currentFrameIndex);

    U32 imageIndex = 0u;
    VkResult acquireResult = vkAcquireNextImageKHR(vulkan->device.device,
                                   vulkan->swapchain.handle,
                                   SECONDS_TO_NANOSECONDS(1),
                                   frame->swapchainSemaphore,
                                   VK_NULL_HANDLE,
                                   &imageIndex);
    
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        return 0;
    } else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to acquire swapchain image");
        return 0;
    }

    frame->imageIndex = imageIndex;

    if (imageIndex >= vulkan->swapchain.imageCount) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Swapchain returned invalid image index {} (count {})",
                  imageIndex, vulkan->swapchain.imageCount);
        return 0;
    }

    VK_CHECK(vkResetCommandBuffer(frame->commandBuffer, 0));

    VkCommandBufferBeginInfo beginInfo = vulkan_command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(frame->commandBuffer, &beginInfo));

    *outCmd = frame->commandBuffer;
    return 1;
}

static B32 vulkan_end_frame(RendererVulkan* vulkan) {
    if (!vulkan) return 0;

    RendererVulkanFrame* frame = &vulkan->commands.frames[vulkan->commands.currentFrameIndex];
    U32 imageIndex = frame->imageIndex;

    VK_CHECK(vkEndCommandBuffer(frame->commandBuffer));

    VkSemaphoreSubmitInfo waitSemaphoreInfo = vulkan_semaphore_submit_info(
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, frame->swapchainSemaphore);
    
    VkSemaphoreSubmitInfo signalSemaphoreInfo = vulkan_semaphore_submit_info(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, vulkan->swapchain.imageSemaphores[imageIndex]);
    
    VkCommandBufferSubmitInfo cmdBufferInfo = vulkan_command_buffer_submit_info(frame->commandBuffer);
    
    VkSubmitInfo2 submitInfo = vulkan_submit_info2(&cmdBufferInfo, &signalSemaphoreInfo, &waitSemaphoreInfo);

    VK_CHECK(vkQueueSubmit2(vulkan->device.graphicsQueue, 1, &submitInfo, frame->renderFence));

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &vulkan->swapchain.imageSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &vulkan->swapchain.handle;
    presentInfo.pImageIndices = &frame->imageIndex;

    VkResult presentResult = vkQueuePresentKHR(vulkan->device.graphicsQueue, &presentInfo);
    
    vulkan->commands.currentFrameIndex = (vulkan->commands.currentFrameIndex + 1u) % VULKAN_FRAME_OVERLAP;
    
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        return 0; 
    } else if (presentResult != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Queue present failed");
        return 0;
    }

    return 1;
}
