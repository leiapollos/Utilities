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

static GPUMesh* vulkan_get_mesh(RendererVulkan* vulkan, MeshHandle handle);
static GPUMaterial* vulkan_get_material(RendererVulkan* vulkan, MaterialHandle handle);
static GPUTexture* vulkan_get_texture(RendererVulkan* vulkan, TextureHandle handle);

static MeshHandle mesh_handle_from_slot_(U32 slot, U32 generation) {
    MeshHandle handle = {};
    handle.slot = slot;
    handle.generation = generation;
    return handle;
}

static TextureHandle texture_handle_from_slot_(U32 slot, U32 generation) {
    TextureHandle handle = {};
    handle.slot = slot;
    handle.generation = generation;
    return handle;
}

static MaterialHandle material_handle_from_slot_(U32 slot, U32 generation) {
    MaterialHandle handle = {};
    handle.slot = slot;
    handle.generation = generation;
    return handle;
}

// ////////////////////////
// Lifetime

B32 renderer_vulkan_backend_init(Arena* arena, Renderer* renderer) {
    if (!arena || !renderer) {
        ASSERT_ALWAYS(false && "Invalid arguments");
        return 0;
    }

    RendererVulkan* vulkan = ARENA_PUSH_STRUCT(arena, RendererVulkan);
    if (!vulkan) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate RendererVulkan");
        return 0;
    }

    MEMSET(vulkan, 0, sizeof(*vulkan));
    vulkan->arena = arena;
    vulkan->radianceCascadeImageCount = 0u;
    vulkan->radianceGridWidth = 0u;
    vulkan->radianceGridHeight = 0u;
    vulkan->radianceImagesInitialized = 0;
    
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

    INIT_SUCCESS(slot_map_init(&vulkan->meshSlots, arena, sizeof(VulkanMeshSlot), 256u) &&
                 "Failed to initialize mesh slot map");
    INIT_SUCCESS(slot_map_init(&vulkan->textureSlots, arena, sizeof(VulkanTextureSlot), 256u) &&
                 "Failed to initialize texture slot map");
    INIT_SUCCESS(slot_map_init(&vulkan->materialSlots, arena, sizeof(VulkanMaterialSlot), 256u) &&
                 "Failed to initialize material slot map");
    INIT_SUCCESS(slot_map_init(&vulkan->pipelines.shaderSlots, arena, sizeof(VulkanShaderSlot), 64u) &&
                 "Failed to initialize shader slot map");

    LOG_INFO(VULKAN_LOG_DOMAIN, "Vulkan renderer initialized successfully");

    renderer->backendData = vulkan;
    renderer->compileShader = vulkan_compile_shader_wrapper;
    renderer->mergeShaderResults = vulkan_merge_shader_results_wrapper;
    return 1;
}

void renderer_vulkan_shutdown(RendererVulkan* vulkan) {
    if (!vulkan) {
        return;
    }
    
    if (vulkan->device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vulkan->device.device);
        renderer_vulkan_imgui_shutdown(vulkan);
    }

    vulkan_destroy_swapchain(&vulkan->swapchain, vulkan->device.device);
    vulkan_destroy_radiance_images(vulkan);
    vulkan_destroy_draw_image(vulkan);
    vulkan_destroy_depth_image(vulkan);
    vulkan_destroy_shadow_map(vulkan);
    vulkan_destroy_immediate_submit(vulkan, &vulkan->immSubmit);
    
    vulkan_destroy_default_resources(vulkan);

    for (U32 slot = 0; slot < vulkan->meshSlots.capacity; ++slot) {
        if (!slot_map_is_occupied(&vulkan->meshSlots, slot)) {
            continue;
        }
        VulkanMeshSlot* meshSlot = (VulkanMeshSlot*)slot_map_item_at(&vulkan->meshSlots, slot);
        if (meshSlot && meshSlot->mesh) {
            vulkan_destroy_mesh(vulkan, &meshSlot->mesh->gpu);
            meshSlot->mesh->indexCount = 0;
            meshSlot->mesh = 0;
        }
    }

    for (U32 slot = 0; slot < vulkan->textureSlots.capacity; ++slot) {
        if (!slot_map_is_occupied(&vulkan->textureSlots, slot)) {
            continue;
        }
        VulkanTextureSlot* textureSlot = (VulkanTextureSlot*)slot_map_item_at(&vulkan->textureSlots, slot);
        if (textureSlot && textureSlot->texture && textureSlot->texture->image.image != VK_NULL_HANDLE) {
            vulkan_destroy_image(vulkan, &textureSlot->texture->image);
            textureSlot->texture = 0;
        }
    }

    for (U32 slot = 0; slot < vulkan->materialSlots.capacity; ++slot) {
        if (!slot_map_is_occupied(&vulkan->materialSlots, slot)) {
            continue;
        }
        VulkanMaterialSlot* materialSlot = (VulkanMaterialSlot*)slot_map_item_at(&vulkan->materialSlots, slot);
        if (materialSlot && materialSlot->material &&
            materialSlot->material->uniformBuffer.buffer != VK_NULL_HANDLE) {
            vulkan_destroy_buffer(vulkan->device.allocator, &materialSlot->material->uniformBuffer);
            materialSlot->material->uniformBuffer = {};
            materialSlot->material = 0;
        }
    }

    slot_map_clear(&vulkan->meshSlots);
    slot_map_clear(&vulkan->textureSlots);
    slot_map_clear(&vulkan->materialSlots);
    slot_map_clear(&vulkan->pipelines.shaderSlots);
    
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
            
            if (vulkan->shadowMap.image == VK_NULL_HANDLE) {
                U32 shadowRes = (vulkan->shadowMapResolution > 0) ? vulkan->shadowMapResolution : 2048;
                vulkan_create_shadow_map(vulkan, shadowRes);
            }
            
            if (vulkan->shadowMap.image != VK_NULL_HANDLE) {
                vulkan_transition_image(cmd, vulkan->shadowMap.image,
                                        VK_IMAGE_LAYOUT_UNDEFINED,
                                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

                VkRenderingAttachmentInfo shadowDepth = {};
                shadowDepth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                shadowDepth.imageView = vulkan->shadowMap.imageView;
                shadowDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                shadowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                shadowDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                shadowDepth.clearValue.depthStencil.depth = 1.0f;

                VkRenderingInfo shadowRenderInfo = {};
                shadowRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                shadowRenderInfo.renderArea.extent = {vulkan->shadowMapResolution, vulkan->shadowMapResolution};
                shadowRenderInfo.layerCount = 1;
                shadowRenderInfo.colorAttachmentCount = 0;
                shadowRenderInfo.pColorAttachments = nullptr;
                shadowRenderInfo.pDepthAttachment = &shadowDepth;

                vkCmdBeginRendering(cmd, &shadowRenderInfo);

                VkViewport shadowViewport = {};
                shadowViewport.x = 0;
                shadowViewport.y = 0;
                shadowViewport.width = (F32)vulkan->shadowMapResolution;
                shadowViewport.height = (F32)vulkan->shadowMapResolution;
                shadowViewport.minDepth = 0.0f;
                shadowViewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &shadowViewport);

                VkRect2D shadowScissor = {};
                shadowScissor.extent = {vulkan->shadowMapResolution, vulkan->shadowMapResolution};
                vkCmdSetScissor(cmd, 0, 1, &shadowScissor);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan->shadowPipeline);
                
                VkDescriptorSet shadowSceneSet;
                if (vulkan_descriptor_allocator_allocate(&vulkan->device, frame->frameDescriptors,
                                                          vulkan->sceneDataLayout, &shadowSceneSet)) {
                    VulkanDescriptorWriter writer = {};
                    vulkan_descriptor_writer_write_buffer(&writer, 0, frame->sceneBuffer->buffer,
                                                           sizeof(SceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                    vulkan_descriptor_writer_update_set(&vulkan->device, &writer, shadowSceneSet);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            vulkan->shadowPipelineLayout, 0, 1, &shadowSceneSet, 0, 0);
                }

                for (U32 i = 0; i < objectCount; ++i) {
                    const RenderObject* obj = &objects[i];
                    GPUMesh* mesh = vulkan_get_mesh(vulkan, obj->mesh);
                    if (mesh) {
                        GPUMaterial* mat = vulkan_get_material(vulkan, obj->material);
                        if (mat->type != MaterialType_Opaque) {
                            continue;
                        }
                        
                        GPUDrawPushConstants pushConstants = {};
                        pushConstants.worldMatrix = obj->transform;
                        pushConstants.vertexBuffer = mesh->gpu.vertexBufferAddress;
                        pushConstants.color = obj->color;
                        
                        vkCmdPushConstants(cmd,
                                           vulkan->shadowPipelineLayout,
                                           VK_SHADER_STAGE_VERTEX_BIT,
                                           0,
                                           sizeof(GPUDrawPushConstants),
                                           &pushConstants);
                        
                        vkCmdBindIndexBuffer(cmd, mesh->gpu.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                        
                        U32 firstIdx = obj->firstIndex;
                        U32 idxCount = (obj->indexCount > 0) ? obj->indexCount : mesh->indexCount;
                        vkCmdDrawIndexed(cmd, idxCount, 1, firstIdx, 0, 0);
                    }
                }

                vkCmdEndRendering(cmd);

                vulkan_transition_image(cmd, vulkan->shadowMap.image,
                                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }

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
            
            VkViewport mainViewport = {};
            mainViewport.x = 0;
            mainViewport.y = 0;
            mainViewport.width = (F32)vulkan->drawExtent.width;
            mainViewport.height = (F32)vulkan->drawExtent.height;
            mainViewport.minDepth = 0.0f;
            mainViewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &mainViewport);

            VkRect2D mainScissor = {};
            mainScissor.extent = vulkan->drawExtent;
            vkCmdSetScissor(cmd, 0, 1, &mainScissor);
            
            VkDescriptorSet sceneDescSet;
            if (vulkan_descriptor_allocator_allocate(&vulkan->device, frame->frameDescriptors,
                                                      vulkan->sceneDataLayout, &sceneDescSet)) {
                VulkanDescriptorWriter writer = {};
                vulkan_descriptor_writer_write_buffer(&writer, 0, frame->sceneBuffer->buffer,
                                                       sizeof(SceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                vulkan_descriptor_writer_update_set(&vulkan->device, &writer, sceneDescSet);
                
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        vulkan->materialPipelineLayout, 0, 1, &sceneDescSet, 0, 0);
            }
            
            VkDescriptorSet shadowDescSet;
            if (vulkan->shadowMap.image != VK_NULL_HANDLE &&
                vulkan_descriptor_allocator_allocate(&vulkan->device, frame->frameDescriptors,
                                                      vulkan->shadowMapLayout, &shadowDescSet)) {
                VulkanDescriptorWriter writer = {};
                vulkan_descriptor_writer_write_image(&writer, 0, vulkan->shadowMap.imageView, vulkan->shadowSampler,
                                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
                vulkan_descriptor_writer_update_set(&vulkan->device, &writer, shadowDescSet);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        vulkan->materialPipelineLayout, 2, 1, &shadowDescSet, 0, 0);
            }
            
            GPUMaterial* lastMaterial = 0;

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan->opaquePipeline);
            
            for (U32 i = 0; i < objectCount; ++i) {
                const RenderObject* obj = &objects[i];
                GPUMesh* mesh = vulkan_get_mesh(vulkan, obj->mesh);
                if (mesh) {
                    GPUMaterial* mat = vulkan_get_material(vulkan, obj->material);
                    if (mat->descriptorSet == VK_NULL_HANDLE) {
                        mat = &vulkan->defaultMaterial;
                    }
                    if (mat->type != MaterialType_Opaque) {
                        continue;
                    }
                    if (mat != lastMaterial) {
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                vulkan->materialPipelineLayout, 1, 1, &mat->descriptorSet, 0, 0);
                        lastMaterial = mat;
                    }
                    U32 firstIdx = obj->firstIndex;
                    U32 idxCount = (obj->indexCount > 0) ? obj->indexCount : mesh->indexCount;
                    vulkan_draw_mesh(vulkan, cmd, &mesh->gpu, obj->transform, firstIdx, idxCount, obj->color);
                }
            }
            
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan->transparentPipeline);
            lastMaterial = 0;
            
            for (U32 i = 0; i < objectCount; ++i) {
                const RenderObject* obj = &objects[i];
                GPUMesh* mesh = vulkan_get_mesh(vulkan, obj->mesh);
                if (mesh) {
                    GPUMaterial* mat = vulkan_get_material(vulkan, obj->material);
                    if (mat->descriptorSet == VK_NULL_HANDLE) {
                        mat = &vulkan->defaultMaterial;
                    }
                    if (mat->type != MaterialType_Transparent) {
                        continue;
                    }
                    if (mat != lastMaterial) {
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                vulkan->materialPipelineLayout, 1, 1, &mat->descriptorSet, 0, 0);
                        lastMaterial = mat;
                    }
                    U32 firstIdx = obj->firstIndex;
                    U32 idxCount = (obj->indexCount > 0) ? obj->indexCount : mesh->indexCount;
                    vulkan_draw_mesh(vulkan, cmd, &mesh->gpu, obj->transform, firstIdx, idxCount, obj->color);
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

void renderer_vulkan_draw_radiance_2d(RendererVulkan* vulkan, OS_WindowHandle window,
                                      const RendererRadiance2DDesc* desc) {
    if (!vulkan || !window.handle || !desc) {
        return;
    }
    if (desc->gridWidth == 0u || desc->gridHeight == 0u) {
        return;
    }
    if (!TEXTURE_HANDLE_IS_VALID(desc->emissiveTexture) || !TEXTURE_HANDLE_IS_VALID(desc->occluderTexture)) {
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

        B32 dispatchedRadiance = 0;
        GPUTexture* emissiveTexture = vulkan_get_texture(vulkan, desc->emissiveTexture);
        GPUTexture* occluderTexture = vulkan_get_texture(vulkan, desc->occluderTexture);
        if (emissiveTexture && occluderTexture) {
            dispatchedRadiance = vulkan_dispatch_radiance_2d(vulkan, cmd, frame, desc, emissiveTexture, occluderTexture);
        }
        if (!dispatchedRadiance) {
            vulkan_dispatch_gradient(vulkan, cmd, clearColor);
        }

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

static GPUMesh* vulkan_get_mesh(RendererVulkan* vulkan, MeshHandle handle) {
    if (!vulkan || !MESH_HANDLE_IS_VALID(handle)) {
        return 0;
    }
    VulkanMeshSlot* meshSlot =
        (VulkanMeshSlot*)slot_map_get(&vulkan->meshSlots, handle.slot, handle.generation);
    if (!meshSlot) {
        return 0;
    }
    return meshSlot->mesh;
}

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

    void* slotItem = 0;
    U32 slotIndex = 0;
    U32 generation = 0;
    if (!slot_map_alloc(&vulkan->meshSlots, &slotItem, &slotIndex, &generation)) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate mesh slot");
        vulkan_destroy_mesh(vulkan, &mesh->gpu);
        mesh->indexCount = 0;
        return MESH_HANDLE_INVALID;
    }

    VulkanMeshSlot* meshSlot = (VulkanMeshSlot*)slotItem;
    meshSlot->mesh = mesh;
    MeshHandle handle = mesh_handle_from_slot_(slotIndex, generation);

    LOG_DEBUG(VULKAN_LOG_DOMAIN,
             "Uploaded mesh ({} vertices, {} indices) -> slot {} gen {}",
             meshData->data.vertexCount,
             meshData->data.indexCount,
             handle.slot,
             handle.generation);
    return handle;
}

void renderer_vulkan_destroy_mesh(RendererVulkan* vulkan, MeshHandle handle) {
    if (!vulkan || !MESH_HANDLE_IS_VALID(handle)) {
        return;
    }

    void* slotItem = 0;
    if (!slot_map_release(&vulkan->meshSlots, handle.slot, handle.generation, &slotItem)) {
        return;
    }

    VulkanMeshSlot* meshSlot = (VulkanMeshSlot*)slotItem;
    if (meshSlot && meshSlot->mesh) {
        vulkan_destroy_mesh(vulkan, &meshSlot->mesh->gpu);
        meshSlot->mesh->indexCount = 0;
        meshSlot->mesh = 0;
    }
}

// ////////////////////////
// Texture API

static GPUTexture* vulkan_get_texture(RendererVulkan* vulkan, TextureHandle handle) {
    if (!vulkan || !TEXTURE_HANDLE_IS_VALID(handle)) {
        return 0;
    }
    VulkanTextureSlot* textureSlot =
        (VulkanTextureSlot*)slot_map_get(&vulkan->textureSlots, handle.slot, handle.generation);
    if (!textureSlot) {
        return 0;
    }
    return textureSlot->texture;
}

TextureHandle renderer_vulkan_upload_texture(RendererVulkan* vulkan, const LoadedImage* image) {
    if (!vulkan || !image || !image->pixels) {
        return TEXTURE_HANDLE_INVALID;
    }

    GPUTexture* tex = ARENA_PUSH_STRUCT(vulkan->arena, GPUTexture);
    if (!tex) {
        return TEXTURE_HANDLE_INVALID;
    }

    VkExtent3D extent = {image->width, image->height, 1};
    tex->image = vulkan_create_image_data(vulkan, image->pixels, extent,
                                           VK_FORMAT_R8G8B8A8_UNORM,
                                           VK_IMAGE_USAGE_SAMPLED_BIT);
    if (tex->image.image == VK_NULL_HANDLE) {
        return TEXTURE_HANDLE_INVALID;
    }

    void* slotItem = 0;
    U32 slotIndex = 0;
    U32 generation = 0;
    if (!slot_map_alloc(&vulkan->textureSlots, &slotItem, &slotIndex, &generation)) {
        vulkan_destroy_image(vulkan, &tex->image);
        return TEXTURE_HANDLE_INVALID;
    }

    VulkanTextureSlot* textureSlot = (VulkanTextureSlot*)slotItem;
    textureSlot->texture = tex;
    return texture_handle_from_slot_(slotIndex, generation);
}

void renderer_vulkan_destroy_texture(RendererVulkan* vulkan, TextureHandle handle) {
    if (!vulkan || !TEXTURE_HANDLE_IS_VALID(handle)) {
        return;
    }

    void* slotItem = 0;
    if (!slot_map_release(&vulkan->textureSlots, handle.slot, handle.generation, &slotItem)) {
        return;
    }

    VulkanTextureSlot* textureSlot = (VulkanTextureSlot*)slotItem;
    if (textureSlot && textureSlot->texture && textureSlot->texture->image.image != VK_NULL_HANDLE) {
        vulkan_destroy_image(vulkan, &textureSlot->texture->image);
        textureSlot->texture = 0;
    }
}

B32 renderer_vulkan_update_texture(RendererVulkan* vulkan, TextureHandle handle, const LoadedImage* image) {
    if (!vulkan || !image || !image->pixels || !TEXTURE_HANDLE_IS_VALID(handle)) {
        return 0;
    }

    GPUTexture* texture = vulkan_get_texture(vulkan, handle);
    if (!texture || texture->image.image == VK_NULL_HANDLE) {
        return 0;
    }

    if (texture->image.imageExtent.width != image->width || texture->image.imageExtent.height != image->height) {
        LOG_WARNING(VULKAN_LOG_DOMAIN,
                    "Texture update skipped due to size mismatch (existing={}x{}, incoming={}x{})",
                    texture->image.imageExtent.width,
                    texture->image.imageExtent.height,
                    image->width,
                    image->height);
        return 0;
    }

    return vulkan_upload_pixels_to_image(vulkan,
                                         &texture->image,
                                         image->pixels,
                                         image->width,
                                         image->height,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// ////////////////////////
// Material API

MaterialHandle renderer_vulkan_upload_material(RendererVulkan* vulkan, const MaterialData* material,
                                                TextureHandle colorTexture, TextureHandle metalRoughTexture) {
    if (!vulkan || !material) {
        return MATERIAL_HANDLE_INVALID;
    }
    (void)metalRoughTexture;

    if (vulkan->opaquePipeline == VK_NULL_HANDLE) {
        vulkan_init_material_pipelines(vulkan);
    }

    GPUMaterial* mat = ARENA_PUSH_STRUCT(vulkan->arena, GPUMaterial);
    if (!mat) {
        return MATERIAL_HANDLE_INVALID;
    }

    mat->uniformBuffer = vulkan_create_buffer(vulkan->device.allocator,
                                               sizeof(MaterialConstants),
                                               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                               VMA_MEMORY_USAGE_CPU_TO_GPU);
    if (mat->uniformBuffer.buffer == VK_NULL_HANDLE) {
        return MATERIAL_HANDLE_INVALID;
    }

    MaterialConstants* consts = (MaterialConstants*)mat->uniformBuffer.info.pMappedData;
    consts->colorFactor = material->colorFactor;
    consts->metalRoughFactor.r = material->metallicFactor;
    consts->metalRoughFactor.g = material->roughnessFactor;
    consts->alphaCutoff = material->alphaCutoff;

    VkDescriptorSet descSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = vulkan->globalDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &vulkan->materialLayout;
    if (vkAllocateDescriptorSets(vulkan->device.device, &allocInfo, &descSet) != VK_SUCCESS) {
        vulkan_destroy_buffer(vulkan->device.allocator, &mat->uniformBuffer);
        return MATERIAL_HANDLE_INVALID;
    }

    VkImageView texView = vulkan->whiteImage.imageView;
    GPUTexture* gpuTex = vulkan_get_texture(vulkan, colorTexture);
    if (gpuTex && gpuTex->image.imageView != VK_NULL_HANDLE) {
        texView = gpuTex->image.imageView;
    }

    VulkanDescriptorWriter writer = {};
    vulkan_descriptor_writer_write_buffer(&writer, 0, mat->uniformBuffer.buffer,
                                           sizeof(MaterialConstants), 0,
                                           VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    vulkan_descriptor_writer_write_image(&writer, 1, texView, vulkan->samplerLinear,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    vulkan_descriptor_writer_update_set(&vulkan->device, &writer, descSet);

    MaterialType materialType = (material->alphaMode == AlphaMode_Blend)
                                ? MaterialType_Transparent
                                : MaterialType_Opaque;
    material_fill(mat, vulkan, materialType, descSet);

    void* slotItem = 0;
    U32 slotIndex = 0;
    U32 generation = 0;
    if (!slot_map_alloc(&vulkan->materialSlots, &slotItem, &slotIndex, &generation)) {
        if (mat->uniformBuffer.buffer != VK_NULL_HANDLE) {
            vulkan_destroy_buffer(vulkan->device.allocator, &mat->uniformBuffer);
        }
        return MATERIAL_HANDLE_INVALID;
    }

    VulkanMaterialSlot* materialSlot = (VulkanMaterialSlot*)slotItem;
    materialSlot->material = mat;
    return material_handle_from_slot_(slotIndex, generation);
}

void renderer_vulkan_destroy_material(RendererVulkan* vulkan, MaterialHandle handle) {
    if (!vulkan || !MATERIAL_HANDLE_IS_VALID(handle)) {
        return;
    }

    void* slotItem = 0;
    if (!slot_map_release(&vulkan->materialSlots, handle.slot, handle.generation, &slotItem)) {
        return;
    }

    VulkanMaterialSlot* materialSlot = (VulkanMaterialSlot*)slotItem;
    if (materialSlot && materialSlot->material &&
        materialSlot->material->uniformBuffer.buffer != VK_NULL_HANDLE) {
        vulkan_destroy_buffer(vulkan->device.allocator, &materialSlot->material->uniformBuffer);
        materialSlot->material->uniformBuffer = {};
        materialSlot->material = 0;
    }
}

// ////////////////////////
// Scene API

static GPUMaterial* vulkan_get_material(RendererVulkan* vulkan, MaterialHandle handle) {
    if (!vulkan || !MATERIAL_HANDLE_IS_VALID(handle)) {
        return &vulkan->defaultMaterial;
    }

    VulkanMaterialSlot* materialSlot =
        (VulkanMaterialSlot*)slot_map_get(&vulkan->materialSlots, handle.slot, handle.generation);
    if (!materialSlot || !materialSlot->material) {
        return &vulkan->defaultMaterial;
    }

    return materialSlot->material;
}

B32 renderer_vulkan_upload_scene(RendererVulkan* vulkan, Arena* arena, const LoadedScene* scene, GPUSceneData* outGPU) {
    if (!vulkan || !arena || !scene || !outGPU) {
        return 0;
    }
    
    MEMSET(outGPU, 0, sizeof(GPUSceneData));
    
    if (vulkan->opaquePipeline == VK_NULL_HANDLE) {
        vulkan_init_material_pipelines(vulkan);
    }

    if (scene->imageCount > 0) {
        outGPU->textures = ARENA_PUSH_ARRAY(arena, TextureHandle, scene->imageCount);
        if (!outGPU->textures) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate scene texture handles");
            return 0;
        }
        outGPU->textureCount = scene->imageCount;
        U64 uploadedTextureBytes = 0;
        for (U32 i = 0; i < scene->imageCount; ++i) {
            outGPU->textures[i] = renderer_vulkan_upload_texture(vulkan, &scene->images[i]);
            uploadedTextureBytes += ((U64)scene->images[i].width * (U64)scene->images[i].height * 4u);
        }
        LOG_INFO(VULKAN_LOG_DOMAIN,
                 "Scene texture upload footprint: {} bytes ~= {} MB",
                 uploadedTextureBytes,
                 (uploadedTextureBytes / MB(1)));
    }

    if (scene->materialCount > 0) {
        outGPU->materials = ARENA_PUSH_ARRAY(arena, MaterialHandle, scene->materialCount);
        if (!outGPU->materials) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate scene material handles");
            return 0;
        }
        outGPU->materialCount = scene->materialCount;
        for (U32 i = 0; i < scene->materialCount; ++i) {
            const MaterialData* matData = &scene->materials[i];
            TextureHandle colorTexture = TEXTURE_HANDLE_INVALID;
            TextureHandle metalRoughTexture = TEXTURE_HANDLE_INVALID;

            if (matData->colorTextureIndex != MATERIAL_NO_TEXTURE &&
                matData->colorTextureIndex < outGPU->textureCount) {
                colorTexture = outGPU->textures[matData->colorTextureIndex];
            }

            if (matData->metalRoughTextureIndex != MATERIAL_NO_TEXTURE &&
                matData->metalRoughTextureIndex < outGPU->textureCount) {
                metalRoughTexture = outGPU->textures[matData->metalRoughTextureIndex];
            }

            outGPU->materials[i] = renderer_vulkan_upload_material(vulkan, matData, colorTexture, metalRoughTexture);
        }
    }

    if (scene->meshCount > 0) {
        outGPU->meshes = ARENA_PUSH_ARRAY(arena, MeshHandle, scene->meshCount);
        if (!outGPU->meshes) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate scene mesh handles");
            return 0;
        }
        outGPU->meshCount = scene->meshCount;
        for (U32 m = 0; m < scene->meshCount; ++m) {
            outGPU->meshes[m] = renderer_vulkan_upload_mesh(vulkan, &scene->meshes[m]);
        }
    }
    
    LOG_INFO(VULKAN_LOG_DOMAIN, "Scene upload complete: {} textures, {} materials, {} meshes",
             outGPU->textureCount, outGPU->materialCount, outGPU->meshCount);
    vulkan_log_allocator_stats_(vulkan, "after scene upload");
    return 1;
}

void renderer_vulkan_destroy_scene(RendererVulkan* vulkan, GPUSceneData* gpu) {
    if (!vulkan || !gpu) {
        return;
    }
    
    if (vulkan->device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vulkan->device.device);
    }
    
    if (gpu->textures && gpu->textureCount > 0) {
        for (U32 i = 0; i < gpu->textureCount; ++i) {
            if (TEXTURE_HANDLE_IS_VALID(gpu->textures[i])) {
                renderer_vulkan_destroy_texture(vulkan, gpu->textures[i]);
            }
        }
    }
    
    if (gpu->materials && gpu->materialCount > 0) {
        for (U32 i = 0; i < gpu->materialCount; ++i) {
            if (MATERIAL_HANDLE_IS_VALID(gpu->materials[i])) {
                renderer_vulkan_destroy_material(vulkan, gpu->materials[i]);
            }
        }
    }
    
    if (gpu->meshes && gpu->meshCount > 0) {
        for (U32 m = 0; m < gpu->meshCount; ++m) {
            if (MESH_HANDLE_IS_VALID(gpu->meshes[m])) {
                renderer_vulkan_destroy_mesh(vulkan, gpu->meshes[m]);
            }
        }
    }
    
    MEMSET(gpu, 0, sizeof(GPUSceneData));
}
