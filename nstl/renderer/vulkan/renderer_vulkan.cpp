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
                
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        vulkan->materialPipelineLayout, 0, 1, &sceneDescSet, 0, 0);
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
    if (handle == MESH_HANDLE_INVALID || !vulkan || !vulkan->gpuMeshes) {
        return 0;
    }
    U64 index = handle - 1;
    if (index >= vulkan->gpuMeshCount) {
        return 0;
    }
    return vulkan->gpuMeshes[index];
}

MeshHandle renderer_vulkan_upload_mesh(RendererVulkan* vulkan, const MeshAssetData* meshData) {
    if (!vulkan || !meshData || !meshData->data.vertices || !meshData->data.indices) {
        return MESH_HANDLE_INVALID;
    }
    
    if (vulkan->gpuMeshCount >= vulkan->gpuMeshCapacity) {
        U32 newCapacity = (vulkan->gpuMeshCapacity == 0) ? 64 : vulkan->gpuMeshCapacity * 2;
        GPUMesh** newArray = ARENA_PUSH_ARRAY(vulkan->arena, GPUMesh*, newCapacity);
        if (!newArray) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to grow GPU mesh array");
            return MESH_HANDLE_INVALID;
        }
        if (vulkan->gpuMeshes && vulkan->gpuMeshCount > 0) {
            MEMCPY(newArray, vulkan->gpuMeshes, sizeof(GPUMesh*) * vulkan->gpuMeshCount);
        }
        vulkan->gpuMeshes = newArray;
        vulkan->gpuMeshCapacity = newCapacity;
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
    
    U32 index = vulkan->gpuMeshCount++;
    vulkan->gpuMeshes[index] = mesh;
    MeshHandle handle = (MeshHandle)(index + 1);  // +1 so 0 is invalid

    LOG_INFO(VULKAN_LOG_DOMAIN,
             "Uploaded mesh ({} vertices, {} indices) -> handle {}",
             meshData->data.vertexCount,
             meshData->data.indexCount,
             handle);
    return handle;
}

void renderer_vulkan_destroy_mesh(RendererVulkan* vulkan, MeshHandle handle) {
    if (handle == MESH_HANDLE_INVALID || !vulkan) {
        return;
    }
    
    GPUMesh* mesh = vulkan_get_mesh(vulkan, handle);
    if (mesh) {
        vulkan_destroy_mesh(vulkan, &mesh->gpu);
        mesh->indexCount = 0;
    }
}

// ////////////////////////
// Texture API

TextureHandle renderer_vulkan_upload_texture(RendererVulkan* vulkan, const LoadedImage* image) {
    if (!vulkan || !image || !image->pixels) {
        return TEXTURE_HANDLE_INVALID;
    }
    
    if (vulkan->gpuTextureCount >= vulkan->gpuTextureCapacity) {
        U32 newCapacity = (vulkan->gpuTextureCapacity == 0) ? 64 : vulkan->gpuTextureCapacity * 2;
        GPUTexture** newArray = ARENA_PUSH_ARRAY(vulkan->arena, GPUTexture*, newCapacity);
        if (!newArray) {
            return TEXTURE_HANDLE_INVALID;
        }
        if (vulkan->gpuTextures && vulkan->gpuTextureCount > 0) {
            MEMCPY(newArray, vulkan->gpuTextures, sizeof(GPUTexture*) * vulkan->gpuTextureCount);
        }
        vulkan->gpuTextures = newArray;
        vulkan->gpuTextureCapacity = newCapacity;
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
    
    U32 index = vulkan->gpuTextureCount++;
    vulkan->gpuTextures[index] = tex;
    return (TextureHandle)(index + 1);
}

void renderer_vulkan_destroy_texture(RendererVulkan* vulkan, TextureHandle handle) {
    if (handle == TEXTURE_HANDLE_INVALID || !vulkan) {
        return;
    }
    
    U64 index = handle - 1;
    if (index >= vulkan->gpuTextureCount || !vulkan->gpuTextures[index]) {
        return;
    }
    
    GPUTexture* tex = vulkan->gpuTextures[index];
    if (tex->image.image != VK_NULL_HANDLE) {
        vulkan_destroy_image(vulkan, &tex->image);
    }
}

// ////////////////////////
// Material API

MaterialHandle renderer_vulkan_upload_material(RendererVulkan* vulkan, const MaterialData* material,
                                                TextureHandle colorTexture, TextureHandle metalRoughTexture) {
    if (!vulkan || !material) {
        return MATERIAL_HANDLE_INVALID;
    }
    
    if (vulkan->opaquePipeline == VK_NULL_HANDLE) {
        vulkan_init_material_pipelines(vulkan);
    }
    
    if (vulkan->gpuMaterialCount >= vulkan->gpuMaterialCapacity) {
        U32 newCapacity = (vulkan->gpuMaterialCapacity == 0) ? 64 : vulkan->gpuMaterialCapacity * 2;
        GPUMaterial** newArray = ARENA_PUSH_ARRAY(vulkan->arena, GPUMaterial*, newCapacity);
        if (!newArray) {
            return MATERIAL_HANDLE_INVALID;
        }
        if (vulkan->gpuMaterials && vulkan->gpuMaterialCount > 0) {
            MEMCPY(newArray, vulkan->gpuMaterials, sizeof(GPUMaterial*) * vulkan->gpuMaterialCount);
        }
        vulkan->gpuMaterials = newArray;
        vulkan->gpuMaterialCapacity = newCapacity;
    }
    
    GPUMaterial* mat = ARENA_PUSH_STRUCT(vulkan->arena, GPUMaterial);
    if (!mat) {
        return MATERIAL_HANDLE_INVALID;
    }
    
    mat->uniformBuffer = vulkan_create_buffer(vulkan->device.allocator,
                                               sizeof(MaterialConstants),
                                               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                               VMA_MEMORY_USAGE_CPU_TO_GPU);
    
    MaterialConstants* consts = (MaterialConstants*)mat->uniformBuffer.info.pMappedData;
    consts->colorFactor = material->colorFactor;
    consts->metalRoughFactor.r = material->metallicFactor;
    consts->metalRoughFactor.g = material->roughnessFactor;
    
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
    if (colorTexture != TEXTURE_HANDLE_INVALID) {
        U64 texIndex = colorTexture - 1;
        if (texIndex < vulkan->gpuTextureCount && vulkan->gpuTextures[texIndex]) {
            GPUTexture* gpuTex = vulkan->gpuTextures[texIndex];
            if (gpuTex->image.imageView != VK_NULL_HANDLE) {
                texView = gpuTex->image.imageView;
            }
        }
    }
    
    VulkanDescriptorWriter writer = {};
    vulkan_descriptor_writer_write_buffer(&writer, 0, mat->uniformBuffer.buffer,
                                           sizeof(MaterialConstants), 0,
                                           VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    vulkan_descriptor_writer_write_image(&writer, 1, texView, vulkan->samplerLinear,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    vulkan_descriptor_writer_update_set(&vulkan->device, &writer, descSet);
    
    material_fill(mat, vulkan, MaterialType_Opaque, descSet);
    
    U32 index = vulkan->gpuMaterialCount++;
    vulkan->gpuMaterials[index] = mat;
    return (MaterialHandle)(index + 1);
}

void renderer_vulkan_destroy_material(RendererVulkan* vulkan, MaterialHandle handle) {
    if (handle == MATERIAL_HANDLE_INVALID || !vulkan) {
        return;
    }
    
    U64 index = handle - 1;
    if (index >= vulkan->gpuMaterialCount || !vulkan->gpuMaterials[index]) {
        return;
    }
    
    GPUMaterial* mat = vulkan->gpuMaterials[index];
    if (mat->uniformBuffer.buffer != VK_NULL_HANDLE) {
        vulkan_destroy_buffer(vulkan->device.allocator, &mat->uniformBuffer);
    }
}

// ////////////////////////
// Scene API

struct VulkanSceneTexture {
    RendererVulkanAllocatedImage image;
};

struct VulkanSceneMaterial {
    GPUMaterial material;
    RendererVulkanAllocatedBuffer uniformBuffer;
};

static GPUMaterial* vulkan_get_material(RendererVulkan* vulkan, MaterialHandle handle) {
    if (handle == MATERIAL_HANDLE_INVALID || !vulkan) {
        return &vulkan->defaultMaterial;
    }
    return (GPUMaterial*)(uintptr_t)handle;
}

B32 renderer_vulkan_upload_scene(RendererVulkan* vulkan, Arena* arena, const LoadedScene* scene, GPUSceneData* outGPU) {
    if (!vulkan || !arena || !scene || !outGPU) {
        return 0;
    }
    
    MEMSET(outGPU, 0, sizeof(GPUSceneData));
    
    if (vulkan->opaquePipeline == VK_NULL_HANDLE) {
        vulkan_init_material_pipelines(vulkan);
    }
    
    U32 texCount = scene->imageCount;
    if (texCount > 0) {
        outGPU->textures = ARENA_PUSH_ARRAY(arena, TextureHandle, texCount);
        VulkanSceneTexture* internalTextures = ARENA_PUSH_ARRAY(arena, VulkanSceneTexture, texCount);
        if (!outGPU->textures || !internalTextures) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate texture arrays");
            return 0;
        }
        
        for (U32 i = 0; i < texCount; ++i) {
            LoadedImage* img = &scene->images[i];
            if (img->pixels && img->width > 0 && img->height > 0) {
                VkExtent3D extent = {img->width, img->height, 1};
                internalTextures[i].image = vulkan_create_image_data(vulkan, img->pixels, extent,
                                                                      VK_FORMAT_R8G8B8A8_UNORM,
                                                                      VK_IMAGE_USAGE_SAMPLED_BIT);
                if (internalTextures[i].image.image == VK_NULL_HANDLE) {
                    LOG_WARNING(VULKAN_LOG_DOMAIN, "Failed to upload texture {}", i);
                    outGPU->textures[i] = TEXTURE_HANDLE_INVALID;
                } else {
                    outGPU->textures[i] = (TextureHandle)(uintptr_t)&internalTextures[i];
                }
            } else {
                outGPU->textures[i] = TEXTURE_HANDLE_INVALID;
            }
        }
        
        outGPU->textureCount = texCount;
        LOG_INFO(VULKAN_LOG_DOMAIN, "Uploaded {} scene textures", texCount);
    }
    
    U32 matCount = scene->materialCount;
    if (matCount > 0) {
        outGPU->materials = ARENA_PUSH_ARRAY(arena, MaterialHandle, matCount);
        VulkanSceneMaterial* internalMaterials = ARENA_PUSH_ARRAY(arena, VulkanSceneMaterial, matCount);
        if (!outGPU->materials || !internalMaterials) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate material arrays");
            return 0;
        }
        
        for (U32 i = 0; i < matCount; ++i) {
            MaterialData* matData = &scene->materials[i];
            
            internalMaterials[i].uniformBuffer = vulkan_create_buffer(vulkan->device.allocator,
                                                                       sizeof(MaterialConstants),
                                                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                                       VMA_MEMORY_USAGE_CPU_TO_GPU);
            
            MaterialConstants* consts = (MaterialConstants*)internalMaterials[i].uniformBuffer.info.pMappedData;
            consts->colorFactor = matData->colorFactor;
            consts->metalRoughFactor.r = matData->metallicFactor;
            consts->metalRoughFactor.g = matData->roughnessFactor;
            consts->alphaCutoff = matData->alphaCutoff;
            
            VkDescriptorSet descSet = VK_NULL_HANDLE;
            VkDescriptorSetAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = vulkan->globalDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &vulkan->materialLayout;
            VkResult allocResult = vkAllocateDescriptorSets(vulkan->device.device, &allocInfo, &descSet);
            if (allocResult != VK_SUCCESS) {
                LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate material descriptor set: {}", allocResult);
                outGPU->materials[i] = MATERIAL_HANDLE_INVALID;
                continue;
            }
            
            VkImageView texView = vulkan->whiteImage.imageView;
            if (matData->colorTextureIndex != MATERIAL_NO_TEXTURE && 
                matData->colorTextureIndex < outGPU->textureCount) {
                TextureHandle texHandle = outGPU->textures[matData->colorTextureIndex];
                if (texHandle != TEXTURE_HANDLE_INVALID) {
                    VulkanSceneTexture* sceneTex = (VulkanSceneTexture*)(uintptr_t)texHandle;
                    if (sceneTex->image.imageView != VK_NULL_HANDLE) {
                        texView = sceneTex->image.imageView;
                    }
                }
            }
            
            VulkanDescriptorWriter writer = {};
            vulkan_descriptor_writer_write_buffer(&writer, 0, internalMaterials[i].uniformBuffer.buffer,
                                                   sizeof(MaterialConstants), 0,
                                                   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            vulkan_descriptor_writer_write_image(&writer, 1, texView, vulkan->samplerLinear,
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            vulkan_descriptor_writer_update_set(&vulkan->device, &writer, descSet);
            
            MaterialType matType = (matData->alphaMode == AlphaMode_Blend) 
                                   ? MaterialType_Transparent 
                                   : MaterialType_Opaque;
            material_fill(&internalMaterials[i].material, vulkan, matType, descSet);
            
            outGPU->materials[i] = (MaterialHandle)(uintptr_t)&internalMaterials[i].material;
        }
        
        outGPU->materialCount = matCount;
        LOG_INFO(VULKAN_LOG_DOMAIN, "Created {} GPU materials", matCount);
    }
    
    if (scene->meshCount > 0) {
        outGPU->meshes = ARENA_PUSH_ARRAY(arena, MeshHandle, scene->meshCount);
        if (!outGPU->meshes) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate GPU mesh array");
            return 0;
        }
        
        for (U32 m = 0; m < scene->meshCount; ++m) {
            outGPU->meshes[m] = renderer_vulkan_upload_mesh(vulkan, &scene->meshes[m]);
        }
        
        outGPU->meshCount = scene->meshCount;
        LOG_INFO(VULKAN_LOG_DOMAIN, "Uploaded {} GPU meshes", scene->meshCount);
    }
    
    LOG_INFO(VULKAN_LOG_DOMAIN, "Scene upload complete: {} textures, {} materials, {} meshes",
             outGPU->textureCount, outGPU->materialCount, outGPU->meshCount);
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
            if (gpu->textures[i] != TEXTURE_HANDLE_INVALID) {
                VulkanSceneTexture* sceneTex = (VulkanSceneTexture*)(uintptr_t)gpu->textures[i];
                if (sceneTex->image.image != VK_NULL_HANDLE) {
                    vulkan_destroy_image(vulkan, &sceneTex->image);
                }
            }
        }
    }
    
    if (gpu->materials && gpu->materialCount > 0) {
        for (U32 i = 0; i < gpu->materialCount; ++i) {
            if (gpu->materials[i] != MATERIAL_HANDLE_INVALID) {
                GPUMaterial* mat = (GPUMaterial*)(uintptr_t)gpu->materials[i];
                VulkanSceneMaterial* sceneMat = (VulkanSceneMaterial*)((U8*)mat - offsetof(VulkanSceneMaterial, material));
                if (sceneMat->uniformBuffer.buffer != VK_NULL_HANDLE) {
                    vulkan_destroy_buffer(vulkan->device.allocator, &sceneMat->uniformBuffer);
                }
            }
        }
    }
    
    if (gpu->meshes && gpu->meshCount > 0) {
        for (U32 m = 0; m < gpu->meshCount; ++m) {
            if (gpu->meshes[m] != MESH_HANDLE_INVALID) {
                renderer_vulkan_destroy_mesh(vulkan, gpu->meshes[m]);
            }
        }
    }
    
    MEMSET(gpu, 0, sizeof(GPUSceneData));
}
