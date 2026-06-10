//
// Created by André Leite on 10/06/2026.
//

#define APP_RENDERER_FONT_PATH "app/fonts/NotoSans-Regular.ttf"
#define APP_RENDERER_MAX_QUADS (DRAW2D_DEFAULT_MAX_QUADS_PER_LAYER * Draw2DLayer_COUNT)
#define APP_RENDERER_FRAME_BUFFER_COUNT 2u

static_assert(sizeof(Draw2DQuad) == sizeof(ShdDraw2DQuadRecord), "Draw2DQuad shader ABI mismatch");
static_assert(sizeof(Draw2DQuad) == SHD_Draw2DQuadRecord_STRIDE_WORDS * 4u, "Draw2DQuad stride mismatch");
static_assert(sizeof(TextQuad) == sizeof(ShdDraw2DQuadRecord), "TextQuad shader ABI mismatch");

enum AppRendererLoadLog {
    AppRendererLoadLog_Started = (1u << 0u),
    AppRendererLoadLog_Ready = (1u << 1u),
};

struct AppRendererFrame {
    GfxFrame* frame;
    GfxCommandBuffer* commands;
};

struct AppRendererPacket {
    GfxRenderPassDesc pass;
    GfxColorTarget colorTarget;
    GfxResourceUse resourceUses[2];
    GfxDrawArea area;
    GfxDraw draws[Draw2DLayer_COUNT];
    U32 drawCount;
};

static AppRendererFrame g_appRendererFrame;

enum AppWorldShaderSlot {
    AppWorldShaderSlot_Vertex = 0,
    AppWorldShaderSlot_Fragment,
    AppWorldShaderSlot_Reset,
    AppWorldShaderSlot_Cull,
    AppWorldShaderSlot_Prefix,
    AppWorldShaderSlot_Compact,
    AppWorldShaderSlot_Args,
};

static const char* APP_WORLD_SHADER_PATHS[APP_WORLD_SHADER_COUNT] = {
    APP_SHADER_WORLD_VERTEX_RUNTIME_PATH,
    APP_SHADER_WORLD_FRAGMENT_RUNTIME_PATH,
    APP_SHADER_WORLD_RESET_RUNTIME_PATH,
    APP_SHADER_WORLD_CULL_RUNTIME_PATH,
    APP_SHADER_WORLD_PREFIX_RUNTIME_PATH,
    APP_SHADER_WORLD_COMPACT_RUNTIME_PATH,
    APP_SHADER_WORLD_ARGS_RUNTIME_PATH,
};


static void app_renderer_log_once(AppCoreState* state, U32 bit, const char* message) {
    if (state == 0 || message == 0 || FLAGS_HAS(state->render2d.loadLogMask, bit)) {
        return;
    }

    LOG_INFO("gfx", "{}", str8(message));
    state->render2d.loadLogMask |= bit;
}

static void app_renderer_resource_cache_reset(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppRender2DState* render = &ctx->core->render2d;
    render->fontFile = FILE_HANDLE_ZERO;
    render->vertexShaderFile = FILE_HANDLE_ZERO;
    render->fragmentShaderFile = FILE_HANDLE_ZERO;
    render->vertexShaderHash = CONTENT_HASH_ZERO;
    render->fragmentShaderHash = CONTENT_HASH_ZERO;
    render->failedFontGeneration = 0u;

    AppWorldState* world = &ctx->core->world;
    for (U32 shaderIndex = 0u; shaderIndex < APP_WORLD_SHADER_COUNT; ++shaderIndex) {
        world->shaderFiles[shaderIndex] = FILE_HANDLE_ZERO;
        world->shaderHashes[shaderIndex] = CONTENT_HASH_ZERO;
    }
}

static void app_renderer_watch_files(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (!state->resources.fileStream) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    StringU8 exeDir = OS_get_executable_directory(scratch.arena);
    StringU8 vertexPath = str8_concat(scratch.arena, exeDir, str8("/../" APP_SHADER_DRAW2D_VERTEX_RUNTIME_PATH));
    StringU8 fragmentPath = str8_concat(scratch.arena, exeDir, str8("/../" APP_SHADER_DRAW2D_FRAGMENT_RUNTIME_PATH));
    StringU8 fontPath = str8_concat(scratch.arena, exeDir, str8("/../" APP_RENDERER_FONT_PATH));

    state->render2d.vertexShaderFile = file_watch(state->resources.fileStream, vertexPath, 0u);
    state->render2d.fragmentShaderFile = file_watch(state->resources.fileStream, fragmentPath, 0u);
    state->render2d.fontFile = file_watch(state->resources.fileStream, fontPath, 0u);

    StringU8 duckMeshPath = str8_concat(scratch.arena, exeDir, str8("/../app/assets/cooked/Duck.umsh"));
    StringU8 duckTexturePath = str8_concat(scratch.arena, exeDir, str8("/../app/assets/cooked/Duck.utex"));
    state->world.duckMeshFile = file_watch(state->resources.fileStream, duckMeshPath, 0u);
    state->world.duckTextureFile = file_watch(state->resources.fileStream, duckTexturePath, 0u);

    for (U32 shaderIndex = 0u; shaderIndex < APP_WORLD_SHADER_COUNT; ++shaderIndex) {
        StringU8 worldPath = str8_concat(scratch.arena, exeDir, str8("/../"));
        worldPath = str8_concat(scratch.arena, worldPath, str8(APP_WORLD_SHADER_PATHS[shaderIndex]));
        state->world.shaderFiles[shaderIndex] = file_watch(state->resources.fileStream, worldPath, 0u);
    }
}

static B32 app_renderer_ensure_text_context(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
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

static void app_renderer_try_load_font(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
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

static B32 app_renderer_create_pipeline(APP_Context* ctx, ContentHash vertexHash, ContentHash fragmentHash, GfxPipeline* outPipeline) {
    *outPipeline = {};
    if (ctx->host->gfxDevice == 0 || ctx->core->resources.contentStore == 0) {
        return 0;
    }

    ContentView vertexView = content_view_hash(ctx->core->resources.contentStore, vertexHash);
    ContentView fragmentView = content_view_hash(ctx->core->resources.contentStore, fragmentHash);
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
    pipelineDesc.vertexShader.entry = APP_SHADER_DRAW2D_VERTEX_ENTRY;
    pipelineDesc.vertexShader.data = vertexView.data;
    pipelineDesc.vertexShader.size = vertexView.size;
    pipelineDesc.fragmentShader.entry = APP_SHADER_DRAW2D_FRAGMENT_ENTRY;
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

static void app_renderer_try_update_pipeline(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
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
    if (!app_renderer_create_pipeline(ctx, vertexView.hash, fragmentView.hash, &newPipeline)) {
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

static void app_renderer_try_create_gpu_resources(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    AppRender2DState* render = &state->render2d;
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

    U32 indexCount = APP_RENDERER_MAX_QUADS * 6u;
    U32* indices = ARENA_PUSH_ARRAY(scratch.arena, U32, indexCount);
    if (indices == 0) {
        return;
    }
    for (U32 quadIndex = 0u; quadIndex < APP_RENDERER_MAX_QUADS; ++quadIndex) {
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

    GfxBuffer quadBuffers[APP_RENDERER_FRAME_BUFFER_COUNT] = {};
    GfxResourceId quadBufferIds[APP_RENDERER_FRAME_BUFFER_COUNT] = {};
    for (U32 bufferIndex = 0u; bufferIndex < APP_RENDERER_FRAME_BUFFER_COUNT; ++bufferIndex) {
        GfxBufferDesc quadDesc = {};
        quadDesc.name = "draw2d quads";
        quadDesc.size = sizeof(Draw2DQuad) * APP_RENDERER_MAX_QUADS;
        quadDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
        quadDesc.memoryKind = GfxMemoryKind_Device;
        quadBuffers[bufferIndex] = gfx_create_buffer(device, &quadDesc);
        quadBufferIds[bufferIndex] = gfx_register_buffer(device, quadBuffers[bufferIndex]);
    }

    B32 created = atlasTexture.generation != 0u &&
                  atlasTextureId.index != 0u &&
                  atlasSampler.generation != 0u &&
                  atlasSamplerId.index != 0u &&
                  indexBuffer.generation != 0u;
    for (U32 bufferIndex = 0u; bufferIndex < APP_RENDERER_FRAME_BUFFER_COUNT; ++bufferIndex) {
        created = created &&
                  quadBuffers[bufferIndex].generation != 0u &&
                  quadBufferIds[bufferIndex].index != 0u;
    }
    if (!created) {
        LOG_ERROR("gfx", "Failed to create draw2d GPU resources");
        gfx_destroy_buffer(device, indexBuffer);
        gfx_destroy_sampler(device, atlasSampler);
        gfx_destroy_texture(device, atlasTexture);
        for (U32 bufferIndex = 0u; bufferIndex < APP_RENDERER_FRAME_BUFFER_COUNT; ++bufferIndex) {
            gfx_destroy_buffer(device, quadBuffers[bufferIndex]);
        }
        return;
    }

    render->atlasTexture = atlasTexture;
    render->atlasTextureId = atlasTextureId;
    render->atlasSampler = atlasSampler;
    render->atlasSamplerId = atlasSamplerId;
    render->indexBuffer = indexBuffer;
    for (U32 bufferIndex = 0u; bufferIndex < APP_RENDERER_FRAME_BUFFER_COUNT; ++bufferIndex) {
        render->quadBuffers[bufferIndex] = quadBuffers[bufferIndex];
        render->quadBufferIds[bufferIndex] = quadBufferIds[bufferIndex];
    }
    render->gpuResourcesCreated = 1;
    render->atlasSeeded = 0;
}

static void app_renderer_upload_atlas(APP_Context* ctx, GfxFrame* frame, const TextAtlasUpload* upload) {
    GfxTextureUploadRegion region = {};
    region.layerCount = 1u;
    region.x = upload->x;
    region.y = upload->y;
    region.width = upload->width;
    region.height = upload->height;
    region.depth = 1u;
    region.bytesPerRow = upload->pitch;
    region.rowsPerImage = upload->height;
    gfx_upload_texture(frame, ctx->core->render2d.atlasTexture, &region, upload->pixels);
}

static void app_renderer_try_seed_atlas(APP_Context* ctx, GfxFrame* frame) {
    AppRender2DState* render = &ctx->core->render2d;
    if (render->atlasSeeded || !render->gpuResourcesCreated || render->textContext == 0) {
        return;
    }

    TextAtlasUpload fullUpload = text_atlas_full_upload(render->textContext);
    if (fullUpload.width == 0u) {
        return;
    }
    app_renderer_upload_atlas(ctx, frame, &fullUpload);
    render->atlasSeeded = 1;
}

static void app_renderer_execute_2d(APP_Context* ctx, AppRendererFrame* rendererFrame, Draw2DResult result) {
    AppCoreState* state = ctx->core;
    AppRender2DState* render = &state->render2d;
    GfxFrame* frame = rendererFrame->frame;

    AppRendererPacket packet = {};
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

    U32 frameBufferIndex = (U32)(state->frameCounter & (APP_RENDERER_FRAME_BUFFER_COUNT - 1u));
    B32 drawsReady = result.quadCount != 0u &&
                     result.quadCount <= APP_RENDERER_MAX_QUADS &&
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
        app_renderer_log_once(state, AppRendererLoadLog_Ready, "Renderer 2d overlay ready");
    }
}

// ////////////////////////
// World renderer (U5)

#define APP_WORLD_CULL_GROUP_SIZE 64u
#define APP_WORLD_COMPACT_GROUP_SIZE 256u
#define APP_WORLD_MATERIAL_FLAG_ALPHA_TEST 1u
#define APP_WORLD_MATERIAL_FLAG_TEXTURED 2u

static const char* APP_WORLD_COMPUTE_ENTRIES[5] = {
    APP_SHADER_WORLD_RESET_ENTRY,
    APP_SHADER_WORLD_CULL_ENTRY,
    APP_SHADER_WORLD_PREFIX_ENTRY,
    APP_SHADER_WORLD_COMPACT_ENTRY,
    APP_SHADER_WORLD_ARGS_ENTRY,
};

static const U32 APP_WORLD_COMPUTE_GROUP_SIZES[5] = {
    APP_WORLD_CULL_GROUP_SIZE,
    APP_WORLD_CULL_GROUP_SIZE,
    1u,
    APP_WORLD_COMPACT_GROUP_SIZE,
    APP_WORLD_CULL_GROUP_SIZE,
};

static U32 app_world_cell_count_(const AppWorldState* world) {
    (void)world;
    return APP_WORLD_BIN_COUNT * APP_WORLD_MAX_MESHES;
}

static Vec3F32 app_world_vec3_(F32 x, F32 y, F32 z) {
    Vec3F32 result;
    result.x = x;
    result.y = y;
    result.z = z;
    return result;
}

struct AppWorldMeshBuilder {
    ShdWorldVertexRecord* vertices;
    U32* indices;
    U32 vertexCount;
    U32 indexCount;
    U32 vertexCapacity;
    U32 indexCapacity;
};

static void app_world_builder_vertex_(AppWorldMeshBuilder* builder, F32 px, F32 py, F32 pz,
                                      F32 nx, F32 ny, F32 nz, F32 u, F32 v) {
    if (builder->vertexCount >= builder->vertexCapacity) {
        return;
    }
    ShdWorldVertexRecord* vertex = builder->vertices + builder->vertexCount;
    builder->vertexCount += 1u;
    vertex->position[0] = px;
    vertex->position[1] = py;
    vertex->position[2] = pz;
    vertex->normal[0] = nx;
    vertex->normal[1] = ny;
    vertex->normal[2] = nz;
    vertex->uv[0] = u;
    vertex->uv[1] = v;
}

static void app_world_builder_index_(AppWorldMeshBuilder* builder, U32 a, U32 b, U32 c) {
    if (builder->indexCount + 3u > builder->indexCapacity) {
        return;
    }
    builder->indices[builder->indexCount + 0u] = a;
    builder->indices[builder->indexCount + 1u] = b;
    builder->indices[builder->indexCount + 2u] = c;
    builder->indexCount += 3u;
}

static AppWorldMeshHandle app_world_register_mesh_(AppWorldState* world, U32 firstIndex, U32 indexCount,
                                                   U32 baseVertex, GfxBuffer vertexBuffer,
                                                   GfxResourceId vertexBufferId, GfxBuffer indexBuffer,
                                                   B32 ownsBuffers, Vec3F32 center, Vec3F32 extents) {
    AppWorldMeshHandle handle = {};
    void* item = 0;
    U32 slot = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&world->meshes, &item, &slot, &generation)) {
        return handle;
    }
    AppWorldMesh* mesh = (AppWorldMesh*)item;
    mesh->indexCount = indexCount;
    mesh->firstIndex = firstIndex;
    mesh->baseVertex = baseVertex;
    mesh->vertexBuffer = vertexBuffer;
    mesh->vertexBufferId = vertexBufferId;
    mesh->vertexByteOffset = 0u;
    mesh->indexBuffer = indexBuffer;
    mesh->indexByteOffset = 0u;
    mesh->ownsBuffers = ownsBuffers;
    mesh->boundsCenter = center;
    mesh->boundsExtents = extents;
    mesh->boundsRadius = vec3_length(extents);
    handle.index = slot;
    handle.generation = generation;
    world->meshCount += 1u;
    world->meshRecordsDirty = 1;
    return handle;
}

static void app_world_release_mesh_(GfxDevice* device, AppWorldState* world, AppWorldMeshHandle handle) {
    void* item = 0;
    if (!slot_map_release(&world->meshes, handle.index, handle.generation, &item) || !item) {
        return;
    }
    AppWorldMesh* mesh = (AppWorldMesh*)item;
    if (mesh->ownsBuffers) {
        gfx_destroy_buffer(device, mesh->vertexBuffer);
        gfx_destroy_buffer(device, mesh->indexBuffer);
    }
    MEMSET(mesh, 0, sizeof(*mesh));
    world->meshCount -= 1u;
    world->meshRecordsDirty = 1;
}

static void app_world_build_cube_(AppWorldMeshBuilder* builder) {
    static const F32 faces[6][3] = {
        {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
    };
    for (U32 face = 0u; face < 6u; ++face) {
        F32 nx = faces[face][0];
        F32 ny = faces[face][1];
        F32 nz = faces[face][2];
        F32 ux = ny;
        F32 uy = nz;
        F32 uz = nx;
        F32 vx = ny * uz - nz * uy;
        F32 vy = nz * ux - nx * uz;
        F32 vz = nx * uy - ny * ux;
        U32 base = builder->vertexCount;
        for (U32 corner = 0u; corner < 4u; ++corner) {
            F32 s = (corner == 1u || corner == 2u) ? 0.5f : -0.5f;
            F32 t = (corner >= 2u) ? 0.5f : -0.5f;
            app_world_builder_vertex_(builder,
                                      nx * 0.5f + ux * s + vx * t,
                                      ny * 0.5f + uy * s + vy * t,
                                      nz * 0.5f + uz * s + vz * t,
                                      nx, ny, nz,
                                      (corner == 1u || corner == 2u) ? 1.0f : 0.0f,
                                      (corner >= 2u) ? 1.0f : 0.0f);
        }
        app_world_builder_index_(builder, base + 0u, base + 1u, base + 2u);
        app_world_builder_index_(builder, base + 0u, base + 2u, base + 3u);
    }
}

static void app_world_build_sphere_(AppWorldMeshBuilder* builder, U32 rings, U32 sectors) {
    U32 base = builder->vertexCount;
    for (U32 ring = 0u; ring <= rings; ++ring) {
        F32 v = (F32)ring / (F32)rings;
        F32 phi = v * 3.14159265f;
        F32 y = COS_F32(phi);
        F32 r = SIN_F32(phi);
        for (U32 sector = 0u; sector <= sectors; ++sector) {
            F32 u = (F32)sector / (F32)sectors;
            F32 theta = u * 2.0f * 3.14159265f;
            F32 x = r * COS_F32(theta);
            F32 z = r * SIN_F32(theta);
            app_world_builder_vertex_(builder, x * 0.5f, y * 0.5f, z * 0.5f, x, y, z, u, v);
        }
    }
    for (U32 ring = 0u; ring < rings; ++ring) {
        for (U32 sector = 0u; sector < sectors; ++sector) {
            U32 a = base + ring * (sectors + 1u) + sector;
            U32 b = a + sectors + 1u;
            app_world_builder_index_(builder, a, b, a + 1u);
            app_world_builder_index_(builder, a + 1u, b, b + 1u);
        }
    }
}

static void app_world_build_plane_(AppWorldMeshBuilder* builder) {
    U32 base = builder->vertexCount;
    app_world_builder_vertex_(builder, -0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    app_world_builder_vertex_(builder, 0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f);
    app_world_builder_vertex_(builder, 0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f);
    app_world_builder_vertex_(builder, -0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f);
    app_world_builder_index_(builder, base + 0u, base + 2u, base + 1u);
    app_world_builder_index_(builder, base + 0u, base + 3u, base + 2u);
}

static B32 app_world_try_create_resources_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
    if (world->gpuResourcesCreated || ctx->host->gfxDevice == 0 || state->resources.arena == 0) {
        return world->gpuResourcesCreated;
    }

    GfxDevice* device = ctx->host->gfxDevice;
    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    if (world->meshes.items == 0 &&
        !slot_map_init(&world->meshes, state->resources.arena, sizeof(AppWorldMesh), APP_WORLD_MAX_MESHES)) {
        return 0;
    }

    AppWorldMeshBuilder builder = {};
    builder.vertexCapacity = 4096u;
    builder.indexCapacity = 16384u;
    builder.vertices = ARENA_PUSH_ARRAY(scratch.arena, ShdWorldVertexRecord, builder.vertexCapacity);
    builder.indices = ARENA_PUSH_ARRAY(scratch.arena, U32, builder.indexCapacity);
    if (!builder.vertices || !builder.indices) {
        return 0;
    }

    U32 cubeFirstIndex = builder.indexCount;
    app_world_build_cube_(&builder);
    U32 cubeIndexCount = builder.indexCount - cubeFirstIndex;

    U32 sphereFirstIndex = builder.indexCount;
    app_world_build_sphere_(&builder, 12u, 18u);
    U32 sphereIndexCount = builder.indexCount - sphereFirstIndex;

    U32 planeFirstIndex = builder.indexCount;
    app_world_build_plane_(&builder);
    U32 planeIndexCount = builder.indexCount - planeFirstIndex;

    GfxBufferDesc vertexDesc = {};
    vertexDesc.name = "world vertices";
    vertexDesc.size = sizeof(ShdWorldVertexRecord) * builder.vertexCount;
    vertexDesc.usageFlags = GfxBufferUsageFlags_Storage;
    vertexDesc.memoryKind = GfxMemoryKind_Upload;
    vertexDesc.initialData = builder.vertices;
    world->vertexBuffer = gfx_create_buffer(device, &vertexDesc);
    world->vertexBufferId = gfx_register_buffer(device, world->vertexBuffer);

    GfxBufferDesc indexDesc = {};
    indexDesc.name = "world indices";
    indexDesc.size = sizeof(U32) * builder.indexCount;
    indexDesc.usageFlags = GfxBufferUsageFlags_Index;
    indexDesc.memoryKind = GfxMemoryKind_Upload;
    indexDesc.initialData = builder.indices;
    world->indexBuffer = gfx_create_buffer(device, &indexDesc);

    // Builder indices are pool-absolute, so builtin records carry baseVertex 0;
    // cooked meshes bring mesh-relative indices in their own buffers.
    world->meshCount = 0u;
    world->builtinMeshes[0] = app_world_register_mesh_(world, cubeFirstIndex, cubeIndexCount, 0u,
                                                       world->vertexBuffer, world->vertexBufferId, world->indexBuffer, 0,
                                                       app_world_vec3_(0.0f, 0.0f, 0.0f), app_world_vec3_(0.5f, 0.5f, 0.5f));
    world->builtinMeshes[1] = app_world_register_mesh_(world, sphereFirstIndex, sphereIndexCount, 0u,
                                                       world->vertexBuffer, world->vertexBufferId, world->indexBuffer, 0,
                                                       app_world_vec3_(0.0f, 0.0f, 0.0f), app_world_vec3_(0.5f, 0.5f, 0.5f));
    world->builtinMeshes[2] = app_world_register_mesh_(world, planeFirstIndex, planeIndexCount, 0u,
                                                       world->vertexBuffer, world->vertexBufferId, world->indexBuffer, 0,
                                                       app_world_vec3_(0.0f, 0.0f, 0.0f), app_world_vec3_(0.5f, 0.02f, 0.5f));

    GfxBufferDesc meshRecordDesc = {};
    meshRecordDesc.name = "world mesh records";
    meshRecordDesc.size = sizeof(ShdWorldMeshRecord) * APP_WORLD_MAX_MESHES;
    meshRecordDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    meshRecordDesc.memoryKind = GfxMemoryKind_Device;
    world->meshRecordBuffer = gfx_create_buffer(device, &meshRecordDesc);
    world->meshRecordBufferId = gfx_register_buffer(device, world->meshRecordBuffer);
    world->meshRecordsDirty = 1;

    ShdWorldMaterialRecord* materials = world->materialRecords;
    MEMSET(materials, 0, sizeof(world->materialRecords));
    static const F32 palette[][4] = {
        {0.80f, 0.34f, 0.26f, 1.0f},
        {0.30f, 0.62f, 0.85f, 1.0f},
        {0.92f, 0.78f, 0.32f, 1.0f},
        {0.42f, 0.78f, 0.45f, 1.0f},
        {0.72f, 0.52f, 0.86f, 1.0f},
        {0.34f, 0.36f, 0.42f, 1.0f},
    };
    for (U32 materialIndex = 0u; materialIndex < 6u; ++materialIndex) {
        materials[materialIndex].baseColor[0] = palette[materialIndex][0];
        materials[materialIndex].baseColor[1] = palette[materialIndex][1];
        materials[materialIndex].baseColor[2] = palette[materialIndex][2];
        materials[materialIndex].baseColor[3] = palette[materialIndex][3];
    }
    materials[6].baseColor[0] = 0.95f;
    materials[6].baseColor[1] = 0.95f;
    materials[6].baseColor[2] = 0.95f;
    materials[6].baseColor[3] = 1.0f;
    materials[6].flags = APP_WORLD_MATERIAL_FLAG_ALPHA_TEST;
    materials[7].baseColor[0] = 0.35f;
    materials[7].baseColor[1] = 0.65f;
    materials[7].baseColor[2] = 0.95f;
    materials[7].baseColor[3] = 0.38f;
    materials[8].baseColor[0] = 0.95f;
    materials[8].baseColor[1] = 0.45f;
    materials[8].baseColor[2] = 0.55f;
    materials[8].baseColor[3] = 0.42f;
    materials[9].baseColor[0] = 1.0f;
    materials[9].baseColor[1] = 1.0f;
    materials[9].baseColor[2] = 1.0f;
    materials[9].baseColor[3] = 1.0f;
    world->materialCount = 10u;
    world->materialsDirty = 1;

    GfxSamplerDesc worldSamplerDesc = {};
    worldSamplerDesc.name = "world sampler";
    worldSamplerDesc.minFilter = GfxFilter_Linear;
    worldSamplerDesc.magFilter = GfxFilter_Linear;
    worldSamplerDesc.addressU = GfxAddressMode_Repeat;
    worldSamplerDesc.addressV = GfxAddressMode_Repeat;
    world->worldSampler = gfx_create_sampler(device, &worldSamplerDesc);
    world->worldSamplerId = gfx_register_sampler(device, world->worldSampler);

    GfxBufferDesc materialDesc = {};
    materialDesc.name = "world materials";
    materialDesc.size = sizeof(world->materialRecords);
    materialDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    materialDesc.memoryKind = GfxMemoryKind_Device;
    world->materialBuffer = gfx_create_buffer(device, &materialDesc);
    world->materialBufferId = gfx_register_buffer(device, world->materialBuffer);

    for (U32 bufferIndex = 0u; bufferIndex < APP_WORLD_FRAME_BUFFER_COUNT; ++bufferIndex) {
        GfxBufferDesc frameDesc = {};
        frameDesc.name = "world frame record";
        frameDesc.size = sizeof(ShdWorldFrameRecord);
        frameDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
        frameDesc.memoryKind = GfxMemoryKind_Device;
        world->frameRecordBuffers[bufferIndex] = gfx_create_buffer(device, &frameDesc);
        world->frameRecordBufferIds[bufferIndex] = gfx_register_buffer(device, world->frameRecordBuffers[bufferIndex]);

        GfxBufferDesc renderableDesc = {};
        renderableDesc.name = "world renderables";
        renderableDesc.size = sizeof(ShdWorldRenderableRecord) * APP_WORLD_MAX_RENDERABLES;
        renderableDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
        renderableDesc.memoryKind = GfxMemoryKind_Device;
        world->renderableBuffers[bufferIndex] = gfx_create_buffer(device, &renderableDesc);
        world->renderableBufferIds[bufferIndex] = gfx_register_buffer(device, world->renderableBuffers[bufferIndex]);
    }

    GfxBufferDesc flagsDesc = {};
    flagsDesc.name = "world visibility flags";
    flagsDesc.size = sizeof(U32) * APP_WORLD_MAX_RENDERABLES;
    flagsDesc.usageFlags = GfxBufferUsageFlags_Storage;
    flagsDesc.memoryKind = GfxMemoryKind_Device;
    world->flagsBuffer = gfx_create_buffer(device, &flagsDesc);
    world->flagsBufferId = gfx_register_buffer(device, world->flagsBuffer);

    U32 maxCells = APP_WORLD_BIN_COUNT * APP_WORLD_MAX_MESHES;
    GfxBufferDesc cellDesc = {};
    cellDesc.name = "world cell counts";
    cellDesc.size = sizeof(U32) * maxCells;
    cellDesc.usageFlags = GfxBufferUsageFlags_Storage;
    cellDesc.memoryKind = GfxMemoryKind_Device;
    world->cellCountBuffer = gfx_create_buffer(device, &cellDesc);
    world->cellCountBufferId = gfx_register_buffer(device, world->cellCountBuffer);
    cellDesc.name = "world cell offsets";
    world->cellOffsetBuffer = gfx_create_buffer(device, &cellDesc);
    world->cellOffsetBufferId = gfx_register_buffer(device, world->cellOffsetBuffer);

    GfxBufferDesc visibleDesc = {};
    visibleDesc.name = "world visible list";
    visibleDesc.size = sizeof(U32) * APP_WORLD_MAX_RENDERABLES;
    visibleDesc.usageFlags = GfxBufferUsageFlags_Storage;
    visibleDesc.memoryKind = GfxMemoryKind_Device;
    world->visibleBuffer = gfx_create_buffer(device, &visibleDesc);
    world->visibleBufferId = gfx_register_buffer(device, world->visibleBuffer);

    GfxBufferDesc argsDesc = {};
    argsDesc.name = "world indirect args";
    argsDesc.size = sizeof(GfxDrawIndexedIndirectArgs) * maxCells;
    argsDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_Indirect;
    argsDesc.memoryKind = GfxMemoryKind_Device;
    world->argsBuffer = gfx_create_buffer(device, &argsDesc);
    world->argsBufferId = gfx_register_buffer(device, world->argsBuffer);

    B32 created = world->vertexBuffer.generation != 0u &&
                  world->indexBuffer.generation != 0u &&
                  world->meshRecordBuffer.generation != 0u &&
                  world->materialBuffer.generation != 0u &&
                  world->flagsBuffer.generation != 0u &&
                  world->cellCountBuffer.generation != 0u &&
                  world->cellOffsetBuffer.generation != 0u &&
                  world->visibleBuffer.generation != 0u &&
                  world->argsBuffer.generation != 0u;
    for (U32 bufferIndex = 0u; bufferIndex < APP_WORLD_FRAME_BUFFER_COUNT; ++bufferIndex) {
        created = created &&
                  world->frameRecordBuffers[bufferIndex].generation != 0u &&
                  world->renderableBuffers[bufferIndex].generation != 0u;
    }
    if (!created) {
        LOG_ERROR("gfx", "Failed to create world GPU resources");
        return 0;
    }

    world->gpuResourcesCreated = 1;
    LOG_INFO("gfx", "World renderer resources ready (meshes {}, vertices {}, indices {})",
             world->meshCount, builder.vertexCount, builder.indexCount);
    return 1;
}

// ////////////////////////
// Cooked asset decode (mesh + texture artifacts)

#define APP_ARTIFACT_TYPE_MESH 0x4D455348u
#define APP_ARTIFACT_TYPE_TEXTURE 0x54455855u
#define APP_ARTIFACT_PUBLISHED_MARK 1ull

struct AppAssetRequest {
    ContentHash hash;
};

static B32 app_asset_build_blob_(ArtifactBuildContext* buildCtx, U32 expectedMagic, U64 minimumSize,
                                 ArtifactValue* outValue, U64* outBytes) {
    const AppAssetRequest* request = (const AppAssetRequest*)buildCtx->requestData;
    if (!request || buildCtx->requestDataSize < sizeof(AppAssetRequest)) {
        return 0;
    }
    ContentView view = content_view_hash(buildCtx->content, request->hash);
    if (!view.valid || view.size < minimumSize) {
        return 0;
    }
    if (*(const U32*)view.data != expectedMagic) {
        return 0;
    }
    Arena* arena = arena_alloc(.arenaSize = view.size + KB(64), .committedSize = KB(64));
    if (!arena) {
        return 0;
    }
    U8* blob = ARENA_PUSH_ARRAY(arena, U8, view.size);
    if (!blob) {
        arena_release(arena);
        return 0;
    }
    MEMCPY(blob, view.data, view.size);
    outValue->u64[0] = (U64)arena;
    outValue->u64[1] = (U64)blob;
    outValue->u64[2] = view.size;
    outValue->u64[3] = 0ull;
    *outBytes = view.size;
    return 1;
}

static B32 app_mesh_artifact_build_(ArtifactBuildContext* buildCtx, ArtifactValue* outValue, U64* outBytes) {
    return app_asset_build_blob_(buildCtx, ASSET_MESH_MAGIC, sizeof(AssetMeshHeader), outValue, outBytes);
}

static B32 app_texture_artifact_build_(ArtifactBuildContext* buildCtx, ArtifactValue* outValue, U64* outBytes) {
    return app_asset_build_blob_(buildCtx, ASSET_TEXTURE_MAGIC, sizeof(AssetTextureHeader), outValue, outBytes);
}

static B32 app_mesh_artifact_publish_(ArtifactPublishContext* publishCtx, ArtifactValue buildValue,
                                      ArtifactValue* outValue, U64* outBytes) {
    AppWorldArtifactBridge* bridge = (AppWorldArtifactBridge*)publishCtx->typeUserData;
    Arena* arena = (Arena*)buildValue.u64[0];
    const U8* blob = (const U8*)buildValue.u64[1];
    U64 blobSize = buildValue.u64[2];
    if (!bridge || !bridge->device || !bridge->state || !blob) {
        arena_release(arena);
        return 0;
    }
    AppWorldState* world = &bridge->state->world;

    const AssetMeshHeader* header = (const AssetMeshHeader*)blob;
    U64 vertexBytes = (U64)header->vertexCount * sizeof(ShdWorldVertexRecord);
    U64 indexBytes = (U64)header->indexCount * sizeof(U32);
    if (header->version != ASSET_MESH_VERSION ||
        sizeof(AssetMeshHeader) + vertexBytes + indexBytes > blobSize) {
        arena_release(arena);
        return 0;
    }

    GfxBufferDesc vertexDesc = {};
    vertexDesc.name = "asset mesh vertices";
    vertexDesc.size = vertexBytes;
    vertexDesc.usageFlags = GfxBufferUsageFlags_Storage;
    vertexDesc.memoryKind = GfxMemoryKind_Upload;
    vertexDesc.initialData = blob + sizeof(AssetMeshHeader);
    GfxBuffer vertexBuffer = gfx_create_buffer(bridge->device, &vertexDesc);

    GfxBufferDesc indexDesc = {};
    indexDesc.name = "asset mesh indices";
    indexDesc.size = indexBytes;
    indexDesc.usageFlags = GfxBufferUsageFlags_Index;
    indexDesc.memoryKind = GfxMemoryKind_Upload;
    indexDesc.initialData = blob + sizeof(AssetMeshHeader) + vertexBytes;
    GfxBuffer indexBuffer = gfx_create_buffer(bridge->device, &indexDesc);
    GfxResourceId vertexBufferId = gfx_register_buffer(bridge->device, vertexBuffer);

    U32 vertexCount = header->vertexCount;
    U32 indexCount = header->indexCount;
    Vec3F32 center = app_world_vec3_(header->boundsCenter[0], header->boundsCenter[1], header->boundsCenter[2]);
    Vec3F32 extents = app_world_vec3_(header->boundsExtents[0], header->boundsExtents[1], header->boundsExtents[2]);
    arena_release(arena);

    if (vertexBuffer.generation == 0u || indexBuffer.generation == 0u || vertexBufferId.index == 0u) {
        gfx_destroy_buffer(bridge->device, vertexBuffer);
        gfx_destroy_buffer(bridge->device, indexBuffer);
        return 0;
    }

    AppWorldMeshHandle handle = app_world_register_mesh_(world, 0u, indexCount, 0u,
                                                         vertexBuffer, vertexBufferId, indexBuffer, 1,
                                                         center, extents);
    if (handle.generation == 0u) {
        gfx_destroy_buffer(bridge->device, vertexBuffer);
        gfx_destroy_buffer(bridge->device, indexBuffer);
        return 0;
    }

    outValue->u64[0] = handle.index;
    outValue->u64[1] = handle.generation;
    outValue->u64[2] = 0ull;
    outValue->u64[3] = APP_ARTIFACT_PUBLISHED_MARK;
    *outBytes = vertexBytes + indexBytes;
    LOG_INFO("asset", "Mesh published: {} vertices {} indices", vertexCount, indexCount);
    return 1;
}

static B32 app_texture_artifact_publish_(ArtifactPublishContext* publishCtx, ArtifactValue buildValue,
                                         ArtifactValue* outValue, U64* outBytes) {
    AppWorldArtifactBridge* bridge = (AppWorldArtifactBridge*)publishCtx->typeUserData;
    Arena* arena = (Arena*)buildValue.u64[0];
    const U8* blob = (const U8*)buildValue.u64[1];
    U64 blobSize = buildValue.u64[2];
    if (!bridge || !bridge->device || !bridge->state || !blob) {
        arena_release(arena);
        return 0;
    }
    AppWorldState* world = &bridge->state->world;

    const AssetTextureHeader* header = (const AssetTextureHeader*)blob;
    if (header->version != ASSET_TEXTURE_VERSION ||
        header->mipCount == 0u || header->mipCount > ASSET_TEXTURE_MAX_MIPS) {
        arena_release(arena);
        return 0;
    }

    GfxFrame* frame = g_appRendererFrame.frame;
    if (!frame) {
        arena_release(arena);
        return 0;
    }

    GfxTextureDesc textureDesc = {};
    textureDesc.name = "asset texture";
    textureDesc.width = header->width;
    textureDesc.height = header->height;
    textureDesc.mipCount = header->mipCount;
    textureDesc.format = (header->format == ASSET_TEXTURE_FORMAT_BC3) ? GfxFormat_BC3_RGBA_UNorm
                                                                      : GfxFormat_BC1_RGBA_UNorm;
    textureDesc.usageFlags = GfxTextureUsageFlags_Sampled | GfxTextureUsageFlags_CopyDst;
    GfxTexture texture = gfx_create_texture(bridge->device, &textureDesc);
    if (texture.generation == 0u) {
        arena_release(arena);
        return 0;
    }

    U32 blockBytes = (header->format == ASSET_TEXTURE_FORMAT_BC3) ? 16u : 8u;
    U64 uploadedBytes = 0u;
    U32 mipWidth = header->width;
    U32 mipHeight = header->height;
    for (U32 mipIndex = 0u; mipIndex < header->mipCount; ++mipIndex) {
        if (header->mipOffsets[mipIndex] + header->mipSizes[mipIndex] > blobSize) {
            gfx_destroy_texture(bridge->device, texture);
            arena_release(arena);
            return 0;
        }
        GfxTextureUploadRegion region = {};
        region.mip = mipIndex;
        region.layerCount = 1u;
        region.width = mipWidth;
        region.height = mipHeight;
        region.depth = 1u;
        U64 packedRow = (U64)((mipWidth + 3u) / 4u) * blockBytes;
        region.bytesPerRow = ((packedRow + GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT - 1u) /
                              GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT) *
                             GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT;
        region.rowsPerImage = (mipHeight + 3u) / 4u;
        if (!gfx_upload_texture(frame, texture, &region, blob + header->mipOffsets[mipIndex])) {
            gfx_destroy_texture(bridge->device, texture);
            arena_release(arena);
            return 0;
        }
        uploadedBytes += header->mipSizes[mipIndex];
        mipWidth = MAX(mipWidth / 2u, 1u);
        mipHeight = MAX(mipHeight / 2u, 1u);
    }
    U32 publishedWidth = header->width;
    U32 publishedHeight = header->height;
    U32 publishedMips = header->mipCount;
    arena_release(arena);

    GfxResourceId textureId = gfx_register_texture(bridge->device, texture);
    if (textureId.index == 0u) {
        gfx_destroy_texture(bridge->device, texture);
        return 0;
    }

    if (world->assetTextureCount < APP_WORLD_MAX_MESHES) {
        world->assetTextures[world->assetTextureCount] = texture;
        world->assetTextureCount += 1u;
    }

    ShdWorldMaterialRecord* duckMaterial = &world->materialRecords[9];
    duckMaterial->textureIndex = textureId.index;
    duckMaterial->samplerIndex = world->worldSamplerId.index;
    duckMaterial->flags |= APP_WORLD_MATERIAL_FLAG_TEXTURED;
    world->materialsDirty = 1;
    world->duckTextureReady = 1;

    outValue->u64[0] = texture.index;
    outValue->u64[1] = texture.generation;
    outValue->u64[2] = textureId.index;
    outValue->u64[3] = APP_ARTIFACT_PUBLISHED_MARK;
    *outBytes = uploadedBytes;
    LOG_INFO("asset", "Texture published: {}x{} mips {}", publishedWidth, publishedHeight, publishedMips);
    return 1;
}

static void app_mesh_artifact_destroy_(void* typeUserData, ArtifactValue value) {
    AppWorldArtifactBridge* bridge = (AppWorldArtifactBridge*)typeUserData;
    if (!bridge || !bridge->device || !bridge->state) {
        return;
    }
    if (value.u64[3] != APP_ARTIFACT_PUBLISHED_MARK) {
        Arena* arena = (Arena*)value.u64[0];
        arena_release(arena);
        return;
    }
    AppWorldMeshHandle handle = {};
    handle.index = (U32)value.u64[0];
    handle.generation = (U32)value.u64[1];
    app_world_release_mesh_(bridge->device, &bridge->state->world, handle);
}

static void app_texture_artifact_destroy_(void* typeUserData, ArtifactValue value) {
    AppWorldArtifactBridge* bridge = (AppWorldArtifactBridge*)typeUserData;
    if (!bridge || !bridge->device) {
        return;
    }
    if (value.u64[3] != APP_ARTIFACT_PUBLISHED_MARK) {
        Arena* arena = (Arena*)value.u64[0];
        arena_release(arena);
        return;
    }
    GfxTexture texture = {};
    texture.index = (U32)value.u64[0];
    texture.generation = (U32)value.u64[1];
    AppWorldState* world = &bridge->state->world;
    for (U32 at = 0u; at < world->assetTextureCount; ++at) {
        if (world->assetTextures[at].index == texture.index &&
            world->assetTextures[at].generation == texture.generation) {
            world->assetTextures[at] = world->assetTextures[world->assetTextureCount - 1u];
            world->assetTextureCount -= 1u;
            break;
        }
    }
    gfx_destroy_texture(bridge->device, texture);
}

static B32 app_renderer_register_artifact_types(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    AppCoreState* state = ctx->core;
    if (!state->resources.artifactCache || !ctx->host->gfxDevice) {
        return 1;
    }

    state->world.artifactBridge.device = ctx->host->gfxDevice;
    state->world.artifactBridge.state = state;

    ArtifactTypeDesc meshType = {};
    meshType.typeId = APP_ARTIFACT_TYPE_MESH;
    meshType.name = str8("mesh");
    meshType.buildProc = app_mesh_artifact_build_;
    meshType.publishProc = app_mesh_artifact_publish_;
    meshType.destroyProc = app_mesh_artifact_destroy_;
    meshType.userData = &state->world.artifactBridge;
    if (!artifact_register_type(state->resources.artifactCache, &meshType)) {
        return 0;
    }

    ArtifactTypeDesc textureType = {};
    textureType.typeId = APP_ARTIFACT_TYPE_TEXTURE;
    textureType.name = str8("texture");
    textureType.buildProc = app_texture_artifact_build_;
    textureType.publishProc = app_texture_artifact_publish_;
    textureType.destroyProc = app_texture_artifact_destroy_;
    textureType.userData = &state->world.artifactBridge;
    if (!artifact_register_type(state->resources.artifactCache, &textureType)) {
        return 0;
    }
    return 1;
}

static void app_world_try_load_assets_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;

    if (!state->resources.fileStream || !state->resources.artifactCache || !world->gpuResourcesCreated) {
        return;
    }

    FileView meshView = file_view(state->resources.fileStream, world->duckMeshFile);
    if (meshView.status == FileStatus_Ready && !content_hash_is_zero(meshView.hash)) {
        AppAssetRequest request = {};
        request.hash = meshView.hash;
        ArtifactResult result = artifact_get(state->resources.artifactCache, APP_ARTIFACT_TYPE_MESH,
                                             app_artifact_key_from_label("assets/Duck.umsh"),
                                             meshView.generation, &request, sizeof(request),
                                             ArtifactGetFlags_None, 0u);
        if (result.status == ArtifactStatus_Ready &&
            result.value.u64[3] == APP_ARTIFACT_PUBLISHED_MARK) {
            world->duckMesh.index = (U32)result.value.u64[0];
            world->duckMesh.generation = (U32)result.value.u64[1];
        }
    }

    FileView textureView = file_view(state->resources.fileStream, world->duckTextureFile);
    if (textureView.status == FileStatus_Ready && !content_hash_is_zero(textureView.hash)) {
        AppAssetRequest request = {};
        request.hash = textureView.hash;
        artifact_get(state->resources.artifactCache, APP_ARTIFACT_TYPE_TEXTURE,
                     app_artifact_key_from_label("assets/Duck.utex"),
                     textureView.generation, &request, sizeof(request),
                     ArtifactGetFlags_None, 0u);
    }
}

static B32 app_world_create_graphics_pipeline_(APP_Context* ctx, ContentHash vertexHash, ContentHash fragmentHash,
                                               B32 transparent, GfxPipeline* outPipeline) {
    *outPipeline = {};
    ContentView vertexView = content_view_hash(ctx->core->resources.contentStore, vertexHash);
    ContentView fragmentView = content_view_hash(ctx->core->resources.contentStore, fragmentHash);
    if (!vertexView.valid || vertexView.size == 0u || !fragmentView.valid || fragmentView.size == 0u) {
        return 0;
    }

    GfxFormat colorFormats[1] = {GfxFormat_BGRA8_UNorm};
    GfxColorBlendState blendStates[1] = {};
    blendStates[0].writeFlags = GfxColorWriteFlags_RGBA;
    if (transparent) {
        blendStates[0].blendEnabled = 1;
        blendStates[0].srcColorFactor = GfxBlendFactor_SrcAlpha;
        blendStates[0].dstColorFactor = GfxBlendFactor_OneMinusSrcAlpha;
        blendStates[0].colorOp = GfxBlendOp_Add;
        blendStates[0].srcAlphaFactor = GfxBlendFactor_One;
        blendStates[0].dstAlphaFactor = GfxBlendFactor_OneMinusSrcAlpha;
        blendStates[0].alphaOp = GfxBlendOp_Add;
    }

    GfxGraphicsPipelineDesc desc = {};
    desc.name = transparent ? "world transparent" : "world opaque";
#if defined(PLATFORM_OS_WINDOWS)
    desc.vertexShader.format = GfxShaderFormat_SPIRV;
    desc.fragmentShader.format = GfxShaderFormat_SPIRV;
#else
    desc.vertexShader.format = GfxShaderFormat_MSL_Source;
    desc.fragmentShader.format = GfxShaderFormat_MSL_Source;
#endif
    desc.vertexShader.entry = APP_SHADER_WORLD_VERTEX_ENTRY;
    desc.vertexShader.data = vertexView.data;
    desc.vertexShader.size = vertexView.size;
    desc.fragmentShader.entry = APP_SHADER_WORLD_FRAGMENT_ENTRY;
    desc.fragmentShader.data = fragmentView.data;
    desc.fragmentShader.size = fragmentView.size;
    desc.topology = GfxPrimitiveTopology_TriangleList;
    desc.raster.cullMode = GfxCullMode_None;
    desc.raster.frontFace = GfxFrontFace_CCW;
    desc.depth.depthTestEnabled = 1;
    desc.depth.depthWriteEnabled = transparent ? 0 : 1;
    desc.depth.compareOp = GfxCompareOp_Less;
    desc.colorFormats = colorFormats;
    desc.colorFormatCount = 1u;
    desc.blendStates = blendStates;
    desc.blendStateCount = 1u;
    desc.depthFormat = GfxFormat_D32_Float;

    GfxPipeline pipeline = gfx_create_graphics_pipeline(ctx->host->gfxDevice, &desc);
    if (pipeline.generation == 0u) {
        return 0;
    }
    *outPipeline = pipeline;
    return 1;
}

static void app_world_try_update_pipelines_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
    if (state->resources.fileStream == 0 || state->resources.contentStore == 0 || ctx->host->gfxDevice == 0) {
        return;
    }

    FileView views[APP_WORLD_SHADER_COUNT];
    B32 allReady = 1;
    B32 anyChanged = 0;
    for (U32 shaderIndex = 0u; shaderIndex < APP_WORLD_SHADER_COUNT; ++shaderIndex) {
        views[shaderIndex] = file_view(state->resources.fileStream, world->shaderFiles[shaderIndex]);
        if (views[shaderIndex].status != FileStatus_Ready || content_hash_is_zero(views[shaderIndex].hash)) {
            allReady = 0;
            break;
        }
        if (!content_hash_equal(world->shaderHashes[shaderIndex], views[shaderIndex].hash)) {
            anyChanged = 1;
        }
    }
    if (!allReady || (!anyChanged && world->opaquePipeline.generation != 0u)) {
        return;
    }

    GfxPipeline newOpaque = {};
    GfxPipeline newTransparent = {};
    if (!app_world_create_graphics_pipeline_(ctx, views[AppWorldShaderSlot_Vertex].hash,
                                             views[AppWorldShaderSlot_Fragment].hash, 0, &newOpaque) ||
        !app_world_create_graphics_pipeline_(ctx, views[AppWorldShaderSlot_Vertex].hash,
                                             views[AppWorldShaderSlot_Fragment].hash, 1, &newTransparent)) {
        gfx_destroy_pipeline(ctx->host->gfxDevice, newOpaque);
        return;
    }

    GfxPipeline newCompute[5] = {};
    B32 computeOk = 1;
    for (U32 passIndex = 0u; passIndex < 5u; ++passIndex) {
        ContentView view = content_view_hash(ctx->core->resources.contentStore,
                                             views[AppWorldShaderSlot_Reset + passIndex].hash);
        if (!view.valid || view.size == 0u) {
            computeOk = 0;
            break;
        }
        GfxComputePipelineDesc desc = {};
        desc.name = APP_WORLD_COMPUTE_ENTRIES[passIndex];
#if defined(PLATFORM_OS_WINDOWS)
        desc.shader.format = GfxShaderFormat_SPIRV;
#else
        desc.shader.format = GfxShaderFormat_MSL_Source;
#endif
        desc.shader.entry = APP_WORLD_COMPUTE_ENTRIES[passIndex];
        desc.shader.data = view.data;
        desc.shader.size = view.size;
        desc.threadsPerThreadgroupX = APP_WORLD_COMPUTE_GROUP_SIZES[passIndex];
        desc.threadsPerThreadgroupY = 1u;
        desc.threadsPerThreadgroupZ = 1u;
        newCompute[passIndex] = gfx_create_compute_pipeline(ctx->host->gfxDevice, &desc);
        if (newCompute[passIndex].generation == 0u) {
            computeOk = 0;
            break;
        }
    }
    if (!computeOk) {
        gfx_destroy_pipeline(ctx->host->gfxDevice, newOpaque);
        gfx_destroy_pipeline(ctx->host->gfxDevice, newTransparent);
        for (U32 passIndex = 0u; passIndex < 5u; ++passIndex) {
            gfx_destroy_pipeline(ctx->host->gfxDevice, newCompute[passIndex]);
        }
        return;
    }

    gfx_destroy_pipeline(ctx->host->gfxDevice, world->opaquePipeline);
    gfx_destroy_pipeline(ctx->host->gfxDevice, world->transparentPipeline);
    for (U32 passIndex = 0u; passIndex < 5u; ++passIndex) {
        gfx_destroy_pipeline(ctx->host->gfxDevice, world->computePipelines[passIndex]);
        world->computePipelines[passIndex] = newCompute[passIndex];
    }
    world->opaquePipeline = newOpaque;
    world->transparentPipeline = newTransparent;
    for (U32 shaderIndex = 0u; shaderIndex < APP_WORLD_SHADER_COUNT; ++shaderIndex) {
        world->shaderHashes[shaderIndex] = views[shaderIndex].hash;
    }
    LOG_INFO("gfx", "World pipelines ready");
}

static void app_world_ensure_depth_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
    U32 width = ctx->host->windowWidth;
    U32 height = ctx->host->windowHeight;
    if (width == 0u || height == 0u) {
        return;
    }
    if (world->depthTexture.generation != 0u && world->depthWidth == width && world->depthHeight == height) {
        return;
    }
    if (world->depthTexture.generation != 0u) {
        gfx_destroy_texture(ctx->host->gfxDevice, world->depthTexture);
    }
    GfxTextureDesc depthDesc = {};
    depthDesc.name = "world depth";
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.mipCount = 1u;
    depthDesc.format = GfxFormat_D32_Float;
    depthDesc.usageFlags = GfxTextureUsageFlags_DepthTarget;
    depthDesc.storageKind = GfxTextureStorageKind_Transient;
    world->depthTexture = gfx_create_texture(ctx->host->gfxDevice, &depthDesc);
    world->depthWidth = width;
    world->depthHeight = height;
}

static void app_world_frustum_planes_(const Mat4x4F32* m, F32* outPlanes) {
    // Gribb-Hartmann from column-major viewProj; rowI[j] = v[j][i].
    for (U32 planeIndex = 0u; planeIndex < 6u; ++planeIndex) {
        F32 plane[4];
        U32 row = planeIndex / 2u;
        B32 add = (planeIndex & 1u) == 0u;
        for (U32 component = 0u; component < 4u; ++component) {
            F32 row3 = m->v[component][3];
            F32 rowN = m->v[component][row];
            plane[component] = add ? (row3 + rowN) : (row3 - rowN);
        }
        if (planeIndex == 4u) {
            // Near plane for [0,1] clip depth is row2 itself.
            for (U32 component = 0u; component < 4u; ++component) {
                plane[component] = m->v[component][2];
            }
        }
        F32 lengthSq = plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2];
        F32 inverseLength = (lengthSq > 0.0f) ? (1.0f / SQRT_F32(lengthSq)) : 0.0f;
        for (U32 component = 0u; component < 4u; ++component) {
            outPlanes[planeIndex * 4u + component] = plane[component] * inverseLength;
        }
    }
}

static void app_world_begin_frame_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
    world->renderables = ARENA_PUSH_ARRAY(ctx->host->frameArena, ShdWorldRenderableRecord, APP_WORLD_MAX_RENDERABLES);
    world->transparents = ARENA_PUSH_ARRAY(ctx->host->frameArena, ShdWorldRenderableRecord, APP_WORLD_MAX_RENDERABLES / 4u);
    world->transparentDepths = ARENA_PUSH_ARRAY(ctx->host->frameArena, F32, APP_WORLD_MAX_RENDERABLES / 4u);
    world->renderableCount = 0u;
    world->transparentCount = 0u;
    world->frameOpen = (world->renderables != 0 && world->transparents != 0 && world->transparentDepths != 0);
}

static void app_world_set_camera(APP_Context* ctx, Vec3F32 eye, Vec3F32 target, F32 fovYRadians,
                                 F32 zNear, F32 zFar) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
    F32 aspect = (ctx->host->windowHeight != 0u)
        ? ((F32)ctx->host->windowWidth / (F32)ctx->host->windowHeight)
        : 1.0f;
    Mat4x4F32 view = mat4_look_at(eye, target, app_world_vec3_(0.0f, 1.0f, 0.0f));
    Mat4x4F32 projection = mat4_perspective(fovYRadians, aspect, zNear, zFar);
    Mat4x4F32 viewProj = view * projection;

    MEMSET(&world->frameRecord, 0, sizeof(world->frameRecord));
    MEMCPY(world->frameRecord.viewProj, &viewProj, sizeof(world->frameRecord.viewProj));
    app_world_frustum_planes_(&viewProj, world->frameRecord.frustumPlanes);
    world->frameRecord.cameraPos[0] = eye.x;
    world->frameRecord.cameraPos[1] = eye.y;
    world->frameRecord.cameraPos[2] = eye.z;
    world->frameRecord.time = (F32)((F64)state->frameCounter / 60.0);
}

static void app_world_push(APP_Context* ctx, AppWorldMeshHandle meshHandle, U32 materialIndex,
                           AppWorldBin bin, const Mat4x4F32* transform) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
    if (!world->frameOpen) {
        return;
    }
    AppWorldMesh* mesh = (AppWorldMesh*)slot_map_get(&world->meshes, meshHandle.index, meshHandle.generation);
    if (!mesh) {
        return;
    }

    ShdWorldRenderableRecord record = {};
    MEMCPY(record.transform, transform, sizeof(record.transform));

    // Row-vector convention: world = local . M, translation in storage row 3.
    Vec3F32 local = mesh->boundsCenter;
    Vec3F32 worldCenter;
    worldCenter.x = local.x * transform->v[0][0] + local.y * transform->v[1][0] + local.z * transform->v[2][0] + transform->v[3][0];
    worldCenter.y = local.x * transform->v[0][1] + local.y * transform->v[1][1] + local.z * transform->v[2][1] + transform->v[3][1];
    worldCenter.z = local.x * transform->v[0][2] + local.y * transform->v[1][2] + local.z * transform->v[2][2] + transform->v[3][2];
    F32 scaleX = SQRT_F32(transform->v[0][0] * transform->v[0][0] + transform->v[0][1] * transform->v[0][1] + transform->v[0][2] * transform->v[0][2]);
    F32 scaleY = SQRT_F32(transform->v[1][0] * transform->v[1][0] + transform->v[1][1] * transform->v[1][1] + transform->v[1][2] * transform->v[1][2]);
    F32 scaleZ = SQRT_F32(transform->v[2][0] * transform->v[2][0] + transform->v[2][1] * transform->v[2][1] + transform->v[2][2] * transform->v[2][2]);
    F32 maxScale = MAX(scaleX, MAX(scaleY, scaleZ));

    record.boundsCenter[0] = worldCenter.x;
    record.boundsCenter[1] = worldCenter.y;
    record.boundsCenter[2] = worldCenter.z;
    record.boundsRadius = mesh->boundsRadius * maxScale;
    record.boundsExtents[0] = mesh->boundsExtents.x * maxScale;
    record.boundsExtents[1] = mesh->boundsExtents.y * maxScale;
    record.boundsExtents[2] = mesh->boundsExtents.z * maxScale;
    record.materialIndex = materialIndex;
    record.cellIndex = (U32)bin * APP_WORLD_MAX_MESHES + meshHandle.index;
    record.flags = 0u;

    if (bin == AppWorldBin_Transparent) {
        if (world->transparentCount >= APP_WORLD_MAX_RENDERABLES / 4u) {
            return;
        }
        F32 dx = worldCenter.x - world->frameRecord.cameraPos[0];
        F32 dy = worldCenter.y - world->frameRecord.cameraPos[1];
        F32 dz = worldCenter.z - world->frameRecord.cameraPos[2];
        world->transparents[world->transparentCount] = record;
        world->transparentDepths[world->transparentCount] = dx * dx + dy * dy + dz * dz;
        world->transparentCount += 1u;
        return;
    }

    if (world->renderableCount >= APP_WORLD_MAX_RENDERABLES) {
        return;
    }
    world->renderables[world->renderableCount] = record;
    world->renderableCount += 1u;
}

static void app_world_sort_transparents_(AppWorldState* world, Arena* frameArena) {
    U32 count = world->transparentCount;
    if (count < 2u) {
        return;
    }
    U32* order = ARENA_PUSH_ARRAY(frameArena, U32, count);
    U32* scratch = ARENA_PUSH_ARRAY(frameArena, U32, count);
    if (!order || !scratch) {
        return;
    }
    for (U32 at = 0u; at < count; ++at) {
        order[at] = at;
    }
    // Back-to-front: descending squared distance; radix over flipped F32 bits,
    // two 16-bit passes, then reversed copy-out.
    for (U32 pass = 0u; pass < 2u; ++pass) {
        U32 shift = pass * 16u;
        U32 histogram[65536];
        MEMSET(histogram, 0, sizeof(histogram));
        for (U32 at = 0u; at < count; ++at) {
            union { F32 f; U32 u; } bits;
            bits.f = world->transparentDepths[order[at]];
            U32 key = ((bits.u >> 31u) != 0u) ? ~bits.u : (bits.u | 0x80000000u);
            histogram[(key >> shift) & 0xFFFFu] += 1u;
        }
        U32 running = 0u;
        for (U32 bucket = 0u; bucket < 65536u; ++bucket) {
            U32 bucketCount = histogram[bucket];
            histogram[bucket] = running;
            running += bucketCount;
        }
        for (U32 at = 0u; at < count; ++at) {
            union { F32 f; U32 u; } bits;
            bits.f = world->transparentDepths[order[at]];
            U32 key = ((bits.u >> 31u) != 0u) ? ~bits.u : (bits.u | 0x80000000u);
            scratch[histogram[(key >> shift) & 0xFFFFu]++] = order[at];
        }
        U32* swap = order;
        order = scratch;
        scratch = swap;
    }
    for (U32 at = 0u; at < count; ++at) {
        U32 source = order[count - 1u - at];
        if (world->renderableCount >= APP_WORLD_MAX_RENDERABLES) {
            break;
        }
        world->renderables[world->renderableCount] = world->transparents[source];
        world->renderableCount += 1u;
    }
    world->transparentCount = 0u;
}

static void app_world_execute_(APP_Context* ctx, AppRendererFrame* rendererFrame) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
    GfxFrame* frame = rendererFrame->frame;

    if (!world->frameOpen) {
        world->lastRenderableCount = 0u;
        return;
    }
    world->frameOpen = 0;

    app_world_sort_transparents_(world, ctx->host->frameArena);
    if (world->transparentCount != 0u) {
        for (U32 at = 0u; at < world->transparentCount && world->renderableCount < APP_WORLD_MAX_RENDERABLES; ++at) {
            world->renderables[world->renderableCount] = world->transparents[at];
            world->renderableCount += 1u;
        }
        world->transparentCount = 0u;
    }

    world->lastRenderableCount = world->renderableCount;
    if (world->renderableCount == 0u ||
        !world->gpuResourcesCreated ||
        world->opaquePipeline.generation == 0u ||
        world->depthTexture.generation == 0u) {
        world->lastRenderableCount = 0u;
        return;
    }

    U32 frameBufferIndex = (U32)(state->frameCounter & (APP_WORLD_FRAME_BUFFER_COUNT - 1u));
    world->frameRecord.renderableCount = world->renderableCount;
    if (!gfx_upload_buffer(frame, world->frameRecordBuffers[frameBufferIndex], 0u,
                           &world->frameRecord, sizeof(world->frameRecord)) ||
        !gfx_upload_buffer(frame, world->renderableBuffers[frameBufferIndex], 0u,
                           world->renderables, sizeof(ShdWorldRenderableRecord) * world->renderableCount)) {
        world->lastRenderableCount = 0u;
        return;
    }

    if (world->meshRecordsDirty) {
        ShdWorldMeshRecord meshRecords[APP_WORLD_MAX_MESHES] = {};
        for (U32 meshSlot = 0u; meshSlot < APP_WORLD_MAX_MESHES; ++meshSlot) {
            if (!slot_map_is_occupied(&world->meshes, meshSlot)) {
                continue;
            }
            AppWorldMesh* mesh = (AppWorldMesh*)slot_map_item_at(&world->meshes, meshSlot);
            meshRecords[meshSlot].indexCount = mesh->indexCount;
            meshRecords[meshSlot].firstIndex = mesh->firstIndex;
            meshRecords[meshSlot].baseVertex = mesh->baseVertex;
        }
        if (gfx_upload_buffer(frame, world->meshRecordBuffer, 0u, meshRecords, sizeof(meshRecords))) {
            world->meshRecordsDirty = 0;
        }
    }
    if (world->materialsDirty) {
        if (gfx_upload_buffer(frame, world->materialBuffer, 0u, world->materialRecords,
                              sizeof(world->materialRecords))) {
            world->materialsDirty = 0;
        }
    }

    U32 cellCount = app_world_cell_count_(world);

    GfxTemp rootTemp = gfx_allocate_temp(frame, sizeof(ShdWorldCullRootData), 16u);
    if (!rootTemp.cpu) {
        world->lastRenderableCount = 0u;
        return;
    }
    ShdWorldCullRootData* cullRoot = (ShdWorldCullRootData*)rootTemp.cpu;
    MEMSET(cullRoot, 0, sizeof(*cullRoot));
    cullRoot->frameBuffer = world->frameRecordBufferIds[frameBufferIndex].index;
    cullRoot->renderableBuffer = world->renderableBufferIds[frameBufferIndex].index;
    cullRoot->flagsBuffer = world->flagsBufferId.index;
    cullRoot->cellCountBuffer = world->cellCountBufferId.index;
    cullRoot->cellOffsetBuffer = world->cellOffsetBufferId.index;
    cullRoot->visibleBuffer = world->visibleBufferId.index;
    cullRoot->argsBuffer = world->argsBufferId.index;
    cullRoot->meshBuffer = world->meshRecordBufferId.index;
    cullRoot->renderableCount = world->renderableCount;
    cullRoot->cellCount = cellCount;
    cullRoot->meshCount = APP_WORLD_MAX_MESHES;

    static const char* passNames[5] = {
        "world reset", "world cull", "world prefix", "world compact", "world args",
    };
    U32 groupCounts[5] = {
        (cellCount + APP_WORLD_CULL_GROUP_SIZE - 1u) / APP_WORLD_CULL_GROUP_SIZE,
        (world->renderableCount + APP_WORLD_CULL_GROUP_SIZE - 1u) / APP_WORLD_CULL_GROUP_SIZE,
        1u,
        cellCount,
        (cellCount + APP_WORLD_CULL_GROUP_SIZE - 1u) / APP_WORLD_CULL_GROUP_SIZE,
    };

    for (U32 passIndex = 0u; passIndex < 5u; ++passIndex) {
        GfxResourceUse uses[8] = {};
        U32 useCount = 0u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->renderableBuffers[frameBufferIndex];
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->frameRecordBuffers[frameBufferIndex];
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead | GfxResourceAccessFlags_ShaderWrite;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->flagsBuffer;
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead | GfxResourceAccessFlags_ShaderWrite;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->cellCountBuffer;
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead | GfxResourceAccessFlags_ShaderWrite;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->cellOffsetBuffer;
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead | GfxResourceAccessFlags_ShaderWrite;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->visibleBuffer;
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderWrite;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->argsBuffer;
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->meshRecordBuffer;
        useCount += 1u;

        GfxComputePassDesc passDesc = {};
        passDesc.name = passNames[passIndex];
        passDesc.resourceUses = uses;
        passDesc.resourceUseCount = useCount;

        GfxDispatch dispatch = {};
        dispatch.pipeline = world->computePipelines[passIndex];
        dispatch.rootDataOffset = (U32)rootTemp.gpu.offset;
        dispatch.rootDataSize = (U32)rootTemp.gpu.size;
        dispatch.groupsX = groupCounts[passIndex];
        dispatch.groupsY = 1u;
        dispatch.groupsZ = 1u;
        gfx_compute_pass(rendererFrame->commands, &passDesc, &dispatch, 1u);
    }

    GfxColorTarget colorTarget = {};
    colorTarget.texture = gfx_get_backbuffer(frame);
    colorTarget.loadOp = GfxLoadOp_Clear;
    colorTarget.storeOp = GfxStoreOp_Store;
    colorTarget.clearColor[0] = 0.06f;
    colorTarget.clearColor[1] = 0.08f;
    colorTarget.clearColor[2] = 0.10f;
    colorTarget.clearColor[3] = 1.0f;

    GfxDepthTarget depthTarget = {};
    depthTarget.texture = world->depthTexture;
    depthTarget.loadOp = GfxLoadOp_Clear;
    depthTarget.storeOp = GfxStoreOp_DontCare;
    depthTarget.clearDepth = 1.0f;

    U32 maxDrawUses = 7u + APP_WORLD_MAX_MESHES + world->assetTextureCount;
    GfxResourceUse* drawUses = ARENA_PUSH_ARRAY(ctx->host->frameArena, GfxResourceUse, maxDrawUses);
    if (!drawUses) {
        world->lastRenderableCount = 0u;
        return;
    }
    MEMSET(drawUses, 0, sizeof(GfxResourceUse) * maxDrawUses);
    U32 drawUseCount = 0u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_IndirectRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
    drawUses[drawUseCount].buffer = world->argsBuffer;
    drawUseCount += 1u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
    drawUses[drawUseCount].buffer = world->visibleBuffer;
    drawUseCount += 1u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
    drawUses[drawUseCount].buffer = world->cellOffsetBuffer;
    drawUseCount += 1u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
    drawUses[drawUseCount].buffer = world->renderableBuffers[frameBufferIndex];
    drawUseCount += 1u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
    drawUses[drawUseCount].buffer = world->frameRecordBuffers[frameBufferIndex];
    drawUseCount += 1u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
    drawUses[drawUseCount].buffer = world->vertexBuffer;
    drawUseCount += 1u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Fragment;
    drawUses[drawUseCount].buffer = world->materialBuffer;
    drawUseCount += 1u;
    for (U32 meshSlot = 0u; meshSlot < APP_WORLD_MAX_MESHES; ++meshSlot) {
        if (!slot_map_is_occupied(&world->meshes, meshSlot)) {
            continue;
        }
        AppWorldMesh* slotMesh = (AppWorldMesh*)slot_map_item_at(&world->meshes, meshSlot);
        if (!slotMesh->ownsBuffers) {
            continue;
        }
        drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
        drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
        drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
        drawUses[drawUseCount].buffer = slotMesh->vertexBuffer;
        drawUseCount += 1u;
    }
    for (U32 at = 0u; at < world->assetTextureCount; ++at) {
        drawUses[drawUseCount].kind = GfxResourceUseKind_Texture;
        drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
        drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Fragment;
        drawUses[drawUseCount].texture = world->assetTextures[at];
        drawUseCount += 1u;
    }

    GfxRenderPassDesc passDesc = {};
    passDesc.name = "world forward";
    passDesc.colorTargets = &colorTarget;
    passDesc.colorTargetCount = 1u;
    passDesc.depthTarget = &depthTarget;
    passDesc.resourceUses = drawUses;
    passDesc.resourceUseCount = drawUseCount;

    U32 maxCells = APP_WORLD_BIN_COUNT * APP_WORLD_MAX_MESHES;
    GfxDraw* draws = ARENA_PUSH_ARRAY(ctx->host->frameArena, GfxDraw, cellCount <= maxCells ? cellCount : maxCells);
    if (!draws) {
        world->lastRenderableCount = 0u;
        return;
    }
    U32 drawCount = 0u;
    for (U32 cell = 0u; cell < cellCount; ++cell) {
        U32 bin = cell / APP_WORLD_MAX_MESHES;
        U32 meshSlot = cell % APP_WORLD_MAX_MESHES;
        if (!slot_map_is_occupied(&world->meshes, meshSlot)) {
            continue;
        }
        AppWorldMesh* mesh = (AppWorldMesh*)slot_map_item_at(&world->meshes, meshSlot);

        GfxTemp drawRoot = gfx_allocate_temp(frame, sizeof(ShdWorldForwardRootData), 16u);
        if (!drawRoot.cpu) {
            break;
        }
        ShdWorldForwardRootData* rootData = (ShdWorldForwardRootData*)drawRoot.cpu;
        MEMSET(rootData, 0, sizeof(*rootData));
        rootData->frameBuffer = world->frameRecordBufferIds[frameBufferIndex].index;
        rootData->renderableBuffer = world->renderableBufferIds[frameBufferIndex].index;
        rootData->visibleBuffer = world->visibleBufferId.index;
        rootData->cellOffsetBuffer = world->cellOffsetBufferId.index;
        rootData->materialBuffer = world->materialBufferId.index;
        rootData->vertexBuffer = mesh->vertexBufferId.index;
        rootData->vertexByteOffset = mesh->vertexByteOffset;
        rootData->cellIndex = cell;

        GfxDraw* draw = draws + drawCount;
        *draw = {};
        draw->pipeline = (bin == (U32)AppWorldBin_Transparent) ? world->transparentPipeline : world->opaquePipeline;
        draw->indexBuffer = mesh->indexBuffer;
        draw->indexByteOffset = mesh->indexByteOffset;
        draw->indirectBuffer = world->argsBuffer;
        draw->indirectByteOffset = cell * (U32)sizeof(GfxDrawIndexedIndirectArgs);
        draw->indexType = GfxIndexType_U32;
        draw->rootDataOffset = (U32)drawRoot.gpu.offset;
        draw->rootDataSize = (U32)drawRoot.gpu.size;
        drawCount += 1u;
    }

    GfxDrawArea area = {};
    area.viewport.width = (F32)ctx->host->windowWidth;
    area.viewport.height = (F32)ctx->host->windowHeight;
    area.viewport.maxDepth = 1.0f;
    area.scissor.width = ctx->host->windowWidth;
    area.scissor.height = ctx->host->windowHeight;
    area.draws = draws;
    area.drawCount = drawCount;

    gfx_render_pass(rendererFrame->commands, &passDesc, &area, 1u);
}

static B32 app_renderer_init(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    if (state->render2d.initialized) {
        return 1;
    }
    if (!ctx->host->gfxDevice) {
        LOG_ERROR("gfx", "App has no gfx device");
        return 0;
    }
    if (!app_resource_cache_init(ctx)) {
        return 0;
    }
    if (!app_renderer_ensure_text_context(ctx)) {
        return 0;
    }

    state->render2d.initialized = 1;
    app_renderer_log_once(state, AppRendererLoadLog_Started, "Renderer resources requested");
    return 1;
}

static void app_renderer_shutdown(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    AppRender2DState* render = &state->render2d;
    GfxDevice* device = ctx->host ? ctx->host->gfxDevice : 0;

    if (render->textContext != 0) {
        text_context_destroy(render->textContext);
    }
    app_resource_cache_shutdown(ctx);

    if (device != 0) {
        for (U32 bufferIndex = 0u; bufferIndex < APP_RENDERER_FRAME_BUFFER_COUNT; ++bufferIndex) {
            gfx_destroy_buffer(device, render->quadBuffers[bufferIndex]);
        }
        gfx_destroy_buffer(device, render->indexBuffer);
        gfx_destroy_sampler(device, render->atlasSampler);
        gfx_destroy_texture(device, render->atlasTexture);
        gfx_destroy_pipeline(device, render->pipeline);

        AppWorldState* world = &state->world;
        gfx_destroy_buffer(device, world->vertexBuffer);
        gfx_destroy_buffer(device, world->indexBuffer);
        gfx_destroy_buffer(device, world->meshRecordBuffer);
        gfx_destroy_buffer(device, world->materialBuffer);
        for (U32 bufferIndex = 0u; bufferIndex < APP_WORLD_FRAME_BUFFER_COUNT; ++bufferIndex) {
            gfx_destroy_buffer(device, world->frameRecordBuffers[bufferIndex]);
            gfx_destroy_buffer(device, world->renderableBuffers[bufferIndex]);
        }
        gfx_destroy_buffer(device, world->flagsBuffer);
        gfx_destroy_buffer(device, world->cellCountBuffer);
        gfx_destroy_buffer(device, world->cellOffsetBuffer);
        gfx_destroy_buffer(device, world->visibleBuffer);
        gfx_destroy_buffer(device, world->argsBuffer);
        gfx_destroy_sampler(device, world->worldSampler);
        gfx_destroy_texture(device, world->depthTexture);
        gfx_destroy_pipeline(device, world->opaquePipeline);
        gfx_destroy_pipeline(device, world->transparentPipeline);
        for (U32 passIndex = 0u; passIndex < 5u; ++passIndex) {
            gfx_destroy_pipeline(device, world->computePipelines[passIndex]);
        }
        MEMSET(world, 0, sizeof(*world));
    }

    *render = {};
}

static AppRendererFrame* app_renderer_begin_frame(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    if (!ctx->host->gfxDevice ||
        ctx->host->windowWidth == 0u ||
        ctx->host->windowHeight == 0u) {
        return 0;
    }
    if (!app_renderer_init(ctx)) {
        return 0;
    }

#if defined(PLATFORM_BUILD_DEBUG)
    {
        PROF_SCOPE("dev shaders");
        app_gfx_try_build_dev_shaders(ctx);
    }
    if (ctx->core->resources.fileStream) {
        PROF_SCOPE("file stream tick");
        file_stream_tick(ctx->core->resources.fileStream, OS_get_time_nanoseconds(), 16u);
    }
#endif
    GfxFrame* frame = 0;
    {
        PROF_SCOPE("gfx begin frame");
        frame = gfx_begin_frame(ctx->host->gfxDevice);
    }
    if (!frame) {
        return 0;
    }

    g_appRendererFrame.frame = frame;
    g_appRendererFrame.commands = gfx_get_command_buffer(frame);

    // Publishes may record uploads into the current frame, so the tick runs
    // with the frame pointer established.
    if (ctx->core->resources.artifactCache) {
        PROF_SCOPE("artifact tick");
        artifact_cache_tick(ctx->core->resources.artifactCache, ctx->core->frameCounter, 16u, 16u);
    }

    app_renderer_try_load_font(ctx);
    app_renderer_try_update_pipeline(ctx);
    app_renderer_try_create_gpu_resources(ctx);
    app_renderer_try_seed_atlas(ctx, frame);
    app_world_try_create_resources_(ctx);
    app_world_try_update_pipelines_(ctx);
    app_world_ensure_depth_(ctx);
    app_world_begin_frame_(ctx);
    app_world_try_load_assets_(ctx);

    F32 whiteU = 0.0f;
    F32 whiteV = 0.0f;
    text_white_uv(ctx->core->render2d.textContext, &whiteU, &whiteV);
    draw2d_begin(&ctx->core->render2d.draw2d, ctx->host->frameArena, whiteU, whiteV);

    return &g_appRendererFrame;
}

static void app_renderer_submit_text(APP_Context* ctx, AppRendererFrame* rendererFrame, const TextDrawData* drawData, Draw2DLayer layer) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    if (rendererFrame == 0 || drawData == 0) {
        return;
    }

    if (ctx->core->render2d.gpuResourcesCreated) {
        for (U32 uploadIndex = 0u; uploadIndex < drawData->uploadCount; ++uploadIndex) {
            app_renderer_upload_atlas(ctx, rendererFrame->frame, drawData->uploads + uploadIndex);
        }
    }
    draw2d_glyph_quads(&ctx->core->render2d.draw2d, layer, (const Draw2DQuad*)drawData->quads, drawData->quadCount, 0.0f, 0.0f);
}

static void app_renderer_apply_text_uploads(APP_Context* ctx, AppRendererFrame* rendererFrame, const TextAtlasUpload* uploads, U32 uploadCount) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    if (rendererFrame == 0 || uploads == 0 || !ctx->core->render2d.gpuResourcesCreated) {
        return;
    }
    for (U32 uploadIndex = 0u; uploadIndex < uploadCount; ++uploadIndex) {
        app_renderer_upload_atlas(ctx, rendererFrame->frame, uploads + uploadIndex);
    }
}

static void app_renderer_end_frame(APP_Context* ctx, AppRendererFrame* rendererFrame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    if (rendererFrame == 0 || rendererFrame->frame == 0) {
        return;
    }

    {
        PROF_SCOPE("world passes");
        app_world_execute_(ctx, rendererFrame);
    }

    Draw2DResult result = {};
    {
        PROF_SCOPE("draw2d end");
        result = draw2d_end(&ctx->core->render2d.draw2d);
    }
    {
        PROF_SCOPE("2d pass");
        app_renderer_execute_2d(ctx, rendererFrame, result);
    }

    ctx->core->render2d.lastDraw2DStats = ctx->core->render2d.draw2d.stats;
    ctx->core->render2d.lastGfxStats = gfx_get_stats(ctx->host->gfxDevice);

    {
        PROF_SCOPE("gfx submit");
        gfx_submit(rendererFrame->commands);
    }
    {
        PROF_SCOPE("gfx end frame");
        gfx_end_frame(rendererFrame->frame);
    }
    if (ctx->core->resources.artifactCache) {
        artifact_cache_evict(ctx->core->resources.artifactCache, ctx->core->frameCounter, 128u);
    }

    g_appRendererFrame = {};
}
