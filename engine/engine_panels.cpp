//
// Created by André Leite on 10/06/2026.
//

#define eng_stat_line(ui, rgba8, fmt, ...) \
    ui_label_value((ui), (rgba8), fmt, __VA_ARGS__)

static void eng_stats_panel(EngContext* ctx, UI_Context* ui) {
    EngState* state = ctx->engine;
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

    eng_stat_line(ui, UI_COLOR_TEXT_BRIGHT, "frame {}  reloads {}  [F1] stats  [F2] profiler",
                     gfx->frameIndex, state->reloadCount);
    eng_stat_line(ui, UI_COLOR_TEXT_DIM, "gfx  draws {}  dispatches {}  pso {}  table {}",
                     gfx->drawCount, gfx->dispatchCount, gfx->pipelineSwitchCount, gfx->resourceTableCount);
    eng_stat_line(ui, UI_COLOR_TEXT_DIM, "temp {}KB ovf {}   staging {}KB ovf {}",
                     gfx->tempBytesUsed / 1024u, gfx->tempOverflowCount,
                     gfx->stagingBytesUsed / 1024u, gfx->stagingOverflowCount);
    eng_stat_line(ui, UI_COLOR_TEXT_DIM, "draw2d  quads {}  clipped {}  dropped {}  batches {}",
                     draw2d->quadsSubmitted, draw2d->quadsClipped, draw2d->quadsDropped, draw2d->batchesBuilt);
    eng_stat_line(ui, UI_COLOR_TEXT_DIM, "ui  widgets {}  retained {}  hits {}  evict {}  dup {}",
                     uiStats->widgetCount, uiStats->retainedCount, uiStats->hitRectCount,
                     uiStats->retainedEvictCount, uiStats->duplicateKeyCount);
    eng_stat_line(ui, UI_COLOR_TEXT_DIM, "ui text  value hits {}  misses {}  uncached {}",
                     uiStats->valueRunHits, uiStats->valueRunMisses, uiStats->valueRunUninsertable);
    if (eng_project_()->capabilities & ENG_CAP_WORLD3D) {
        eng_stat_line(ui, UI_COLOR_TEXT_DIM, "world  renderables {}  dropped {}  lanes {}  meshes {}  tdraws {}",
                         state->world.lastRenderableCount, state->world.lastDroppedCount,
                         state->world.laneCount, state->world.meshCount,
                         state->world.lastTransparentDraws);
    }
    if (eng_project_()->capabilities & ENG_CAP_AUDIO) {
        AudioStats audioStats = audio_stats(ctx->host->audioSystem);
        eng_stat_line(ui, UI_COLOR_TEXT_DIM, "audio  voices {}/{}  buffers {}  drop v{} c{}  cb {}us max {}us",
                         audioStats.voicesActive, AUDIO_VOICE_COUNT, audioStats.buffersLive,
                         audioStats.voicesDropped, audioStats.commandsDropped,
                         audioStats.lastCallbackNanos / 1000ull, audioStats.maxCallbackNanos / 1000ull);
    }
    {
        const EngProject* project = eng_project_();
        if (project->debug_stats) {
            project->debug_stats(ctx, ui);
        }
    }

    if (state->resources.artifactCache) {
        ArtifactStats artifact = artifact_cache_stats(state->resources.artifactCache);
        eng_stat_line(ui, UI_COLOR_TEXT_DIM, "artifact  live {}  working {}  hits {}  misses {}  {}KB",
                         artifact.liveCount, artifact.workingCount, artifact.hits, artifact.misses,
                         artifact.bytesLive / 1024u);
    }
    if (state->resources.contentStore) {
        ContentStats content = content_stats(state->resources.contentStore);
        eng_stat_line(ui, UI_COLOR_TEXT_DIM, "content  blobs {}  keys {}  {}KB  hits {}  misses {}",
                         content.blobCount, content.keyCount, content.payloadBytes / 1024u,
                         content.hitCount, content.missCount);
    }
    if (state->resources.fileStream) {
        FileStreamStats files = file_stream_stats(state->resources.fileStream);
        eng_stat_line(ui, UI_COLOR_TEXT_DIM, "files  watched {}  checked {}  published {}  failed {}",
                         files.fileCount, files.checkedCount, files.publishCount, files.failedCount);
    }

    eng_stat_line(ui, 0x6B7480FFu, "window {}x{}  workers {}",
                     state->windowWidth, state->windowHeight, state->workerCount);

    ui_panel_end(ui);
}

static void eng_profiler_row(UI_Context* ui, const ProfSiteStats* stats, U32 site, U32 depth,
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
        ui_label_value(ui, UI_COLOR_TEXT, "{}{}", indent, str8(siteStats->label));
        temp_end(&scratch);
    }

    ui_spacer(ui, ui_grow(1.0f));
    eng_stat_line(ui, UI_COLOR_TEXT, "{:.3}", siteStats->avgInclMs);
    eng_stat_line(ui, UI_COLOR_TEXT_DIM, "{:.3}", siteStats->avgExclMs);
    eng_stat_line(ui, UI_COLOR_TEXT_DIM, "{:.3}", siteStats->maxInclMs);
    eng_stat_line(ui, 0x6B7480FFu, "x{}", (U32)(siteStats->avgHits + 0.5f));
    ui_row_end(ui);
}

static void eng_profiler_path_row(UI_Context* ui, const ProfSiteStats* stats,
                                     const ProfPathStats* path, U32 pathIndex, U32* ioSelectedSite) {
    const ProfSiteStats* siteStats = stats + path->site;

    UI_Signal rowSignal = ui_row_begin_keyed(ui, str8_fmt(ui->frameArena, "###prof_p_{}", pathIndex),
                                             ui_grow(1.0f), ui_px(22.0f),
                                             (*ioSelectedSite == path->site));
    if (rowSignal.clicked) {
        *ioSelectedSite = path->site;
    }

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena) {
        StringU8 indent = str8("");
        if (path->depth != 0u) {
            U64 pad = (U64)path->depth * 2u;
            U8* padBytes = ARENA_PUSH_ARRAY(scratch.arena, U8, pad);
            if (padBytes) {
                MEMSET(padBytes, ' ', pad);
                indent = str8(padBytes, pad);
            }
        }
        ui_label_value(ui, UI_COLOR_TEXT, "{}{}", indent, str8(siteStats->label));
        temp_end(&scratch);
    }

    ui_spacer(ui, ui_grow(1.0f));
    eng_stat_line(ui, UI_COLOR_TEXT, "{:.3}", path->avgInclMs);
    eng_stat_line(ui, UI_COLOR_TEXT_DIM, "{:.3}", path->avgExclMs);
    eng_stat_line(ui, UI_COLOR_TEXT_DIM, "{:.3}", path->maxInclMs);
    eng_stat_line(ui, 0x6B7480FFu, "x{}", (U32)(path->avgHits + 0.5f));
    ui_row_end(ui);
}

static B32 eng_profiler_path_live(const ProfPathStats* path) {
    return (path->avgInclMs > 0.0f || path->lastInclMs > 0.0f || path->avgHits > 0.0f) ? 1 : 0;
}

static void eng_profiler_panel(EngContext* ctx, UI_Context* ui) {
    EngState* state = ctx->engine;
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
    eng_stat_line(ui, UI_COLOR_TEXT, "frame {:.2}ms  {:.0}fps  tick {}  clamps {}  |  res {:.0}ns  scope {:.1}ns  drops {}  sites {}",
                     averageFrameMs, fps, (U32)state->simTickCounter, state->simClampCount,
                     info.resolutionNs, info.overheadNsPerScope,
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
            eng_profiler_row(ui, stats, order[at], 0u,
                                str8_fmt(ui->frameArena, "###prof_flat_{}", at),
                                &state->profSelectedSite);
        }
    } else {
        PROF_SCOPE("tree view");
        U32 pathCount = 0u;
        const ProfPathStats* paths = prof_path_stats(&pathCount);
        const ProfFrameView* view = prof_frame_view();
        if (view && paths) {
            for (U32 laneIndex = 0u; laneIndex < view->laneCount; ++laneIndex) {
                const ProfLaneView* lane = view->lanes + laneIndex;
                ui_label_value(ui, 0x6B7480FFu, "[{}]", str8(lane->name));

                for (U32 root = 1u; root < pathCount; ++root) {
                    if (paths[root].parent != PROF_PATH_NIL || paths[root].thread != lane->threadIndex) {
                        continue;
                    }
                    if (!eng_profiler_path_live(paths + root)) {
                        continue;
                    }
                    eng_profiler_path_row(ui, stats, paths + root, root, &state->profSelectedSite);

                    U32 walkStack[PROF_OPEN_STACK_DEPTH];
                    U32 walkTop = 0u;
                    U32 current = paths[root].firstChild;
                    while (current != PROF_PATH_NIL || walkTop != 0u) {
                        if (current == PROF_PATH_NIL) {
                            walkTop -= 1u;
                            current = paths[walkStack[walkTop]].nextSibling;
                            continue;
                        }
                        if (!eng_profiler_path_live(paths + current)) {
                            current = paths[current].nextSibling;
                            continue;
                        }
                        eng_profiler_path_row(ui, stats, paths + current, current,
                                                 &state->profSelectedSite);
                        if (paths[current].firstChild != PROF_PATH_NIL && walkTop < PROF_OPEN_STACK_DEPTH) {
                            walkStack[walkTop] = current;
                            walkTop += 1u;
                            current = paths[current].firstChild;
                        } else {
                            current = paths[current].nextSibling;
                        }
                    }
                }
            }
        }
    }
    ui_scroll_end(ui);

    if (state->profSelectedSite != 0u && state->profSelectedSite < siteCount) {
        const ProfSiteStats* selected = stats + state->profSelectedSite;
        eng_stat_line(ui, UI_COLOR_TEXT_BRIGHT, "{}  avg {:.3}ms  max {:.3}ms",
                         str8(selected->label), selected->avgInclMs, selected->maxInclMs);
        ui_plot(ui, selected->historyMs, PROF_HISTORY_FRAMES, historyOffset, ui_grow(1.0f), ui_px(48.0f));
    }

    ui_panel_end(ui);
}

static void eng_panels_submit(EngContext* ctx, EngRendererFrame* rendererFrame, const EngInput* input) {
    EngState* state = ctx->engine;
    EngRender2D* render = &state->render2d;
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

    const EngProject* project = eng_project_();
    {
        PROF_SCOPE("ui build");
        if (project->panels) {
            project->panels(ctx, ui);
        }
        if (state->debugOverlayVisible) {
            PROF_SCOPE("stats panel");
            eng_stats_panel(ctx, ui);
        }
        if (state->profilerVisible) {
            PROF_SCOPE("profiler panel");
            eng_profiler_panel(ctx, ui);
        }
    }

    if (project->panels_post) {
        project->panels_post(ctx, ui);
    }

    UI_Output output = {};
    {
        PROF_SCOPE("ui end");
        output = ui_end(ui);
    }
    eng_renderer_apply_text_uploads(ctx, rendererFrame, output.uploads, output.uploadCount);
}
