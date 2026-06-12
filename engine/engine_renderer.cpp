//
// Created by André Leite on 10/06/2026.
//

#define ENG_RENDERER_FONT_PATH "engine/fonts/NotoSans-Regular.ttf"
#define ENG_RENDERER_MAX_QUADS (DRAW2D_DEFAULT_MAX_QUADS_PER_LAYER * Draw2DLayer_COUNT)
#define ENG_RENDERER_FRAME_BUFFER_COUNT 2u

static_assert(sizeof(Draw2DQuad) == sizeof(ShdDraw2DQuadRecord), "Draw2DQuad shader ABI mismatch");
static_assert(sizeof(Draw2DQuad) == SHD_Draw2DQuadRecord_STRIDE_WORDS * 4u, "Draw2DQuad stride mismatch");
static_assert(sizeof(TextQuad) == sizeof(ShdDraw2DQuadRecord), "TextQuad shader ABI mismatch");

enum EngRendererLoadLog {
    EngRendererLoadLog_Started = (1u << 0u),
    EngRendererLoadLog_Ready = (1u << 1u),
};


struct EngRendererPacket {
    GfxRenderPassDesc pass;
    GfxColorTarget colorTarget;
    GfxResourceUse resourceUses[2];
    GfxDrawArea area;
    GfxDraw draws[Draw2DLayer_COUNT];
    U32 drawCount;
};


static void eng_renderer_log_once(EngState* state, U32 bit, const char* message) {
    if (state == 0 || message == 0 || FLAGS_HAS(state->render2d.loadLogMask, bit)) {
        return;
    }

    LOG_INFO("gfx", "{}", str8(message));
    state->render2d.loadLogMask |= bit;
}

static void eng_renderer_resource_cache_reset(EngContext* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->engine != 0);

    EngRender2D* render = &ctx->engine->render2d;
    render->fontFile = FILE_HANDLE_ZERO;
    render->vertexShaderFile = FILE_HANDLE_ZERO;
    render->fragmentShaderFile = FILE_HANDLE_ZERO;
    render->vertexShaderHash = CONTENT_HASH_ZERO;
    render->fragmentShaderHash = CONTENT_HASH_ZERO;
    render->failedFontGeneration = 0u;

    eng_world_resource_cache_reset_(ctx);
}

static void eng_renderer_watch_files(EngContext* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->engine != 0);

    EngState* state = ctx->engine;
    if (!state->resources.fileStream) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    StringU8 exeDir = OS_get_executable_directory(scratch.arena);
    StringU8 vertexPath = eng_shader_runtime_path_(scratch.arena, exeDir, EngShader_Draw2dVertex);
    StringU8 fragmentPath = eng_shader_runtime_path_(scratch.arena, exeDir, EngShader_Draw2dFragment);
    StringU8 fontPath = str8_concat(scratch.arena, exeDir, str8("/../" ENG_RENDERER_FONT_PATH));

    state->render2d.vertexShaderFile = file_watch(state->resources.fileStream, vertexPath, 0u);
    state->render2d.fragmentShaderFile = file_watch(state->resources.fileStream, fragmentPath, 0u);
    state->render2d.fontFile = file_watch(state->resources.fileStream, fontPath, 0u);

    eng_world_watch_files_(ctx);
    eng_assets_watch_files_(ctx);
}

static B32 eng_renderer_ensure_text_context(EngContext* ctx) {
    EngState* state = ctx->engine;
    if (state->render2d.textContext != 0) {
        return 1;
    }
    if (state->resources.arena == 0) {
        return 0;
    }

    TextContextDesc desc = {};
    desc.arena = state->resources.arena;
    if (!text_context_create(&desc, &state->render2d.textContext)) {
        LOG_ERROR("text", "Failed to create text context");
        return 0;
    }
    return 1;
}

static void eng_renderer_try_load_font(EngContext* ctx) {
    EngState* state = ctx->engine;
    if (state->render2d.font.generation != 0u ||
        state->render2d.textContext == 0 ||
        state->resources.fileStream == 0) {
        return;
    }

    FileView fontView = file_view(state->resources.fileStream, state->render2d.fontFile);
    if (fontView.status != FileStatus_Ready || fontView.size == 0u || fontView.data == 0) {
        return;
    }
    if (state->render2d.failedFontGeneration == fontView.generation) {
        return;
    }

    TextFontDesc fontDesc = {};
    fontDesc.debugName = str8("NotoSans-Regular");
    fontDesc.data = fontView.data;
    fontDesc.size = fontView.size;
    TextFont font = text_font_load_memory(state->render2d.textContext, &fontDesc);
    if (font.generation == 0u) {
        state->render2d.failedFontGeneration = fontView.generation;
        LOG_ERROR("text", "Failed to load bundled font");
        return;
    }
    state->render2d.font = font;
}

static B32 eng_renderer_create_pipeline(EngContext* ctx, ContentHash vertexHash, ContentHash fragmentHash, GfxPipeline* outPipeline) {
    *outPipeline = {};
    if (ctx->host->gfxDevice == 0 || ctx->engine->resources.contentStore == 0) {
        return 0;
    }

    ContentView vertexView = content_view_hash(ctx->engine->resources.contentStore, vertexHash);
    ContentView fragmentView = content_view_hash(ctx->engine->resources.contentStore, fragmentHash);
    if (!vertexView.valid || vertexView.size == 0u ||
        !fragmentView.valid || fragmentView.size == 0u) {
        return 0;
    }

    GfxFormat colorFormats[1] = {
        GfxFormat_BGRA8_UNorm,
    };
    GfxColorBlendState blendStates[1] = {};
    blendStates[0].blendEnabled = 1;
    blendStates[0].srcColorFactor = GfxBlendFactor_SrcAlpha;
    blendStates[0].dstColorFactor = GfxBlendFactor_OneMinusSrcAlpha;
    blendStates[0].colorOp = GfxBlendOp_Add;
    blendStates[0].srcAlphaFactor = GfxBlendFactor_One;
    blendStates[0].dstAlphaFactor = GfxBlendFactor_OneMinusSrcAlpha;
    blendStates[0].alphaOp = GfxBlendOp_Add;
    blendStates[0].writeFlags = GfxColorWriteFlags_RGBA;

    GfxGraphicsPipelineDesc pipelineDesc = {};
    pipelineDesc.name = "draw2d pipeline";
#if defined(PLATFORM_OS_WINDOWS)
    pipelineDesc.vertexShader.format = GfxShaderFormat_SPIRV;
    pipelineDesc.fragmentShader.format = GfxShaderFormat_SPIRV;
#else
    pipelineDesc.vertexShader.format = GfxShaderFormat_MSL_Source;
    pipelineDesc.fragmentShader.format = GfxShaderFormat_MSL_Source;
#endif
    pipelineDesc.vertexShader.entry = ENG_SHADER_ENTRY_NAMES[EngShader_Draw2dVertex];
    pipelineDesc.vertexShader.data = vertexView.data;
    pipelineDesc.vertexShader.size = vertexView.size;
    pipelineDesc.fragmentShader.entry = ENG_SHADER_ENTRY_NAMES[EngShader_Draw2dFragment];
    pipelineDesc.fragmentShader.data = fragmentView.data;
    pipelineDesc.fragmentShader.size = fragmentView.size;
    pipelineDesc.topology = GfxPrimitiveTopology_TriangleList;
    pipelineDesc.raster.cullMode = GfxCullMode_None;
    pipelineDesc.raster.frontFace = GfxFrontFace_CCW;
    pipelineDesc.depth.depthTestEnabled = 0;
    pipelineDesc.depth.depthWriteEnabled = 0;
    pipelineDesc.depth.compareOp = GfxCompareOp_Always;
    pipelineDesc.colorFormats = colorFormats;
    pipelineDesc.colorFormatCount = ARRAY_COUNT(colorFormats);
    pipelineDesc.blendStates = blendStates;
    pipelineDesc.blendStateCount = ARRAY_COUNT(blendStates);
    pipelineDesc.depthFormat = GfxFormat_Invalid;

    GfxPipeline pipeline = gfx_create_graphics_pipeline(ctx->host->gfxDevice, &pipelineDesc);
    if (pipeline.generation == 0u) {
        return 0;
    }
    *outPipeline = pipeline;
    return 1;
}

static void eng_renderer_try_update_pipeline(EngContext* ctx) {
    EngState* state = ctx->engine;
    if (state->resources.fileStream == 0 ||
        state->resources.contentStore == 0 ||
        ctx->host->gfxDevice == 0) {
        return;
    }

    FileView vertexView = file_view(state->resources.fileStream, state->render2d.vertexShaderFile);
    FileView fragmentView = file_view(state->resources.fileStream, state->render2d.fragmentShaderFile);
    if (vertexView.status != FileStatus_Ready ||
        fragmentView.status != FileStatus_Ready ||
        content_hash_is_zero(vertexView.hash) ||
        content_hash_is_zero(fragmentView.hash)) {
        return;
    }

    if (state->render2d.pipeline.generation != 0u &&
        content_hash_equal(state->render2d.vertexShaderHash, vertexView.hash) &&
        content_hash_equal(state->render2d.fragmentShaderHash, fragmentView.hash)) {
        return;
    }

    GfxPipeline newPipeline = {};
    if (!eng_renderer_create_pipeline(ctx, vertexView.hash, fragmentView.hash, &newPipeline)) {
        return;
    }

    GfxPipeline oldPipeline = state->render2d.pipeline;
    state->render2d.pipeline = newPipeline;
    state->render2d.vertexShaderHash = vertexView.hash;
    state->render2d.fragmentShaderHash = fragmentView.hash;
    if (oldPipeline.generation != 0u) {
        gfx_destroy_pipeline(ctx->host->gfxDevice, oldPipeline);
    }
}

static void eng_renderer_try_create_gpu_resources(EngContext* ctx) {
    EngState* state = ctx->engine;
    EngRender2D* render = &state->render2d;
    if (render->gpuResourcesCreated ||
        render->textContext == 0 ||
        ctx->host->gfxDevice == 0) {
        return;
    }

    GfxDevice* device = ctx->host->gfxDevice;
    TextAtlasUpload atlas = text_atlas_full_upload(render->textContext);
    if (atlas.width == 0u || atlas.height == 0u) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena == 0) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    GfxTextureDesc atlasDesc = {};
    atlasDesc.name = "draw2d atlas";
    atlasDesc.width = atlas.width;
    atlasDesc.height = atlas.height;
    atlasDesc.mipCount = 1u;
    atlasDesc.format = GfxFormat_R8_UNorm;
    atlasDesc.usageFlags = GfxTextureUsageFlags_Sampled | GfxTextureUsageFlags_CopyDst;
    GfxTexture atlasTexture = gfx_create_texture(device, &atlasDesc);
    GfxResourceId atlasTextureId = gfx_register_texture(device, atlasTexture);

    GfxSamplerDesc samplerDesc = {};
    samplerDesc.name = "draw2d atlas sampler";
    samplerDesc.minFilter = GfxFilter_Linear;
    samplerDesc.magFilter = GfxFilter_Linear;
    samplerDesc.addressU = GfxAddressMode_ClampToEdge;
    samplerDesc.addressV = GfxAddressMode_ClampToEdge;
    GfxSampler atlasSampler = gfx_create_sampler(device, &samplerDesc);
    GfxResourceId atlasSamplerId = gfx_register_sampler(device, atlasSampler);

    U32 indexCount = ENG_RENDERER_MAX_QUADS * 6u;
    U32* indices = ARENA_PUSH_ARRAY(scratch.arena, U32, indexCount);
    if (indices == 0) {
        return;
    }
    for (U32 quadIndex = 0u; quadIndex < ENG_RENDERER_MAX_QUADS; ++quadIndex) {
        U32 baseVertex = quadIndex * 4u;
        U32 baseIndex = quadIndex * 6u;
        indices[baseIndex + 0u] = baseVertex + 0u;
        indices[baseIndex + 1u] = baseVertex + 1u;
        indices[baseIndex + 2u] = baseVertex + 2u;
        indices[baseIndex + 3u] = baseVertex + 2u;
        indices[baseIndex + 4u] = baseVertex + 3u;
        indices[baseIndex + 5u] = baseVertex + 0u;
    }

    GfxBufferDesc indexDesc = {};
    indexDesc.name = "draw2d quad indices";
    indexDesc.size = sizeof(U32) * indexCount;
    indexDesc.usageFlags = GfxBufferUsageFlags_Index;
    indexDesc.memoryKind = GfxMemoryKind_Upload;
    indexDesc.initialData = indices;
    GfxBuffer indexBuffer = gfx_create_buffer(device, &indexDesc);

    GfxBuffer quadBuffers[ENG_RENDERER_FRAME_BUFFER_COUNT] = {};
    GfxResourceId quadBufferIds[ENG_RENDERER_FRAME_BUFFER_COUNT] = {};
    for (U32 bufferIndex = 0u; bufferIndex < ENG_RENDERER_FRAME_BUFFER_COUNT; ++bufferIndex) {
        GfxBufferDesc quadDesc = {};
        quadDesc.name = "draw2d quads";
        quadDesc.size = sizeof(Draw2DQuad) * ENG_RENDERER_MAX_QUADS;
        quadDesc.usageFlags = GfxBufferUsageFlags_Storage;
        quadDesc.memoryKind = GfxMemoryKind_Upload;
        quadBuffers[bufferIndex] = gfx_create_buffer(device, &quadDesc);
        quadBufferIds[bufferIndex] = gfx_register_buffer(device, quadBuffers[bufferIndex]);
    }

    B32 created = atlasTexture.generation != 0u &&
                  atlasTextureId.index != 0u &&
                  atlasSampler.generation != 0u &&
                  atlasSamplerId.index != 0u &&
                  indexBuffer.generation != 0u;
    for (U32 bufferIndex = 0u; bufferIndex < ENG_RENDERER_FRAME_BUFFER_COUNT; ++bufferIndex) {
        created = created &&
                  quadBuffers[bufferIndex].generation != 0u &&
                  quadBufferIds[bufferIndex].index != 0u;
    }
    if (!created) {
        LOG_ERROR("gfx", "Failed to create draw2d GPU resources");
        gfx_destroy_buffer(device, indexBuffer);
        gfx_destroy_sampler(device, atlasSampler);
        gfx_destroy_texture(device, atlasTexture);
        for (U32 bufferIndex = 0u; bufferIndex < ENG_RENDERER_FRAME_BUFFER_COUNT; ++bufferIndex) {
            gfx_destroy_buffer(device, quadBuffers[bufferIndex]);
        }
        return;
    }

    render->atlasTexture = atlasTexture;
    render->atlasTextureId = atlasTextureId;
    render->atlasSampler = atlasSampler;
    render->atlasSamplerId = atlasSamplerId;
    render->indexBuffer = indexBuffer;
    for (U32 bufferIndex = 0u; bufferIndex < ENG_RENDERER_FRAME_BUFFER_COUNT; ++bufferIndex) {
        render->quadBuffers[bufferIndex] = quadBuffers[bufferIndex];
        render->quadBufferIds[bufferIndex] = quadBufferIds[bufferIndex];
    }
    render->gpuResourcesCreated = 1;
    render->atlasSeeded = 0;
}

static void eng_renderer_upload_atlas(EngContext* ctx, GfxFrame* frame,
                                      const TextAtlasUpload* uploads, U32 uploadCount) {
    if (uploadCount == 0u) {
        return;
    }
    GfxTextureUploadRegion* regions = ARENA_PUSH_ARRAY(ctx->host->frameArena, GfxTextureUploadRegion, uploadCount);
    if (!regions) {
        return;
    }
    MEMSET(regions, 0, sizeof(GfxTextureUploadRegion) * uploadCount);
    for (U32 at = 0u; at < uploadCount; ++at) {
        const TextAtlasUpload* upload = uploads + at;
        regions[at].src = upload->pixels;
        regions[at].layerCount = 1u;
        regions[at].x = upload->x;
        regions[at].y = upload->y;
        regions[at].width = upload->width;
        regions[at].height = upload->height;
        regions[at].depth = 1u;
        regions[at].bytesPerRow = upload->pitch;
        regions[at].rowsPerImage = upload->height;
    }
    gfx_upload_texture(frame, ctx->engine->render2d.atlasTexture, regions, uploadCount);
}

static void eng_renderer_try_seed_atlas(EngContext* ctx, GfxFrame* frame) {
    EngRender2D* render = &ctx->engine->render2d;
    if (render->atlasSeeded || !render->gpuResourcesCreated || render->textContext == 0) {
        return;
    }

    TextAtlasUpload fullUpload = text_atlas_full_upload(render->textContext);
    if (fullUpload.width == 0u) {
        return;
    }
    eng_renderer_upload_atlas(ctx, frame, &fullUpload, 1u);
    render->atlasSeeded = 1;
}

static void eng_renderer_execute_2d(EngContext* ctx, EngRendererFrame* rendererFrame, Draw2DResult result) {
    EngState* state = ctx->engine;
    EngRender2D* render = &state->render2d;
    GfxFrame* frame = rendererFrame->frame;

    EngRendererPacket packet = {};
    packet.colorTarget.texture = gfx_get_backbuffer(frame);
    packet.colorTarget.loadOp = (state->world.lastRenderableCount != 0u) ? GfxLoadOp_Load : GfxLoadOp_Clear;
    packet.colorTarget.storeOp = GfxStoreOp_Store;
    packet.colorTarget.clearColor[0] = 0.06f;
    packet.colorTarget.clearColor[1] = 0.08f;
    packet.colorTarget.clearColor[2] = 0.10f;
    packet.colorTarget.clearColor[3] = 1.0f;
    packet.pass.name = "2d overlay pass";
    packet.pass.colorTargets = &packet.colorTarget;
    packet.pass.colorTargetCount = 1u;

    U32 frameBufferIndex = (U32)(state->frameCounter & (ENG_RENDERER_FRAME_BUFFER_COUNT - 1u));
    B32 drawsReady = result.quadCount != 0u &&
                     result.quadCount <= ENG_RENDERER_MAX_QUADS &&
                     render->gpuResourcesCreated &&
                     render->pipeline.generation != 0u &&
                     gfx_upload_buffer(frame,
                                       render->quadBuffers[frameBufferIndex],
                                       0u,
                                       result.quads,
                                       sizeof(Draw2DQuad) * result.quadCount);

    if (drawsReady) {
        for (U32 batchIndex = 0u; batchIndex < result.batchCount && batchIndex < ARRAY_COUNT(packet.draws); ++batchIndex) {
            const Draw2DBatch* batch = result.batches + batchIndex;

            GfxTemp rootTemp = gfx_allocate_temp(frame, sizeof(ShdDraw2DRootData), 16u);
            if (rootTemp.cpu == 0) {
                break;
            }
            ShdDraw2DRootData* rootData = (ShdDraw2DRootData*)rootTemp.cpu;
            *rootData = {};
            rootData->quadBuffer = render->quadBufferIds[frameBufferIndex].index;
            rootData->quadByteOffset = batch->firstQuad * (U32)sizeof(Draw2DQuad);
            rootData->atlasTexture = render->atlasTextureId.index;
            rootData->atlasSampler = render->atlasSamplerId.index;
            rootData->targetWidth = (F32)ctx->host->windowWidth;
            rootData->targetHeight = (F32)ctx->host->windowHeight;

            GfxDraw* draw = packet.draws + packet.drawCount;
            *draw = {};
            draw->pipeline = render->pipeline;
            draw->indexBuffer = render->indexBuffer;
            draw->indexCount = batch->quadCount * 6u;
            draw->instanceCount = 1u;
            draw->indexType = GfxIndexType_U32;
            draw->rootDataOffset = (U32)rootTemp.gpu.offset;
            draw->rootDataSize = (U32)rootTemp.gpu.size;
            packet.drawCount += 1u;
        }

        packet.resourceUses[0].kind = GfxResourceUseKind_Buffer;
        packet.resourceUses[0].accessFlags = GfxResourceAccessFlags_ShaderRead;
        packet.resourceUses[0].shaderStages = GfxShaderStageFlags_Vertex;
        packet.resourceUses[0].buffer = render->quadBuffers[frameBufferIndex];
        packet.resourceUses[1].kind = GfxResourceUseKind_Texture;
        packet.resourceUses[1].accessFlags = GfxResourceAccessFlags_ShaderRead;
        packet.resourceUses[1].shaderStages = GfxShaderStageFlags_Fragment;
        packet.resourceUses[1].texture = render->atlasTexture;
        packet.pass.resourceUses = packet.resourceUses;
        packet.pass.resourceUseCount = ARRAY_COUNT(packet.resourceUses);
    }

    GfxViewport viewport = {};
    viewport.width = (F32)ctx->host->windowWidth;
    viewport.height = (F32)ctx->host->windowHeight;
    viewport.maxDepth = 1.0f;
    packet.area.viewport = viewport;
    packet.area.scissor.width = ctx->host->windowWidth;
    packet.area.scissor.height = ctx->host->windowHeight;
    packet.area.draws = packet.draws;
    packet.area.drawCount = packet.drawCount;

    gfx_render_pass(rendererFrame->commands, &packet.pass, &packet.area, 1u);

    if (packet.drawCount != 0u) {
        eng_renderer_log_once(state, EngRendererLoadLog_Ready, "Renderer 2d overlay ready");
    }
}

static B32 eng_renderer_init(EngContext* ctx) {
    EngState* state = ctx->engine;
    if (state->render2d.initialized) {
        return 1;
    }
    if (!ctx->host->gfxDevice) {
        LOG_ERROR("gfx", "App has no gfx device");
        return 0;
    }
    if (!eng_resource_cache_init(ctx)) {
        return 0;
    }
    if (!eng_renderer_ensure_text_context(ctx)) {
        return 0;
    }

    state->render2d.initialized = 1;
    eng_renderer_log_once(state, EngRendererLoadLog_Started, "Renderer resources requested");
    return 1;
}

static void eng_renderer_shutdown(EngContext* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->engine != 0);

    EngState* state = ctx->engine;
    EngRender2D* render = &state->render2d;
    GfxDevice* device = ctx->host ? ctx->host->gfxDevice : 0;

    if (render->textContext != 0) {
        text_context_destroy(render->textContext);
    }
    eng_resource_cache_shutdown(ctx);

    if (device != 0) {
        for (U32 bufferIndex = 0u; bufferIndex < ENG_RENDERER_FRAME_BUFFER_COUNT; ++bufferIndex) {
            gfx_destroy_buffer(device, render->quadBuffers[bufferIndex]);
        }
        gfx_destroy_buffer(device, render->indexBuffer);
        gfx_destroy_sampler(device, render->atlasSampler);
        gfx_destroy_texture(device, render->atlasTexture);
        gfx_destroy_pipeline(device, render->pipeline);
    }
    if (eng_project_()->capabilities & ENG_CAP_WORLD3D) {
        eng_world_shutdown_(ctx);
    }

    *render = {};
}

static EngRendererFrame* eng_renderer_begin_frame(EngContext* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->engine != 0);

    if (!ctx->host->gfxDevice ||
        ctx->host->windowWidth == 0u ||
        ctx->host->windowHeight == 0u) {
        return 0;
    }
    if (!eng_renderer_init(ctx)) {
        return 0;
    }

#if defined(PLATFORM_BUILD_DEBUG)
    {
        PROF_SCOPE("dev shaders");
        eng_try_build_dev_shaders(ctx);
    }
    if (ctx->engine->resources.fileStream) {
        PROF_SCOPE("file stream tick");
        file_stream_tick(ctx->engine->resources.fileStream, OS_get_time_nanoseconds(), 16u);
    }
#endif
    GfxFrame* frame = 0;
    {
        PROF_SCOPE("gfx begin frame");
        frame = gfx_begin_frame(ctx->host->gfxDevice, ctx->host->windowWidth, ctx->host->windowHeight);
    }
    if (!frame) {
        return 0;
    }

    g_engRendererFrame.frame = frame;
    g_engRendererFrame.commands = gfx_get_command_buffer(frame);

    // Publishes record uploads into the current frame, parked on the bridge
    // for the duration of the tick.
    if (ctx->engine->resources.artifactCache) {
        PROF_SCOPE("artifact tick");
        ctx->engine->assetBridge.frame = frame;
        artifact_cache_tick(ctx->engine->resources.artifactCache, ctx->engine->frameCounter, 16u, 16u);
        ctx->engine->assetBridge.frame = 0;
    }

    U32 caps = eng_project_()->capabilities;
    eng_renderer_try_create_gpu_resources(ctx);
    eng_renderer_try_seed_atlas(ctx, frame);
    if (caps & ENG_CAP_WORLD3D) {
        eng_world_try_create_resources_(ctx);
        eng_world_ensure_depth_(ctx);
    }

    // File-driven polls run only when the stream published something new or
    // a resource is still unresolved; the steady state is one stats compare.
    EngRender2D* render = &ctx->engine->render2d;
    B32 filesChanged = 1;
    if (ctx->engine->resources.fileStream) {
        FileStreamStats fileStats = file_stream_stats(ctx->engine->resources.fileStream);
        filesChanged = fileStats.publishCount != render->lastFilePublishCount;
        render->lastFilePublishCount = fileStats.publishCount;
    }
    B32 worldSettled = !(caps & ENG_CAP_WORLD3D) ||
                       (ctx->engine->world.opaquePipeline.generation != 0u &&
                        ctx->engine->world.assetsSettled);
    B32 audioSettled = !(caps & ENG_CAP_AUDIO) || ctx->engine->audio.settled;
    B32 resourcesSettled = render->font.generation != 0u &&
                           render->pipeline.generation != 0u &&
                           worldSettled && audioSettled;
    if (filesChanged || !resourcesSettled) {
        eng_renderer_try_load_font(ctx);
        eng_renderer_try_update_pipeline(ctx);
        if (caps & ENG_CAP_WORLD3D) {
            eng_world_try_update_pipelines_(ctx);
            eng_assets_try_load_models_(ctx);
        }
        if (caps & ENG_CAP_AUDIO) {
            eng_assets_try_load_sounds_(ctx);
        }
    }

    if (caps & ENG_CAP_WORLD3D) {
        eng_world_begin_frame_(ctx);
    }

    F32 whiteU = 0.0f;
    F32 whiteV = 0.0f;
    text_white_uv(ctx->engine->render2d.textContext, &whiteU, &whiteV);
    draw2d_begin(&ctx->engine->render2d.draw2d, ctx->host->frameArena, whiteU, whiteV);

    return &g_engRendererFrame;
}

static void eng_renderer_submit_text(EngContext* ctx, EngRendererFrame* rendererFrame, const TextDrawData* drawData, Draw2DLayer layer) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->engine != 0);

    if (rendererFrame == 0 || drawData == 0) {
        return;
    }

    if (ctx->engine->render2d.gpuResourcesCreated) {
        eng_renderer_upload_atlas(ctx, rendererFrame->frame, drawData->uploads, drawData->uploadCount);
    }
    draw2d_glyph_quads(&ctx->engine->render2d.draw2d, layer, (const Draw2DQuad*)drawData->quads, drawData->quadCount, 0.0f, 0.0f);
}

static void eng_renderer_apply_text_uploads(EngContext* ctx, EngRendererFrame* rendererFrame, const TextAtlasUpload* uploads, U32 uploadCount) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->engine != 0);

    if (rendererFrame == 0 || uploads == 0 || !ctx->engine->render2d.gpuResourcesCreated) {
        return;
    }
    eng_renderer_upload_atlas(ctx, rendererFrame->frame, uploads, uploadCount);
}

static void eng_renderer_end_frame(EngContext* ctx, EngRendererFrame* rendererFrame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->engine != 0);

    if (rendererFrame == 0 || rendererFrame->frame == 0) {
        return;
    }

    if (eng_project_()->capabilities & ENG_CAP_WORLD3D) {
        PROF_SCOPE("world passes");
        eng_world_execute_(ctx, rendererFrame);
    }

    Draw2DResult result = {};
    {
        PROF_SCOPE("draw2d end");
        result = draw2d_end(&ctx->engine->render2d.draw2d);
    }
    {
        PROF_SCOPE("2d pass");
        eng_renderer_execute_2d(ctx, rendererFrame, result);
    }

    ctx->engine->render2d.lastDraw2DStats = ctx->engine->render2d.draw2d.stats;
    ctx->engine->render2d.lastGfxStats = gfx_get_stats(ctx->host->gfxDevice);

    {
        PROF_SCOPE("gfx submit");
        gfx_submit(rendererFrame->commands);
    }
    {
        PROF_SCOPE("gfx end frame");
        gfx_end_frame(rendererFrame->frame);
    }
    if (ctx->engine->resources.artifactCache) {
        artifact_cache_evict(ctx->engine->resources.artifactCache, ctx->engine->frameCounter, 128u);
    }

    g_engRendererFrame = {};
}
