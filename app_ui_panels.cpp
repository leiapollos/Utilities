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

    app_ui_stat_line(ui, UI_COLOR_TEXT_BRIGHT, "frame {}  reloads {}  [F1] stats  [F2] profiler",
                     gfx->frameIndex, state->reloadCount);
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

#define APP_PROF_MERGED_CAP 128u
#define APP_PROF_NIL 0xFFFFFFFFu

struct AppProfMergedNode {
    U32 site;
    U32 depth;
    U32 firstChild;
    U32 lastChild;
    U32 nextSibling;
};

static void app_ui_profiler_row(UI_Context* ui, const ProfSiteStats* stats, U32 site, U32 depth,
                                StringU8 rowKey, U32* ioSelectedSite) {
    const ProfSiteStats* siteStats = stats + site;

    UI_Signal rowSignal = ui_row_begin_keyed(ui, rowKey, ui_grow(1.0f), ui_px(22.0f),
                                             (*ioSelectedSite == site));
    if (rowSignal.clicked) {
        *ioSelectedSite = site;
    }

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena) {
        StringU8 indent = str8("");
        if (depth != 0u) {
            U64 pad = (U64)depth * 2u;
            U8* padBytes = ARENA_PUSH_ARRAY(scratch.arena, U8, pad);
            if (padBytes) {
                MEMSET(padBytes, ' ', pad);
                indent = str8(padBytes, pad);
            }
        }
        ui_label_colored(ui, str8_fmt((ui)->frameArena, "{}{}", indent, str8(siteStats->label)),
                         UI_COLOR_TEXT);
        temp_end(&scratch);
    }

    ui_spacer(ui, ui_grow(1.0f));
    app_ui_stat_line(ui, UI_COLOR_TEXT, "{}", siteStats->avgInclMs);
    app_ui_stat_line(ui, UI_COLOR_TEXT_DIM, "{}", siteStats->avgExclMs);
    app_ui_stat_line(ui, UI_COLOR_TEXT_DIM, "{}", siteStats->maxInclMs);
    app_ui_stat_line(ui, 0x6B7480FFu, "x{}", (U32)(siteStats->avgHits + 0.5f));
    ui_row_end(ui);
}

static void app_ui_profiler_panel(APP_Context* ctx, UI_Context* ui) {
    AppCoreState* state = ctx->core;
    ProfInfo info = prof_info();
    U32 siteCount = 0u;
    const ProfSiteStats* stats = prof_site_stats(&siteCount);
    if (!stats || siteCount <= 1u) {
        return;
    }

    UI_PanelDesc desc = {};
    desc.anchorX = 1.0f;
    desc.anchorY = 1.0f;
    desc.offsetX = -16.0f;
    desc.offsetY = -16.0f;
    desc.width = ui_px(640.0f);
    desc.height = ui_fit();
    ui_panel_begin(ui, str8("profiler###prof"), &desc);

    F32 averageFrameMs = state->averageDeltaSeconds * 1000.0f;
    F32 fps = (state->averageDeltaSeconds > 0.0f) ? (1.0f / state->averageDeltaSeconds) : 0.0f;
    app_ui_stat_line(ui, UI_COLOR_TEXT, "frame {}ms  {}fps  |  res {}ns  scope {}ns  drops {}  sites {}",
                     averageFrameMs, fps, info.resolutionNs, info.overheadNsPerScope,
                     info.droppedEvents, info.siteCount);

    U32 historyCount = 0u;
    U32 historyOffset = 0u;
    const F32* frameHistory = prof_frame_history(&historyCount, &historyOffset);
    if (frameHistory && historyCount != 0u) {
        ui_plot(ui, frameHistory, historyCount, historyOffset, ui_grow(1.0f), ui_px(48.0f));
    }

    ui_row_begin(ui, ui_grow(1.0f), ui_fit());
    B32 paused = prof_is_paused();
    if (ui_checkbox(ui, str8("pause"), &paused)) {
        prof_pause(paused);
    }
    ui_checkbox(ui, str8("flat"), &state->profFlatView);
    ui_spacer(ui, ui_grow(1.0f));
    if (ui_button(ui, str8("capture")).clicked) {
        prof_capture(64u, "captures");
    }
    ui_row_end(ui);

    ui_scroll_begin(ui, str8("###prof_scroll"), ui_grow(1.0f), ui_px(330.0f));
    if (state->profFlatView) {
        U32 order[24];
        U32 orderCount = 0u;
        for (U32 site = 1u; site < siteCount; ++site) {
            if (stats[site].avgInclMs <= 0.0f && stats[site].lastInclMs <= 0.0f) {
                continue;
            }
            U32 at = orderCount;
            if (at < ARRAY_COUNT(order)) {
                orderCount += 1u;
            } else {
                at = ARRAY_COUNT(order) - 1u;
                if (stats[order[at]].avgExclMs >= stats[site].avgExclMs) {
                    continue;
                }
            }
            while (at > 0u && stats[order[at - 1u]].avgExclMs < stats[site].avgExclMs) {
                order[at] = order[at - 1u];
                at -= 1u;
            }
            order[at] = site;
        }
        for (U32 at = 0u; at < orderCount; ++at) {
            app_ui_profiler_row(ui, stats, order[at], 0u,
                                str8_fmt(ui->frameArena, "###prof_flat_{}", at),
                                &state->profSelectedSite);
        }
    } else {
        const ProfFrameView* view = prof_frame_view();
        if (view) {
            for (U32 laneIndex = 0u; laneIndex < view->laneCount; ++laneIndex) {
                const ProfLaneView* lane = view->lanes + laneIndex;
                ui_label_colored(ui, str8_fmt(ui->frameArena, "[{}]", str8(lane->name)), 0x6B7480FFu);

                AppProfMergedNode merged[APP_PROF_MERGED_CAP];
                U32 mergedCount = 0u;
                U32 rootFirst = APP_PROF_NIL;
                U32 rootLast = APP_PROF_NIL;
                U32 depthStack[PROF_OPEN_STACK_DEPTH];
                for (U32 nodeIndex = 0u; nodeIndex < lane->nodeCount; ++nodeIndex) {
                    const ProfFrameNode* node = lane->nodes + nodeIndex;
                    U32 depth = node->depth;
                    if (depth >= PROF_OPEN_STACK_DEPTH) {
                        continue;
                    }
                    U32* firstLink = &rootFirst;
                    U32* lastLink = &rootLast;
                    if (depth != 0u) {
                        U32 parent = depthStack[depth - 1u];
                        if (parent == APP_PROF_NIL) {
                            depthStack[depth] = APP_PROF_NIL;
                            continue;
                        }
                        firstLink = &merged[parent].firstChild;
                        lastLink = &merged[parent].lastChild;
                    }
                    U32 found = APP_PROF_NIL;
                    for (U32 m = *firstLink; m != APP_PROF_NIL; m = merged[m].nextSibling) {
                        if (merged[m].site == node->site) {
                            found = m;
                            break;
                        }
                    }
                    if (found == APP_PROF_NIL) {
                        if (mergedCount >= APP_PROF_MERGED_CAP) {
                            depthStack[depth] = APP_PROF_NIL;
                            continue;
                        }
                        found = mergedCount;
                        mergedCount += 1u;
                        merged[found].site = node->site;
                        merged[found].depth = depth;
                        merged[found].firstChild = APP_PROF_NIL;
                        merged[found].lastChild = APP_PROF_NIL;
                        merged[found].nextSibling = APP_PROF_NIL;
                        if (*lastLink == APP_PROF_NIL) {
                            *firstLink = found;
                        } else {
                            merged[*lastLink].nextSibling = found;
                        }
                        *lastLink = found;
                    }
                    depthStack[depth] = found;
                }

                U32 walkStack[PROF_OPEN_STACK_DEPTH];
                U32 walkTop = 0u;
                U32 current = rootFirst;
                while (current != APP_PROF_NIL || walkTop != 0u) {
                    if (current == APP_PROF_NIL) {
                        walkTop -= 1u;
                        current = merged[walkStack[walkTop]].nextSibling;
                        continue;
                    }
                    app_ui_profiler_row(ui, stats, merged[current].site, merged[current].depth,
                                        str8_fmt(ui->frameArena, "###prof_m_{}_{}", laneIndex, current),
                                        &state->profSelectedSite);
                    if (merged[current].firstChild != APP_PROF_NIL && walkTop < PROF_OPEN_STACK_DEPTH) {
                        walkStack[walkTop] = current;
                        walkTop += 1u;
                        current = merged[current].firstChild;
                    } else {
                        current = merged[current].nextSibling;
                    }
                }
            }
        }
    }
    ui_scroll_end(ui);

    if (state->profSelectedSite != 0u && state->profSelectedSite < siteCount) {
        const ProfSiteStats* selected = stats + state->profSelectedSite;
        app_ui_stat_line(ui, UI_COLOR_TEXT_BRIGHT, "{}  avg {}ms  max {}ms",
                         str8(selected->label), selected->avgInclMs, selected->maxInclMs);
        ui_plot(ui, selected->historyMs, PROF_HISTORY_FRAMES, historyOffset, ui_grow(1.0f), ui_px(48.0f));
    }

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

    UI_Context* ui = 0;
    {
        PROF_SCOPE("ui begin");
        ui = ui_begin(&desc);
    }
    if (!ui) {
        return;
    }

    {
        PROF_SCOPE("ui build");
        {
            PROF_SCOPE("controls panel");
            app_ui_controls_panel(ctx, ui);
        }
        if (state->debugOverlayVisible) {
            PROF_SCOPE("stats panel");
            app_ui_stats_panel(ctx, ui);
        }
        if (state->profilerVisible) {
            PROF_SCOPE("profiler panel");
            app_ui_profiler_panel(ctx, ui);
        }
    }

    UI_Output output = {};
    {
        PROF_SCOPE("ui end");
        output = ui_end(ui);
    }
    app_renderer_apply_text_uploads(ctx, rendererFrame, output.uploads, output.uploadCount);
}
