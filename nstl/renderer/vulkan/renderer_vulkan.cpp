//
// Created by André Leite on 03/11/2025.
//

#include "renderer_vulkan.hpp"

// Subsystems (Unity Build)
#include "vulkan_device.cpp"
#include "vulkan_resources.cpp"
#include "vulkan_descriptors.cpp"
#include "vulkan_commands.cpp"
#include "vulkan_pipelines.cpp"
// ImGui needs to be included before swapchain because swapchain calls on_swapchain_updated
// But ImGui implementation is in renderer_vulkan_imgui.cpp. 
// renderer_vulkan.hpp declares the function, so we can include it after or before.
// However, we must include the implementation somewhere.
#include "renderer_vulkan_imgui.cpp" 
#include "vulkan_swapchain.cpp"

// ////////////////////////
// Lifetime

B32 renderer_init(Arena* arena, Renderer* renderer) {
    if (!arena || !renderer) {
        ASSERT_ALWAYS(false && "Invalid arguments");
        return 0;
    }

    RendererVulkan* vulkan = ARENA_PUSH_STRUCT(arena, RendererVulkan);
    if (!vulkan) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate RendererVulkan");
        return 0;
    }

    vulkan->arena = arena;
    
    // Init Defer Memory first
    U32 perFrameTotalBytes = KB(64) * VULKAN_FRAME_OVERLAP;
    vulkan->deferPerFrameMem = (U8*) arena_push(arena, perFrameTotalBytes, 8);
    vulkan->deferGlobalMem = (U8*) arena_push(arena, KB(256), 8);
    
    vkdefer_init_memory(&vulkan->deferCtx,
                        VULKAN_FRAME_OVERLAP,
                        vulkan->deferPerFrameMem,
                        KB(64),
                        vulkan->deferGlobalMem,
                        KB(256));

    // Device Init
    INIT_SUCCESS(vulkan_device_init(&vulkan->device, arena) && "Failed to initialize Vulkan device");
    
    vkdefer_init_device(&vulkan->deferCtx, vulkan->device.device, 0);
    vkdefer_set_vma_allocator(&vulkan->deferCtx, vulkan->device.allocator);
    vkdefer_destroy_VmaAllocator(&vulkan->deferCtx.globalBuf, vulkan->device.allocator);

    // Commands Init
    INIT_SUCCESS(vulkan_commands_init(vulkan, &vulkan->commands) && "Failed to initialize Vulkan commands");
    
    // Pipelines Init
    INIT_SUCCESS(vulkan_init_draw_pipeline(vulkan) && "Failed to initialize Vulkan draw pipeline");

    LOG_INFO(VULKAN_LOG_DOMAIN, "Vulkan renderer initialized successfully");

    renderer->backendData = vulkan;
    renderer->compileShader = vulkan_compile_shader_wrapper;
    renderer->mergeShaderResults = vulkan_merge_shader_results_wrapper;
    return 1;
}

void renderer_shutdown(Renderer* renderer) {
    if (!renderer || !renderer->backendData) {
        return;
    }

    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;

    if (vulkan->device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vulkan->device.device);
        renderer_vulkan_imgui_shutdown(vulkan);
    }

    vulkan_destroy_swapchain(&vulkan->swapchain, vulkan->device.device);
    vulkan_destroy_draw_image(vulkan);
    
    // Note: vulkan_commands_shutdown is removed because resources are deferred
    
    if (vulkan->device.device != VK_NULL_HANDLE) {
        vkdefer_shutdown(&vulkan->deferCtx);
    }
    
    vulkan_destroy_surface(&vulkan->device, &vulkan->swapchain.surface);
    
    vulkan_device_shutdown(&vulkan->device);

    renderer->backendData = 0;
}

// ////////////////////////
// Draw

void renderer_vulkan_draw_color(RendererVulkan* vulkan, OS_WindowHandle window, Vec4F32 color) {
    if (!vulkan || !window.handle) {
        return;
    }

    if (vulkan->device.device == VK_NULL_HANDLE || vulkan->device.graphicsQueue == VK_NULL_HANDLE) {
        return;
    }

    if (vulkan->swapchain.handle == VK_NULL_HANDLE) {
        if (!vulkan_create_swapchain(vulkan, window)) {
            return;
        }
    }

    if (!vulkan->imguiInitialized && window.handle) {
        if (renderer_vulkan_imgui_init(vulkan, window)) {
            renderer_vulkan_imgui_on_swapchain_updated(vulkan);
        }
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (!vulkan_begin_frame(vulkan, &cmd)) {
        return;
    }
    
    U32 imageIndex = vulkan->commands.frames[vulkan->commands.currentFrameIndex].imageIndex;
    RendererVulkanSwapchainImage* image = &vulkan->swapchain.images[imageIndex];

    B32 hasDrawImage = (vulkan->drawImage.image != VK_NULL_HANDLE) &&
                       (vulkan->drawImage.imageView != VK_NULL_HANDLE);
    VkImageLayout swapchainLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (hasDrawImage) {
        vulkan_transition_image(cmd,
                                vulkan->drawImage.image,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_GENERAL);

        // Draw Background (Compute Gradient)
        vulkan_dispatch_gradient(vulkan, cmd, color);

        vulkan_transition_image(cmd,
                                vulkan->drawImage.image,
                                VK_IMAGE_LAYOUT_GENERAL,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vulkan_transition_image(cmd,
                                image->handle,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vulkan_copy_image_to_image(cmd,
                                   vulkan->drawImage.image,
                                   image->handle,
                                   vulkan->drawExtent,
                                   vulkan->swapchain.extent);
        swapchainLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    } else {
        VkClearColorValue clearValue = {{color.r, color.g, color.b, color.a}};
        VkImageSubresourceRange clearRange = vulkan_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

        vulkan_transition_image(cmd,
                                image->handle,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearColorImage(cmd,
                             image->handle,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clearValue,
                             1u,
                             &clearRange);
        swapchainLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    if (vulkan->imguiInitialized) {
        vulkan_transition_image(cmd,
                                image->handle,
                                swapchainLayout,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        renderer_vulkan_imgui_render(vulkan, cmd, image->view, vulkan->swapchain.extent);
        vulkan_transition_image(cmd,
                                image->handle,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    } else {
        vulkan_transition_image(cmd,
                                image->handle,
                                swapchainLayout,
                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    vulkan_end_frame(vulkan);
}

void renderer_vulkan_on_window_resized(RendererVulkan* vulkan, U32 width, U32 height) {
    if (!vulkan || vulkan->device.device == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(vulkan->device.device);
    vulkan_destroy_swapchain(&vulkan->swapchain, vulkan->device.device);
    vulkan_destroy_draw_image(vulkan);
}
