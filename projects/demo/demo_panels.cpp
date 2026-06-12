//
// Created by André Leite on 10/06/2026.
//
// The demo's controls panel and its panel hooks. The engine owns the UI
// frame shell, the stats panel, and the profiler; the demo contributes
// its panel before those and the click sound after all of them.
//

static void demo_settings_reset(DemoSettings* demo) {
    StringU8 title = str8("world: gpu-driven indirect draws");
    MEMSET(demo, 0, sizeof(*demo));
    MEMCPY(demo->titleBuffer, title.data, title.size);
    demo->titleLength = (U32)title.size;
    demo->titleSize = 32.0f;
    demo->showBounds = 0;
    demo->animate = 1;
    demo->threadedExtract = (B32)eng_env_u32_(str8("UTILITIES_DEMO_THREADED"), 1u, 0u, 1u);
    demo->playerMode = (B32)eng_env_u32_(str8("UTILITIES_DEMO_PLAYER"), 0u, 0u, 1u);
    demo->maxLanes = eng_env_u32_(str8("UTILITIES_DEMO_MAX_LANES"), ENG_WORLD_MAX_LANES,
                                  1u, ENG_WORLD_MAX_LANES);
    demo->gridSide = eng_env_u32_(str8("UTILITIES_DEMO_GRID"), 48u,
                                  DEMO_GRID_MIN, DEMO_GRID_MAX);
}

static void demo_controls_panel(EngContext* ctx, UI_Context* ui) {
    DemoState* demoState = demo_state_(ctx);
    DemoSettings* demo = &demoState->settings;

    UI_PanelDesc desc = {};
    desc.anchorX = 0.0f;
    desc.anchorY = 0.0f;
    desc.offsetX = 40.0f;
    desc.offsetY = 440.0f;
    desc.width = ui_px(480.0f);
    desc.height = ui_fit();
    ui_panel_begin(ui, str8("controls###controls"), &desc);

    ui_row_begin(ui, ui_grow(1.0f), ui_fit());
    ui_label(ui, str8("title"));
    ui_text_edit(ui, str8("###title_edit"), demo->titleBuffer,
                 (U32)sizeof(demo->titleBuffer), &demo->titleLength);
    ui_row_end(ui);

    ui_slider(ui, str8("size###title_size"), &demo->titleSize, 20.0f, 60.0f);

    F32 gridSide = (F32)demo->gridSide;
    ui_slider(ui, str8("grid###world_grid"), &gridSide, (F32)DEMO_GRID_MIN, (F32)DEMO_GRID_MAX);
    demo->gridSide = (U32)(gridSide + 0.5f);

    ui_checkbox(ui, str8("cull bounds"), &demo->showBounds);
    ui_checkbox(ui, str8("animate"), &demo->animate);
    ui_checkbox(ui, str8("threaded extract"), &demo->threadedExtract);
    ui_checkbox(ui, str8("player mode"), &demo->playerMode);
    if (ui_checkbox(ui, str8("ambience"), &demoState->ambienceOn)) {
        if (demoState->ambienceOn) {
            audio_play(ctx->host->audioSystem, ctx->engine->audio.sounds[DemoSound_Ambience],
                       DEMO_SOUND_GAIN_AMBIENCE, 1);
        } else {
            audio_stop(ctx->host->audioSystem, ctx->engine->audio.sounds[DemoSound_Ambience]);
        }
    }

    EngReplay* replay = &ctx->engine->replay;
    ui_row_begin(ui, ui_grow(1.0f), ui_fit());
    if (ui_button(ui, str8("save [F5]###persist_save")).clicked) {
        eng_sim_save_write_(ctx);
    }
    if (ui_button(ui, str8("load [F9]###persist_load")).clicked) {
        eng_sim_save_read_(ctx);
    }
    if (ui_button(ui, replay->mode == EngReplayMode_Recording
                          ? str8("stop rec [F6]###replay_rec")
                          : str8("record [F6]###replay_rec")).clicked) {
        if (replay->mode == EngReplayMode_Recording) {
            eng_sim_record_stop_(ctx);
        } else {
            eng_sim_record_start_(ctx);
        }
    }
    if (ui_button(ui, replay->mode == EngReplayMode_Playing
                          ? str8("stop play [F7]###replay_play")
                          : str8("replay [F7]###replay_play")).clicked) {
        if (replay->mode == EngReplayMode_Playing) {
            eng_sim_replay_stop_(ctx);
        } else {
            eng_sim_replay_start_(ctx);
        }
    }
    ui_row_end(ui);
    {
        const char* modeName = replay->mode == EngReplayMode_Recording ? "recording"
                             : replay->mode == EngReplayMode_Playing ? "playing"
                             : "idle";
        StringU8 status;
        if (replay->divergedAtTick != 0ull) {
            status = str8_fmt(ui->frameArena, "replay {} {}/{}  DIVERGED @{}",
                              str8(modeName), replay->cursor,
                              replay->mode == EngReplayMode_Recording ? ENG_REPLAY_MAX_TICKS
                                                                      : replay->tickCount,
                              replay->divergedAtTick);
        } else {
            status = str8_fmt(ui->frameArena, "replay {} {}/{}", str8(modeName), replay->cursor,
                              replay->mode == EngReplayMode_Recording ? ENG_REPLAY_MAX_TICKS
                                                                      : replay->tickCount);
        }
        ui_label(ui, status);
    }

    ui_row_begin(ui, ui_grow(1.0f), ui_fit());
    if (ui_button(ui, str8("reset demo")).clicked) {
        demo_settings_reset(demo);
    }
    ui_spacer(ui, ui_grow(1.0f));
    if (ui_button(ui, str8("hide stats [F1]")).clicked) {
        ctx->engine->debugOverlayVisible = !ctx->engine->debugOverlayVisible;
    }
    ui_row_end(ui);


    ui_panel_end(ui);
}

static void demo_panels_(EngContext* ctx, UI_Context* ui) {
    PROF_SCOPE("controls panel");
    demo_controls_panel(ctx, ui);
}

// One site for the whole UI: any widget click this frame ticks.
static void demo_panels_post_(EngContext* ctx, UI_Context* ui) {
    if (ui->clickedKey != 0u) {
        audio_play(ctx->host->audioSystem, ctx->engine->audio.sounds[DemoSound_Click],
                   DEMO_SOUND_GAIN_CLICK, 0);
    }
}
