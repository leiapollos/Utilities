//
// Created by André Leite on 12/06/2026.
//
// The demo's project table and per-frame policy: which cooked assets
// exist, how the state seeds, and the pre-drain work (event folding, the
// collision-world rebuild trigger, lane policy, the ambience loop).
//

#define DEMO_MODEL_DESC_ENTRY_(name) {DEMO_ASSET_COOKED_DIR #name ".umdl", "assets/" #name ".umdl"},
static const EngModelDesc DEMO_MODEL_DESCS[DemoModel_Count] = {
    DEMO_MODEL_LIST(DEMO_MODEL_DESC_ENTRY_)
};
#undef DEMO_MODEL_DESC_ENTRY_

#define DEMO_SOUND_DESC_ENTRY_(name) {DEMO_ASSET_COOKED_DIR #name ".uaud", "assets/" #name ".uaud"},
static const EngSoundDesc DEMO_SOUND_DESCS[DemoSound_Count] = {
    DEMO_SOUND_LIST(DEMO_SOUND_DESC_ENTRY_)
};
#undef DEMO_SOUND_DESC_ENTRY_

static void demo_state_init_(EngContext* ctx, void* memory) {
    DemoState* demo = (DemoState*)memory;
    MEMSET(demo, 0, sizeof(*demo));
    demo_settings_reset(&demo->settings);
    // Spawn just outside the grid's +x/-z corner: the grid is impassable
    // terrain (cell gaps are narrower than the player), so spawning
    // inside it would wedge the resolve between two cells on the first
    // tick. The free corner is also clear of the showcase models
    // (Lantern +x+z, Buggy -x-z).
    F32 gridExtent = demo_scene_grid_extent_(demo->settings.gridSide);
    demo->game.player.position.x = gridExtent + DEMO_SCENE_SPAWN_MARGIN;
    demo->game.player.position.y = DEMO_GAME_GROUND_Y + DEMO_GAME_PLAYER_RADIUS;
    demo->game.player.position.z = -(gridExtent + DEMO_SCENE_SPAWN_MARGIN);
    demo->game.playerPrevPosition = demo->game.player.position;
    // The ambience generation watcher auto-plays on publish when the
    // toggle is already on, so the knob just preloads the toggle.
    demo->ambienceOn = (B32)eng_env_u32_(str8("UTILITIES_DEMO_AMBIENCE"), 0u, 0u, 1u);
}

static void demo_pre_frame_(EngContext* ctx) {
    EngState* eng = ctx->engine;
    DemoState* demo = demo_state_(ctx);

    demo_game_fold_events_(ctx);

    // Lane policy for the world extraction (engine reads the request at
    // world frame open).
    if (demo->settings.threadedExtract && eng->jobSystem && eng->workerCount > 1u) {
        eng->world.requestedLaneCount = MIN(eng->workerCount,
                                            MIN(demo->settings.maxLanes, ENG_WORLD_MAX_LANES));
    } else {
        eng->world.requestedLaneCount = 1u;
    }

    // The collision world derives from the grid side alone; rebuild on
    // change (slider/env) or module reload (classifier edits must land
    // live), before any tick can read it. A live grid change invalidates
    // a running replay's world, so the replay stops first (a recording
    // keeps everything up to the change — no tick under the new grid has
    // run yet).
    U32 colliderSide = CLAMP(demo->settings.gridSide, DEMO_GRID_MIN, DEMO_GRID_MAX);
    B32 reloaded = demo->reloadSeen != eng->reloadCount;
    if (demo->colliderBuiltSide != colliderSide || reloaded) {
        if (demo->colliderBuiltSide != colliderSide && eng->replay.mode != EngReplayMode_Idle) {
            LOG_INFO("replay", "Grid {} -> {} invalidates the session; stopping",
                     demo->colliderBuiltSide, colliderSide);
            eng_sim_replay_stop_(ctx);
        }
        demo_scene_build_colliders_(colliderSide, &demo->colliders);
        demo->colliderBuiltSide = colliderSide;
        demo->reloadSeen = eng->reloadCount;
        LOG_INFO("game", "Collision world rebuilt: {} colliders ({} dropped) side {}",
                 demo->colliders.count, demo->colliders.dropped, colliderSide);
    }

    // Ambience rides sound publish generations: a (re)publish while the
    // toggle is on restarts the loop — project policy, not engine's.
    U32 ambienceGeneration = eng->audio.soundGenerations[DemoSound_Ambience];
    if (demo->ambienceGenerationSeen != ambienceGeneration) {
        demo->ambienceGenerationSeen = ambienceGeneration;
        if (demo->ambienceOn) {
            audio_play(ctx->host->audioSystem, eng->audio.sounds[DemoSound_Ambience],
                       DEMO_SOUND_GAIN_AMBIENCE, 1);
        }
    }
}

static void demo_debug_stats_(EngContext* ctx, UI_Context* ui) {
    EngState* eng = ctx->engine;
    DemoState* demo = demo_state_(ctx);
    if (demo->settings.playerMode) {
        const DemoTickStats* tick = &demo->game.lastTickStats;
        ui_label_value(ui, UI_COLOR_TEXT_DIM, "collision  colliders {}  contacts {}  depth {}  tick {}us",
                      demo->colliders.count, tick->contactCount,
                      (F64)tick->deepestDepth, eng->lastSimTickNanos / 1000ull);
    }
}

static const EngProject* eng_project_(void) {
    static const EngProject project = {
        .name = "demo",
        .stateId = ENG_STATE_ID('D', 'E', 'M', 'O'),
        .stateVersion = DEMO_STATE_VERSION,
        .stateSize = sizeof(DemoState),
        .stateAlignment = alignof(DemoState),
        .capabilities = ENG_CAP_WORLD3D | ENG_CAP_SIM | ENG_CAP_AUDIO,
        .models = DEMO_MODEL_DESCS,
        .modelCount = (U32)DemoModel_Count,
        .sounds = DEMO_SOUND_DESCS,
        .soundCount = (U32)DemoSound_Count,
        .actionSize = (U32)sizeof(DemoActions),
        .saveSize = (U32)sizeof(DemoSaveState),
        .saveVersion = 1u,
        .state_init = demo_state_init_,
        .pre_frame = demo_pre_frame_,
        .sim_sample = demo_sim_sample_,
        .sim_tick = demo_sim_tick_,
        .sim_capture = demo_sim_capture_,
        .sim_apply = demo_sim_apply_,
        .sim_checksum = demo_sim_checksum_,
        .frame = demo_scene_submit,
        .panels = demo_panels_,
        .panels_post = demo_panels_post_,
        .debug_stats = demo_debug_stats_,
    };
    return &project;
}
