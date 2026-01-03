//
// Created by André Leite on 03/11/2025.
//

#include "renderer_vulkan.hpp"

// Subsystems (Unity Build)
#include "vulkan_device.cpp"
#include "vulkan_descriptors.cpp"
#include "vulkan_commands.cpp"
#include "vulkan_resources.cpp"
#include "vulkan_pipelines.cpp"
#include "vulkan_materials.cpp"
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
    
    U32 perFrameTotalBytes = KB(64) * VULKAN_FRAME_OVERLAP;
    vulkan->deferPerFrameMem = (U8*) arena_push(arena, perFrameTotalBytes, 8);
    vulkan->deferGlobalMem = (U8*) arena_push(arena, KB(256), 8);
    
    vkdefer_init_memory(&vulkan->deferCtx,
                        VULKAN_FRAME_OVERLAP,
                        vulkan->deferPerFrameMem,
                        KB(64),
                        vulkan->deferGlobalMem,
                        KB(256));

    INIT_SUCCESS(vulkan_device_init(&vulkan->device, arena) && "Failed to initialize Vulkan device");
    
    vkdefer_init_device(&vulkan->deferCtx, vulkan->device.device, 0);
    vkdefer_set_vma_allocator(&vulkan->deferCtx, vulkan->device.allocator);
    
    INIT_SUCCESS(vulkan_commands_init(vulkan, &vulkan->commands) && "Failed to initialize Vulkan commands");
    
    INIT_SUCCESS(vulkan_init_immediate_submit(vulkan, &vulkan->immSubmit) && "Failed to initialize immediate submit");
    
    INIT_SUCCESS(vulkan_init_draw_pipeline(vulkan) && "Failed to initialize Vulkan draw pipeline");
    
    INIT_SUCCESS(vulkan_init_default_resources(vulkan) && "Failed to initialize default resources");

    VkDescriptorType frameDescTypes[] = {
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    };
    F32 frameDescRatios[] = { 3.0f, 3.0f, 3.0f, 4.0f };

    for (U32 i = 0; i < VULKAN_FRAME_OVERLAP; ++i) {
        RendererVulkanFrame* frame = &vulkan->commands.frames[i];
        
        frame->frameDescriptors = ARENA_PUSH_STRUCT(arena, VulkanDescriptorAllocator);
        ASSERT_ALWAYS(frame->frameDescriptors != 0);
        INIT_SUCCESS(vulkan_descriptor_allocator_init(&vulkan->device,
                                                       frame->frameDescriptors,
                                                       arena,
                                                       1000,
                                                       frameDescTypes,
                                                       frameDescRatios,
                                                       4) && "Failed to init per-frame descriptor allocator");
        
        frame->sceneBuffer = ARENA_PUSH_STRUCT(arena, RendererVulkanAllocatedBuffer);
        ASSERT_ALWAYS(frame->sceneBuffer != 0);
        *frame->sceneBuffer = vulkan_create_buffer(vulkan->device.allocator,
                                                    sizeof(SceneData),
                                                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                    VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

    vulkan->frameArena = arena_alloc(.arenaSize = MB(1));
    ASSERT_ALWAYS(vulkan->frameArena != 0);

    LOG_INFO(VULKAN_LOG_DOMAIN, "Vulkan renderer initialized successfully");

    renderer->backendData = vulkan;
    renderer->compileShader = vulkan_compile_shader_wrapper;
    renderer->mergeShaderResults = vulkan_merge_shader_results_wrapper;
    return 1;
}

void renderer_vulkan_shutdown(RendererVulkan* vulkan) {
    Renderer* renderer = (Renderer*)((U8*)vulkan - offsetof(Renderer, backendData));
    if (!vulkan) {
        return;
    }
    
    if (vulkan->device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vulkan->device.device);
        renderer_vulkan_imgui_shutdown(vulkan);
    }

    vulkan_destroy_swapchain(&vulkan->swapchain, vulkan->device.device);
    vulkan_destroy_draw_image(vulkan);
    vulkan_destroy_depth_image(vulkan);
    vulkan_destroy_immediate_submit(vulkan, &vulkan->immSubmit);
    
    vulkan_destroy_default_resources(vulkan);
    
    for (U32 i = 0; i < VULKAN_FRAME_OVERLAP; ++i) {
        RendererVulkanFrame* frame = &vulkan->commands.frames[i];
        if (frame->frameDescriptors) {
            vulkan_descriptor_allocator_destroy(&vulkan->device, frame->frameDescriptors);
        }
        if (frame->sceneBuffer) {
            vulkan_destroy_buffer(vulkan->device.allocator, frame->sceneBuffer);
        }
    }

    if (vulkan->frameArena) {
        arena_release(vulkan->frameArena);
        vulkan->frameArena = 0;
    }
    
    if (vulkan->defaultMaterialBuffer.buffer != VK_NULL_HANDLE) {
        vulkan_destroy_buffer(vulkan->device.allocator, &vulkan->defaultMaterialBuffer);
    }
    
    if (vulkan->device.device != VK_NULL_HANDLE) {
        vkdefer_shutdown(&vulkan->deferCtx);
    }
    
    vulkan_destroy_surface(&vulkan->device, &vulkan->swapchain.surface);
    
    vulkan_device_shutdown(&vulkan->device);
}

// ////////////////////////
// Draw

void renderer_vulkan_draw(RendererVulkan* vulkan, OS_WindowHandle window,
                          const SceneData* scene, const RenderObject* objects, U32 objectCount) {
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
    
    RendererVulkanFrame* frame = &vulkan->commands.frames[vulkan->commands.currentFrameIndex];
    
    vulkan_descriptor_allocator_clear(&vulkan->device, frame->frameDescriptors);
    
    if (scene) {
        SceneData* mappedScene = (SceneData*)frame->sceneBuffer->info.pMappedData;
        *mappedScene = *scene;
    }
    
    U32 imageIndex = frame->imageIndex;
    RendererVulkanSwapchainImage* image = &vulkan->swapchain.images[imageIndex];

    B32 hasDrawImage = (vulkan->drawImage.image != VK_NULL_HANDLE) &&
                       (vulkan->drawImage.imageView != VK_NULL_HANDLE);
    VkImageLayout swapchainLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    Vec4F32 clearColor = {{0.1f, 0.1f, 0.1f, 1.0f}};

    if (hasDrawImage) {
        vulkan_transition_image(cmd,
                                vulkan->drawImage.image,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_GENERAL);

        vulkan_dispatch_gradient(vulkan, cmd, clearColor);

        if (vulkan->opaquePipeline == VK_NULL_HANDLE) {
            vulkan_init_material_pipelines(vulkan);
        }

        if (vulkan->depthImage.image == VK_NULL_HANDLE) {
            VkExtent3D depthExtent = {vulkan->drawExtent.width, vulkan->drawExtent.height, 1};
            vulkan_create_depth_image(vulkan, depthExtent);
        }

        B32 hasDrawCommands = (objects != 0 && objectCount > 0);
        if (hasDrawCommands && vulkan->opaquePipeline != VK_NULL_HANDLE) {
            vulkan_transition_image(cmd,
                                    vulkan->drawImage.image,
                                    VK_IMAGE_LAYOUT_GENERAL,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            vulkan_transition_image(cmd,
                                    vulkan->depthImage.image,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

            VkRenderingAttachmentInfo colorAttachment = {};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = vulkan->drawImage.imageView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingAttachmentInfo depthAttachment = {};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = vulkan->depthImage.imageView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil.depth = 1.0f;

            VkRenderingInfo renderingInfo = {};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.extent = vulkan->drawExtent;
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.pDepthAttachment = &depthAttachment;

            vkCmdBeginRendering(cmd, &renderingInfo);
            
            VkDescriptorSet sceneDescSet;
            if (vulkan_descriptor_allocator_allocate(&vulkan->device, frame->frameDescriptors,
                                                      vulkan->sceneDataLayout, &sceneDescSet)) {
                VulkanDescriptorWriter writer = {};
                vulkan_descriptor_writer_write_buffer(&writer, 0, frame->sceneBuffer->buffer,
                                                       sizeof(SceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                vulkan_descriptor_writer_update_set(&vulkan->device, &writer, sceneDescSet);
                
                VkDescriptorSet descSets[2] = { sceneDescSet, vulkan->defaultMaterial.descriptorSet };
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        vulkan->materialPipelineLayout, 0, 2, descSets, 0, 0);
            }

            for (U32 i = 0; i < objectCount; ++i) {
                const RenderObject* obj = &objects[i];
                if (obj->mesh) {
                    vulkan_draw_mesh(vulkan, cmd, &obj->mesh->gpu, obj->transform, obj->mesh->indexCount, obj->color);
                }
            }

            vkCmdEndRendering(cmd);

            vulkan_transition_image(cmd,
                                    vulkan->drawImage.image,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        } else {
            vulkan_transition_image(cmd,
                                    vulkan->drawImage.image,
                                    VK_IMAGE_LAYOUT_GENERAL,
                                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        }
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
        VkClearColorValue clearValue = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
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

    arena_pop_to(vulkan->frameArena, ARENA_HEADER_SIZE);

    vulkan_end_frame(vulkan);
}

void renderer_vulkan_on_window_resized(RendererVulkan* vulkan, U32 width, U32 height) {
    if (!vulkan || vulkan->device.device == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(vulkan->device.device);
    vulkan_destroy_swapchain(&vulkan->swapchain, vulkan->device.device);
    vulkan_destroy_draw_image(vulkan);
    vulkan_destroy_depth_image(vulkan);
}

// ////////////////////////
// Mesh API

MeshHandle renderer_vulkan_upload_mesh(RendererVulkan* vulkan, const MeshAssetData* meshData) {
    if (!vulkan || !meshData || !meshData->data.vertices || !meshData->data.indices) {
        return MESH_HANDLE_INVALID;
    }

    GPUMesh* mesh = ARENA_PUSH_STRUCT(vulkan->arena, GPUMesh);
    if (!mesh) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate GPUMesh");
        return MESH_HANDLE_INVALID;
    }

    mesh->gpu = vulkan_upload_mesh(vulkan,
                                    meshData->data.indices,
                                    meshData->data.indexCount,
                                    meshData->data.vertices,
                                    meshData->data.vertexCount);
    mesh->indexCount = meshData->data.indexCount;

    if (mesh->gpu.vertexBuffer.buffer == VK_NULL_HANDLE) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to upload mesh GPU buffers");
        return MESH_HANDLE_INVALID;
    }

    LOG_INFO(VULKAN_LOG_DOMAIN,
             "Uploaded mesh ({} vertices, {} indices)",
             meshData->data.vertexCount,
             meshData->data.indexCount);
    return mesh;
}

void renderer_vulkan_destroy_mesh(RendererVulkan* vulkan, MeshHandle mesh) {
    if (!vulkan || !mesh) {
        return;
    }

    vulkan_destroy_mesh(vulkan, &mesh->gpu);
    mesh->indexCount = 0;
}
