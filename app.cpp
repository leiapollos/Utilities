//
// Created by André Leite on 31/10/2025.
//

#define APP_CORE_STATE_VERSION 60u

#if defined(PLATFORM_BUILD_DEBUG)
#define APP_GFX_DEV_SHADER_SOURCE_ENTRY(name, source) source,
static const char* APP_GFX_DEV_SHADER_SOURCES[] = {
    APP_SHADER_MANIFEST_SOURCE,
    APP_SHADER_SOURCE_LIST(APP_GFX_DEV_SHADER_SOURCE_ENTRY)
};
#undef APP_GFX_DEV_SHADER_SOURCE_ENTRY
#endif

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
static ArtifactKey app_artifact_key_from_label(const char* label);
static ArtifactKey app_artifact_key_from_content(const char* label, ContentHash hash);
static ContentHash app_content_hash_from_value(ArtifactValue value);
static ArtifactValue app_content_hash_to_value(ContentHash hash);
static void app_demo_scene_submit(APP_Context* ctx, AppRendererFrame* rendererFrame);
static void app_demo_state_reset(AppDemoState* demo);
static void app_ui_panels_submit(APP_Context* ctx, AppRendererFrame* rendererFrame, const AppInput* input);
#if defined(PLATFORM_BUILD_DEBUG)
static U64 app_gfx_newest_shader_source_timestamp(void);
static void app_gfx_try_build_dev_shaders(APP_Context* ctx);
#endif

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
        LOG_ERROR("app", "frame context resolve failed");
        return;
    }

    AppCoreState* state = ctx.core;
    state->windowWidth = host->windowWidth;
    state->windowHeight = host->windowHeight;
    state->frameCounter += 1ull;
    state->lastDeltaSeconds = input->deltaSeconds;
    state->averageDeltaSeconds = (state->averageDeltaSeconds <= 0.0f)
        ? input->deltaSeconds
        : state->averageDeltaSeconds * 0.95f + input->deltaSeconds * 0.05f;
    for (U32 eventIndex = 0; eventIndex < input->eventCount; ++eventIndex) {
        const OS_GraphicsEvent* event = input->events + eventIndex;
        ASSERT_ALWAYS(event != 0);

        if (event->tag == OS_GraphicsEvent_Tag_KeyDown &&
            event->keyDown.keyCode == OS_KeyCode_Escape &&
            !event->keyDown.isRepeat &&
            !state->ui.wantKeyboard) {
            host->shouldQuit = 1;
        }
        if (event->tag == OS_GraphicsEvent_Tag_KeyDown &&
            event->keyDown.keyCode == OS_KeyCode_F1 &&
            !event->keyDown.isRepeat) {
            state->debugOverlayVisible = !state->debugOverlayVisible;
        }
        if (event->tag == OS_GraphicsEvent_Tag_KeyDown &&
            event->keyDown.keyCode == OS_KeyCode_F2 &&
            !event->keyDown.isRepeat) {
            state->profilerVisible = !state->profilerVisible;
        }
    }

    text_frame_advance(state->render2d.textContext);

    AppRendererFrame* rendererFrame = 0;
    {
        PROF_SCOPE("renderer begin");
        rendererFrame = app_renderer_begin_frame(&ctx);
    }
    if (rendererFrame) {
        {
            PROF_SCOPE("scene");
            app_demo_scene_submit(&ctx, rendererFrame);
        }
        {
            PROF_SCOPE("ui panels");
            app_ui_panels_submit(&ctx, rendererFrame, input);
        }
        {
            PROF_SCOPE("renderer end");
            app_renderer_end_frame(&ctx, rendererFrame);
        }
    }
}

static void app_shutdown(AppHost* host, HOT_StateStore* store) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);

    APP_Context ctx = {};
    if (!app_context_from_call(host, store, &ctx)) {
        return;
    }

    app_renderer_shutdown(&ctx);

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
    if (state->resources.contentStore != 0 && state->resources.fileStream != 0 && state->resources.artifactCache != 0) {
        return 1;
    }

    if (!app_ensure_job_system(ctx)) {
        return 0;
    }

    state->resources.arena = arena_alloc(.arenaSize = MB(16),
                                       .committedSize = KB(64),
                                       .flags = ArenaFlags_DoChain);
    if (state->resources.arena == 0) {
        LOG_ERROR("resource", "Failed to create resource arena");
        return 0;
    }

    ContentStoreDesc contentDesc = {};
    contentDesc.arena = state->resources.arena;
    contentDesc.initialBlobCapacity = 128u;
    contentDesc.initialKeyCapacity = 64u;
    state->resources.contentStore = content_store_alloc(&contentDesc);
    if (state->resources.contentStore == 0) {
        LOG_ERROR("resource", "Failed to create content store");
        app_resource_cache_shutdown(ctx);
        return 0;
    }

    FileStreamDesc fileDesc = {};
    fileDesc.arena = state->resources.arena;
    fileDesc.content = state->resources.contentStore;
    fileDesc.initialFileCapacity = 16u;
    state->resources.fileStream = file_stream_alloc(&fileDesc);
    if (state->resources.fileStream == 0) {
        LOG_ERROR("resource", "Failed to create file stream");
        app_resource_cache_shutdown(ctx);
        return 0;
    }

    ArtifactCacheDesc artifactDesc = {};
    artifactDesc.arena = state->resources.arena;
    artifactDesc.jobSystem = state->jobSystem;
    artifactDesc.content = state->resources.contentStore;
    artifactDesc.initialSlotCapacity = 128u;
    artifactDesc.initialTableCapacity = 256u;
    artifactDesc.initialTypeCapacity = 8u;
    artifactDesc.requestDataSize = 128u;
    state->resources.artifactCache = artifact_cache_alloc(&artifactDesc);
    if (state->resources.artifactCache == 0) {
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
    if (state->resources.artifactCache != 0) {
        artifact_cache_destroy(state->resources.artifactCache);
        state->resources.artifactCache = 0;
    }
    if (state->resources.fileStream != 0) {
        file_stream_destroy(state->resources.fileStream);
        state->resources.fileStream = 0;
    }
    if (state->resources.contentStore != 0) {
        content_store_destroy(state->resources.contentStore);
        state->resources.contentStore = 0;
    }
    if (state->resources.arena != 0) {
        arena_release(state->resources.arena);
        state->resources.arena = 0;
    }

    app_renderer_resource_cache_reset(ctx);
}

static B32 app_bind_current_module(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->resources.artifactCache && !app_register_artifact_types(ctx)) {
        return 0;
    }
    if (state->resources.fileStream) {
        app_watch_demo_files(ctx);
    }
    return 1;
}

static B32 app_register_artifact_types(APP_Context* ctx) {
    return app_renderer_register_artifact_types(ctx);
}

static void app_watch_demo_files(APP_Context* ctx) {
    app_renderer_watch_files(ctx);
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
    if (state->gfxShaderBuild.initialized && sourceTimestamp == state->gfxShaderBuild.sourceTimestamp) {
        return;
    }

#if defined(PLATFORM_OS_WINDOWS)
    StringU8 buildCommand = str8(".\\sob.exe shaders debug");
#else
    StringU8 buildCommand = str8("./sob shaders debug");
#endif

    LOG_INFO("gfx", "Building shaders");
    S32 buildResult = APP_OS_CALL(ctx->host, OS_execute, buildCommand);
    if (buildResult != 0) {
        LOG_ERROR("gfx", "Shader build failed (exit code {})", buildResult);
        return;
    }

    state->gfxShaderBuild.initialized = 1;
    state->gfxShaderBuild.sourceTimestamp = sourceTimestamp;
}
#endif

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
            core->debugOverlayVisible = 1;
            core->profilerVisible = 1;
            app_demo_state_reset(&core->demo);

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
