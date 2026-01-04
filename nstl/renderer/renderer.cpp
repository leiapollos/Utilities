//
// Created by André Leite on 03/11/2025.
//

void renderer_draw(Renderer* renderer, OS_WindowHandle window, const SceneData* scene,
                   const RenderObject* objects, U32 objectCount) {
    if (!renderer || !renderer->backendData) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_draw(vulkan, window, scene, objects, objectCount);
#else
    (void) window;
    (void) scene;
    (void) objects;
    (void) objectCount;
#endif
}

B32 renderer_imgui_init(Renderer* renderer, OS_WindowHandle window) {
    if (!renderer || !renderer->backendData) {
        return 0;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    return renderer_vulkan_imgui_init(vulkan, window);
#else
    (void) window;
    return 0;
#endif
}

void renderer_shutdown(Renderer* renderer) {
    if (!renderer || !renderer->backendData) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_shutdown(vulkan);
#endif
}

void renderer_imgui_shutdown(Renderer* renderer) {
    if (!renderer || !renderer->backendData) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_imgui_shutdown(vulkan);
#endif
}

void renderer_imgui_process_events(Renderer* renderer, const OS_GraphicsEvent* events, U32 eventCount) {
    if (!renderer || !renderer->backendData) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_imgui_process_events(vulkan, events, eventCount);
#else
    (void) events;
    (void) eventCount;
#endif
}

void renderer_imgui_begin_frame(Renderer* renderer, F32 deltaSeconds) {
    if (!renderer || !renderer->backendData) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_imgui_begin_frame(vulkan, deltaSeconds);
#else
    (void) deltaSeconds;
#endif
}

void renderer_imgui_end_frame(Renderer* renderer) {
    if (!renderer || !renderer->backendData) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_imgui_end_frame(vulkan);
#endif
}

void renderer_imgui_set_window_size(Renderer* renderer, U32 width, U32 height) {
    if (!renderer || !renderer->backendData) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_imgui_set_window_size(vulkan, width, height);
#else
    (void) width;
    (void) height;
#endif
}

void renderer_on_window_resized(Renderer* renderer, U32 width, U32 height) {
    if (!renderer || !renderer->backendData) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_on_window_resized(vulkan, width, height);
#else
    (void) width;
    (void) height;
#endif
}

MeshHandle renderer_upload_mesh(Renderer* renderer, const MeshAssetData* meshData) {
    if (!renderer || !renderer->backendData || !meshData) {
        return MESH_HANDLE_INVALID;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    return renderer_vulkan_upload_mesh(vulkan, meshData);
#else
    return MESH_HANDLE_INVALID;
#endif
}

void renderer_destroy_mesh(Renderer* renderer, MeshHandle mesh) {
    if (!renderer || !renderer->backendData || mesh == MESH_HANDLE_INVALID) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_destroy_mesh(vulkan, mesh);
#endif
}

TextureHandle renderer_upload_texture(Renderer* renderer, const LoadedImage* image) {
    if (!renderer || !renderer->backendData || !image) {
        return TEXTURE_HANDLE_INVALID;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    return renderer_vulkan_upload_texture(vulkan, image);
#else
    return TEXTURE_HANDLE_INVALID;
#endif
}

void renderer_destroy_texture(Renderer* renderer, TextureHandle texture) {
    if (!renderer || !renderer->backendData || texture == TEXTURE_HANDLE_INVALID) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_destroy_texture(vulkan, texture);
#endif
}

MaterialHandle renderer_upload_material(Renderer* renderer, const MaterialData* material,
                                        TextureHandle colorTexture, TextureHandle metalRoughTexture) {
    if (!renderer || !renderer->backendData || !material) {
        return MATERIAL_HANDLE_INVALID;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    return renderer_vulkan_upload_material(vulkan, material, colorTexture, metalRoughTexture);
#else
    return MATERIAL_HANDLE_INVALID;
#endif
}

void renderer_destroy_material(Renderer* renderer, MaterialHandle material) {
    if (!renderer || !renderer->backendData || material == MATERIAL_HANDLE_INVALID) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_destroy_material(vulkan, material);
#endif
}

B32 renderer_upload_scene(Renderer* renderer, Arena* arena, const LoadedScene* scene, GPUSceneData* outGPU) {
    if (!renderer || !renderer->backendData || !scene || !outGPU) {
        return 0;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    return renderer_vulkan_upload_scene(vulkan, arena, scene, outGPU);
#else
    return 0;
#endif
}

void renderer_destroy_scene(Renderer* renderer, GPUSceneData* gpu) {
    if (!renderer || !renderer->backendData || !gpu) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_destroy_scene(vulkan, gpu);
#endif
}

#ifndef SHADER_COMPILE_WORKER_COUNT
#define SHADER_COMPILE_WORKER_COUNT 10u
#endif

struct ShaderCompileKernelParams {
    Renderer* renderer;
    Arena* arena;
    const ShaderCompileRequest* requests;
    U32 requestCount;
    ShaderCompileResult* perLaneResults;
    U32* perLaneResultCounts;
    U32 maxResultsPerLane;
};

static void renderer_shader_compile_kernel(void* kernelParameters) {
    ShaderCompileKernelParams* params = (ShaderCompileKernelParams*) kernelParameters;
    if (!params || !params->renderer || !params->arena || !params->requests || params->requestCount == 0u) {
        return;
    }

    U64 laneCount = spmd_lane_count();

    Arena* excludes[1] = {params->arena};
    Temp scratch = get_scratch(excludes, ARRAY_COUNT(excludes));
    DEFER_REF(temp_end(&scratch));
    Arena* scratchArena = scratch.arena;

    ShaderCompileResult* localResults = ARENA_PUSH_ARRAY(scratchArena, ShaderCompileResult, params->requestCount);
    U32 localResultCount = 0u;

    if (!localResults) {
        return;
    }

    RangeU64 range = SPMD_SPLIT_RANGE(params->requestCount);
    for (U64 i = range.min; i < range.max; ++i) {
        const ShaderCompileRequest* request = &params->requests[i];
        if (str8_is_nil(request->shaderPath) || !request->outHandle) {
            continue;
        }

        if (params->renderer->compileShader) {
            ShaderCompileResult result = {};
            B32 success = params->renderer->compileShader(params->renderer->backendData, params->arena,
                                                          request->shaderPath, &result);
            if (success) {
                localResults[localResultCount] = result;
                localResultCount++;
            } else {
                if (request->outHandle) {
                    *request->outHandle = SHADER_HANDLE_INVALID;
                }
            }
        }
    }

    if (localResultCount > 0u && localResultCount <= params->maxResultsPerLane) {
        ShaderCompileResult* sharedResults = &params->perLaneResults[spmd_lane_id() * params->maxResultsPerLane];
        for (U32 i = 0; i < localResultCount; ++i) {
            sharedResults[i] = localResults[i];
        }
        params->perLaneResultCounts[spmd_lane_id()] = localResultCount;
    } else {
        params->perLaneResultCounts[spmd_lane_id()] = 0u;
    }

    SPMD_SYNC();

    if (SPMD_IS_ROOT(0)) {
        if (!params->renderer->mergeShaderResults) {
            return;
        }

        U32 totalResultCount = 0u;
        for (U64 lane = 0; lane < laneCount; ++lane) {
            totalResultCount += params->perLaneResultCounts[lane];
        }

        if (totalResultCount == 0u) {
            return;
        }

        ShaderCompileResult* allResults = ARENA_PUSH_ARRAY(scratchArena, ShaderCompileResult, totalResultCount);
        if (!allResults) {
            return;
        }

        U32 resultIndex = 0u;
        for (U64 lane = 0; lane < laneCount; ++lane) {
            U32 laneResultCount = params->perLaneResultCounts[lane];
            ShaderCompileResult* laneResults = &params->perLaneResults[lane * params->maxResultsPerLane];
            for (U32 i = 0; i < laneResultCount; ++i) {
                if (laneResults[i].valid) {
                    allResults[resultIndex++] = laneResults[i];
                }
            }
        }

        params->renderer->mergeShaderResults(params->renderer->backendData, params->arena, allResults, resultIndex,
                                             params->requests, params->requestCount);
    }

    SPMD_SYNC();
}

void renderer_compile_shaders(Renderer* renderer, Arena* arena, JobSystem* jobSystem,
                              const ShaderCompileRequest* requests, U32 requestCount) {
    if (!renderer || !renderer->backendData || !arena || !jobSystem || !requests || requestCount == 0u) {
        return;
    }

    U32 workerCount = SHADER_COMPILE_WORKER_COUNT;
    if (jobSystem->workerCount < workerCount) {
        workerCount = jobSystem->workerCount;
    }
    if (requestCount < workerCount) {
        workerCount = requestCount;
    }
    if (workerCount == 0u) {
        workerCount = 1u;
    }

    U32 maxResultsPerLane = requestCount;
    ShaderCompileResult* perLaneResults = (ShaderCompileResult*) arena_push(arena,
                                                                            sizeof(ShaderCompileResult) * workerCount *
                                                                            maxResultsPerLane,
                                                                            alignof(ShaderCompileResult));
    U32* perLaneResultCounts = (U32*) arena_push(arena,
                                                 sizeof(U32) * workerCount, alignof(U32));

    if (!perLaneResults || !perLaneResultCounts) {
        return;
    }

    for (U32 i = 0; i < workerCount; ++i) {
        perLaneResultCounts[i] = 0u;
    }

    ShaderCompileKernelParams kernelParams = {};
    kernelParams.renderer = renderer;
    kernelParams.arena = arena;
    kernelParams.requests = requests;
    kernelParams.requestCount = requestCount;
    kernelParams.perLaneResults = perLaneResults;
    kernelParams.perLaneResultCounts = perLaneResultCounts;
    kernelParams.maxResultsPerLane = maxResultsPerLane;

    spmd_dispatch(jobSystem, arena,
                  .laneCount = workerCount,
                  .kernel = renderer_shader_compile_kernel,
                  .kernelParameters = &kernelParams
    );
}

