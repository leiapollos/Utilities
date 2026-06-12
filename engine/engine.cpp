//
// Created by André Leite on 31/10/2025.
//
// The module entry and frame orchestration. The engine owns the window
// stats, both state slots (its own and the project's), the resource
// cache, the fixed-tick drain, the replay keys, and the render phases;
// the project supplies policy through its EngProject table.
//

#if defined(PLATFORM_BUILD_DEBUG)
#define ENG_DEV_SHADER_SOURCE_ENTRY(name, source) source,
static const char* ENG_DEV_SHADER_SOURCES[] = {
    ENG_SHADER_MANIFEST_SOURCE,
    ENG_SHADER_SOURCE_LIST(ENG_DEV_SHADER_SOURCE_ENTRY)
};
#undef ENG_DEV_SHADER_SOURCE_ENTRY
#endif

static B32 eng_context_from_call(EngHost* host, HOT_StateStore* store, EngContext* outCtx);
static B32 eng_ensure_job_system(EngContext* ctx);
static B32 eng_resource_cache_init(EngContext* ctx);
static void eng_resource_cache_shutdown(EngContext* ctx);
static B32 eng_bind_current_module(EngContext* ctx);
static B32 eng_assets_register_types_(EngContext* ctx);
static void eng_renderer_watch_files(EngContext* ctx);
static ArtifactKey eng_artifact_key_from_label(const char* label);
static ArtifactKey eng_artifact_key_from_content(const char* label, ContentHash hash);
static ContentHash eng_content_hash_from_value(ArtifactValue value);
static ArtifactValue eng_content_hash_to_value(ContentHash hash);
static void eng_panels_submit(EngContext* ctx, EngRendererFrame* rendererFrame, const EngInput* input);
#if defined(PLATFORM_BUILD_DEBUG)
static U64 eng_newest_shader_source_timestamp(void);
static void eng_try_build_dev_shaders(EngContext* ctx);
#endif

static U32 eng_env_u32_(StringU8 name, U32 fallback, U32 minValue, U32 maxValue) {
    U32 result = fallback;
    Temp scratch = get_scratch(0, 0);
    if (scratch.arena) {
        StringU8 text = OS_get_environment_variable(scratch.arena, name);
        if (text.data && text.size != 0u) {
            U64 parsed = 0ull;
            for (U64 at = 0u; at < text.size; ++at) {
                U8 ch = text.data[at];
                if (ch < (U8)'0' || ch > (U8)'9') {
                    break;
                }
                parsed = parsed * 10ull + (U64)(ch - (U8)'0');
            }
            result = (U32)CLAMP(parsed, (U64)minValue, (U64)maxValue);
        }
        temp_end(&scratch);
    }
    return result;
}

static void eng_state_init_(EngContext* ctx, EngState* engine) {
    MEMSET(engine, 0, sizeof(*engine));
    engine->windowWidth = ctx->host->windowWidth;
    engine->windowHeight = ctx->host->windowHeight;
    engine->debug.windowVisible = 1;
    engine->debug.activeTab = EngDbgTab_Overview;
    engine->debug.masterGain = 1.0f;
    // UTILITIES_FIXED_DT=1 locks every frame to one tick so
    // frame-indexed captures stay deterministic under load.
    engine->simForcedDt = eng_env_u32_(str8("UTILITIES_FIXED_DT"), 0u, 0u, 1u)
        ? ENG_SIM_TICK_DT : 0.0f;

    StringU8 eventsDomain = str8((const char*) "events", 6);
    set_log_domain_level(eventsDomain, LogLevel_Debug);
}

static B32 eng_context_from_call(EngHost* host, HOT_StateStore* store, EngContext* outCtx) {
    ASSERT_ALWAYS(outCtx != 0);
    MEMSET(outCtx, 0, sizeof(*outCtx));

    if (host == 0 || store == 0 || !hot_state_store_is_valid(store)) {
        LOG_ERROR("eng", "Invalid module call context");
        return 0;
    }

    EngContext ctx = {};
    ctx.host = host;
    ctx.store = store;

    ctx.engine = (EngState*) hot_state_store_require(store, ENG_STATE_ID('E', 'N', 'G', 'C'),
                                                     ENG_STATE_VERSION, sizeof(EngState),
                                                     alignof(EngState));
    if (ctx.engine == 0) {
        LOG_ERROR("eng", "Engine state unavailable (size={})", (U64)sizeof(EngState));
        return 0;
    }
    if (hot_state_store_take_needs_init(store, ENG_STATE_ID('E', 'N', 'G', 'C'))) {
        eng_state_init_(&ctx, ctx.engine);
    }

    const EngProject* project = eng_project_();
    ASSERT_ALWAYS(project != 0);
    ASSERT_ALWAYS(project->stateId != 0u && project->stateSize != 0u);
    ASSERT_ALWAYS(!(project->capabilities & ENG_CAP_SIM) ||
                  (project->actionSize != 0u && project->actionSize <= ENG_SIM_MAX_ACTION_SIZE &&
                   project->saveSize != 0u && project->saveSize <= ENG_SIM_MAX_SAVE_SIZE));
    ctx.project = hot_state_store_require(store, project->stateId, project->stateVersion,
                                          project->stateSize, project->stateAlignment);
    if (ctx.project == 0) {
        LOG_ERROR("eng", "Project state '{}' unavailable (size={})",
                  str8(project->name), project->stateSize);
        return 0;
    }
    if (hot_state_store_take_needs_init(store, project->stateId) && project->state_init) {
        project->state_init(&ctx, ctx.project);
    }

    *outCtx = ctx;
    return 1;
}

static B32 eng_boot(EngHost* host, HOT_StateStore* store) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);

    log_init();
    set_log_level(LogLevel_Info);

    EngContext ctx = {};
    if (!eng_context_from_call(host, store, &ctx)) {
        return 0;
    }
    if (!eng_bind_current_module(&ctx)) {
        return 0;
    }

    ASSERT_ALWAYS(host->window.handle != 0);
    return 1;
}

static void eng_before_reload(EngHost* host, HOT_StateStore* store) {
    (void)host;
    (void)store;
}

static B32 eng_after_reload(EngHost* host, HOT_StateStore* store) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);

    log_init();
    set_log_level(LogLevel_Info);

    EngContext ctx = {};
    if (!eng_context_from_call(host, store, &ctx)) {
        return 0;
    }
    if (!eng_bind_current_module(&ctx)) {
        return 0;
    }

    // Projects watch reloadCount to refresh anything derived from code
    // (collision worlds, classifier-driven caches), so edits land live.
    ctx.engine->reloadCount += 1u;
    return 1;
}

static void eng_frame(EngHost* host, HOT_StateStore* store, const EngInput* input) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);
    ASSERT_ALWAYS(input != 0);

    EngContext ctx = {};
    if (!eng_context_from_call(host, store, &ctx)) {
        LOG_ERROR("eng", "frame context resolve failed");
        return;
    }
    ctx.input = input;

    const EngProject* project = eng_project_();
    EngState* state = ctx.engine;
    state->windowWidth = host->windowWidth;
    state->windowHeight = host->windowHeight;
    state->frameCounter += 1ull;
    state->lastDeltaSeconds = input->deltaSeconds;
    state->averageDeltaSeconds = (state->averageDeltaSeconds <= 0.0f)
        ? input->deltaSeconds
        : state->averageDeltaSeconds * 0.95f + input->deltaSeconds * 0.05f;

    if (project->pre_frame) {
        project->pre_frame(&ctx);
    }

    // Fixed-tick sim clock: integer ticks are the canonical clock; the
    // accumulator remainder keeps render time continuous at any refresh
    // rate. One action sample per frame; catch-up ticks share it.
    B32 simEnabled = (project->capabilities & ENG_CAP_SIM) != 0u && project->sim_tick != 0;
    U8 sampledActions[ENG_SIM_MAX_ACTION_SIZE] = {};
    B32 simActive = 0;
    if (simEnabled && project->sim_sample) {
        simActive = project->sim_sample(&ctx, sampledActions);
    }
    F32 frameDt = (state->simForcedDt > 0.0f) ? state->simForcedDt : input->deltaSeconds;
    if (frameDt > ENG_SIM_MAX_FRAME_DT) {
        frameDt = ENG_SIM_MAX_FRAME_DT;
        state->simClampCount += 1u;
    }
    state->simAccumulator += frameDt;
    while (state->simAccumulator >= ENG_SIM_TICK_DT) {
        state->simTickCounter += 1ull;
        state->simAccumulator -= ENG_SIM_TICK_DT;
        if (simEnabled && simActive) {
            // Playback replaces the live sample per tick (relative-tick
            // indexed, so replay is frame-rate independent); recording
            // stores whatever actually fed the tick.
            U8 tickActions[ENG_SIM_MAX_ACTION_SIZE];
            MEMCPY(tickActions, sampledActions, sizeof(tickActions));
            eng_sim_replay_tick_actions_(&ctx, tickActions);
            U64 tickBeginNanos = OS_get_time_nanoseconds();
            project->sim_tick(&ctx, tickActions);
            state->lastSimTickNanos = OS_get_time_nanoseconds() - tickBeginNanos;
            eng_sim_replay_post_tick_(&ctx, tickActions);
        }
    }
    state->simTimeSeconds = (F64)state->simTickCounter / (F64)ENG_SIM_TICK_HZ +
                            (F64)state->simAccumulator;

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
            state->debug.windowVisible = !state->debug.windowVisible;
        }
        if (event->tag == OS_GraphicsEvent_Tag_KeyDown &&
            event->keyDown.keyCode == OS_KeyCode_F2 &&
            !event->keyDown.isRepeat) {
            if (state->debug.windowVisible && state->debug.activeTab == EngDbgTab_Profiler) {
                state->debug.windowVisible = 0;
            } else {
                state->debug.windowVisible = 1;
                state->debug.activeTab = EngDbgTab_Profiler;
            }
        }
        if (simEnabled &&
            event->tag == OS_GraphicsEvent_Tag_KeyDown &&
            !event->keyDown.isRepeat && !state->ui.wantKeyboard) {
            if (event->keyDown.keyCode == OS_KeyCode_F5) {
                eng_sim_save_write_(&ctx);
            }
            if (event->keyDown.keyCode == OS_KeyCode_F9) {
                eng_sim_save_read_(&ctx);
            }
            if (event->keyDown.keyCode == OS_KeyCode_F6) {
                if (state->replay.mode == EngReplayMode_Recording) {
                    eng_sim_record_stop_(&ctx);
                } else {
                    eng_sim_record_start_(&ctx);
                }
            }
            if (event->keyDown.keyCode == OS_KeyCode_F7) {
                if (state->replay.mode == EngReplayMode_Playing) {
                    eng_sim_replay_stop_(&ctx);
                } else {
                    eng_sim_replay_start_(&ctx);
                }
            }
        }
    }

    text_frame_advance(state->render2d.textContext);

    EngRendererFrame* rendererFrame = 0;
    {
        PROF_SCOPE("renderer begin");
        rendererFrame = eng_renderer_begin_frame(&ctx);
    }
    if (rendererFrame) {
        if (project->frame) {
            PROF_SCOPE("scene");
            project->frame(&ctx, rendererFrame);
        }
        {
            PROF_SCOPE("ui panels");
            eng_panels_submit(&ctx, rendererFrame, input);
        }
        {
            PROF_SCOPE("renderer end");
            eng_renderer_end_frame(&ctx, rendererFrame);
        }
    }
}

static void eng_shutdown(EngHost* host, HOT_StateStore* store) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);

    EngContext ctx = {};
    if (!eng_context_from_call(host, store, &ctx)) {
        return;
    }

    eng_renderer_shutdown(&ctx);

    if (ctx.engine->jobSystem) {
        job_system_destroy(ctx.engine->jobSystem);
        ctx.engine->jobSystem = 0;
        ctx.engine->workerCount = 0;
    }
}

static B32 eng_resource_cache_init(EngContext* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->engine != 0);

    EngState* state = ctx->engine;
    if (state->resources.contentStore != 0 && state->resources.fileStream != 0 && state->resources.artifactCache != 0) {
        return 1;
    }

    if (!eng_ensure_job_system(ctx)) {
        return 0;
    }

    state->resources.arena = arena_alloc(.arenaSize = MB(16),
                                       .committedSize = KB(64),
                                       .flags = ArenaFlags_DoChain,
                                       .debugName = "engine/resources");
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
        eng_resource_cache_shutdown(ctx);
        return 0;
    }

    FileStreamDesc fileDesc = {};
    fileDesc.arena = state->resources.arena;
    fileDesc.content = state->resources.contentStore;
    fileDesc.initialFileCapacity = 16u;
    state->resources.fileStream = file_stream_alloc(&fileDesc);
    if (state->resources.fileStream == 0) {
        LOG_ERROR("resource", "Failed to create file stream");
        eng_resource_cache_shutdown(ctx);
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
        eng_resource_cache_shutdown(ctx);
        return 0;
    }

    if (!eng_assets_register_types_(ctx)) {
        LOG_ERROR("resource", "Failed to register artifact types");
        eng_resource_cache_shutdown(ctx);
        return 0;
    }

    eng_renderer_watch_files(ctx);
    return 1;
}

static void eng_resource_cache_shutdown(EngContext* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->engine != 0);

    EngState* state = ctx->engine;
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

    eng_renderer_resource_cache_reset(ctx);
}

static B32 eng_bind_current_module(EngContext* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->engine != 0);

    EngState* state = ctx->engine;
    if (state->resources.artifactCache && !eng_assets_register_types_(ctx)) {
        return 0;
    }
    if (state->resources.fileStream) {
        eng_renderer_watch_files(ctx);
    }
    return 1;
}


#if defined(PLATFORM_BUILD_DEBUG)
static U64 eng_newest_shader_source_timestamp(void) {
    U64 newestTimestamp = 0u;
    for (U32 index = 0; index < ARRAY_COUNT(ENG_DEV_SHADER_SOURCES); ++index) {
        OS_FileInfo info = OS_get_file_info(ENG_DEV_SHADER_SOURCES[index]);
        if (info.exists && info.lastWriteTimestampNs > newestTimestamp) {
            newestTimestamp = info.lastWriteTimestampNs;
        }
    }
    return newestTimestamp;
}

static void eng_try_build_dev_shaders(EngContext* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->engine != 0);

    EngState* state = ctx->engine;
    U64 sourceTimestamp = eng_newest_shader_source_timestamp();
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
    S32 buildResult = ENG_OS_CALL(ctx->host, OS_execute, buildCommand);
    if (buildResult != 0) {
        LOG_ERROR("gfx", "Shader build failed (exit code {})", buildResult);
        return;
    }

    state->gfxShaderBuild.initialized = 1;
    state->gfxShaderBuild.sourceTimestamp = sourceTimestamp;
}
#endif

static ArtifactKey eng_artifact_key_from_label(const char* label) {
    StringU8 labelStr = str8(label);
    return artifact_key_from_bytes(labelStr.data, labelStr.size);
}

static ArtifactKey eng_artifact_key_from_content(const char* label, ContentHash hash) {
    ArtifactKey result = eng_artifact_key_from_label(label);
    ArtifactKey contentKey = {};
    contentKey.hash[0] = hash.hash[0];
    contentKey.hash[1] = hash.hash[1];
    return artifact_key_mix(result, contentKey);
}

static ContentHash eng_content_hash_from_value(ArtifactValue value) {
    ContentHash result = {};
    result.hash[0] = value.u64[0];
    result.hash[1] = value.u64[1];
    return result;
}

static ArtifactValue eng_content_hash_to_value(ContentHash hash) {
    ArtifactValue result = {};
    result.u64[0] = hash.hash[0];
    result.u64[1] = hash.hash[1];
    return result;
}

static U32 eng_select_worker_count(const EngHost* host) {
    ASSERT_ALWAYS(host != 0);
    U32 logicalCores = host->logicalCoreCount;
    if (logicalCores == 0u) {
        ASSERT_ALWAYS(logicalCores != 0u);
        logicalCores = 1u;
    }
    U32 workers = (logicalCores > 1u) ? (logicalCores - 1u) : 1u;
    return workers;
}

static B32 eng_ensure_job_system(EngContext* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->engine != 0);
    ASSERT_ALWAYS(ctx->host->stateArena != 0);

    EngState* state = ctx->engine;
    if (state->jobSystem) {
        return 1;
    }

    state->workerCount = eng_select_worker_count(ctx->host);
    state->jobSystem = job_system_create(ctx->host->stateArena, state->workerCount);
    if (!state->jobSystem) {
        LOG_ERROR("jobs", "Failed to create job system (workers={})", state->workerCount);
        ASSERT_ALWAYS(state->jobSystem != 0);
        return 0;
    }

    LOG_INFO("jobs", "Job system ready (workers={})", state->workerCount);
    return 1;
}

ENG_EXPORT B32 eng_load(EngLoadParams* params, EngCode* outCode) {
    ASSERT_ALWAYS(params != 0);
    ASSERT_ALWAYS(outCode != 0);

    if (params == 0 || outCode == 0) {
        return 0;
    }
    if (params->size != sizeof(EngLoadParams) || params->abiVersion != ENG_ABI_VERSION) {
        return 0;
    }
    if (params->host == 0 || params->store == 0) {
        return 0;
    }

    MEMSET(outCode, 0, sizeof(*outCode));
    outCode->size = sizeof(*outCode);
    outCode->abiVersion = ENG_ABI_VERSION;
    outCode->schemaVersion = ENG_STATE_SCHEMA_VERSION;
    outCode->boot = eng_boot;
    outCode->before_reload = eng_before_reload;
    outCode->after_reload = eng_after_reload;
    outCode->frame = eng_frame;
    outCode->shutdown = eng_shutdown;
    return 1;
}
