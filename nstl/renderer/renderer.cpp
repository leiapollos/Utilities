//
// Created by André Leite on 03/11/2025.
//

struct RendererBackendOps {
    void (*shutdown)(void* backend);
    void (*draw)(void* backend, OS_WindowHandle window, const SceneData* scene,
                 const RenderObject* objects, U32 objectCount);
    void (*drawRadiance2D)(void* backend, OS_WindowHandle window, const RendererRadiance2DDesc* desc);
    void (*resize)(void* backend, U32 width, U32 height);
    B32 (*imguiInit)(void* backend, OS_WindowHandle window);
    void (*imguiShutdown)(void* backend);
    void (*imguiProcessEvents)(void* backend, const OS_GraphicsEvent* events, U32 eventCount);
    void (*imguiBeginFrame)(void* backend, F32 deltaSeconds);
    void (*imguiEndFrame)(void* backend);
    void (*imguiSetWindowSize)(void* backend, U32 width, U32 height);
    MeshHandle (*uploadMesh)(void* backend, const MeshAssetData* meshData);
    void (*destroyMesh)(void* backend, MeshHandle mesh);
    TextureHandle (*uploadTexture)(void* backend, const LoadedImage* image);
    void (*destroyTexture)(void* backend, TextureHandle texture);
    B32 (*updateTexture)(void* backend, TextureHandle texture, const LoadedImage* image);
    MaterialHandle (*uploadMaterial)(void* backend, const MaterialData* material,
                                     TextureHandle colorTexture, TextureHandle metalRoughTexture);
    void (*destroyMaterial)(void* backend, MaterialHandle material);
    B32 (*uploadScene)(void* backend, Arena* arena, const LoadedScene* scene, GPUSceneData* outGPU);
    void (*destroyScene)(void* backend, GPUSceneData* gpu);
    RendererCompileShaderFunc compileShader;
    RendererMergeShaderResultsFunc mergeShaderResults;
};

struct RendererCore {
    void* backend;
    RendererBackendOps ops;
};

static RendererCore* renderer_get_core_(Renderer* renderer) {
    if (!renderer || !renderer->backendData) {
        return 0;
    }
    return (RendererCore*)renderer->backendData;
}

static B32 renderer_backend_compile_shader_(void* backendData, Arena* arena, StringU8 shaderPath,
                                            ShaderCompileResult* outResult) {
    RendererCore* core = (RendererCore*)backendData;
    if (!core || !core->backend || !core->ops.compileShader) {
        return 0;
    }
    return core->ops.compileShader(core->backend, arena, shaderPath, outResult);
}

static void renderer_backend_merge_shader_results_(void* backendData, Arena* arena, const ShaderCompileResult* results,
                                                   U32 resultCount, const ShaderCompileRequest* requests,
                                                   U32 requestCount) {
    RendererCore* core = (RendererCore*)backendData;
    if (!core || !core->backend || !core->ops.mergeShaderResults) {
        return;
    }
    core->ops.mergeShaderResults(core->backend, arena, results, resultCount, requests, requestCount);
}

static B32 renderer_desc_is_valid_(U32 structSize, U32 expectedSize, U32 apiVersion) {
    if (structSize < expectedSize) {
        return 0;
    }
    if (apiVersion != RENDERER_API_VERSION) {
        return 0;
    }
    return 1;
}

B32 renderer_create(const RendererCreateDesc* createDesc, Renderer* outRenderer) {
    if (!createDesc || !outRenderer) {
        return 0;
    }
    if (!renderer_desc_is_valid_(createDesc->structSize, sizeof(RendererCreateDesc), createDesc->apiVersion)) {
        return 0;
    }
    if (!createDesc->arena) {
        return 0;
    }

    MEMSET(outRenderer, 0, sizeof(*outRenderer));

#if defined(RENDERER_BACKEND_VULKAN)
    RendererCore* core = ARENA_PUSH_STRUCT(createDesc->arena, RendererCore);
    if (!core) {
        return 0;
    }

    Renderer backendRenderer = {};
    if (!renderer_vulkan_backend_init(createDesc->arena, &backendRenderer)) {
        return 0;
    }

    MEMSET(core, 0, sizeof(*core));
    core->backend = backendRenderer.backendData;
    core->ops.shutdown = (void (*)(void*))renderer_vulkan_shutdown;
    core->ops.draw = (void (*)(void*, OS_WindowHandle, const SceneData*, const RenderObject*, U32))renderer_vulkan_draw;
    core->ops.drawRadiance2D =
        (void (*)(void*, OS_WindowHandle, const RendererRadiance2DDesc*))renderer_vulkan_draw_radiance_2d;
    core->ops.resize = (void (*)(void*, U32, U32))renderer_vulkan_on_window_resized;
    core->ops.imguiInit = (B32 (*)(void*, OS_WindowHandle))renderer_vulkan_imgui_init;
    core->ops.imguiShutdown = (void (*)(void*))renderer_vulkan_imgui_shutdown;
    core->ops.imguiProcessEvents = (void (*)(void*, const OS_GraphicsEvent*, U32))renderer_vulkan_imgui_process_events;
    core->ops.imguiBeginFrame = (void (*)(void*, F32))renderer_vulkan_imgui_begin_frame;
    core->ops.imguiEndFrame = (void (*)(void*))renderer_vulkan_imgui_end_frame;
    core->ops.imguiSetWindowSize = (void (*)(void*, U32, U32))renderer_vulkan_imgui_set_window_size;
    core->ops.uploadMesh = (MeshHandle (*)(void*, const MeshAssetData*))renderer_vulkan_upload_mesh;
    core->ops.destroyMesh = (void (*)(void*, MeshHandle))renderer_vulkan_destroy_mesh;
    core->ops.uploadTexture = (TextureHandle (*)(void*, const LoadedImage*))renderer_vulkan_upload_texture;
    core->ops.destroyTexture = (void (*)(void*, TextureHandle))renderer_vulkan_destroy_texture;
    core->ops.updateTexture = (B32 (*)(void*, TextureHandle, const LoadedImage*))renderer_vulkan_update_texture;
    core->ops.uploadMaterial = (MaterialHandle (*)(void*, const MaterialData*, TextureHandle, TextureHandle))
        renderer_vulkan_upload_material;
    core->ops.destroyMaterial = (void (*)(void*, MaterialHandle))renderer_vulkan_destroy_material;
    core->ops.uploadScene = (B32 (*)(void*, Arena*, const LoadedScene*, GPUSceneData*))renderer_vulkan_upload_scene;
    core->ops.destroyScene = (void (*)(void*, GPUSceneData*))renderer_vulkan_destroy_scene;
    core->ops.compileShader = backendRenderer.compileShader;
    core->ops.mergeShaderResults = backendRenderer.mergeShaderResults;

    outRenderer->backendData = core;
    outRenderer->compileShader = renderer_backend_compile_shader_;
    outRenderer->mergeShaderResults = renderer_backend_merge_shader_results_;
    return 1;
#else
    return 0;
#endif
}

void renderer_shutdown(Renderer* renderer) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !core->backend) {
        return;
    }

    if (core->ops.shutdown) {
        core->ops.shutdown(core->backend);
    }
    core->backend = 0;
    core->ops = {};
    renderer->backendData = 0;
    renderer->compileShader = 0;
    renderer->mergeShaderResults = 0;
    renderer->frameInProgress = 0;
}

B32 renderer_begin_frame(Renderer* renderer, const RendererFrameBeginDesc* frameBeginDesc) {
    if (!renderer || !renderer->backendData || !frameBeginDesc) {
        return 0;
    }
    if (!renderer_desc_is_valid_(frameBeginDesc->structSize, sizeof(RendererFrameBeginDesc), frameBeginDesc->apiVersion)) {
        return 0;
    }

    renderer->activeWindow = frameBeginDesc->window;
    renderer->activeScene = frameBeginDesc->scene;
    renderer->frameInProgress = 1;
    return 1;
}

void renderer_submit(Renderer* renderer, const RendererSubmitDesc* submitDesc) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !submitDesc) {
        return;
    }
    if (!renderer->frameInProgress) {
        return;
    }
    if (!renderer_desc_is_valid_(submitDesc->structSize, sizeof(RendererSubmitDesc), submitDesc->apiVersion)) {
        return;
    }

    if (core->ops.draw) {
        core->ops.draw(core->backend, renderer->activeWindow, renderer->activeScene, submitDesc->objects,
                       submitDesc->objectCount);
    }
}

void renderer_submit_radiance_2d(Renderer* renderer, const RendererRadiance2DDesc* radianceDesc) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !radianceDesc) {
        return;
    }
    if (!renderer->frameInProgress) {
        return;
    }
    if (!renderer_desc_is_valid_(radianceDesc->structSize, sizeof(RendererRadiance2DDesc), radianceDesc->apiVersion)) {
        return;
    }

    if (core->ops.drawRadiance2D) {
        core->ops.drawRadiance2D(core->backend, renderer->activeWindow, radianceDesc);
    }
}

void renderer_end_frame(Renderer* renderer, const RendererEndFrameDesc* endFrameDesc) {
    if (!renderer || !renderer->backendData || !endFrameDesc) {
        return;
    }
    if (!renderer_desc_is_valid_(endFrameDesc->structSize, sizeof(RendererEndFrameDesc), endFrameDesc->apiVersion)) {
        return;
    }

    renderer->frameInProgress = 0;
    renderer->activeWindow.handle = 0;
    renderer->activeScene = 0;
}

void renderer_resize(Renderer* renderer, const RendererResizeDesc* resizeDesc) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !resizeDesc) {
        return;
    }
    if (!renderer_desc_is_valid_(resizeDesc->structSize, sizeof(RendererResizeDesc), resizeDesc->apiVersion)) {
        return;
    }

    if (core->ops.resize) {
        core->ops.resize(core->backend, resizeDesc->width, resizeDesc->height);
    }
}

B32 renderer_imgui_init(Renderer* renderer, OS_WindowHandle window) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core) {
        return 0;
    }

    if (!core->ops.imguiInit) {
        return 0;
    }
    return core->ops.imguiInit(core->backend, window);
}

void renderer_imgui_shutdown(Renderer* renderer) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core) {
        return;
    }
    if (core->ops.imguiShutdown) {
        core->ops.imguiShutdown(core->backend);
    }
}

void renderer_imgui_process_events(Renderer* renderer, const OS_GraphicsEvent* events, U32 eventCount) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core) {
        return;
    }
    if (core->ops.imguiProcessEvents) {
        core->ops.imguiProcessEvents(core->backend, events, eventCount);
    }
}

void renderer_imgui_begin_frame(Renderer* renderer, F32 deltaSeconds) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core) {
        return;
    }
    if (core->ops.imguiBeginFrame) {
        core->ops.imguiBeginFrame(core->backend, deltaSeconds);
    }
}

void renderer_imgui_end_frame(Renderer* renderer) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core) {
        return;
    }
    if (core->ops.imguiEndFrame) {
        core->ops.imguiEndFrame(core->backend);
    }
}

void renderer_imgui_set_window_size(Renderer* renderer, U32 width, U32 height) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core) {
        return;
    }
    if (core->ops.imguiSetWindowSize) {
        core->ops.imguiSetWindowSize(core->backend, width, height);
    }
}

MeshHandle renderer_upload_mesh(Renderer* renderer, const MeshAssetData* meshData) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !meshData || !core->ops.uploadMesh) {
        return MESH_HANDLE_INVALID;
    }
    return core->ops.uploadMesh(core->backend, meshData);
}

void renderer_destroy_mesh(Renderer* renderer, MeshHandle mesh) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !MESH_HANDLE_IS_VALID(mesh) || !core->ops.destroyMesh) {
        return;
    }
    core->ops.destroyMesh(core->backend, mesh);
}

TextureHandle renderer_upload_texture(Renderer* renderer, const LoadedImage* image) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !image || !core->ops.uploadTexture) {
        return TEXTURE_HANDLE_INVALID;
    }
    return core->ops.uploadTexture(core->backend, image);
}

void renderer_destroy_texture(Renderer* renderer, TextureHandle texture) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !TEXTURE_HANDLE_IS_VALID(texture) || !core->ops.destroyTexture) {
        return;
    }
    core->ops.destroyTexture(core->backend, texture);
}

B32 renderer_update_texture(Renderer* renderer, TextureHandle texture, const LoadedImage* image) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !TEXTURE_HANDLE_IS_VALID(texture) || !image || !core->ops.updateTexture) {
        return 0;
    }
    return core->ops.updateTexture(core->backend, texture, image);
}

MaterialHandle renderer_upload_material(Renderer* renderer, const MaterialData* material,
                                        TextureHandle colorTexture, TextureHandle metalRoughTexture) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !material || !core->ops.uploadMaterial) {
        return MATERIAL_HANDLE_INVALID;
    }
    return core->ops.uploadMaterial(core->backend, material, colorTexture, metalRoughTexture);
}

void renderer_destroy_material(Renderer* renderer, MaterialHandle material) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !MATERIAL_HANDLE_IS_VALID(material) || !core->ops.destroyMaterial) {
        return;
    }
    core->ops.destroyMaterial(core->backend, material);
}

B32 renderer_upload_scene(Renderer* renderer, Arena* arena, const LoadedScene* scene, GPUSceneData* outGPU) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !arena || !scene || !outGPU || !core->ops.uploadScene) {
        return 0;
    }
    return core->ops.uploadScene(core->backend, arena, scene, outGPU);
}

void renderer_destroy_scene(Renderer* renderer, GPUSceneData* gpu) {
    RendererCore* core = renderer_get_core_(renderer);
    if (!core || !gpu || !core->ops.destroyScene) {
        return;
    }
    core->ops.destroyScene(core->backend, gpu);
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
    if (!requests || requestCount == 0u) {
        return;
    }

    for (U32 i = 0; i < requestCount; ++i) {
        if (requests[i].outHandle) {
            *requests[i].outHandle = SHADER_HANDLE_INVALID;
        }
    }

    if (!renderer || !renderer->backendData || !arena || !jobSystem) {
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
