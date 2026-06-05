//
// Created by André Leite on 31/10/2025.
//

#include "app_interface.hpp"
#include "app_state.hpp"

#include "nstl/artifact/artifact_include.cpp"

#define APP_CORE_STATE_VERSION 26u
#define APP_GFX_TRIANGLE_SHADER_PATH "app/shaders/triangle.metal"
#define APP_GFX_DEMO_COMPUTE_SHADER_PATH "app/shaders/demo_compute.metal"
#define APP_GFX_DEMO_TEXTURE_PATH "app/textures/demo.ppm"
#define APP_GFX_DEMO_DRAW_COLUMNS 12u
#define APP_GFX_DEMO_DRAW_ROWS 8u
#define APP_GFX_DEMO_DRAW_COUNT (APP_GFX_DEMO_DRAW_COLUMNS * APP_GFX_DEMO_DRAW_ROWS)
#define APP_GFX_DEMO_MATERIAL_COUNT APP_GFX_DEMO_DRAW_COUNT
#define APP_GFX_DEMO_COMPUTE_THREADS_PER_GROUP 64u
#define APP_IMAGE_RGBA8_BYTES_PER_PIXEL 4u

struct AppGfxVertex {
    F32 position[2];
    F32 color[4];
};

struct AppGfxDrawData {
    F32 offsetScale[4];
    U32 materialIndex;
    U32 objectId;
    U32 flags;
    U32 _padding;
};

struct AppGfxMaterial {
    F32 baseColor[4];
    U32 albedoTexture;
    U32 sampler;
    U32 flags;
    U32 _padding;
};

struct AppGfxMaterialComputeData {
    U32 materialCount;
    U32 columns;
    U32 rows;
    U32 albedoTexture;
    U32 sampler;
    U32 _padding0;
    U32 _padding1;
    U32 _padding2;
};

struct AppImageRGBA8 {
    U32 width;
    U32 height;
    U8* pixels;
    U64 bytesPerRow;
};

struct AppPPMToken {
    const U8* data;
    U64 size;
};

struct AppPPMCursor {
    const U8* at;
    const U8* end;
};

enum AppResourceType {
    AppResourceType_Invalid = 0,
    AppResourceType_RawFile = 1,
};

static const APP_StateDesc* app_state_desc(APP_StateKind kind);
static void* app_state_require(APP_Context* ctx, APP_StateKind kind);
static B32 app_context_from_call(AppHost* host, HOT_StateStore* store, APP_Context* outCtx);
static void app_state_init(APP_Context* ctx, APP_StateKind kind, void* memory);
static U32 app_select_worker_count(const AppHost* host);
static B32 app_ensure_job_system(APP_Context* ctx);
static B32 app_resource_cache_init(APP_Context* ctx);
static void app_resource_cache_shutdown(APP_Context* ctx);
static B32 app_decode_ppm_rgba8(Arena* arena, const void* data, U64 size, AppImageRGBA8* outImage);
static B32 app_gfx_demo_init(APP_Context* ctx);
static void app_gfx_try_create_triangle_pipeline(APP_Context* ctx);
static void app_gfx_try_create_demo_compute_pipeline(APP_Context* ctx);
static void app_gfx_upload_demo_texture(APP_Context* ctx, GfxFrame* frame);
static B32 app_gfx_dispatch_demo_materials(APP_Context* ctx, GfxCommandBuffer* commands, GfxFrame* frame);
static void app_gfx_demo_shutdown(APP_Context* ctx);
static void app_gfx_demo_frame(APP_Context* ctx);

static B32 app_boot(AppHost* host, HOT_StateStore* store) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);

    log_init();
    set_log_level(LogLevel_Info);

    APP_Context ctx = {};
    if (!app_context_from_call(host, store, &ctx)) {
        return 0;
    }

    if (!app_gfx_demo_init(&ctx)) {
        return 0;
    }

    ASSERT_ALWAYS(host->window.handle != 0);
    return 1;
}

static void app_before_reload(AppHost* host, HOT_StateStore* store) {
    APP_Context ctx = {};
    if (app_context_from_call(host, store, &ctx)) {
        app_gfx_demo_shutdown(&ctx);
    }
}

static B32 app_after_reload(AppHost* host, HOT_StateStore* store) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);

    log_init();

    APP_Context ctx = {};
    if (!app_context_from_call(host, store, &ctx)) {
        return 0;
    }

    ctx.core->reloadCount += 1u;
    if (!ctx.core->gfxDemoInitialized && !app_gfx_demo_init(&ctx)) {
        return 0;
    }

    return 1;
}

static void app_frame(AppHost* host, HOT_StateStore* store, const AppInput* input) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);
    ASSERT_ALWAYS(input != 0);

    APP_Context ctx = {};
    if (!app_context_from_call(host, store, &ctx)) {
        return;
    }

    AppCoreState* state = ctx.core;
    state->windowWidth = host->windowWidth;
    state->windowHeight = host->windowHeight;
    state->frameCounter += 1ull;

    for (U32 eventIndex = 0; eventIndex < input->eventCount; ++eventIndex) {
        const OS_GraphicsEvent* event = input->events + eventIndex;
        ASSERT_ALWAYS(event != 0);

        if (event->tag == OS_GraphicsEvent_Tag_KeyDown &&
            event->keyDown.keyCode == OS_KeyCode_Escape &&
            !event->keyDown.isRepeat) {
            host->shouldQuit = 1;
        }
    }

    app_gfx_demo_frame(&ctx);
}

static void app_shutdown(AppHost* host, HOT_StateStore* store) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);

    APP_Context ctx = {};
    if (!app_context_from_call(host, store, &ctx)) {
        return;
    }

    app_gfx_demo_shutdown(&ctx);

    if (ctx.core->jobSystem) {
        job_system_destroy(ctx.core->jobSystem);
        ctx.core->jobSystem = 0;
        ctx.core->workerCount = 0;
    }
}

static B32 app_resource_cache_init(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->resourceCache != 0) {
        return 1;
    }

    if (!app_ensure_job_system(ctx)) {
        return 0;
    }

    state->resourceArena = arena_alloc(.arenaSize = MB(4),
                                       .committedSize = KB(64),
                                       .flags = ArenaFlags_DoChain);
    if (state->resourceArena == 0) {
        LOG_ERROR("resource", "Failed to create resource arena");
        return 0;
    }

    ArtifactCacheDesc desc = {};
    desc.structSize = sizeof(desc);
    desc.apiVersion = ARTIFACT_CACHE_API_VERSION;
    desc.arena = state->resourceArena;
    desc.jobSystem = state->jobSystem;
    desc.initialSlotCapacity = 64u;
    desc.initialHashCapacity = 128u;
    desc.budgetBytes = MB(16);
    desc.maxTypeId = AppResourceType_RawFile;

    state->resourceCache = artifact_cache_alloc(&desc);
    if (state->resourceCache == 0) {
        LOG_ERROR("resource", "Failed to create artifact cache");
        app_resource_cache_shutdown(ctx);
        return 0;
    }

    ArtifactTypeOps rawFileOps = {};
    rawFileOps.kind = ArtifactTypeKind_RawFile;
    if (!artifact_cache_register_type(state->resourceCache, AppResourceType_RawFile, &rawFileOps)) {
        LOG_ERROR("resource", "Failed to register raw file artifact type");
        app_resource_cache_shutdown(ctx);
        return 0;
    }

    return 1;
}

static void app_resource_cache_shutdown(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->resourceCache != 0) {
        artifact_cache_destroy(state->resourceCache);
        state->resourceCache = 0;
    }
    if (state->resourceArena != 0) {
        arena_release(state->resourceArena);
        state->resourceArena = 0;
    }

    state->gfxTriangleShader = ARTIFACT_HANDLE_INVALID;
    state->gfxDemoComputeShader = ARTIFACT_HANDLE_INVALID;
    state->gfxDemoTextureSource = ARTIFACT_HANDLE_INVALID;
}

static B32 app_ppm_is_space(U8 c) {
    return c == (U8)' ' ||
           c == (U8)'\n' ||
           c == (U8)'\r' ||
           c == (U8)'\t' ||
           c == (U8)'\v' ||
           c == (U8)'\f';
}

static void app_ppm_skip_space(AppPPMCursor* cursor) {
    ASSERT_ALWAYS(cursor != 0);

    for (;;) {
        while (cursor->at < cursor->end && app_ppm_is_space(*cursor->at)) {
            cursor->at += 1;
        }

        if (cursor->at < cursor->end && *cursor->at == (U8)'#') {
            while (cursor->at < cursor->end && *cursor->at != (U8)'\n') {
                cursor->at += 1;
            }
            continue;
        }

        break;
    }
}

static B32 app_ppm_read_token(AppPPMCursor* cursor, AppPPMToken* outToken) {
    ASSERT_ALWAYS(cursor != 0);
    ASSERT_ALWAYS(outToken != 0);

    app_ppm_skip_space(cursor);
    if (cursor->at >= cursor->end) {
        return 0;
    }

    const U8* start = cursor->at;
    while (cursor->at < cursor->end &&
           !app_ppm_is_space(*cursor->at) &&
           *cursor->at != (U8)'#') {
        cursor->at += 1;
    }

    outToken->data = start;
    outToken->size = (U64)(cursor->at - start);
    return outToken->size != 0u;
}

static B32 app_ppm_token_is(AppPPMToken token, const char* text) {
    ASSERT_ALWAYS(text != 0);

    U64 index = 0u;
    while (text[index] != 0) {
        if (index >= token.size || token.data[index] != (U8)text[index]) {
            return 0;
        }
        index += 1;
    }

    return index == token.size;
}

static B32 app_ppm_read_u32(AppPPMCursor* cursor, U32* outValue) {
    ASSERT_ALWAYS(cursor != 0);
    ASSERT_ALWAYS(outValue != 0);

    AppPPMToken token = {};
    if (!app_ppm_read_token(cursor, &token)) {
        return 0;
    }

    U64 value = 0u;
    for (U64 i = 0u; i < token.size; ++i) {
        U8 c = token.data[i];
        if (c < (U8)'0' || c > (U8)'9') {
            return 0;
        }

        value = value * 10u + (U64)(c - (U8)'0');
        if (value > 0xFFFFFFFFu) {
            return 0;
        }
    }

    *outValue = (U32)value;
    return 1;
}

static B32 app_decode_ppm_rgba8(Arena* arena, const void* data, U64 size, AppImageRGBA8* outImage) {
    if (outImage != 0) {
        *outImage = {};
    }
    if (arena == 0 || data == 0 || size == 0u || outImage == 0) {
        return 0;
    }

    AppPPMCursor cursor = {};
    cursor.at = (const U8*)data;
    cursor.end = cursor.at + size;

    AppPPMToken magic = {};
    U32 width = 0u;
    U32 height = 0u;
    U32 maxValue = 0u;
    if (!app_ppm_read_token(&cursor, &magic) ||
        !app_ppm_token_is(magic, "P3") ||
        !app_ppm_read_u32(&cursor, &width) ||
        !app_ppm_read_u32(&cursor, &height) ||
        !app_ppm_read_u32(&cursor, &maxValue) ||
        width == 0u ||
        height == 0u ||
        maxValue == 0u ||
        maxValue > 255u) {
        return 0;
    }

    if ((U64)width > ((U64)-1) / APP_IMAGE_RGBA8_BYTES_PER_PIXEL) {
        return 0;
    }

    U64 tightRowBytes = (U64)width * APP_IMAGE_RGBA8_BYTES_PER_PIXEL;
    U64 bytesPerRow = align_pow2(tightRowBytes, GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT);
    if ((U64)height > ((U64)-1) / bytesPerRow) {
        return 0;
    }

    U64 pixelBytes = bytesPerRow * height;
    U8* pixels = ARENA_PUSH_ARRAY(arena, U8, pixelBytes);
    MEMSET(pixels, 0, pixelBytes);

    for (U32 y = 0u; y < height; ++y) {
        for (U32 x = 0u; x < width; ++x) {
            U32 r = 0u;
            U32 g = 0u;
            U32 b = 0u;
            if (!app_ppm_read_u32(&cursor, &r) ||
                !app_ppm_read_u32(&cursor, &g) ||
                !app_ppm_read_u32(&cursor, &b) ||
                r > maxValue ||
                g > maxValue ||
                b > maxValue) {
                return 0;
            }

            U64 offset = (U64)y * bytesPerRow + (U64)x * APP_IMAGE_RGBA8_BYTES_PER_PIXEL;
            pixels[offset + 0u] = (U8)((r * 255u + maxValue / 2u) / maxValue);
            pixels[offset + 1u] = (U8)((g * 255u + maxValue / 2u) / maxValue);
            pixels[offset + 2u] = (U8)((b * 255u + maxValue / 2u) / maxValue);
            pixels[offset + 3u] = 255u;
        }
    }

    outImage->width = width;
    outImage->height = height;
    outImage->pixels = pixels;
    outImage->bytesPerRow = bytesPerRow;
    return 1;
}

static B32 app_gfx_demo_init(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->gfxDemoInitialized) {
        return 1;
    }
    if (!ctx->host->gfxDevice) {
        LOG_ERROR("gfx", "App has no gfx device");
        return 0;
    }
    if (!app_resource_cache_init(ctx)) {
        return 0;
    }

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena != 0) {
        ArtifactUseScope scope = {};
        if (artifact_use_scope_open(state->resourceCache, scratch.arena, &scope)) {
            StringU8 exeDir = OS_get_executable_directory(scratch.arena);
            StringU8 shaderPath = str8_concat(scratch.arena, exeDir, str8("/../" APP_GFX_TRIANGLE_SHADER_PATH));
            state->gfxTriangleShader = artifact_acquire(&scope,
                                                        AppResourceType_RawFile,
                                                        shaderPath,
                                                        ArtifactAcquireFlags_Async);
            StringU8 computePath = str8_concat(scratch.arena, exeDir, str8("/../" APP_GFX_DEMO_COMPUTE_SHADER_PATH));
            state->gfxDemoComputeShader = artifact_acquire(&scope,
                                                           AppResourceType_RawFile,
                                                           computePath,
                                                           ArtifactAcquireFlags_Async);
            StringU8 texturePath = str8_concat(scratch.arena, exeDir, str8("/../" APP_GFX_DEMO_TEXTURE_PATH));
            state->gfxDemoTextureSource = artifact_acquire(&scope,
                                                           AppResourceType_RawFile,
                                                           texturePath,
                                                           ArtifactAcquireFlags_Async);
            artifact_use_scope_close(&scope);
        }
        temp_end(&scratch);
    }

    static const AppGfxVertex vertices[] = {
        {{ 0.0f,  0.55f}, {1.0f, 1.0f, 1.0f, 1.0f}},
        {{-0.55f, -0.45f}, {1.0f, 1.0f, 1.0f, 1.0f}},
        {{ 0.55f, -0.45f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    };

    static const U16 indices[] = {
        0u, 1u, 2u,
    };

    GfxBufferDesc vertexDesc = {};
    vertexDesc.name = "triangle vertices";
    vertexDesc.size = sizeof(vertices);
    vertexDesc.usageFlags = GfxBufferUsageFlags_Vertex;
    vertexDesc.memoryKind = GfxMemoryKind_Device;
    vertexDesc.initialData = vertices;
    state->gfxTriangleVertexBuffer = gfx_create_buffer(ctx->host->gfxDevice, &vertexDesc);

    GfxBufferDesc indexDesc = {};
    indexDesc.name = "triangle indices";
    indexDesc.size = sizeof(indices);
    indexDesc.usageFlags = GfxBufferUsageFlags_Index;
    indexDesc.memoryKind = GfxMemoryKind_Device;
    indexDesc.initialData = indices;
    state->gfxTriangleIndexBuffer = gfx_create_buffer(ctx->host->gfxDevice, &indexDesc);

    state->gfxDemoMaterialCount = APP_GFX_DEMO_MATERIAL_COUNT;
    GfxBufferDesc materialDesc = {};
    materialDesc.name = "demo materials";
    materialDesc.size = sizeof(AppGfxMaterial) * state->gfxDemoMaterialCount;
    materialDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    materialDesc.memoryKind = GfxMemoryKind_Device;
    state->gfxDemoMaterialBuffer = gfx_create_buffer(ctx->host->gfxDevice, &materialDesc);

    app_gfx_try_create_triangle_pipeline(ctx);

    state->gfxDemoInitialized = 1;
    return 1;
}

static void app_gfx_try_create_triangle_pipeline(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->resourceCache == 0 || ctx->host->gfxDevice == 0) {
        return;
    }
    if (state->gfxTrianglePipeline.index != 0u || state->gfxTrianglePipeline.generation != 0u) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena == 0) {
        return;
    }

    ArtifactUseScope scope = {};
    if (!artifact_use_scope_open(state->resourceCache, scratch.arena, &scope)) {
        temp_end(&scratch);
        return;
    }

    ArtifactView shaderView = artifact_resolve_view(&scope, state->gfxTriangleShader);
    if (shaderView.data != 0 && shaderView.size != 0u) {
        GfxVertexAttribute attributes[2] = {};
        attributes[0].location = 0u;
        attributes[0].offset = 0u;
        attributes[0].format = GfxVertexFormat_F32x2;
        attributes[1].location = 1u;
        attributes[1].offset = sizeof(F32) * 2u;
        attributes[1].format = GfxVertexFormat_F32x4;

        GfxFormat colorFormats[1] = {
            GfxFormat_BGRA8_UNorm,
        };

        GfxGraphicsPipelineDesc pipelineDesc = {};
        pipelineDesc.name = "triangle pipeline";
        pipelineDesc.vertexShader.format = GfxShaderFormat_MSL_Source;
        pipelineDesc.vertexShader.data = shaderView.data;
        pipelineDesc.vertexShader.size = shaderView.size;
        pipelineDesc.vertexShader.entry = "vertex_main";
        pipelineDesc.fragmentShader = pipelineDesc.vertexShader;
        pipelineDesc.fragmentShader.entry = "fragment_main";
        pipelineDesc.attributes = attributes;
        pipelineDesc.attributeCount = ARRAY_COUNT(attributes);
        pipelineDesc.vertexBuffer.stride = sizeof(AppGfxVertex);
        pipelineDesc.topology = GfxPrimitiveTopology_TriangleList;
        pipelineDesc.raster.cullMode = GfxCullMode_None;
        pipelineDesc.raster.frontFace = GfxFrontFace_CCW;
        pipelineDesc.depth.compareOp = GfxCompareOp_Always;
        pipelineDesc.colorFormats = colorFormats;
        pipelineDesc.colorFormatCount = ARRAY_COUNT(colorFormats);
        pipelineDesc.depthFormat = GfxFormat_Invalid;

        state->gfxTrianglePipeline = gfx_create_graphics_pipeline(ctx->host->gfxDevice, &pipelineDesc);
    }

    artifact_use_scope_close(&scope);
    temp_end(&scratch);
}

static void app_gfx_try_create_demo_compute_pipeline(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->resourceCache == 0 || ctx->host->gfxDevice == 0) {
        return;
    }
    if (state->gfxDemoComputePipeline.index != 0u || state->gfxDemoComputePipeline.generation != 0u) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena == 0) {
        return;
    }

    ArtifactUseScope scope = {};
    if (!artifact_use_scope_open(state->resourceCache, scratch.arena, &scope)) {
        temp_end(&scratch);
        return;
    }

    ArtifactView shaderView = artifact_resolve_view(&scope, state->gfxDemoComputeShader);
    if (shaderView.data != 0 && shaderView.size != 0u) {
        GfxComputePipelineDesc pipelineDesc = {};
        pipelineDesc.name = "demo material compute pipeline";
        pipelineDesc.shader.format = GfxShaderFormat_MSL_Source;
        pipelineDesc.shader.data = shaderView.data;
        pipelineDesc.shader.size = shaderView.size;
        pipelineDesc.shader.entry = "material_main";
        pipelineDesc.threadsPerThreadgroupX = APP_GFX_DEMO_COMPUTE_THREADS_PER_GROUP;
        pipelineDesc.threadsPerThreadgroupY = 1u;
        pipelineDesc.threadsPerThreadgroupZ = 1u;

        state->gfxDemoComputePipeline = gfx_create_compute_pipeline(ctx->host->gfxDevice, &pipelineDesc);
    }

    artifact_use_scope_close(&scope);
    temp_end(&scratch);
}

static void app_gfx_upload_demo_texture(APP_Context* ctx, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->gfxDemoTextureUploaded ||
        state->resourceCache == 0 ||
        ctx->host->gfxDevice == 0 ||
        frame == 0) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena == 0) {
        return;
    }

    ArtifactUseScope scope = {};
    if (!artifact_use_scope_open(state->resourceCache, scratch.arena, &scope)) {
        temp_end(&scratch);
        return;
    }

    ArtifactView textureView = artifact_resolve_view(&scope, state->gfxDemoTextureSource);
    AppImageRGBA8 image = {};
    if (app_decode_ppm_rgba8(scratch.arena, textureView.data, textureView.size, &image)) {
        if (state->gfxDemoTextureId.index == 0u) {
            GfxTextureDesc textureDesc = {};
            textureDesc.name = "demo texture";
            textureDesc.width = image.width;
            textureDesc.height = image.height;
            textureDesc.mipCount = 1u;
            textureDesc.format = GfxFormat_RGBA8_UNorm;
            textureDesc.usageFlags = GfxTextureUsageFlags_Sampled | GfxTextureUsageFlags_CopyDst;
            state->gfxDemoTexture = gfx_create_texture(ctx->host->gfxDevice, &textureDesc);
            state->gfxDemoTextureId = gfx_register_texture(ctx->host->gfxDevice, state->gfxDemoTexture);
        }

        if (state->gfxDemoSamplerId.index == 0u) {
            GfxSamplerDesc samplerDesc = {};
            samplerDesc.name = "demo sampler";
            samplerDesc.minFilter = GfxFilter_Nearest;
            samplerDesc.magFilter = GfxFilter_Nearest;
            samplerDesc.addressU = GfxAddressMode_Repeat;
            samplerDesc.addressV = GfxAddressMode_Repeat;
            state->gfxDemoSampler = gfx_create_sampler(ctx->host->gfxDevice, &samplerDesc);
            state->gfxDemoSamplerId = gfx_register_sampler(ctx->host->gfxDevice, state->gfxDemoSampler);
        }

        if (state->gfxDemoTextureId.index != 0u &&
            state->gfxDemoSamplerId.index != 0u) {
            GfxTextureUploadRegion region = {};
            region.layerCount = 1u;
            region.width = image.width;
            region.height = image.height;
            region.depth = 1u;
            region.bytesPerRow = image.bytesPerRow;
            region.rowsPerImage = image.height;

            gfx_upload_texture(frame, state->gfxDemoTexture, &region, image.pixels);
            state->gfxDemoTextureUploaded = 1;
        }
    }

    artifact_use_scope_close(&scope);
    temp_end(&scratch);
}

static B32 app_gfx_dispatch_demo_materials(APP_Context* ctx, GfxCommandBuffer* commands, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    state->gfxDemoMaterialsUploaded = 0;
    if (!state->gfxDemoTextureUploaded ||
        state->gfxDemoTextureId.index == 0u ||
        state->gfxDemoSamplerId.index == 0u ||
        state->gfxDemoMaterialCount == 0u ||
        state->gfxDemoMaterialBuffer.generation == 0u ||
        state->gfxDemoComputePipeline.generation == 0u ||
        commands == 0 ||
        frame == 0) {
        return 0;
    }

    GfxTemp dispatchTemp = gfx_allocate_temp(frame, sizeof(AppGfxMaterialComputeData), 16u);
    if (dispatchTemp.cpu == 0) {
        return 0;
    }

    AppGfxMaterialComputeData* data = (AppGfxMaterialComputeData*)dispatchTemp.cpu;
    data->materialCount = state->gfxDemoMaterialCount;
    data->columns = APP_GFX_DEMO_DRAW_COLUMNS;
    data->rows = APP_GFX_DEMO_DRAW_ROWS;
    data->albedoTexture = state->gfxDemoTextureId.index;
    data->sampler = state->gfxDemoSamplerId.index;
    data->_padding0 = 0u;
    data->_padding1 = 0u;
    data->_padding2 = 0u;

    GfxComputePassDesc pass = {};
    pass.name = "demo material compute pass";
    pass.passData.buffer = state->gfxDemoMaterialBuffer;
    pass.passData.size = sizeof(AppGfxMaterial) * state->gfxDemoMaterialCount;

    GfxDispatch dispatch = {};
    dispatch.pipeline = state->gfxDemoComputePipeline;
    dispatch.dispatchData = dispatchTemp.gpu;
    dispatch.groupsX = (state->gfxDemoMaterialCount + APP_GFX_DEMO_COMPUTE_THREADS_PER_GROUP - 1u) /
                       APP_GFX_DEMO_COMPUTE_THREADS_PER_GROUP;
    dispatch.groupsY = 1u;
    dispatch.groupsZ = 1u;

    gfx_compute_pass(commands, &pass, &dispatch, 1u);
    state->gfxDemoMaterialsUploaded = 1;
    return 1;
}

static void app_gfx_demo_shutdown(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    GfxDevice* device = ctx->host ? ctx->host->gfxDevice : 0;
    if (device != 0) {
        gfx_wait_idle(device);
        gfx_destroy_pipeline(device, state->gfxDemoComputePipeline);
        gfx_destroy_pipeline(device, state->gfxTrianglePipeline);
        gfx_destroy_buffer(device, state->gfxTriangleIndexBuffer);
        gfx_destroy_buffer(device, state->gfxTriangleVertexBuffer);
        gfx_destroy_buffer(device, state->gfxDemoMaterialBuffer);
        gfx_destroy_sampler(device, state->gfxDemoSampler);
        gfx_destroy_texture(device, state->gfxDemoTexture);
    }

    app_resource_cache_shutdown(ctx);

    state->gfxDemoComputePipeline = {};
    state->gfxTrianglePipeline = {};
    state->gfxTriangleIndexBuffer = {};
    state->gfxTriangleVertexBuffer = {};
    state->gfxDemoMaterialBuffer = {};
    state->gfxDemoTexture = {};
    state->gfxDemoSampler = {};
    state->gfxDemoTextureId = {};
    state->gfxDemoSamplerId = {};
    state->gfxDemoMaterialCount = 0u;
    state->gfxDemoTextureUploaded = 0;
    state->gfxDemoMaterialsUploaded = 0;
    state->gfxDemoInitialized = 0;
}

static void app_gfx_demo_frame(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    if (!ctx->host->gfxDevice || !ctx->core->gfxDemoInitialized) {
        return;
    }
    if (ctx->host->windowWidth == 0u || ctx->host->windowHeight == 0u) {
        return;
    }

    app_gfx_try_create_triangle_pipeline(ctx);
    app_gfx_try_create_demo_compute_pipeline(ctx);

    GfxFrame* frame = gfx_begin_frame(ctx->host->gfxDevice);
    if (!frame) {
        return;
    }

    GfxCommandBuffer* commands = gfx_get_command_buffer(frame);
    GfxTexture backbuffer = gfx_get_backbuffer(frame);
    app_gfx_upload_demo_texture(ctx, frame);
    B32 materialsReady = app_gfx_dispatch_demo_materials(ctx, commands, frame);

    GfxTemp drawTemp = gfx_allocate_temp(frame, sizeof(AppGfxDrawData) * APP_GFX_DEMO_DRAW_COUNT, 16u);

    Temp scratch = get_scratch(0, 0);
    GfxDraw* draws = 0;
    U32 drawCount = 0u;
    if (scratch.arena != 0) {
        draws = ARENA_PUSH_ARRAY(scratch.arena, GfxDraw, APP_GFX_DEMO_DRAW_COUNT);
        drawCount = APP_GFX_DEMO_DRAW_COUNT;
    }

    F32 cellWidth = 2.0f / (F32)APP_GFX_DEMO_DRAW_COLUMNS;
    F32 cellHeight = 2.0f / (F32)APP_GFX_DEMO_DRAW_ROWS;
    F32 scale = MIN(cellWidth, cellHeight) * 0.72f;
    B32 resourcesReady = ctx->core->gfxDemoTextureUploaded &&
                         ctx->core->gfxDemoTextureId.index != 0u &&
                         ctx->core->gfxDemoSamplerId.index != 0u &&
                         materialsReady;
    AppGfxDrawData* drawDataBase = resourcesReady ? (AppGfxDrawData*)drawTemp.cpu : 0;

    for (U32 row = 0u; row < APP_GFX_DEMO_DRAW_ROWS; ++row) {
        for (U32 column = 0u; column < APP_GFX_DEMO_DRAW_COLUMNS; ++column) {
            U32 drawIndex = row * APP_GFX_DEMO_DRAW_COLUMNS + column;
            U64 drawDataOffset = sizeof(AppGfxDrawData) * drawIndex;

            GfxGpuSlice drawDataSlice = {};
            if (drawDataBase != 0) {
                AppGfxDrawData* drawData = drawDataBase + drawIndex;
                drawData->offsetScale[0] = -1.0f + ((F32)column + 0.5f) * cellWidth;
                drawData->offsetScale[1] = -1.0f + ((F32)row + 0.5f) * cellHeight;
                drawData->offsetScale[2] = scale;
                drawData->offsetScale[3] = 1.0f;
                drawData->materialIndex = drawIndex;
                drawData->objectId = drawIndex;
                drawData->flags = 1u;
                drawData->_padding = 0u;

                drawDataSlice = drawTemp.gpu;
                drawDataSlice.offset += drawDataOffset;
                drawDataSlice.size = sizeof(AppGfxDrawData);
            }

            if (draws != 0) {
                GfxDraw* draw = draws + drawIndex;
                *draw = {};
                draw->pipeline = ctx->core->gfxTrianglePipeline;
                draw->vertexBuffer = ctx->core->gfxTriangleVertexBuffer;
                draw->indexBuffer = ctx->core->gfxTriangleIndexBuffer;
                draw->indexCount = 3u;
                draw->instanceCount = 1u;
                draw->indexType = GfxIndexType_U16;
                draw->drawData = drawDataSlice;
            }
        }
    }

    GfxColorTarget colorTarget = {};
    colorTarget.texture = backbuffer;
    colorTarget.loadOp = GfxLoadOp_Clear;
    colorTarget.storeOp = GfxStoreOp_Store;
    colorTarget.clearColor[0] = 0.06f;
    colorTarget.clearColor[1] = 0.08f;
    colorTarget.clearColor[2] = 0.10f;
    colorTarget.clearColor[3] = 1.0f;

    GfxRenderPassDesc pass = {};
    pass.name = "triangle pass";
    pass.colorTargets = &colorTarget;
    pass.colorTargetCount = 1u;
    pass.width = ctx->host->windowWidth;
    pass.height = ctx->host->windowHeight;
    if (materialsReady) {
        pass.passData.buffer = ctx->core->gfxDemoMaterialBuffer;
        pass.passData.size = sizeof(AppGfxMaterial) * ctx->core->gfxDemoMaterialCount;
    }

    GfxDrawArea area = {};
    area.viewport.x = 0.0f;
    area.viewport.y = 0.0f;
    area.viewport.width = (F32)ctx->host->windowWidth;
    area.viewport.height = (F32)ctx->host->windowHeight;
    area.viewport.minDepth = 0.0f;
    area.viewport.maxDepth = 1.0f;
    area.scissor.x = 0;
    area.scissor.y = 0;
    area.scissor.width = ctx->host->windowWidth;
    area.scissor.height = ctx->host->windowHeight;
    area.draws = draws;
    area.drawCount = drawCount;

    gfx_render_pass(commands, &pass, &area, 1u);
    temp_end(&scratch);
    gfx_submit(commands);
    gfx_end_frame(frame);
}

static const APP_StateDesc* app_state_desc(APP_StateKind kind) {
    static const APP_StateDesc descs[APP_State_COUNT] = {
        {APP_STATE_ID('C', 'O', 'R', 'E'), "core", APP_CORE_STATE_VERSION,
         sizeof(AppCoreState), alignof(AppCoreState)},
    };

    if ((U32) kind >= APP_State_COUNT) {
        return 0;
    }
    return &descs[(U32) kind];
}

static void* app_state_require(APP_Context* ctx, APP_StateKind kind) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->store != 0);

    const APP_StateDesc* desc = app_state_desc(kind);
    if (desc == 0) {
        LOG_ERROR("app", "Invalid state kind {}", (U32) kind);
        return 0;
    }

    void* memory = hot_state_store_require(ctx->store, desc->id, desc->version, desc->size, desc->alignment);
    if (memory == 0) {
        LOG_ERROR("app", "State '{}' unavailable (size={} align={})", str8((const char*)desc->name),
                  desc->size, desc->alignment);
        return 0;
    }

    if (hot_state_store_take_needs_init(ctx->store, desc->id)) {
        app_state_init(ctx, kind, memory);
    }

    return memory;
}

static B32 app_context_from_call(AppHost* host, HOT_StateStore* store, APP_Context* outCtx) {
    ASSERT_ALWAYS(outCtx != 0);
    MEMSET(outCtx, 0, sizeof(*outCtx));

    if (host == 0 || store == 0 || !hot_state_store_is_valid(store)) {
        LOG_ERROR("app", "Invalid app call context");
        return 0;
    }

    APP_Context ctx = {};
    ctx.host = host;
    ctx.store = store;

    ctx.core = (AppCoreState*) app_state_require(&ctx, APP_State_Core);
    if (ctx.core == 0) {
        return 0;
    }

    *outCtx = ctx;
    return 1;
}

static void app_state_init(APP_Context* ctx, APP_StateKind kind, void* memory) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(memory != 0);

    switch (kind) {
        case APP_State_Core: {
            AppCoreState* core = (AppCoreState*) memory;
            MEMSET(core, 0, sizeof(*core));
            core->windowWidth = ctx->host->windowWidth;
            core->windowHeight = ctx->host->windowHeight;

            StringU8 eventsDomain = str8((const char*) "events", 6);
            set_log_domain_level(eventsDomain, LogLevel_Debug);
        }
        break;

        default: {
            ASSERT_ALWAYS(false && "Invalid app state kind");
        }
        break;
    }
}

static U32 app_select_worker_count(const AppHost* host) {
    ASSERT_ALWAYS(host != 0);
    U32 logicalCores = host->logicalCoreCount;
    if (logicalCores == 0u) {
        ASSERT_ALWAYS(logicalCores != 0u);
        logicalCores = 1u;
    }
    U32 workers = (logicalCores > 1u) ? (logicalCores - 1u) : 1u;
    return workers;
}

static B32 app_ensure_job_system(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    ASSERT_ALWAYS(ctx->host->stateArena != 0);

    AppCoreState* state = ctx->core;
    if (state->jobSystem) {
        return 1;
    }

    state->workerCount = app_select_worker_count(ctx->host);
    state->jobSystem = job_system_create(ctx->host->stateArena, state->workerCount);
    if (!state->jobSystem) {
        LOG_ERROR("jobs", "Failed to create job system (workers={})", state->workerCount);
        ASSERT_ALWAYS(state->jobSystem != 0);
        return 0;
    }

    LOG_INFO("jobs", "Job system ready (workers={})", state->workerCount);
    return 1;
}

APP_EXPORT B32 app_load(AppLoadParams* params, AppCode* outCode) {
    ASSERT_ALWAYS(params != 0);
    ASSERT_ALWAYS(outCode != 0);

    if (params == 0 || outCode == 0) {
        return 0;
    }
    if (params->size != sizeof(AppLoadParams) || params->abiVersion != APP_ABI_VERSION) {
        return 0;
    }
    if (params->host == 0 || params->store == 0) {
        return 0;
    }

    MEMSET(outCode, 0, sizeof(*outCode));
    outCode->size = sizeof(*outCode);
    outCode->abiVersion = APP_ABI_VERSION;
    outCode->schemaVersion = APP_STATE_SCHEMA_VERSION;
    outCode->boot = app_boot;
    outCode->before_reload = app_before_reload;
    outCode->after_reload = app_after_reload;
    outCode->frame = app_frame;
    outCode->shutdown = app_shutdown;
    return 1;
}
