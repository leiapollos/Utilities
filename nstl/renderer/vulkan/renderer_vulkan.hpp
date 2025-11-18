//
// Created by André Leite on 03/11/2025.
//

#pragma once

#include "vulkan_types.hpp"

// ////////////////////////
// Vulkan Renderer Backend

// RendererVulkan struct is defined in vulkan_types.hpp

void renderer_vulkan_draw_color(RendererVulkan* vulkan, OS_WindowHandle window, Vec4F32 color);
B32 renderer_vulkan_compile_shader_to_result(RendererVulkan* vulkan, Arena* arena, StringU8 shaderPath,
                                             ShaderCompileResult* outResult);
B32 renderer_vulkan_imgui_init(RendererVulkan* vulkan, OS_WindowHandle window);
void renderer_vulkan_imgui_shutdown(RendererVulkan* vulkan);
void renderer_vulkan_imgui_process_events(RendererVulkan* vulkan, const OS_GraphicsEvent* events, U32 eventCount);
void renderer_vulkan_imgui_begin_frame(RendererVulkan* vulkan, F32 deltaSeconds);
void renderer_vulkan_imgui_end_frame(RendererVulkan* vulkan);
void renderer_vulkan_imgui_on_swapchain_updated(RendererVulkan* vulkan);
void renderer_vulkan_imgui_render(RendererVulkan* vulkan, VkCommandBuffer cmd, VkImageView targetImageView,
                                  VkExtent2D extent);
void renderer_vulkan_imgui_set_window_size(RendererVulkan* vulkan, U32 width, U32 height);
