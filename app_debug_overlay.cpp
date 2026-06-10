//
// Created by André Leite on 10/06/2026.
//

#define APP_DEBUG_OVERLAY_TEXT_SIZE 20.0f
#define APP_DEBUG_OVERLAY_LINE_HEIGHT 26.0f
#define APP_DEBUG_OVERLAY_WIDTH 560.0f

struct AppDebugOverlayCursor {
    APP_Context* ctx;
    AppRendererFrame* rendererFrame;
    F32 x;
    F32 y;
};

static void app_debug_overlay_line_(AppDebugOverlayCursor* cursor, StringU8 text, U32 rgba8) {
    debug_draw_text(cursor->ctx, cursor->rendererFrame, cursor->x, cursor->y, APP_DEBUG_OVERLAY_TEXT_SIZE, rgba8, text);
    cursor->y += APP_DEBUG_OVERLAY_LINE_HEIGHT;
}

#define app_debug_overlay_line(cursor, rgba8, fmt, ...) \
    app_debug_overlay_line_((cursor), str8_fmt(scratch.arena, fmt, __VA_ARGS__), (rgba8))

static void app_debug_overlay_submit(APP_Context* ctx, AppRendererFrame* rendererFrame) {
    AppCoreState* state = ctx->core;
    if (!state->debugOverlayVisible) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena == 0) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    const GfxStats* gfx = &state->render2d.lastGfxStats;
    const Draw2DStats* draw2d = &state->render2d.lastDraw2DStats;

    F32 panelMaxX = (F32)ctx->host->windowWidth - 16.0f;
    F32 panelMinX = panelMaxX - APP_DEBUG_OVERLAY_WIDTH;
    F32 panelMinY = 16.0f;

    U32 lineCount = 9u + gfx->passGpuCount;
    F32 panelMaxY = panelMinY + 16.0f + (F32)lineCount * APP_DEBUG_OVERLAY_LINE_HEIGHT + 16.0f;
    debug_draw_rect(ctx, panelMinX, panelMinY, panelMaxX, panelMaxY, 0x10141AE6u);
    debug_draw_box(ctx, panelMinX, panelMinY, panelMaxX, panelMaxY, 1.0f, 0x3A4148FFu);

    AppDebugOverlayCursor cursor = {};
    cursor.ctx = ctx;
    cursor.rendererFrame = rendererFrame;
    cursor.x = panelMinX + 16.0f;
    cursor.y = panelMinY + 12.0f;

    F32 lastMs = state->lastDeltaSeconds * 1000.0f;
    F32 averageMs = state->averageDeltaSeconds * 1000.0f;
    F32 fps = (state->averageDeltaSeconds > 0.0f) ? (1.0f / state->averageDeltaSeconds) : 0.0f;
    app_debug_overlay_line(&cursor, 0xF4F1E8FFu, "debug  frame {}  reloads {}  [F1]", gfx->frameIndex, state->reloadCount);
    app_debug_overlay_line(&cursor, 0xD9D4C7FFu, "cpu {}ms  avg {}ms  {}fps", lastMs, averageMs, fps);
    for (U32 passIndex = 0u; passIndex < gfx->passGpuCount; ++passIndex) {
        app_debug_overlay_line(&cursor, 0xD9D4C7FFu, "gpu pass[{}] {}ms", passIndex, gfx->passGpuMs[passIndex]);
    }
    app_debug_overlay_line(&cursor, 0xB9C4D1FFu, "gfx  draws {}  dispatches {}  pso {}  table {}",
                           gfx->drawCount, gfx->dispatchCount, gfx->pipelineSwitchCount, gfx->resourceTableCount);
    app_debug_overlay_line(&cursor, 0xB9C4D1FFu, "temp {}KB ovf {}   staging {}KB ovf {}",
                           gfx->tempBytesUsed / 1024u, gfx->tempOverflowCount,
                           gfx->stagingBytesUsed / 1024u, gfx->stagingOverflowCount);
    app_debug_overlay_line(&cursor, 0xB9C4D1FFu, "draw2d  quads {}  clipped {}  dropped {}  batches {}",
                           draw2d->quadsSubmitted, draw2d->quadsClipped, draw2d->quadsDropped, draw2d->batchesBuilt);

    if (state->resources.artifactCache) {
        ArtifactStats artifact = artifact_cache_stats(state->resources.artifactCache);
        app_debug_overlay_line(&cursor, 0x9AA7B4FFu, "artifact  live {}  working {}  hits {}  misses {}  {}KB",
                               artifact.liveCount, artifact.workingCount, artifact.hits, artifact.misses,
                               artifact.bytesLive / 1024u);
    }
    if (state->resources.contentStore) {
        ContentStats content = content_stats(state->resources.contentStore);
        app_debug_overlay_line(&cursor, 0x9AA7B4FFu, "content  blobs {}  keys {}  {}KB  hits {}  misses {}",
                               content.blobCount, content.keyCount, content.payloadBytes / 1024u,
                               content.hitCount, content.missCount);
    }
    if (state->resources.fileStream) {
        FileStreamStats files = file_stream_stats(state->resources.fileStream);
        app_debug_overlay_line(&cursor, 0x9AA7B4FFu, "files  watched {}  checked {}  published {}  failed {}",
                               files.fileCount, files.checkedCount, files.publishCount, files.failedCount);
    }

    app_debug_overlay_line(&cursor, 0x6B7480FFu, "window {}x{}  workers {}",
                           state->windowWidth, state->windowHeight, state->workerCount);
}
