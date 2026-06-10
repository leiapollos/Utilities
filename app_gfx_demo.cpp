//
// Created by André Leite on 31/10/2025.
//
// The whole current app: text + draw2d rendered through one 2D overlay pass.
// The demo scene at the bottom is deliberately tiny and disposable.

#define APP_RENDER2D_FONT_PATH "app/fonts/NotoSans-Regular.ttf"
#define APP_RENDER2D_MAX_QUADS (DRAW2D_DEFAULT_MAX_QUADS_PER_LAYER * Draw2DLayer_COUNT)
#define APP_RENDER2D_FRAME_BUFFER_COUNT 2u
#define APP_RENDER2D_MAX_FRAME_DELTA_SECONDS 0.05f

// Shader ABI
struct AppDraw2DRootData {
    U32 quadBuffer;
    U32 quadByteOffset;
    U32 atlasTexture;
    U32 atlasSampler;
    F32 targetWidth;
    F32 targetHeight;
    U32 _padding[10];
};

#define APP_SHADER_ABI_OFFSET(type, member, byteOffset) \
    static_assert(offsetof(type, member) == (byteOffset), #type "." #member " shader ABI offset mismatch")
APP_SHADER_ABI_OFFSET(AppDraw2DRootData, quadBuffer, 0u);
APP_SHADER_ABI_OFFSET(AppDraw2DRootData, quadByteOffset, 4u);
APP_SHADER_ABI_OFFSET(AppDraw2DRootData, atlasTexture, 8u);
APP_SHADER_ABI_OFFSET(AppDraw2DRootData, atlasSampler, 12u);
APP_SHADER_ABI_OFFSET(AppDraw2DRootData, targetWidth, 16u);
APP_SHADER_ABI_OFFSET(AppDraw2DRootData, targetHeight, 20u);
static_assert(sizeof(AppDraw2DRootData) == 64u, "Draw2D root data shader ABI mismatch");
static_assert(sizeof(Draw2DQuad) == sizeof(TextQuad), "Draw2DQuad must stay bit-identical to TextQuad");
static_assert(sizeof(Draw2DQuad) == 36u, "Draw2DQuad shader ABI mismatch");

enum AppRender2DLoadLog {
    AppRender2DLoadLog_Started = (1u << 0u),
    AppRender2DLoadLog_Ready = (1u << 1u),
};

struct AppRender2DPacket {
    GfxRenderPassDesc pass;
    GfxColorTarget colorTarget;
    GfxResourceUse resourceUses[2];
    GfxDrawArea area;
    GfxDraw draws[Draw2DLayer_COUNT];
    U32 drawCount;
};

static void app_render2d_log_once(AppCoreState* state, U32 bit, const char* message) {
    if (state == 0 || message == 0 || FLAGS_HAS(state->render2d.loadLogMask, bit)) {
        return;
    }

    LOG_INFO("gfx", "{}", str8(message));
    state->render2d.loadLogMask |= bit;
}

// Resource cache hooks (called from app.cpp)
static B32 app_gfx_demo_register_artifact_types(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    return 1;
}

static void app_gfx_demo_resource_cache_reset(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppRender2DState* render = &ctx->core->render2d;
    render->fontFile = FILE_HANDLE_ZERO;
    render->vertexShaderFile = FILE_HANDLE_ZERO;
    render->fragmentShaderFile = FILE_HANDLE_ZERO;
    render->vertexShaderHash = CONTENT_HASH_ZERO;
    render->fragmentShaderHash = CONTENT_HASH_ZERO;
    render->failedFontGeneration = 0u;
}

static void app_gfx_demo_watch_files(APP_Context* ctx) {
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
    StringU8 fontPath = str8_concat(scratch.arena, exeDir, str8("/../" APP_RENDER2D_FONT_PATH));

    state->render2d.vertexShaderFile = file_watch(state->resources.fileStream, vertexPath, 0u);
    state->render2d.fragmentShaderFile = file_watch(state->resources.fileStream, fragmentPath, 0u);
    state->render2d.fontFile = file_watch(state->resources.fileStream, fontPath, 0u);
}

// Text + pipeline + GPU resources
static B32 app_render2d_ensure_text_context(APP_Context* ctx) {
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

static void app_render2d_try_load_font(APP_Context* ctx) {
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

static B32 app_render2d_create_pipeline(APP_Context* ctx, ContentHash vertexHash, ContentHash fragmentHash, GfxPipeline* outPipeline) {
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

static void app_render2d_try_update_pipeline(APP_Context* ctx) {
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
    if (!app_render2d_create_pipeline(ctx, vertexView.hash, fragmentView.hash, &newPipeline)) {
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

static void app_render2d_try_create_gpu_resources(APP_Context* ctx) {
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

    U32 indexCount = APP_RENDER2D_MAX_QUADS * 6u;
    U32* indices = ARENA_PUSH_ARRAY(scratch.arena, U32, indexCount);
    if (indices == 0) {
        return;
    }
    for (U32 quadIndex = 0u; quadIndex < APP_RENDER2D_MAX_QUADS; ++quadIndex) {
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

    GfxBuffer quadBuffers[APP_RENDER2D_FRAME_BUFFER_COUNT] = {};
    GfxResourceId quadBufferIds[APP_RENDER2D_FRAME_BUFFER_COUNT] = {};
    for (U32 bufferIndex = 0u; bufferIndex < APP_RENDER2D_FRAME_BUFFER_COUNT; ++bufferIndex) {
        GfxBufferDesc quadDesc = {};
        quadDesc.name = "draw2d quads";
        quadDesc.size = sizeof(Draw2DQuad) * APP_RENDER2D_MAX_QUADS;
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
    for (U32 bufferIndex = 0u; bufferIndex < APP_RENDER2D_FRAME_BUFFER_COUNT; ++bufferIndex) {
        created = created &&
                  quadBuffers[bufferIndex].generation != 0u &&
                  quadBufferIds[bufferIndex].index != 0u;
    }
    if (!created) {
        LOG_ERROR("gfx", "Failed to create draw2d GPU resources");
        gfx_destroy_buffer(device, indexBuffer);
        gfx_destroy_sampler(device, atlasSampler);
        gfx_destroy_texture(device, atlasTexture);
        for (U32 bufferIndex = 0u; bufferIndex < APP_RENDER2D_FRAME_BUFFER_COUNT; ++bufferIndex) {
            gfx_destroy_buffer(device, quadBuffers[bufferIndex]);
        }
        return;
    }

    render->atlasTexture = atlasTexture;
    render->atlasTextureId = atlasTextureId;
    render->atlasSampler = atlasSampler;
    render->atlasSamplerId = atlasSamplerId;
    render->indexBuffer = indexBuffer;
    for (U32 bufferIndex = 0u; bufferIndex < APP_RENDER2D_FRAME_BUFFER_COUNT; ++bufferIndex) {
        render->quadBuffers[bufferIndex] = quadBuffers[bufferIndex];
        render->quadBufferIds[bufferIndex] = quadBufferIds[bufferIndex];
    }
    render->gpuResourcesCreated = 1;
    render->atlasSeeded = 0;
}

static void app_render2d_upload_atlas(APP_Context* ctx, GfxFrame* frame, const TextAtlasUpload* upload) {
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

// GPU texture memory is undefined until written; seed the whole atlas once
// after creation so the gutters between glyph rects sample as zero.
static void app_render2d_try_seed_atlas(APP_Context* ctx, GfxFrame* frame) {
    AppRender2DState* render = &ctx->core->render2d;
    if (render->atlasSeeded || !render->gpuResourcesCreated || render->textContext == 0) {
        return;
    }

    TextAtlasUpload fullUpload = text_atlas_full_upload(render->textContext);
    if (fullUpload.width == 0u) {
        return;
    }
    app_render2d_upload_atlas(ctx, frame, &fullUpload);
    render->atlasSeeded = 1;
}

// Batch execution: upload the frame's quads, emit one draw per batch.
static void app_render2d_execute(APP_Context* ctx, GfxCommandBuffer* commands, GfxFrame* frame, Draw2DResult result) {
    AppCoreState* state = ctx->core;
    AppRender2DState* render = &state->render2d;

    AppRender2DPacket packet = {};
    packet.colorTarget.texture = gfx_get_backbuffer(frame);
    packet.colorTarget.loadOp = GfxLoadOp_Clear;
    packet.colorTarget.storeOp = GfxStoreOp_Store;
    packet.colorTarget.clearColor[0] = 0.06f;
    packet.colorTarget.clearColor[1] = 0.08f;
    packet.colorTarget.clearColor[2] = 0.10f;
    packet.colorTarget.clearColor[3] = 1.0f;
    packet.pass.name = "2d overlay pass";
    packet.pass.colorTargets = &packet.colorTarget;
    packet.pass.colorTargetCount = 1u;

    U32 frameBufferIndex = (U32)(state->frameCounter & (APP_RENDER2D_FRAME_BUFFER_COUNT - 1u));
    B32 drawsReady = result.quadCount != 0u &&
                     result.quadCount <= APP_RENDER2D_MAX_QUADS &&
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

            GfxTemp rootTemp = gfx_allocate_temp(frame, sizeof(AppDraw2DRootData), 16u);
            if (rootTemp.cpu == 0) {
                break;
            }
            AppDraw2DRootData* rootData = (AppDraw2DRootData*)rootTemp.cpu;
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

        packet.resourceUses[0] = {};
        packet.resourceUses[0].kind = GfxResourceUseKind_Buffer;
        packet.resourceUses[0].accessFlags = GfxResourceAccessFlags_ShaderRead;
        packet.resourceUses[0].shaderStages = GfxShaderStageFlags_Vertex;
        packet.resourceUses[0].buffer = render->quadBuffers[frameBufferIndex];
        packet.resourceUses[1] = {};
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

    gfx_render_pass(commands, &packet.pass, &packet.area, 1u);

    if (packet.drawCount != 0u) {
        app_render2d_log_once(state, AppRender2DLoadLog_Ready, "Draw2d overlay ready");
    }
}

// The demo scene. Temporary by design: a panel, some shapes, a clip
// showcase, and the text corpus. Delete freely.
static void app_demo_scene_submit(APP_Context* ctx, Draw2DContext* draw2d, GfxFrame* frame) {
    AppCoreState* state = ctx->core;
    if (state->render2d.textContext == 0 || state->render2d.font.generation == 0u) {
        return;
    }

    F32 panelMinX = 40.0f;
    F32 panelMinY = 40.0f;
    F32 panelMaxX = 980.0f;
    F32 panelMaxY = 420.0f;
    draw2d_rect(draw2d, Draw2DLayer_UI, panelMinX, panelMinY, panelMaxX, panelMaxY, 0x14181CF0u);
    draw2d_box(draw2d, Draw2DLayer_UI, panelMinX, panelMinY, panelMaxX, panelMaxY, 2.0f, 0x3A4148FFu);
    draw2d_line(draw2d, Draw2DLayer_UI, panelMinX + 24.0f, 132.0f, panelMaxX - 24.0f, 132.0f, 2.0f, 0x3A4148FFu);

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena == 0) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    TextDrawDesc titleDesc = {};
    titleDesc.font = state->render2d.font;
    titleDesc.text = str8("draw2d + kb_text_shape + FreeType");
    titleDesc.x = panelMinX + 24.0f;
    titleDesc.y = panelMinY + 24.0f;
    titleDesc.pixelSize = 40.0f;
    titleDesc.rgba8 = 0xF4F1E8FFu;
    TextDrawData title = text_prepare_draw(state->render2d.textContext, scratch.arena, &titleDesc);

    TextDrawDesc bodyDesc = {};
    bodyDesc.font = state->render2d.font;
    bodyDesc.text = str8("Hello, text\nOla, acao, coracao\nOl\xC3\xA1, a\xC3\xA7\xC3\xA3o, cora\xC3\xA7\xC3\xA3o\nAVATAR ToYo office ffi fi fl");
    bodyDesc.x = panelMinX + 24.0f;
    bodyDesc.y = 156.0f;
    bodyDesc.pixelSize = 28.0f;
    bodyDesc.rgba8 = 0xD9D4C7FFu;
    TextDrawData body = text_prepare_draw(state->render2d.textContext, scratch.arena, &bodyDesc);

    for (U32 uploadIndex = 0u; uploadIndex < title.uploadCount; ++uploadIndex) {
        app_render2d_upload_atlas(ctx, frame, title.uploads + uploadIndex);
    }
    for (U32 uploadIndex = 0u; uploadIndex < body.uploadCount; ++uploadIndex) {
        app_render2d_upload_atlas(ctx, frame, body.uploads + uploadIndex);
    }

    draw2d_glyph_quads(draw2d, Draw2DLayer_UI, (const Draw2DQuad*)title.quads, title.quadCount);
    draw2d_glyph_quads(draw2d, Draw2DLayer_UI, (const Draw2DQuad*)body.quads, body.quadCount);

    // Clip showcase: the rect and text are clipped to the marked box.
    F32 clipMinX = panelMinX + 24.0f;
    F32 clipMinY = 320.0f;
    F32 clipMaxX = clipMinX + 360.0f;
    F32 clipMaxY = clipMinY + 72.0f;
    draw2d_box(draw2d, Draw2DLayer_UI, clipMinX, clipMinY, clipMaxX, clipMaxY, 1.0f, 0x6B7480FFu);
    draw2d_push_clip(draw2d, clipMinX, clipMinY, clipMaxX, clipMaxY);
    draw2d_rect(draw2d, Draw2DLayer_UI, clipMinX - 40.0f, clipMinY + 12.0f, clipMaxX + 40.0f, clipMinY + 28.0f, 0x4F8A6AFFu);

    TextDrawDesc clippedDesc = {};
    clippedDesc.font = state->render2d.font;
    clippedDesc.text = str8("clipped text runs past the box edge and gets cut");
    clippedDesc.x = clipMinX + 8.0f;
    clippedDesc.y = clipMinY + 34.0f;
    clippedDesc.pixelSize = 24.0f;
    clippedDesc.rgba8 = 0xB9C4D1FFu;
    TextDrawData clipped = text_prepare_draw(state->render2d.textContext, scratch.arena, &clippedDesc);
    for (U32 uploadIndex = 0u; uploadIndex < clipped.uploadCount; ++uploadIndex) {
        app_render2d_upload_atlas(ctx, frame, clipped.uploads + uploadIndex);
    }
    draw2d_glyph_quads(draw2d, Draw2DLayer_UI, (const Draw2DQuad*)clipped.quads, clipped.quadCount);
    draw2d_pop_clip(draw2d);

    // Debug layer renders above UI.
    draw2d_box(draw2d, Draw2DLayer_Debug, panelMaxX - 56.0f, panelMinY + 16.0f, panelMaxX - 16.0f, panelMinY + 56.0f, 2.0f, 0xE2574BFFu);
}

// App hooks
static B32 app_render2d_init(APP_Context* ctx) {
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
    if (!app_render2d_ensure_text_context(ctx)) {
        return 0;
    }

    state->render2d.initialized = 1;
    app_render2d_log_once(state, AppRender2DLoadLog_Started, "Draw2d resources requested");
    return 1;
}

static void app_gfx_demo_shutdown(APP_Context* ctx) {
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
        for (U32 bufferIndex = 0u; bufferIndex < APP_RENDER2D_FRAME_BUFFER_COUNT; ++bufferIndex) {
            gfx_destroy_buffer(device, render->quadBuffers[bufferIndex]);
        }
        gfx_destroy_buffer(device, render->indexBuffer);
        gfx_destroy_sampler(device, render->atlasSampler);
        gfx_destroy_texture(device, render->atlasTexture);
        gfx_destroy_pipeline(device, render->pipeline);
    }

    *render = {};
}

static void app_gfx_demo_frame(APP_Context* ctx, F32 deltaSeconds) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    (void)deltaSeconds;

    if (!ctx->host->gfxDevice) {
        return;
    }
    if (ctx->host->windowWidth == 0u || ctx->host->windowHeight == 0u) {
        return;
    }
    if (!app_render2d_init(ctx)) {
        return;
    }

#if defined(PLATFORM_BUILD_DEBUG)
    app_gfx_try_build_dev_shaders(ctx);
    if (ctx->core->resources.fileStream) {
        file_stream_tick(ctx->core->resources.fileStream, OS_get_time_nanoseconds(), 16u);
    }
#endif
    if (ctx->core->resources.artifactCache) {
        artifact_cache_tick(ctx->core->resources.artifactCache, ctx->core->frameCounter, 16u, 16u);
    }

    GfxFrame* frame = gfx_begin_frame(ctx->host->gfxDevice);
    if (!frame) {
        return;
    }
    GfxCommandBuffer* commands = gfx_get_command_buffer(frame);

    app_render2d_try_load_font(ctx);
    app_render2d_try_update_pipeline(ctx);
    app_render2d_try_create_gpu_resources(ctx);
    app_render2d_try_seed_atlas(ctx, frame);

    F32 whiteU = 0.0f;
    F32 whiteV = 0.0f;
    text_white_uv(ctx->core->render2d.textContext, &whiteU, &whiteV);
    draw2d_begin(&ctx->core->render2d.draw2d, ctx->host->frameArena, whiteU, whiteV);
    app_demo_scene_submit(ctx, &ctx->core->render2d.draw2d, frame);
    Draw2DResult draw2dResult = draw2d_end(&ctx->core->render2d.draw2d);

    app_render2d_execute(ctx, commands, frame, draw2dResult);

    gfx_submit(commands);
    gfx_end_frame(frame);
    if (ctx->core->resources.artifactCache) {
        artifact_cache_evict(ctx->core->resources.artifactCache, ctx->core->frameCounter, 128u);
    }
}
