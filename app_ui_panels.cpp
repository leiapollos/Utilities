//
// Created by André Leite on 10/06/2026.
//

#define app_ui_stat_line(ui, rgba8, fmt, ...) \
    ui_label_colored((ui), str8_fmt((ui)->frameArena, fmt, __VA_ARGS__), (rgba8))

static void app_demo_state_reset(AppDemoState* demo) {
    StringU8 title = str8("draw2d + kb_text_shape + FreeType");
    MEMSET(demo, 0, sizeof(*demo));
    MEMCPY(demo->titleBuffer, title.data, title.size);
    demo->titleLength = (U32)title.size;
    demo->titleSize = 40.0f;
    demo->showClipDemo = 1;
    demo->showMarker = 1;
}

static void app_ui_controls_panel(APP_Context* ctx, UI_Context* ui) {
    AppDemoState* demo = &ctx->core->demo;

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

    ui_checkbox(ui, str8("clip showcase"), &demo->showClipDemo);
    ui_checkbox(ui, str8("debug marker"), &demo->showMarker);

    ui_row_begin(ui, ui_grow(1.0f), ui_fit());
    if (ui_button(ui, str8("reset demo")).clicked) {
        app_demo_state_reset(demo);
    }
    ui_spacer(ui, ui_grow(1.0f));
    if (ui_button(ui, str8("hide stats [F1]")).clicked) {
        ctx->core->debugOverlayVisible = !ctx->core->debugOverlayVisible;
    }
    ui_row_end(ui);

    ui_scroll_begin(ui, str8("###controls_scroll"), ui_grow(1.0f), ui_px(150.0f));
    for (U32 lineIndex = 0u; lineIndex < 24u; ++lineIndex) {
        app_ui_stat_line(ui, (lineIndex & 1u) ? UI_COLOR_TEXT_DIM : UI_COLOR_TEXT,
                         "scroll line {} — wheel or drag the thumb", lineIndex);
    }
    ui_scroll_end(ui);

    ui_panel_end(ui);
}

static void app_ui_stats_panel(APP_Context* ctx, UI_Context* ui) {
    AppCoreState* state = ctx->core;
    const GfxStats* gfx = &state->render2d.lastGfxStats;
    const Draw2DStats* draw2d = &state->render2d.lastDraw2DStats;
    const UI_Stats* uiStats = &state->ui.stats;

    UI_PanelDesc desc = {};
    desc.anchorX = 1.0f;
    desc.anchorY = 0.0f;
    desc.offsetX = -16.0f;
    desc.offsetY = 16.0f;
    desc.width = ui_px(560.0f);
    desc.height = ui_fit();
    ui_panel_begin(ui, str8("debug###stats"), &desc);

    F32 frameMs = state->lastDeltaSeconds * 1000.0f;
    F32 averageFrameMs = state->averageDeltaSeconds * 1000.0f;
    F32 fps = (state->averageDeltaSeconds > 0.0f) ? (1.0f / state->averageDeltaSeconds) : 0.0f;
    F32 bodyMs = state->lastWorkSeconds * 1000.0f;
    F32 sleepMs = state->lastSleepSeconds * 1000.0f;
    F32 workMs = bodyMs - gfx->gpuWaitMs - gfx->acquireWaitMs;
    if (workMs < 0.0f) {
        workMs = 0.0f;
    }
    F32 hostMs = frameMs - bodyMs - sleepMs;
    if (hostMs < 0.0f) {
        hostMs = 0.0f;
    }

    app_ui_stat_line(ui, UI_COLOR_TEXT_BRIGHT, "frame {}  reloads {}  [F1]", gfx->frameIndex, state->reloadCount);
    app_ui_stat_line(ui, UI_COLOR_TEXT, "frame {}ms  avg {}ms  {}fps", frameMs, averageFrameMs, fps);
    app_ui_stat_line(ui, UI_COLOR_TEXT, "work {}us  gpu {}us  acq {}us  sleep {}us  host {}us",
                     (U32)(workMs * 1000.0f + 0.5f),
                     (U32)(gfx->gpuWaitMs * 1000.0f + 0.5f),
                     (U32)(gfx->acquireWaitMs * 1000.0f + 0.5f),
                     (U32)(sleepMs * 1000.0f + 0.5f),
                     (U32)(hostMs * 1000.0f + 0.5f));
    for (U32 passIndex = 0u; passIndex < gfx->passGpuCount; ++passIndex) {
        app_ui_stat_line(ui, UI_COLOR_TEXT, "gpu pass[{}] {}ms", passIndex, gfx->passGpuMs[passIndex]);
    }
    app_ui_stat_line(ui, UI_COLOR_TEXT_DIM, "gfx  draws {}  dispatches {}  pso {}  table {}",
                     gfx->drawCount, gfx->dispatchCount, gfx->pipelineSwitchCount, gfx->resourceTableCount);
    app_ui_stat_line(ui, UI_COLOR_TEXT_DIM, "temp {}KB ovf {}   staging {}KB ovf {}",
                     gfx->tempBytesUsed / 1024u, gfx->tempOverflowCount,
                     gfx->stagingBytesUsed / 1024u, gfx->stagingOverflowCount);
    app_ui_stat_line(ui, UI_COLOR_TEXT_DIM, "draw2d  quads {}  clipped {}  dropped {}  batches {}",
                     draw2d->quadsSubmitted, draw2d->quadsClipped, draw2d->quadsDropped, draw2d->batchesBuilt);
    app_ui_stat_line(ui, UI_COLOR_TEXT_DIM, "ui  widgets {}  retained {}  hits {}  evict {}  dup {}",
                     uiStats->widgetCount, uiStats->retainedCount, uiStats->hitRectCount,
                     uiStats->retainedEvictCount, uiStats->duplicateKeyCount);

    if (state->resources.artifactCache) {
        ArtifactStats artifact = artifact_cache_stats(state->resources.artifactCache);
        app_ui_stat_line(ui, UI_COLOR_TEXT_DIM, "artifact  live {}  working {}  hits {}  misses {}  {}KB",
                         artifact.liveCount, artifact.workingCount, artifact.hits, artifact.misses,
                         artifact.bytesLive / 1024u);
    }
    if (state->resources.contentStore) {
        ContentStats content = content_stats(state->resources.contentStore);
        app_ui_stat_line(ui, UI_COLOR_TEXT_DIM, "content  blobs {}  keys {}  {}KB  hits {}  misses {}",
                         content.blobCount, content.keyCount, content.payloadBytes / 1024u,
                         content.hitCount, content.missCount);
    }
    if (state->resources.fileStream) {
        FileStreamStats files = file_stream_stats(state->resources.fileStream);
        app_ui_stat_line(ui, UI_COLOR_TEXT_DIM, "files  watched {}  checked {}  published {}  failed {}",
                         files.fileCount, files.checkedCount, files.publishCount, files.failedCount);
    }

    app_ui_stat_line(ui, 0x6B7480FFu, "window {}x{}  workers {}",
                     state->windowWidth, state->windowHeight, state->workerCount);

    ui_panel_end(ui);
}

static void app_ui_panels_submit(APP_Context* ctx, AppRendererFrame* rendererFrame, const AppInput* input) {
    AppCoreState* state = ctx->core;
    AppRender2DState* render = &state->render2d;
    if (render->textContext == 0 || render->font.generation == 0u) {
        return;
    }

    UI_BeginDesc desc = {};
    desc.state = &state->ui;
    desc.frameArena = ctx->host->frameArena;
    desc.textContext = render->textContext;
    desc.font = render->font;
    desc.draw2d = &render->draw2d;
    desc.events = input->events;
    desc.eventCount = input->eventCount;
    desc.viewportWidth = (F32)ctx->host->windowWidth;
    desc.viewportHeight = (F32)ctx->host->windowHeight;
    desc.deltaSeconds = input->deltaSeconds;

    UI_Context* ui = ui_begin(&desc);
    if (!ui) {
        return;
    }

    app_ui_controls_panel(ctx, ui);
    if (state->debugOverlayVisible) {
        app_ui_stats_panel(ctx, ui);
    }

    UI_Output output = ui_end(ui);
    app_renderer_apply_text_uploads(ctx, rendererFrame, output.uploads, output.uploadCount);
}
