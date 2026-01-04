//
// Created by André Leite on 03/11/2025.
//

#pragma once

#include "vulkan_types.hpp"

// ////////////////////////
// Vulkan Renderer Backend

void renderer_vulkan_shutdown(RendererVulkan* vulkan);
void renderer_vulkan_draw(RendererVulkan* vulkan, OS_WindowHandle window,
                          const SceneData* scene, const RenderObject* objects, U32 objectCount);
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
void renderer_vulkan_on_window_resized(RendererVulkan* vulkan, U32 width, U32 height);

MeshHandle renderer_vulkan_upload_mesh(RendererVulkan* vulkan, const MeshAssetData* meshData);
void renderer_vulkan_destroy_mesh(RendererVulkan* vulkan, MeshHandle mesh);

TextureHandle renderer_vulkan_upload_texture(RendererVulkan* vulkan, const LoadedImage* image);
void renderer_vulkan_destroy_texture(RendererVulkan* vulkan, TextureHandle texture);

MaterialHandle renderer_vulkan_upload_material(RendererVulkan* vulkan, const MaterialData* material,
                                                TextureHandle colorTexture, TextureHandle metalRoughTexture);
void renderer_vulkan_destroy_material(RendererVulkan* vulkan, MaterialHandle material);

B32 renderer_vulkan_upload_scene(RendererVulkan* vulkan, Arena* arena, const LoadedScene* scene, GPUSceneData* outGPU);
void renderer_vulkan_destroy_scene(RendererVulkan* vulkan, GPUSceneData* gpu);

