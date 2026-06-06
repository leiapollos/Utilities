//
// Created by André Leite on 31/10/2025.
//

#include "app_interface.hpp"
#include "app_state.hpp"
#include "app/shaders/shader_manifest.h"

#include "nstl/content/content_include.cpp"
#include "nstl/file_stream/file_stream_include.cpp"
#include "nstl/artifact/artifact_include.cpp"

#define APP_CORE_STATE_VERSION 32u
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

static const AppGfxVertex APP_GFX_DEMO_VERTICES[] = {
    {{ 0.0f,  0.55f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{-0.55f, -0.45f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{ 0.55f, -0.45f}, {1.0f, 1.0f, 1.0f, 1.0f}},
};

static const U16 APP_GFX_DEMO_INDICES[] = {
    0u, 1u, 2u,
};

#if defined(PLATFORM_BUILD_DEBUG)
#define APP_GFX_DEV_SHADER_SOURCE_ENTRY(name, source) source,
static const char* APP_GFX_DEV_SHADER_SOURCES[] = {
    APP_SHADER_MANIFEST_SOURCE,
    APP_SHADER_SOURCE_LIST(APP_GFX_DEV_SHADER_SOURCE_ENTRY)
};
#undef APP_GFX_DEV_SHADER_SOURCE_ENTRY
#endif

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

static_assert(sizeof(AppGfxDrawData) == 32u, "DrawData shader ABI mismatch");
static_assert(sizeof(AppGfxMaterial) == 32u, "Material shader ABI mismatch");
static_assert(sizeof(AppGfxMaterialComputeData) == 32u, "MaterialComputeData shader ABI mismatch");
#define APP_SHADER_ABI_OFFSET(type, member, byteOffset) \
    static_assert(offsetof(type, member) == (byteOffset), #type "." #member " shader ABI offset mismatch")
APP_SHADER_ABI_OFFSET(AppGfxDrawData, offsetScale, 0u);
APP_SHADER_ABI_OFFSET(AppGfxDrawData, materialIndex, 16u);
APP_SHADER_ABI_OFFSET(AppGfxDrawData, objectId, 20u);
APP_SHADER_ABI_OFFSET(AppGfxDrawData, flags, 24u);
APP_SHADER_ABI_OFFSET(AppGfxDrawData, _padding, 28u);
APP_SHADER_ABI_OFFSET(AppGfxMaterial, baseColor, 0u);
APP_SHADER_ABI_OFFSET(AppGfxMaterial, albedoTexture, 16u);
APP_SHADER_ABI_OFFSET(AppGfxMaterial, sampler, 20u);
APP_SHADER_ABI_OFFSET(AppGfxMaterial, flags, 24u);
APP_SHADER_ABI_OFFSET(AppGfxMaterial, _padding, 28u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeData, materialCount, 0u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeData, columns, 4u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeData, rows, 8u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeData, albedoTexture, 12u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeData, sampler, 16u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeData, _padding0, 20u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeData, _padding1, 24u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeData, _padding2, 28u);
#undef APP_SHADER_ABI_OFFSET

struct AppImageRGBA8 {
    U32 width;
    U32 height;
    U8* pixels;
    U64 bytesPerRow;
};

struct AppDecodedImageHeader {
    U32 width;
    U32 height;
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

enum AppGfxDemoLoadLog {
    AppGfxDemoLoadLog_Started = (1u << 0u),
    AppGfxDemoLoadLog_GeometryCreated = (1u << 1u),
    AppGfxDemoLoadLog_GeometryUploaded = (1u << 2u),
    AppGfxDemoLoadLog_TrianglePipeline = (1u << 3u),
    AppGfxDemoLoadLog_ComputePipeline = (1u << 4u),
    AppGfxDemoLoadLog_TextureUploaded = (1u << 5u),
    AppGfxDemoLoadLog_Ready = (1u << 6u),
};

enum AppArtifactTypeId {
    AppArtifactTypeId_TrianglePipeline = 1u,
    AppArtifactTypeId_ComputePipeline = 2u,
    AppArtifactTypeId_DecodedTexture = 3u,
};

static const APP_StateDesc* app_state_desc(APP_StateKind kind);
static void* app_state_require(APP_Context* ctx, APP_StateKind kind);
static B32 app_context_from_call(AppHost* host, HOT_StateStore* store, APP_Context* outCtx);
static void app_state_init(APP_Context* ctx, APP_StateKind kind, void* memory);
static U32 app_select_worker_count(const AppHost* host);
static B32 app_ensure_job_system(APP_Context* ctx);
static B32 app_resource_cache_init(APP_Context* ctx);
static void app_resource_cache_shutdown(APP_Context* ctx);
static B32 app_bind_current_module(APP_Context* ctx);
static B32 app_register_artifact_types(APP_Context* ctx);
static void app_watch_demo_files(APP_Context* ctx);
static B32 app_decode_ppm_rgba8(Arena* arena, const void* data, U64 size, AppImageRGBA8* outImage);
static ArtifactKey app_artifact_key_from_label(const char* label);
static ArtifactKey app_artifact_key_from_content(const char* label, ContentHash hash);
static GfxPipeline app_gfx_pipeline_from_value(ArtifactValue value);
static ArtifactValue app_gfx_pipeline_to_value(GfxDevice* device, GfxPipeline pipeline);
static B32 app_build_triangle_pipeline_artifact(ArtifactBuildContext* artifactCtx, ArtifactValue* outValue, U64* outBytes);
static B32 app_publish_triangle_pipeline_artifact(ArtifactPublishContext* artifactCtx,
                                                 ArtifactValue buildValue,
                                                 ArtifactValue* outValue,
                                                 U64* outBytes);
static B32 app_build_compute_pipeline_artifact(ArtifactBuildContext* artifactCtx, ArtifactValue* outValue, U64* outBytes);
static B32 app_publish_compute_pipeline_artifact(ArtifactPublishContext* artifactCtx,
                                                ArtifactValue buildValue,
                                                ArtifactValue* outValue,
                                                U64* outBytes);
static void app_destroy_pipeline_artifact(void* userData, ArtifactValue value);
static B32 app_build_decoded_texture_artifact(ArtifactBuildContext* artifactCtx, ArtifactValue* outValue, U64* outBytes);
static B32 app_publish_decoded_texture_artifact(ArtifactPublishContext* artifactCtx,
                                               ArtifactValue buildValue,
                                               ArtifactValue* outValue,
                                               U64* outBytes);
static void app_destroy_decoded_texture_artifact(void* userData, ArtifactValue value);
#if defined(PLATFORM_BUILD_DEBUG)
static U64 app_gfx_newest_shader_source_timestamp(void);
static void app_gfx_try_build_dev_shaders(APP_Context* ctx);
#endif
static B32 app_gfx_demo_init(APP_Context* ctx);
static void app_gfx_demo_log_once(AppCoreState* state, U32 bit, const char* message);
static void app_gfx_try_create_demo_buffers(APP_Context* ctx);
static void app_gfx_upload_demo_geometry(APP_Context* ctx, GfxFrame* frame);
static void app_gfx_try_update_triangle_pipeline(APP_Context* ctx);
static void app_gfx_try_update_demo_compute_pipeline(APP_Context* ctx);
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
    if (!app_bind_current_module(&ctx)) {
        return 0;
    }

    ASSERT_ALWAYS(host->window.handle != 0);
    return 1;
}

static void app_before_reload(AppHost* host, HOT_StateStore* store) {
    (void)host;
    (void)store;
}

static B32 app_after_reload(AppHost* host, HOT_StateStore* store) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);

    log_init();
    set_log_level(LogLevel_Info);

    APP_Context ctx = {};
    if (!app_context_from_call(host, store, &ctx)) {
        return 0;
    }
    if (!app_bind_current_module(&ctx)) {
        return 0;
    }

    ctx.core->reloadCount += 1u;
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
    if (state->contentStore != 0 && state->fileStream != 0 && state->artifactCache != 0) {
        return 1;
    }

    if (!app_ensure_job_system(ctx)) {
        return 0;
    }

    state->resourceArena = arena_alloc(.arenaSize = MB(16),
                                       .committedSize = KB(64),
                                       .flags = ArenaFlags_DoChain);
    if (state->resourceArena == 0) {
        LOG_ERROR("resource", "Failed to create resource arena");
        return 0;
    }

    ContentStoreDesc contentDesc = {};
    contentDesc.arena = state->resourceArena;
    contentDesc.initialBlobCapacity = 128u;
    contentDesc.initialKeyCapacity = 64u;
    state->contentStore = content_store_alloc(&contentDesc);
    if (state->contentStore == 0) {
        LOG_ERROR("resource", "Failed to create content store");
        app_resource_cache_shutdown(ctx);
        return 0;
    }

    FileStreamDesc fileDesc = {};
    fileDesc.arena = state->resourceArena;
    fileDesc.content = state->contentStore;
    fileDesc.initialFileCapacity = 16u;
    state->fileStream = file_stream_alloc(&fileDesc);
    if (state->fileStream == 0) {
        LOG_ERROR("resource", "Failed to create file stream");
        app_resource_cache_shutdown(ctx);
        return 0;
    }

    ArtifactCacheDesc artifactDesc = {};
    artifactDesc.arena = state->resourceArena;
    artifactDesc.jobSystem = state->jobSystem;
    artifactDesc.content = state->contentStore;
    artifactDesc.initialSlotCapacity = 128u;
    artifactDesc.initialTableCapacity = 256u;
    artifactDesc.initialTypeCapacity = 8u;
    artifactDesc.requestDataSize = 128u;
    state->artifactCache = artifact_cache_alloc(&artifactDesc);
    if (state->artifactCache == 0) {
        LOG_ERROR("resource", "Failed to create artifact cache");
        app_resource_cache_shutdown(ctx);
        return 0;
    }

    if (!app_register_artifact_types(ctx)) {
        LOG_ERROR("resource", "Failed to register artifact types");
        app_resource_cache_shutdown(ctx);
        return 0;
    }

    app_watch_demo_files(ctx);
    return 1;
}

static void app_resource_cache_shutdown(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->artifactCache != 0) {
        artifact_cache_destroy(state->artifactCache);
        state->artifactCache = 0;
    }
    if (state->fileStream != 0) {
        file_stream_destroy(state->fileStream);
        state->fileStream = 0;
    }
    if (state->contentStore != 0) {
        content_store_destroy(state->contentStore);
        state->contentStore = 0;
    }
    if (state->resourceArena != 0) {
        arena_release(state->resourceArena);
        state->resourceArena = 0;
    }

    state->gfxTriangleVertexShader = FILE_HANDLE_ZERO;
    state->gfxTriangleFragmentShader = FILE_HANDLE_ZERO;
    state->gfxDemoComputeShader = FILE_HANDLE_ZERO;
    state->gfxDemoTextureSource = FILE_HANDLE_ZERO;
    state->gfxTrianglePipelineArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemoComputePipelineArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemoTextureDecodeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemoDecodedTextureHash = CONTENT_HASH_ZERO;
}

static B32 app_bind_current_module(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->artifactCache && !app_register_artifact_types(ctx)) {
        return 0;
    }
    if (state->fileStream) {
        app_watch_demo_files(ctx);
    }
    return 1;
}

static B32 app_register_artifact_types(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (!state->artifactCache) {
        return 0;
    }

    ArtifactTypeDesc triangleType = {};
    triangleType.typeId = AppArtifactTypeId_TrianglePipeline;
    triangleType.name = str8("triangle pipeline");
    triangleType.buildProc = app_build_triangle_pipeline_artifact;
    triangleType.publishProc = app_publish_triangle_pipeline_artifact;
    triangleType.destroyProc = app_destroy_pipeline_artifact;
    triangleType.evictionTargetCount = 16u;
    triangleType.evictionMaxIdleFrames = 240u;

    ArtifactTypeDesc computeType = {};
    computeType.typeId = AppArtifactTypeId_ComputePipeline;
    computeType.name = str8("demo compute pipeline");
    computeType.buildProc = app_build_compute_pipeline_artifact;
    computeType.publishProc = app_publish_compute_pipeline_artifact;
    computeType.destroyProc = app_destroy_pipeline_artifact;
    computeType.evictionTargetCount = 16u;
    computeType.evictionMaxIdleFrames = 240u;

    ArtifactTypeDesc decodedType = {};
    decodedType.typeId = AppArtifactTypeId_DecodedTexture;
    decodedType.name = str8("decoded demo texture");
    decodedType.buildProc = app_build_decoded_texture_artifact;
    decodedType.publishProc = app_publish_decoded_texture_artifact;
    decodedType.destroyProc = app_destroy_decoded_texture_artifact;
    decodedType.userData = state->contentStore;
    decodedType.evictionTargetCount = 32u;
    decodedType.evictionMaxIdleFrames = 240u;

    return artifact_register_type(state->artifactCache, &triangleType) &&
           artifact_register_type(state->artifactCache, &computeType) &&
           artifact_register_type(state->artifactCache, &decodedType);
}

static void app_watch_demo_files(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (!state->fileStream) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    StringU8 exeDir = OS_get_executable_directory(scratch.arena);
    StringU8 vertexShaderPath = str8_concat(scratch.arena, exeDir, str8("/../" APP_SHADER_TRIANGLE_VERTEX_RUNTIME_PATH));
    StringU8 fragmentShaderPath = str8_concat(scratch.arena, exeDir, str8("/../" APP_SHADER_TRIANGLE_FRAGMENT_RUNTIME_PATH));
    StringU8 computePath = str8_concat(scratch.arena, exeDir, str8("/../" APP_SHADER_DEMO_COMPUTE_RUNTIME_PATH));
    StringU8 texturePath = str8_concat(scratch.arena, exeDir, str8("/../" APP_GFX_DEMO_TEXTURE_PATH));

    state->gfxTriangleVertexShader = file_watch(state->fileStream, vertexShaderPath, 0u);
    state->gfxTriangleFragmentShader = file_watch(state->fileStream, fragmentShaderPath, 0u);
    state->gfxDemoComputeShader = file_watch(state->fileStream, computePath, 0u);
    state->gfxDemoTextureSource = file_watch(state->fileStream, texturePath, 0u);
}

#if defined(PLATFORM_BUILD_DEBUG)
static U64 app_gfx_newest_shader_source_timestamp(void) {
    U64 newestTimestamp = 0u;
    for (U32 index = 0; index < ARRAY_COUNT(APP_GFX_DEV_SHADER_SOURCES); ++index) {
        OS_FileInfo info = OS_get_file_info(APP_GFX_DEV_SHADER_SOURCES[index]);
        if (info.exists && info.lastWriteTimestampNs > newestTimestamp) {
            newestTimestamp = info.lastWriteTimestampNs;
        }
    }
    return newestTimestamp;
}

static void app_gfx_try_build_dev_shaders(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    U64 sourceTimestamp = app_gfx_newest_shader_source_timestamp();
    if (sourceTimestamp == 0u) {
        return;
    }
    if (state->gfxShaderBuildInitialized && sourceTimestamp == state->gfxShaderSourceTimestamp) {
        return;
    }

#if defined(PLATFORM_OS_WINDOWS)
    StringU8 buildCommand = str8(".\\sob.exe shaders debug");
#else
    StringU8 buildCommand = str8("./sob shaders debug");
#endif

    LOG_INFO("gfx", "Building shaders");
    S32 buildResult = APP_OS_CALL(ctx->host, OS_execute, buildCommand);
    state->gfxShaderBuildInitialized = 1;
    state->gfxShaderSourceTimestamp = sourceTimestamp;
    if (buildResult != 0) {
        LOG_ERROR("gfx", "Shader build failed (exit code {})", buildResult);
        return;
    }
}
#endif

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
    if (!pixels) {
        return 0;
    }
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

struct AppTrianglePipelineArtifactData {
    GfxDevice* device;
    ContentHash vertexHash;
    ContentHash fragmentHash;
};

struct AppComputePipelineArtifactData {
    GfxDevice* device;
    ContentHash shaderHash;
};

struct AppDecodedTextureArtifactData {
    ContentHash sourceHash;
};

static ArtifactKey app_artifact_key_from_label(const char* label) {
    StringU8 labelStr = str8(label);
    return artifact_key_from_bytes(labelStr.data, labelStr.size);
}

static ArtifactKey app_artifact_key_from_content(const char* label, ContentHash hash) {
    ArtifactKey result = app_artifact_key_from_label(label);
    ArtifactKey contentKey = {};
    contentKey.hash[0] = hash.hash[0];
    contentKey.hash[1] = hash.hash[1];
    return artifact_key_mix(result, contentKey);
}

static ContentHash app_content_hash_from_value(ArtifactValue value) {
    ContentHash result = {};
    result.hash[0] = value.u64[0];
    result.hash[1] = value.u64[1];
    return result;
}

static ArtifactValue app_content_hash_to_value(ContentHash hash) {
    ArtifactValue result = {};
    result.u64[0] = hash.hash[0];
    result.u64[1] = hash.hash[1];
    return result;
}

static GfxPipeline app_gfx_pipeline_from_value(ArtifactValue value) {
    GfxPipeline result = {};
    result.index = (U32)(value.u64[0] & 0xFFFFFFFFu);
    result.generation = (U32)(value.u64[0] >> 32u);
    return result;
}

static ArtifactValue app_gfx_pipeline_to_value(GfxDevice* device, GfxPipeline pipeline) {
    ArtifactValue result = {};
    result.u64[0] = ((U64)pipeline.generation << 32u) | (U64)pipeline.index;
    result.u64[1] = (U64)(uintptr_t)device;
    return result;
}

static B32 app_build_triangle_pipeline_artifact(ArtifactBuildContext* artifactCtx, ArtifactValue* outValue, U64* outBytes) {
    if (!artifactCtx || !artifactCtx->content || !outValue ||
        artifactCtx->requestDataSize != sizeof(AppTrianglePipelineArtifactData)) {
        return 0;
    }

    const AppTrianglePipelineArtifactData* data = (const AppTrianglePipelineArtifactData*)artifactCtx->requestData;
    ContentView vertexView = content_view_hash(artifactCtx->content, data->vertexHash);
    ContentView fragmentView = content_view_hash(artifactCtx->content, data->fragmentHash);
    if (!vertexView.valid || vertexView.size == 0u || !fragmentView.valid || fragmentView.size == 0u) {
        return 0;
    }

    outValue->u64[0] = data->vertexHash.hash[0];
    outValue->u64[1] = data->vertexHash.hash[1];
    outValue->u64[2] = data->fragmentHash.hash[0];
    outValue->u64[3] = data->fragmentHash.hash[1];
    if (outBytes) {
        *outBytes = vertexView.size + fragmentView.size;
    }
    return 1;
}

static B32 app_publish_triangle_pipeline_artifact(ArtifactPublishContext* artifactCtx,
                                                 ArtifactValue buildValue,
                                                 ArtifactValue* outValue,
                                                 U64* outBytes) {
    if (!artifactCtx || !artifactCtx->content || !outValue ||
        artifactCtx->requestDataSize != sizeof(AppTrianglePipelineArtifactData)) {
        return 0;
    }

    const AppTrianglePipelineArtifactData* data = (const AppTrianglePipelineArtifactData*)artifactCtx->requestData;
    if (!data->device) {
        return 0;
    }

    ContentHash vertexHash = {{buildValue.u64[0], buildValue.u64[1]}};
    ContentHash fragmentHash = {{buildValue.u64[2], buildValue.u64[3]}};
    ContentView vertexView = content_view_hash(artifactCtx->content, vertexHash);
    ContentView fragmentView = content_view_hash(artifactCtx->content, fragmentHash);
    if (!vertexView.valid || vertexView.size == 0u ||
        !fragmentView.valid || fragmentView.size == 0u) {
        return 0;
    }

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
#if defined(PLATFORM_OS_WINDOWS)
    pipelineDesc.vertexShader.format = GfxShaderFormat_SPIRV;
    pipelineDesc.vertexShader.entry = APP_SHADER_TRIANGLE_VERTEX_ENTRY;
    pipelineDesc.fragmentShader.format = GfxShaderFormat_SPIRV;
    pipelineDesc.fragmentShader.entry = APP_SHADER_TRIANGLE_FRAGMENT_ENTRY;
#else
    pipelineDesc.vertexShader.format = GfxShaderFormat_MSL_Source;
    pipelineDesc.vertexShader.entry = APP_SHADER_TRIANGLE_VERTEX_ENTRY;
    pipelineDesc.fragmentShader.format = GfxShaderFormat_MSL_Source;
    pipelineDesc.fragmentShader.entry = APP_SHADER_TRIANGLE_FRAGMENT_ENTRY;
#endif
    pipelineDesc.vertexShader.data = vertexView.data;
    pipelineDesc.vertexShader.size = vertexView.size;
    pipelineDesc.fragmentShader.data = fragmentView.data;
    pipelineDesc.fragmentShader.size = fragmentView.size;
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

    GfxPipeline pipeline = gfx_create_graphics_pipeline(data->device, &pipelineDesc);
    if (pipeline.generation == 0u) {
        return 0;
    }

    *outValue = app_gfx_pipeline_to_value(data->device, pipeline);
    if (outBytes) {
        *outBytes = 1u;
    }
    return 1;
}

static B32 app_build_compute_pipeline_artifact(ArtifactBuildContext* artifactCtx, ArtifactValue* outValue, U64* outBytes) {
    if (!artifactCtx || !artifactCtx->content || !outValue ||
        artifactCtx->requestDataSize != sizeof(AppComputePipelineArtifactData)) {
        return 0;
    }

    const AppComputePipelineArtifactData* data = (const AppComputePipelineArtifactData*)artifactCtx->requestData;
    ContentView shaderView = content_view_hash(artifactCtx->content, data->shaderHash);
    if (!shaderView.valid || shaderView.size == 0u) {
        return 0;
    }

    *outValue = app_content_hash_to_value(data->shaderHash);
    if (outBytes) {
        *outBytes = shaderView.size;
    }
    return 1;
}

static B32 app_publish_compute_pipeline_artifact(ArtifactPublishContext* artifactCtx,
                                                ArtifactValue buildValue,
                                                ArtifactValue* outValue,
                                                U64* outBytes) {
    if (!artifactCtx || !artifactCtx->content || !outValue ||
        artifactCtx->requestDataSize != sizeof(AppComputePipelineArtifactData)) {
        return 0;
    }

    const AppComputePipelineArtifactData* data = (const AppComputePipelineArtifactData*)artifactCtx->requestData;
    if (!data->device) {
        return 0;
    }

    ContentHash shaderHash = app_content_hash_from_value(buildValue);
    ContentView shaderView = content_view_hash(artifactCtx->content, shaderHash);
    if (!shaderView.valid || shaderView.size == 0u) {
        return 0;
    }

    GfxComputePipelineDesc pipelineDesc = {};
    pipelineDesc.name = "demo material compute pipeline";
#if defined(PLATFORM_OS_WINDOWS)
    pipelineDesc.shader.format = GfxShaderFormat_SPIRV;
    pipelineDesc.shader.entry = APP_SHADER_DEMO_COMPUTE_ENTRY;
#else
    pipelineDesc.shader.format = GfxShaderFormat_MSL_Source;
    pipelineDesc.shader.entry = APP_SHADER_DEMO_COMPUTE_ENTRY;
#endif
    pipelineDesc.shader.data = shaderView.data;
    pipelineDesc.shader.size = shaderView.size;
    pipelineDesc.threadsPerThreadgroupX = APP_GFX_DEMO_COMPUTE_THREADS_PER_GROUP;
    pipelineDesc.threadsPerThreadgroupY = 1u;
    pipelineDesc.threadsPerThreadgroupZ = 1u;

    GfxPipeline pipeline = gfx_create_compute_pipeline(data->device, &pipelineDesc);
    if (pipeline.generation == 0u) {
        return 0;
    }

    *outValue = app_gfx_pipeline_to_value(data->device, pipeline);
    if (outBytes) {
        *outBytes = 1u;
    }
    return 1;
}

static void app_destroy_pipeline_artifact(void* userData, ArtifactValue value) {
    (void)userData;
    GfxDevice* device = (GfxDevice*)(uintptr_t)value.u64[1];
    GfxPipeline pipeline = app_gfx_pipeline_from_value(value);
    if (device && pipeline.generation != 0u) {
        gfx_destroy_pipeline(device, pipeline);
    }
}

static B32 app_build_decoded_texture_artifact(ArtifactBuildContext* artifactCtx, ArtifactValue* outValue, U64* outBytes) {
    if (!artifactCtx || !artifactCtx->content || !outValue ||
        artifactCtx->requestDataSize != sizeof(AppDecodedTextureArtifactData)) {
        return 0;
    }

    const AppDecodedTextureArtifactData* data = (const AppDecodedTextureArtifactData*)artifactCtx->requestData;
    ContentView sourceView = content_view_hash(artifactCtx->content, data->sourceHash);
    if (!sourceView.valid || sourceView.size == 0u) {
        return 0;
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    AppImageRGBA8 image = {};
    if (!app_decode_ppm_rgba8(scratch.arena, sourceView.data, sourceView.size, &image)) {
        return 0;
    }

    U64 pixelBytes = image.bytesPerRow * image.height;
    U64 blobSize = sizeof(AppDecodedImageHeader) + pixelBytes;
    U8* blob = ARENA_PUSH_ARRAY(scratch.arena, U8, blobSize);
    if (!blob) {
        return 0;
    }

    AppDecodedImageHeader header = {};
    header.width = image.width;
    header.height = image.height;
    header.bytesPerRow = image.bytesPerRow;
    MEMCPY(blob, &header, sizeof(header));
    MEMCPY(blob + sizeof(header), image.pixels, pixelBytes);

    ContentHash hash = content_submit_bytes(artifactCtx->content, CONTENT_KEY_ZERO, blob, blobSize, str8("decoded demo texture"));
    if (content_hash_is_zero(hash)) {
        return 0;
    }

    *outValue = app_content_hash_to_value(hash);
    if (outBytes) {
        *outBytes = blobSize;
    }
    return 1;
}

static B32 app_publish_decoded_texture_artifact(ArtifactPublishContext* artifactCtx,
                                               ArtifactValue buildValue,
                                               ArtifactValue* outValue,
                                               U64* outBytes) {
    if (!artifactCtx || !artifactCtx->content || !outValue) {
        return 0;
    }

    ContentHash hash = app_content_hash_from_value(buildValue);
    ContentView view = content_view_hash(artifactCtx->content, hash);
    if (!view.valid || !content_retain_hash(artifactCtx->content, hash)) {
        return 0;
    }

    *outValue = buildValue;
    if (outBytes) {
        *outBytes = view.size;
    }
    return 1;
}

static void app_destroy_decoded_texture_artifact(void* userData, ArtifactValue value) {
    ContentStore* content = (ContentStore*)userData;
    ContentHash hash = app_content_hash_from_value(value);
    if (content && !content_hash_is_zero(hash)) {
        content_release_hash(content, hash);
    }
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

    state->gfxDemoMaterialCount = APP_GFX_DEMO_MATERIAL_COUNT;
    state->gfxDemoMaterialDirty = 1;
    state->gfxDemoInitialized = 1;
    app_gfx_demo_log_once(state, AppGfxDemoLoadLog_Started, "Demo resources requested");
    return 1;
}

static void app_gfx_demo_log_once(AppCoreState* state, U32 bit, const char* message) {
    if (state == 0 || message == 0 || FLAGS_HAS(state->gfxDemoLoadLogMask, bit)) {
        return;
    }

    LOG_INFO("gfx", "{}", str8(message));
    state->gfxDemoLoadLogMask |= bit;
}

static void app_gfx_try_create_demo_buffers(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->gfxDemoGeometryCreated || ctx->host->gfxDevice == 0) {
        return;
    }

    GfxBufferDesc vertexDesc = {};
    vertexDesc.name = "triangle vertices";
    vertexDesc.size = sizeof(APP_GFX_DEMO_VERTICES);
    vertexDesc.usageFlags = GfxBufferUsageFlags_Vertex | GfxBufferUsageFlags_CopyDst;
    vertexDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer vertexBuffer = gfx_create_buffer(ctx->host->gfxDevice, &vertexDesc);

    GfxBufferDesc indexDesc = {};
    indexDesc.name = "triangle indices";
    indexDesc.size = sizeof(APP_GFX_DEMO_INDICES);
    indexDesc.usageFlags = GfxBufferUsageFlags_Index | GfxBufferUsageFlags_CopyDst;
    indexDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer indexBuffer = gfx_create_buffer(ctx->host->gfxDevice, &indexDesc);

    GfxBufferDesc materialDesc = {};
    materialDesc.name = "demo materials";
    materialDesc.size = sizeof(AppGfxMaterial) * state->gfxDemoMaterialCount;
    materialDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    materialDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer materialBuffer = gfx_create_buffer(ctx->host->gfxDevice, &materialDesc);

    state->gfxDemoGeometryCreated = vertexBuffer.generation != 0u &&
                                    indexBuffer.generation != 0u &&
                                    materialBuffer.generation != 0u;
    if (state->gfxDemoGeometryCreated) {
        state->gfxTriangleVertexBuffer = vertexBuffer;
        state->gfxTriangleIndexBuffer = indexBuffer;
        state->gfxDemoMaterialBuffer = materialBuffer;
        state->gfxDemoMaterialDirty = 1;
        app_gfx_demo_log_once(state, AppGfxDemoLoadLog_GeometryCreated, "Demo GPU buffers created");
    } else {
        gfx_destroy_buffer(ctx->host->gfxDevice, materialBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, indexBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, vertexBuffer);
    }
}

static void app_gfx_upload_demo_geometry(APP_Context* ctx, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->gfxDemoGeometryUploaded || !state->gfxDemoGeometryCreated || frame == 0) {
        return;
    }

    B32 uploadedVertices = gfx_upload_buffer(frame,
                                             state->gfxTriangleVertexBuffer,
                                             0u,
                                             APP_GFX_DEMO_VERTICES,
                                             sizeof(APP_GFX_DEMO_VERTICES));
    B32 uploadedIndices = gfx_upload_buffer(frame,
                                            state->gfxTriangleIndexBuffer,
                                            0u,
                                            APP_GFX_DEMO_INDICES,
                                            sizeof(APP_GFX_DEMO_INDICES));
    state->gfxDemoGeometryUploaded = uploadedVertices && uploadedIndices;
    if (state->gfxDemoGeometryUploaded) {
        app_gfx_demo_log_once(state, AppGfxDemoLoadLog_GeometryUploaded, "Demo geometry upload recorded");
    }
}

static void app_gfx_try_update_triangle_pipeline(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->fileStream == 0 || state->artifactCache == 0 || ctx->host->gfxDevice == 0) {
        return;
    }

    FileView vertexShaderView = file_view(state->fileStream, state->gfxTriangleVertexShader);
    FileView fragmentShaderView = file_view(state->fileStream, state->gfxTriangleFragmentShader);
    if (vertexShaderView.status != FileStatus_Ready ||
        fragmentShaderView.status != FileStatus_Ready ||
        content_hash_is_zero(vertexShaderView.hash) ||
        content_hash_is_zero(fragmentShaderView.hash)) {
        return;
    }

    ArtifactKey key = app_artifact_key_from_content("triangle pipeline vertex", vertexShaderView.hash);
    key = artifact_key_mix(key, app_artifact_key_from_content("triangle pipeline fragment", fragmentShaderView.hash));
    if (artifact_key_equal(key, state->gfxTrianglePipelineArtifactKey) &&
        state->gfxTrianglePipeline.generation != 0u) {
        artifact_touch(state->artifactCache, AppArtifactTypeId_TrianglePipeline, key, state->frameCounter);
        return;
    }

    AppTrianglePipelineArtifactData artifactData = {};
    artifactData.device = ctx->host->gfxDevice;
    artifactData.vertexHash = vertexShaderView.hash;
    artifactData.fragmentHash = fragmentShaderView.hash;

    ArtifactResult artifact = artifact_get(state->artifactCache,
                                           AppArtifactTypeId_TrianglePipeline,
                                           key,
                                           1u,
                                           &artifactData,
                                           sizeof(artifactData),
                                           ArtifactGetFlags_HighPriority,
                                           0u);
    if (artifact.status == ArtifactStatus_Ready &&
        !FLAGS_HAS(artifact.flags, ArtifactResultFlags_Stale) &&
        artifact_retain(state->artifactCache, AppArtifactTypeId_TrianglePipeline, key)) {
        if (!artifact_key_is_zero(state->gfxTrianglePipelineArtifactKey)) {
            artifact_release(state->artifactCache,
                             AppArtifactTypeId_TrianglePipeline,
                             state->gfxTrianglePipelineArtifactKey);
        }
        state->gfxTrianglePipeline = app_gfx_pipeline_from_value(artifact.value);
        state->gfxTrianglePipelineArtifactKey = key;
        app_gfx_demo_log_once(state, AppGfxDemoLoadLog_TrianglePipeline, "Demo triangle pipeline ready");
    }
}

static void app_gfx_try_update_demo_compute_pipeline(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->fileStream == 0 || state->artifactCache == 0 || ctx->host->gfxDevice == 0) {
        return;
    }

    FileView shaderView = file_view(state->fileStream, state->gfxDemoComputeShader);
    if (shaderView.status != FileStatus_Ready || content_hash_is_zero(shaderView.hash)) {
        return;
    }

    ArtifactKey key = app_artifact_key_from_content("demo compute pipeline", shaderView.hash);
    if (artifact_key_equal(key, state->gfxDemoComputePipelineArtifactKey) &&
        state->gfxDemoComputePipeline.generation != 0u) {
        artifact_touch(state->artifactCache, AppArtifactTypeId_ComputePipeline, key, state->frameCounter);
        return;
    }

    AppComputePipelineArtifactData artifactData = {};
    artifactData.device = ctx->host->gfxDevice;
    artifactData.shaderHash = shaderView.hash;

    ArtifactResult artifact = artifact_get(state->artifactCache,
                                           AppArtifactTypeId_ComputePipeline,
                                           key,
                                           1u,
                                           &artifactData,
                                           sizeof(artifactData),
                                           ArtifactGetFlags_HighPriority,
                                           0u);
    if (artifact.status == ArtifactStatus_Ready &&
        !FLAGS_HAS(artifact.flags, ArtifactResultFlags_Stale) &&
        artifact_retain(state->artifactCache, AppArtifactTypeId_ComputePipeline, key)) {
        if (!artifact_key_is_zero(state->gfxDemoComputePipelineArtifactKey)) {
            artifact_release(state->artifactCache,
                             AppArtifactTypeId_ComputePipeline,
                             state->gfxDemoComputePipelineArtifactKey);
        }
        state->gfxDemoComputePipeline = app_gfx_pipeline_from_value(artifact.value);
        state->gfxDemoComputePipelineArtifactKey = key;
        state->gfxDemoMaterialDirty = 1;
        app_gfx_demo_log_once(state, AppGfxDemoLoadLog_ComputePipeline, "Demo compute pipeline ready");
    }
}

static void app_gfx_upload_demo_texture(APP_Context* ctx, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->fileStream == 0 ||
        state->artifactCache == 0 ||
        state->contentStore == 0 ||
        ctx->host->gfxDevice == 0 ||
        frame == 0) {
        return;
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
    if (state->gfxDemoSamplerId.index == 0u) {
        return;
    }

    FileView textureView = file_view(state->fileStream, state->gfxDemoTextureSource);
    if (textureView.status != FileStatus_Ready || content_hash_is_zero(textureView.hash)) {
        return;
    }

    ArtifactKey key = app_artifact_key_from_content("decoded demo texture", textureView.hash);
    if (artifact_key_equal(key, state->gfxDemoTextureDecodeArtifactKey) &&
        state->gfxDemoTextureUploaded) {
        artifact_touch(state->artifactCache, AppArtifactTypeId_DecodedTexture, key, state->frameCounter);
        return;
    }

    AppDecodedTextureArtifactData artifactData = {};
    artifactData.sourceHash = textureView.hash;
    ArtifactResult artifact = artifact_get(state->artifactCache,
                                           AppArtifactTypeId_DecodedTexture,
                                           key,
                                           1u,
                                           &artifactData,
                                           sizeof(artifactData),
                                           ArtifactGetFlags_None,
                                           0u);
    if (artifact.status != ArtifactStatus_Ready) {
        if (FLAGS_HAS(artifact.flags, ArtifactResultFlags_ErrorCached)) {
            state->gfxDemoTextureFailedGeneration = textureView.generation;
        }
        return;
    }

    ContentHash decodedHash = app_content_hash_from_value(artifact.value);
    ContentView decodedContent = content_view_hash(state->contentStore, decodedHash);
    if (!decodedContent.valid || decodedContent.size < sizeof(AppDecodedImageHeader)) {
        state->gfxDemoTextureFailedGeneration = textureView.generation;
        return;
    }

    const AppDecodedImageHeader* image = (const AppDecodedImageHeader*)decodedContent.data;
    const U8* pixels = decodedContent.data + sizeof(AppDecodedImageHeader);
    U64 pixelBytes = image->bytesPerRow * image->height;
    if (decodedContent.size < sizeof(AppDecodedImageHeader) + pixelBytes) {
        state->gfxDemoTextureFailedGeneration = textureView.generation;
        return;
    }

    GfxTextureDesc textureDesc = {};
    textureDesc.name = "demo texture";
    textureDesc.width = image->width;
    textureDesc.height = image->height;
    textureDesc.mipCount = 1u;
    textureDesc.format = GfxFormat_RGBA8_UNorm;
    textureDesc.usageFlags = GfxTextureUsageFlags_Sampled | GfxTextureUsageFlags_CopyDst;
    GfxTexture newTexture = gfx_create_texture(ctx->host->gfxDevice, &textureDesc);
    GfxResourceId newTextureId = gfx_register_texture(ctx->host->gfxDevice, newTexture);
    if (newTextureId.index == 0u) {
        gfx_destroy_texture(ctx->host->gfxDevice, newTexture);
        state->gfxDemoTextureFailedGeneration = textureView.generation;
        return;
    }

    GfxTextureUploadRegion region = {};
    region.layerCount = 1u;
    region.width = image->width;
    region.height = image->height;
    region.depth = 1u;
    region.bytesPerRow = image->bytesPerRow;
    region.rowsPerImage = image->height;

    B32 uploaded = gfx_upload_texture(frame, newTexture, &region, pixels);
    if (!uploaded) {
        gfx_destroy_texture(ctx->host->gfxDevice, newTexture);
        state->gfxDemoTextureFailedGeneration = textureView.generation;
        return;
    }

    GfxTexture oldTexture = state->gfxDemoTexture;
    state->gfxDemoTexture = newTexture;
    state->gfxDemoTextureId = newTextureId;
    state->gfxDemoTextureGeneration = textureView.generation;
    state->gfxDemoTextureFailedGeneration = 0u;
    state->gfxDemoTextureDecodeArtifactKey = key;
    state->gfxDemoDecodedTextureHash = decodedHash;
    state->gfxDemoTextureUploaded = 1;
    state->gfxDemoMaterialDirty = 1;
    artifact_touch(state->artifactCache, AppArtifactTypeId_DecodedTexture, key, state->frameCounter);
    gfx_destroy_texture(ctx->host->gfxDevice, oldTexture);
    app_gfx_demo_log_once(state, AppGfxDemoLoadLog_TextureUploaded, "Demo texture upload recorded");
}

static B32 app_gfx_dispatch_demo_materials(APP_Context* ctx, GfxCommandBuffer* commands, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
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

    if (state->gfxDemoMaterialsReady && !state->gfxDemoMaterialDirty) {
        return 1;
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
    GfxGpuSlice materialSlice = {};
    materialSlice.buffer = state->gfxDemoMaterialBuffer;
    materialSlice.size = sizeof(AppGfxMaterial) * state->gfxDemoMaterialCount;
    GfxBufferBinding materialBinding = gfx_buffer_write(GFX_SHADER_SLOT_PASS_DATA,
                                                        materialSlice,
                                                        GfxStageFlags_Compute);
    pass.bufferBindings = &materialBinding;
    pass.bufferBindingCount = 1u;

    GfxDispatch dispatch = {};
    dispatch.pipeline = state->gfxDemoComputePipeline;
    dispatch.dispatchData = dispatchTemp.gpu;
    dispatch.groupsX = (state->gfxDemoMaterialCount + APP_GFX_DEMO_COMPUTE_THREADS_PER_GROUP - 1u) /
                       APP_GFX_DEMO_COMPUTE_THREADS_PER_GROUP;
    dispatch.groupsY = 1u;
    dispatch.groupsZ = 1u;

    gfx_compute_pass(commands, &pass, &dispatch, 1u);
    state->gfxDemoMaterialsReady = 1;
    state->gfxDemoMaterialDirty = 0;
    return 1;
}

static void app_gfx_demo_shutdown(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    GfxDevice* device = ctx->host ? ctx->host->gfxDevice : 0;
    app_resource_cache_shutdown(ctx);

    if (device != 0) {
        gfx_destroy_buffer(device, state->gfxTriangleIndexBuffer);
        gfx_destroy_buffer(device, state->gfxTriangleVertexBuffer);
        gfx_destroy_buffer(device, state->gfxDemoMaterialBuffer);
        gfx_destroy_sampler(device, state->gfxDemoSampler);
        gfx_destroy_texture(device, state->gfxDemoTexture);
    }

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
    state->gfxDemoMaterialsReady = 0;
    state->gfxDemoMaterialDirty = 0;
    state->gfxTrianglePipelineArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemoComputePipelineArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemoTextureDecodeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemoDecodedTextureHash = CONTENT_HASH_ZERO;
    state->gfxDemoTextureGeneration = 0u;
    state->gfxDemoTextureFailedGeneration = 0u;
    state->gfxDemoGeometryCreated = 0;
    state->gfxDemoGeometryUploaded = 0;
    state->gfxDemoReady = 0;
    state->gfxDemoLoadLogMask = 0u;
    state->gfxDemoInitialized = 0;
}

static void app_gfx_demo_frame(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    if (!ctx->host->gfxDevice) {
        return;
    }
    if (ctx->host->windowWidth == 0u || ctx->host->windowHeight == 0u) {
        return;
    }

    if (!ctx->core->gfxDemoInitialized && !app_gfx_demo_init(ctx)) {
        return;
    }

#if defined(PLATFORM_BUILD_DEBUG)
    app_gfx_try_build_dev_shaders(ctx);
#endif

#if defined(PLATFORM_BUILD_DEBUG)
    if (ctx->core->fileStream) {
        file_stream_tick(ctx->core->fileStream, OS_get_time_nanoseconds(), 16u);
    }
#endif
    if (ctx->core->artifactCache) {
        artifact_cache_tick(ctx->core->artifactCache, ctx->core->frameCounter, 16u, 16u);
    }

    Temp scratch = get_scratch(0, 0);

    GfxFrame* frame = gfx_begin_frame(ctx->host->gfxDevice);
    if (!frame) {
        if (scratch.arena != 0) {
            temp_end(&scratch);
        }
        return;
    }

    GfxCommandBuffer* commands = gfx_get_command_buffer(frame);
    GfxTexture backbuffer = gfx_get_backbuffer(frame);

    if (!ctx->core->gfxDemoGeometryCreated) {
        app_gfx_try_create_demo_buffers(ctx);
    }
    if (ctx->core->gfxDemoGeometryCreated && !ctx->core->gfxDemoGeometryUploaded) {
        app_gfx_upload_demo_geometry(ctx, frame);
    }
    if (ctx->core->gfxDemoGeometryUploaded) {
        app_gfx_try_update_triangle_pipeline(ctx);
        app_gfx_try_update_demo_compute_pipeline(ctx);
        app_gfx_upload_demo_texture(ctx, frame);
        if (ctx->core->artifactCache) {
            artifact_cache_tick(ctx->core->artifactCache, ctx->core->frameCounter, 16u, 16u);
        }
    }

    B32 materialsReady = app_gfx_dispatch_demo_materials(ctx, commands, frame);

    B32 resourcesReady = ctx->core->gfxDemoGeometryUploaded &&
                         ctx->core->gfxTrianglePipeline.generation != 0u &&
                         ctx->core->gfxDemoTextureUploaded &&
                         ctx->core->gfxDemoTextureId.index != 0u &&
                         ctx->core->gfxDemoSamplerId.index != 0u &&
                         materialsReady;

    if (resourcesReady && !ctx->core->gfxDemoReady) {
        ctx->core->gfxDemoReady = 1;
        app_gfx_demo_log_once(ctx->core, AppGfxDemoLoadLog_Ready, "Demo resources ready");
    }

    GfxDraw* draws = 0;
    U32 drawCount = 0u;
    if (resourcesReady && scratch.arena != 0) {
        GfxTemp drawTemp = gfx_allocate_temp(frame, sizeof(AppGfxDrawData) * APP_GFX_DEMO_DRAW_COUNT, 16u);
        AppGfxDrawData* drawDataBase = (AppGfxDrawData*)drawTemp.cpu;
        if (drawDataBase != 0) {
            draws = ARENA_PUSH_ARRAY(scratch.arena, GfxDraw, APP_GFX_DEMO_DRAW_COUNT);
        }

        if (draws != 0) {
            F32 cellWidth = 2.0f / (F32)APP_GFX_DEMO_DRAW_COLUMNS;
            F32 cellHeight = 2.0f / (F32)APP_GFX_DEMO_DRAW_ROWS;
            F32 scale = MIN(cellWidth, cellHeight) * 0.72f;

            for (U32 row = 0u; row < APP_GFX_DEMO_DRAW_ROWS; ++row) {
                for (U32 column = 0u; column < APP_GFX_DEMO_DRAW_COLUMNS; ++column) {
                    U32 drawIndex = row * APP_GFX_DEMO_DRAW_COLUMNS + column;
                    U64 drawDataOffset = sizeof(AppGfxDrawData) * drawIndex;

                    AppGfxDrawData* drawData = drawDataBase + drawIndex;
                    drawData->offsetScale[0] = -1.0f + ((F32)column + 0.5f) * cellWidth;
                    drawData->offsetScale[1] = -1.0f + ((F32)row + 0.5f) * cellHeight;
                    drawData->offsetScale[2] = scale;
                    drawData->offsetScale[3] = 1.0f;
                    drawData->materialIndex = drawIndex;
                    drawData->objectId = drawIndex;
                    drawData->flags = 1u;
                    drawData->_padding = 0u;

                    GfxGpuSlice drawDataSlice = drawTemp.gpu;
                    drawDataSlice.offset += drawDataOffset;
                    drawDataSlice.size = sizeof(AppGfxDrawData);

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

            drawCount = APP_GFX_DEMO_DRAW_COUNT;
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
    GfxBufferBinding materialBinding = {};
    if (materialsReady) {
        GfxGpuSlice materialSlice = {};
        materialSlice.buffer = ctx->core->gfxDemoMaterialBuffer;
        materialSlice.size = sizeof(AppGfxMaterial) * ctx->core->gfxDemoMaterialCount;
        materialBinding = gfx_buffer_read(GFX_SHADER_SLOT_PASS_DATA,
                                          materialSlice,
                                          GfxStageFlags_Fragment);
        pass.bufferBindings = &materialBinding;
        pass.bufferBindingCount = 1u;
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
    if (scratch.arena != 0) {
        temp_end(&scratch);
    }
    gfx_submit(commands);
    gfx_end_frame(frame);
    if (ctx->core->artifactCache) {
        artifact_cache_evict(ctx->core->artifactCache, ctx->core->frameCounter, 128u);
    }
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
