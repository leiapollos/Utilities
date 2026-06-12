//
// Created by André Leite on 10/06/2026.
//
// The tabbed debug window. Every counter the systems already publish is
// shown against its budget: meters for capacities, sparklines for the
// per-frame series, and red values for anything dropped, overflowed, or
// failed. F1 toggles the window, F2 jumps to the profiler tab.
//

#define ENG_DBG_COLOR_HEADER 0x6B7480FFu
#define ENG_DBG_COLOR_WARN 0xD96A6AFFu
#define ENG_DBG_COLOR_AMBER 0xC9A86AFFu
#define ENG_DBG_COLOR_OK 0x4F8A6AFFu

#define eng_dbg_kv(ui, name, valueColor, fmt, ...)                       \
    do {                                                                 \
        ui_row_begin((ui), ui_grow(1.0f), ui_fit());                     \
        ui_label_colored((ui), str8(name), UI_COLOR_TEXT_DIM);           \
        ui_spacer((ui), ui_grow(1.0f));                                  \
        ui_label_value((ui), (valueColor), fmt, __VA_ARGS__);            \
        ui_row_end(ui);                                                  \
    } while (0)

static B32 eng_dbg_cstr_eq_(const char* a, const char* b) {
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static U32 eng_dbg_ramp_(F32 fraction) {
    if (fraction < 0.70f) {
        return ENG_DBG_COLOR_OK;
    }
    if (fraction < 0.90f) {
        return ENG_DBG_COLOR_AMBER;
    }
    return ENG_DBG_COLOR_WARN;
}

static StringU8 eng_dbg_bytes_(Arena* arena, U64 bytes) {
    if (bytes >= MB(1)) {
        return str8_fmt(arena, "{:.1}MB", (F64)bytes / (F64)MB(1));
    }
    if (bytes >= KB(1)) {
        return str8_fmt(arena, "{:.1}KB", (F64)bytes / (F64)KB(1));
    }
    return str8_fmt(arena, "{}B", bytes);
}

static void eng_dbg_section_(UI_Context* ui, const char* title) {
    ui_spacer(ui, ui_px(6.0f));
    ui_label_colored(ui, str8(title), ENG_DBG_COLOR_HEADER);
}

static void eng_dbg_meter_row_(UI_Context* ui, StringU8 name, U64 used, U64 cap,
                               StringU8 value) {
    ui_row_begin(ui, ui_grow(1.0f), ui_fit());
    ui_value_cell(ui, 150.0f, 0.0f, UI_COLOR_TEXT_DIM, "{}", name);
    F32 fraction = (cap != 0u) ? ((F32)used / (F32)cap) : 0.0f;
    ui_meter(ui, fraction, eng_dbg_ramp_(fraction), ui_grow(1.0f), ui_px(16.0f));
    ui_value_cell(ui, 190.0f, 1.0f, UI_COLOR_TEXT, "{}", value);
    ui_row_end(ui);
}

static void eng_dbg_count_meter_(UI_Context* ui, const char* name, U64 used, U64 cap) {
    eng_dbg_meter_row_(ui, str8(name), used, cap,
                       str8_fmt(ui->frameArena, "{} / {}", used, cap));
}

static void eng_dbg_bytes_meter_(UI_Context* ui, const char* name, U64 used, U64 cap) {
    eng_dbg_meter_row_(ui, str8(name), used, cap,
                       str8_fmt(ui->frameArena, "{} / {}",
                                eng_dbg_bytes_(ui->frameArena, used),
                                eng_dbg_bytes_(ui->frameArena, cap)));
}

static void eng_dbg_series_row_(UI_Context* ui, EngState* state, const char* name,
                                EngDbgSeriesId series, StringU8 value) {
    ui_row_begin(ui, ui_grow(1.0f), ui_fit());
    ui_value_cell(ui, 150.0f, 0.0f, UI_COLOR_TEXT_DIM, "{}", str8(name));
    ui_plot(ui, state->debug.series[series], ENG_DBG_HISTORY, state->debug.seriesCursor,
            ui_grow(1.0f), ui_px(24.0f));
    ui_value_cell(ui, 190.0f, 1.0f, UI_COLOR_TEXT, "{}", value);
    ui_row_end(ui);
}

static B32 eng_dbg_warn_(UI_Context* ui, const char* name, U64 value) {
    if (value == 0u) {
        return 0;
    }
    eng_dbg_kv(ui, name, ENG_DBG_COLOR_WARN, "{}", value);
    return 1;
}

static void eng_dbg_push_series_(EngContext* ctx) {
    EngState* state = ctx->engine;
    EngDebug* dbg = &state->debug;
    const GfxStats* gfx = &state->render2d.lastGfxStats;
    U32 at = dbg->seriesCursor;
    dbg->series[EngDbgSeries_FrameMs][at] = state->lastDeltaSeconds * 1000.0f;
    dbg->series[EngDbgSeries_SimTickUs][at] = (F32)((F64)state->lastSimTickNanos / 1000.0);
    dbg->series[EngDbgSeries_Draws][at] = (F32)gfx->drawCount;
    dbg->series[EngDbgSeries_Quads2D][at] = (F32)state->render2d.lastDraw2DStats.quadsSubmitted;
    dbg->series[EngDbgSeries_Widgets][at] = (F32)state->ui.stats.widgetCount;
    dbg->series[EngDbgSeries_Renderables][at] = (F32)state->world.lastRenderableCount;
    dbg->series[EngDbgSeries_GfxTempKB][at] = (F32)(gfx->tempBytesUsed / 1024ull);
    F32 callbackUs = 0.0f;
    if ((eng_project_()->capabilities & ENG_CAP_AUDIO) && ctx->host->audioSystem) {
        AudioStats audioStats = audio_stats(ctx->host->audioSystem);
        callbackUs = (F32)((F64)audioStats.lastCallbackNanos / 1000.0);
    }
    dbg->series[EngDbgSeries_AudioCbUs][at] = callbackUs;
    dbg->seriesCursor = (at + 1u) % ENG_DBG_HISTORY;
}

// ////////////////////////
// Tabs

static void eng_dbg_tab_overview_(EngContext* ctx, UI_Context* ui) {
    EngState* state = ctx->engine;
    const EngProject* project = eng_project_();
    ProfInfo info = prof_info();

    U32 historyCount = 0u;
    U32 historyOffset = 0u;
    const F32* frameHistory = prof_frame_history(&historyCount, &historyOffset);
    if (frameHistory && historyCount != 0u) {
        ui_plot(ui, frameHistory, historyCount, historyOffset, ui_grow(1.0f), ui_px(48.0f));
    }

    F32 frameMs = state->averageDeltaSeconds * 1000.0f;
    F32 fps = (state->averageDeltaSeconds > 0.0f) ? (1.0f / state->averageDeltaSeconds) : 0.0f;
    eng_dbg_kv(ui, "frame", UI_COLOR_TEXT_BRIGHT, "{:.2}ms  {:.0}fps  #{}",
               frameMs, fps, state->frameCounter);
    if (project->capabilities & ENG_CAP_SIM) {
        eng_dbg_kv(ui, "sim", UI_COLOR_TEXT, "tick {}  {:.1}s", (U32)state->simTickCounter,
                   state->simTimeSeconds);
    }
    eng_dbg_kv(ui, "window", UI_COLOR_TEXT, "{}x{}  workers {}  reloads {}",
               state->windowWidth, state->windowHeight, state->workerCount, state->reloadCount);
    eng_dbg_kv(ui, "profiler", UI_COLOR_TEXT, "res {:.0}ns  scope {:.1}ns  sites {}",
               info.resolutionNs, info.overheadNsPerScope, info.siteCount);

    eng_dbg_section_(ui, "warnings");
    const UI_Stats* uiStats = &state->ui.stats;
    const GfxStats* gfx = &state->render2d.lastGfxStats;
    const Draw2DStats* draw2d = &state->render2d.lastDraw2DStats;
    U32 warnings = 0u;
    warnings += eng_dbg_warn_(ui, "gfx temp overflow", gfx->tempOverflowCount);
    warnings += eng_dbg_warn_(ui, "gfx staging overflow", gfx->stagingOverflowCount);
    warnings += eng_dbg_warn_(ui, "draw2d quads dropped", draw2d->quadsDropped);
    warnings += eng_dbg_warn_(ui, "ui widget overflow", uiStats->widgetOverflowCount);
    warnings += eng_dbg_warn_(ui, "ui hit-rect overflow", uiStats->hitRectOverflowCount);
    warnings += eng_dbg_warn_(ui, "ui duplicate keys", uiStats->duplicateKeyCount);
    warnings += eng_dbg_warn_(ui, "ui retained evictions", uiStats->retainedEvictCount);
    warnings += eng_dbg_warn_(ui, "ui value-run uncached", uiStats->valueRunUninsertable);
    warnings += eng_dbg_warn_(ui, "prof events dropped", info.droppedEvents);
    warnings += eng_dbg_warn_(ui, "sim clamps", state->simClampCount);
    warnings += eng_dbg_warn_(ui, "replay diverged @tick", state->replay.divergedAtTick);
    if (state->resources.fileStream) {
        FileStreamStats files = file_stream_stats(state->resources.fileStream);
        warnings += eng_dbg_warn_(ui, "file publishes failed", files.failedCount);
    }
    if (project->capabilities & ENG_CAP_WORLD3D) {
        warnings += eng_dbg_warn_(ui, "world renderables dropped", state->world.lastDroppedCount);
    }
    if ((project->capabilities & ENG_CAP_AUDIO) && ctx->host->audioSystem) {
        AudioStats audioStats = audio_stats(ctx->host->audioSystem);
        warnings += eng_dbg_warn_(ui, "audio voices dropped", audioStats.voicesDropped);
        warnings += eng_dbg_warn_(ui, "audio commands dropped", audioStats.commandsDropped);
        warnings += eng_dbg_warn_(ui, "audio blobs leaked", audioStats.blobsLeaked);
    }
    if (warnings == 0u) {
        ui_label_colored(ui, str8("all clear"), UI_COLOR_TEXT_DIM);
    }
}

static void eng_dbg_profiler_row_(UI_Context* ui, const ProfSiteStats* stats, U32 site, U32 depth,
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
    ui_label_value(ui, UI_COLOR_TEXT, "{:.3}", siteStats->avgInclMs);
    ui_label_value(ui, UI_COLOR_TEXT_DIM, "{:.3}", siteStats->avgExclMs);
    ui_label_value(ui, UI_COLOR_TEXT_DIM, "{:.3}", siteStats->maxInclMs);
    ui_label_value(ui, ENG_DBG_COLOR_HEADER, "x{}", (U32)(siteStats->avgHits + 0.5f));
    ui_row_end(ui);
}

static void eng_dbg_profiler_path_row_(UI_Context* ui, const ProfSiteStats* stats,
                                       const ProfPathStats* path, U32 pathIndex,
                                       U32* ioSelectedSite) {
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
    ui_label_value(ui, UI_COLOR_TEXT, "{:.3}", path->avgInclMs);
    ui_label_value(ui, UI_COLOR_TEXT_DIM, "{:.3}", path->avgExclMs);
    ui_label_value(ui, UI_COLOR_TEXT_DIM, "{:.3}", path->maxInclMs);
    ui_label_value(ui, ENG_DBG_COLOR_HEADER, "x{}", (U32)(path->avgHits + 0.5f));
    ui_row_end(ui);
}

static B32 eng_dbg_profiler_path_live_(const ProfPathStats* path) {
    return (path->avgInclMs > 0.0f || path->lastInclMs > 0.0f || path->avgHits > 0.0f) ? 1 : 0;
}

static void eng_dbg_tab_profiler_(EngContext* ctx, UI_Context* ui) {
    EngState* state = ctx->engine;
    EngDebug* dbg = &state->debug;
    ProfInfo info = prof_info();
    U32 siteCount = 0u;
    const ProfSiteStats* stats = prof_site_stats(&siteCount);
    if (!stats || siteCount <= 1u) {
        ui_label_colored(ui, str8("profiler has no sites"), UI_COLOR_TEXT_DIM);
        return;
    }

    F32 averageFrameMs = state->averageDeltaSeconds * 1000.0f;
    F32 fps = (state->averageDeltaSeconds > 0.0f) ? (1.0f / state->averageDeltaSeconds) : 0.0f;
    ui_label_value(ui, UI_COLOR_TEXT, "frame {:.2}ms  {:.0}fps  |  res {:.0}ns  scope {:.1}ns  drops {}",
                   averageFrameMs, fps, info.resolutionNs, info.overheadNsPerScope,
                   info.droppedEvents);

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
    ui_checkbox(ui, str8("flat"), &dbg->profFlat);
    ui_spacer(ui, ui_grow(1.0f));
    if (ui_button(ui, str8("capture")).clicked) {
        prof_capture(64u, "captures");
    }
    ui_row_end(ui);

    if (dbg->profFlat) {
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
            eng_dbg_profiler_row_(ui, stats, order[at], 0u,
                                  str8_fmt(ui->frameArena, "###prof_flat_{}", at),
                                  &dbg->profSelectedSite);
        }
    } else {
        PROF_SCOPE("tree view");
        U32 pathCount = 0u;
        const ProfPathStats* paths = prof_path_stats(&pathCount);
        const ProfFrameView* view = prof_frame_view();
        if (view && paths) {
            for (U32 laneIndex = 0u; laneIndex < view->laneCount; ++laneIndex) {
                const ProfLaneView* lane = view->lanes + laneIndex;
                ui_label_value(ui, ENG_DBG_COLOR_HEADER, "[{}]", str8(lane->name));

                for (U32 root = 1u; root < pathCount; ++root) {
                    if (paths[root].parent != PROF_PATH_NIL || paths[root].thread != lane->threadIndex) {
                        continue;
                    }
                    if (!eng_dbg_profiler_path_live_(paths + root)) {
                        continue;
                    }
                    eng_dbg_profiler_path_row_(ui, stats, paths + root, root, &dbg->profSelectedSite);

                    U32 walkStack[PROF_OPEN_STACK_DEPTH];
                    U32 walkTop = 0u;
                    U32 current = paths[root].firstChild;
                    while (current != PROF_PATH_NIL || walkTop != 0u) {
                        if (current == PROF_PATH_NIL) {
                            walkTop -= 1u;
                            current = paths[walkStack[walkTop]].nextSibling;
                            continue;
                        }
                        if (!eng_dbg_profiler_path_live_(paths + current)) {
                            current = paths[current].nextSibling;
                            continue;
                        }
                        eng_dbg_profiler_path_row_(ui, stats, paths + current, current,
                                                   &dbg->profSelectedSite);
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

    if (dbg->profSelectedSite != 0u && dbg->profSelectedSite < siteCount) {
        const ProfSiteStats* selected = stats + dbg->profSelectedSite;
        U32 historyOffset2 = 0u;
        U32 historyCount2 = 0u;
        prof_frame_history(&historyCount2, &historyOffset2);
        ui_label_value(ui, UI_COLOR_TEXT_BRIGHT, "{}  avg {:.3}ms  max {:.3}ms",
                       str8(selected->label), selected->avgInclMs, selected->maxInclMs);
        ui_plot(ui, selected->historyMs, PROF_HISTORY_FRAMES, historyOffset2, ui_grow(1.0f), ui_px(48.0f));
    }
}

static void eng_dbg_tab_graphics_(EngContext* ctx, UI_Context* ui) {
    EngState* state = ctx->engine;
    const GfxStats* gfx = &state->render2d.lastGfxStats;
    const Draw2DStats* draw2d = &state->render2d.lastDraw2DStats;

    eng_dbg_section_(ui, "gpu");
    eng_dbg_series_row_(ui, state, "draws", EngDbgSeries_Draws,
                        str8_fmt(ui->frameArena, "{}", gfx->drawCount));
    eng_dbg_kv(ui, "dispatches", UI_COLOR_TEXT, "{}", gfx->dispatchCount);
    eng_dbg_kv(ui, "pipeline switches", UI_COLOR_TEXT, "{}", gfx->pipelineSwitchCount);
    eng_dbg_kv(ui, "resource tables", UI_COLOR_TEXT, "{}", gfx->resourceTableCount);
    eng_dbg_bytes_meter_(ui, "frame temp", gfx->tempBytesUsed, gfx->tempBytesCap);
    eng_dbg_bytes_meter_(ui, "staging", gfx->stagingBytesUsed, gfx->stagingBytesCap);

    eng_dbg_section_(ui, "draw2d");
    eng_dbg_series_row_(ui, state, "quads", EngDbgSeries_Quads2D,
                        str8_fmt(ui->frameArena, "{}", draw2d->quadsSubmitted));
    eng_dbg_kv(ui, "clipped / dropped / batches", UI_COLOR_TEXT, "{} / {} / {}",
               draw2d->quadsClipped, draw2d->quadsDropped, draw2d->batchesBuilt);

    if (eng_project_()->capabilities & ENG_CAP_WORLD3D) {
        eng_dbg_section_(ui, "world");
        eng_dbg_series_row_(ui, state, "renderables", EngDbgSeries_Renderables,
                            str8_fmt(ui->frameArena, "{}", state->world.lastRenderableCount));
        eng_dbg_count_meter_(ui, "renderables", state->world.lastRenderableCount,
                             ENG_WORLD_MAX_RENDERABLES);
        eng_dbg_count_meter_(ui, "meshes", state->world.meshCount, ENG_WORLD_MAX_MESHES);
        eng_dbg_kv(ui, "lanes", UI_COLOR_TEXT, "{}", state->world.laneCount);
        eng_dbg_kv(ui, "transparent draws", UI_COLOR_TEXT, "{}", state->world.lastTransparentDraws);
        if (state->world.lastDroppedCount != 0u) {
            eng_dbg_kv(ui, "dropped", ENG_DBG_COLOR_WARN, "{}", state->world.lastDroppedCount);
        }
    }
}

static void eng_dbg_tab_memory_(EngContext* ctx, UI_Context* ui) {
    EngState* state = ctx->engine;

    eng_dbg_section_(ui, "arenas (live / committed, peak)");
    ArenaDebugInfo infos[ARENA_DEBUG_MAX];
    U32 infoCount = arena_debug_snapshot(infos, ARENA_DEBUG_MAX);

    struct ArenaAgg {
        const char* name;
        U64 reserved;
        U64 committed;
        U64 pos;
        U64 highWater;
        U32 instances;
    };
    ArenaAgg aggs[ARENA_DEBUG_MAX];
    U32 aggCount = 0u;
    for (U32 i = 0u; i < infoCount; ++i) {
        ArenaAgg* agg = 0;
        for (U32 j = 0u; j < aggCount; ++j) {
            if (eng_dbg_cstr_eq_(aggs[j].name, infos[i].name)) {
                agg = aggs + j;
                break;
            }
        }
        if (!agg) {
            agg = aggs + aggCount;
            aggCount += 1u;
            agg->name = infos[i].name;
            agg->reserved = 0u;
            agg->committed = 0u;
            agg->pos = 0u;
            agg->highWater = 0u;
            agg->instances = 0u;
        }
        agg->reserved += infos[i].reserved;
        agg->committed += infos[i].committed;
        agg->pos += infos[i].pos;
        agg->highWater = MAX(agg->highWater, infos[i].highWater);
        agg->instances += 1u;
    }
    for (U32 i = 0u; i < aggCount; ++i) {
        const ArenaAgg* agg = aggs + i;
        StringU8 name = (agg->instances > 1u)
            ? str8_fmt(ui->frameArena, "{} x{}", str8(agg->name), agg->instances)
            : str8(agg->name);
        eng_dbg_meter_row_(ui, name, agg->pos, agg->committed,
                           str8_fmt(ui->frameArena, "{} / {}  hw {}",
                                    eng_dbg_bytes_(ui->frameArena, agg->pos),
                                    eng_dbg_bytes_(ui->frameArena, agg->committed),
                                    eng_dbg_bytes_(ui->frameArena, agg->highWater)));
    }

    eng_dbg_section_(ui, "gpu frame memory");
    const GfxStats* gfx = &state->render2d.lastGfxStats;
    eng_dbg_bytes_meter_(ui, "temp", gfx->tempBytesUsed, gfx->tempBytesCap);
    eng_dbg_bytes_meter_(ui, "staging", gfx->stagingBytesUsed, gfx->stagingBytesCap);

    eng_dbg_section_(ui, "stores");
    if (state->resources.contentStore) {
        ContentStats content = content_stats(state->resources.contentStore);
        eng_dbg_kv(ui, "content", UI_COLOR_TEXT, "{} blobs  {} keys  {}",
                   content.blobCount, content.keyCount,
                   eng_dbg_bytes_(ui->frameArena, content.payloadBytes));
        eng_dbg_kv(ui, "content hits / misses", UI_COLOR_TEXT, "{} / {}",
                   content.hitCount, content.missCount);
    }
    if (state->resources.artifactCache) {
        ArtifactStats artifact = artifact_cache_stats(state->resources.artifactCache);
        eng_dbg_kv(ui, "artifacts", UI_COLOR_TEXT, "{} live  {} working  {}",
                   artifact.liveCount, artifact.workingCount,
                   eng_dbg_bytes_(ui->frameArena, artifact.bytesLive));
        eng_dbg_kv(ui, "artifact hits / misses", UI_COLOR_TEXT, "{} / {}",
                   artifact.hits, artifact.misses);
    }

    if (state->render2d.textContext) {
        eng_dbg_section_(ui, "text atlas");
        TextStats text = text_stats(state->render2d.textContext);
        eng_dbg_count_meter_(ui, "glyphs", text.glyphCount, text.maxGlyphs);
        eng_dbg_count_meter_(ui, "atlas rows", text.shelfY + text.shelfHeight, text.atlasHeight);
    }

    if (eng_project_()->capabilities & ENG_CAP_SIM) {
        eng_dbg_section_(ui, "replay buffer");
        const EngReplay* replay = &state->replay;
        U32 actionSize = eng_project_()->actionSize;
        eng_dbg_bytes_meter_(ui, "actions", (U64)replay->cursor * actionSize,
                             sizeof(replay->actions));
    }
}

static void eng_dbg_tab_assets_(EngContext* ctx, UI_Context* ui) {
    EngState* state = ctx->engine;
    const EngProject* project = eng_project_();
    FileStream* stream = state->resources.fileStream;
    if (!stream) {
        ui_label_colored(ui, str8("no file stream"), UI_COLOR_TEXT_DIM);
        return;
    }

    FileStreamStats stats = file_stream_stats(stream);
    eng_dbg_kv(ui, "watched", UI_COLOR_TEXT, "{}", stats.fileCount);
    eng_dbg_kv(ui, "checked / published", UI_COLOR_TEXT, "{} / {}",
               stats.checkedCount, stats.publishCount);
    if (stats.failedCount != 0u) {
        eng_dbg_kv(ui, "failed", ENG_DBG_COLOR_WARN, "{}", stats.failedCount);
    }

    eng_dbg_section_(ui, "watched files");
    U32 capacity = file_stream_capacity(stream);
    for (U32 slot = 0u; slot < capacity; ++slot) {
        FileEntryInfo info;
        if (!file_stream_entry_at(stream, slot, &info)) {
            continue;
        }
        ui_row_begin(ui, ui_grow(1.0f), ui_fit());
        const char* statusText = (info.status == FileStatus_Ready) ? "ok"
                               : (info.status == FileStatus_Error) ? "err"
                               : "-";
        U32 statusColor = (info.status == FileStatus_Ready) ? ENG_DBG_COLOR_OK
                        : (info.status == FileStatus_Error) ? ENG_DBG_COLOR_WARN
                        : UI_COLOR_TEXT_DIM;
        ui_value_cell(ui, 36.0f, 0.0f, statusColor, "{}", str8(statusText));
        StringU8 path = info.path;
        if (path.size > 52u) {
            path = str8_fmt(ui->frameArena, "...{}",
                            str8(path.data + (path.size - 49u), 49u));
        }
        ui_label_colored(ui, path, UI_COLOR_TEXT);
        ui_spacer(ui, ui_grow(1.0f));
        ui_value_cell(ui, 56.0f, 1.0f, UI_COLOR_TEXT_DIM, "g{}", info.generation);
        ui_value_cell(ui, 90.0f, 1.0f, UI_COLOR_TEXT, "{}",
                      eng_dbg_bytes_(ui->frameArena, info.size));
        ui_row_end(ui);
    }

    if (project->capabilities & ENG_CAP_WORLD3D) {
        eng_dbg_section_(ui, "models");
        for (U32 slot = 0u; slot < project->modelCount && slot < ENG_WORLD_MAX_MODELS; ++slot) {
            const EngWorldModelResources* model = state->world.models[slot];
            if (!model) {
                eng_dbg_kv(ui, "slot", UI_COLOR_TEXT_DIM, "{}  not published", slot);
                continue;
            }
            ui_row_begin(ui, ui_grow(1.0f), ui_fit());
            ui_value_cell(ui, 36.0f, 0.0f, UI_COLOR_TEXT_DIM, "{}", slot);
            ui_label_value(ui, UI_COLOR_TEXT, "{} sections  {} materials  {} instances  {} textures",
                           model->sectionCount, model->materialCount, model->instanceCount,
                           model->textureCount);
            ui_row_end(ui);
        }
        eng_dbg_count_meter_(ui, "asset textures", state->world.assetTextureCount,
                             ENG_WORLD_MAX_ASSET_TEXTURES);
    }

    if (project->capabilities & ENG_CAP_AUDIO) {
        eng_dbg_section_(ui, "sounds");
        for (U32 slot = 0u; slot < project->soundCount && slot < ENG_AUDIO_MAX_SOUNDS; ++slot) {
            eng_dbg_kv(ui, "slot", UI_COLOR_TEXT, "{}  gen {}", slot,
                       state->audio.soundGenerations[slot]);
        }
    }
}

static void eng_dbg_tab_sim_(EngContext* ctx, UI_Context* ui) {
    EngState* state = ctx->engine;
    const EngProject* project = eng_project_();

    eng_dbg_series_row_(ui, state, "tick time", EngDbgSeries_SimTickUs,
                        str8_fmt(ui->frameArena, "{}us", state->lastSimTickNanos / 1000ull));
    eng_dbg_kv(ui, "tick", UI_COLOR_TEXT, "{}", (U32)state->simTickCounter);
    eng_dbg_kv(ui, "time", UI_COLOR_TEXT, "{:.2}s", state->simTimeSeconds);
    eng_dbg_kv(ui, "accumulator", UI_COLOR_TEXT, "{:.2}ms", state->simAccumulator * 1000.0f);
    eng_dbg_kv(ui, "forced dt", UI_COLOR_TEXT, "{}", str8(state->simForcedDt > 0.0f ? "on" : "off"));
    if (state->simClampCount != 0u) {
        eng_dbg_kv(ui, "clamps", ENG_DBG_COLOR_WARN, "{}", state->simClampCount);
    }

    eng_dbg_section_(ui, "replay");
    const EngReplay* replay = &state->replay;
    const char* modeName = (replay->mode == EngReplayMode_Recording) ? "recording"
                         : (replay->mode == EngReplayMode_Playing) ? "playing"
                         : "idle";
    eng_dbg_kv(ui, "mode", UI_COLOR_TEXT, "{}", str8(modeName));
    U32 replayCap = (replay->mode == EngReplayMode_Playing) ? replay->tickCount
                                                            : ENG_REPLAY_MAX_TICKS;
    eng_dbg_count_meter_(ui, "ticks", replay->cursor, replayCap);
    eng_dbg_kv(ui, "checks", UI_COLOR_TEXT, "{} every {}", replay->checkCount,
               ENG_REPLAY_CHECK_INTERVAL);
    if (replay->divergedAtTick != 0ull) {
        eng_dbg_kv(ui, "diverged", ENG_DBG_COLOR_WARN, "@tick {}", replay->divergedAtTick);
    }

    eng_dbg_section_(ui, "project blobs");
    eng_dbg_count_meter_(ui, "action bytes", project->actionSize, ENG_SIM_MAX_ACTION_SIZE);
    eng_dbg_count_meter_(ui, "save bytes", project->saveSize, ENG_SIM_MAX_SAVE_SIZE);
    eng_dbg_kv(ui, "save version", UI_COLOR_TEXT, "{}", project->saveVersion);
}

static void eng_dbg_tab_audio_(EngContext* ctx, UI_Context* ui) {
    EngState* state = ctx->engine;
    EngDebug* dbg = &state->debug;
    if (!ctx->host->audioSystem) {
        ui_label_colored(ui, str8("no audio device"), UI_COLOR_TEXT_DIM);
        return;
    }
    AudioStats stats = audio_stats(ctx->host->audioSystem);

    eng_dbg_kv(ui, "device", stats.deviceOpen ? UI_COLOR_TEXT : ENG_DBG_COLOR_WARN, "{}",
               str8(stats.deviceOpen ? "open" : "closed"));
    eng_dbg_count_meter_(ui, "voices", stats.voicesActive, AUDIO_VOICE_COUNT);
    eng_dbg_kv(ui, "buffers live", UI_COLOR_TEXT, "{}", stats.buffersLive);
    eng_dbg_series_row_(ui, state, "callback", EngDbgSeries_AudioCbUs,
                        str8_fmt(ui->frameArena, "{}us", stats.lastCallbackNanos / 1000ull));
    eng_dbg_kv(ui, "callback max", UI_COLOR_TEXT, "{}us  ({} calls)",
               stats.maxCallbackNanos / 1000ull, stats.callbackCount);
    if (stats.voicesDropped != 0ull) {
        eng_dbg_kv(ui, "voices dropped", ENG_DBG_COLOR_WARN, "{}", stats.voicesDropped);
    }
    if (stats.commandsDropped != 0ull) {
        eng_dbg_kv(ui, "commands dropped", ENG_DBG_COLOR_WARN, "{}", stats.commandsDropped);
    }
    if (stats.blobsLeaked != 0ull) {
        eng_dbg_kv(ui, "blobs leaked", ENG_DBG_COLOR_WARN, "{}", stats.blobsLeaked);
    }

    eng_dbg_section_(ui, "mixer");
    if (ui_slider(ui, str8("master###dbg_master_gain"), &dbg->masterGain, 0.0f, 1.0f)) {
        audio_set_master_gain(ctx->host->audioSystem, dbg->masterGain);
    }
}

static void eng_dbg_tab_ui_(EngContext* ctx, UI_Context* ui) {
    EngState* state = ctx->engine;
    const UI_Stats* stats = &state->ui.stats;

    eng_dbg_section_(ui, "widgets");
    eng_dbg_series_row_(ui, state, "widgets", EngDbgSeries_Widgets,
                        str8_fmt(ui->frameArena, "{}", stats->widgetCount));
    eng_dbg_count_meter_(ui, "widgets", stats->widgetCount, UI_MAX_WIDGETS);
    eng_dbg_count_meter_(ui, "retained", stats->retainedCount, UI_MAX_RETAINED);
    eng_dbg_count_meter_(ui, "hit rects", stats->hitRectCount, UI_MAX_HIT_RECTS);

    eng_dbg_section_(ui, "value-run cache");
    U32 lookups = stats->valueRunHits + stats->valueRunMisses;
    F32 hitPct = (lookups != 0u) ? (100.0f * (F32)stats->valueRunHits / (F32)lookups) : 0.0f;
    eng_dbg_kv(ui, "hits / misses", UI_COLOR_TEXT, "{} / {}  ({:.0}%)",
               stats->valueRunHits, stats->valueRunMisses, hitPct);
    if (stats->valueRunUninsertable != 0u) {
        eng_dbg_kv(ui, "uncached", ENG_DBG_COLOR_WARN, "{}  (victim {}  slot {}  resolve {})",
                   stats->valueRunUninsertable, stats->valueRunNoVictim,
                   stats->valueRunNoSlot, stats->valueRunResolveFails);
    }

    if (state->render2d.textContext) {
        eng_dbg_section_(ui, "text");
        TextStats text = text_stats(state->render2d.textContext);
        eng_dbg_kv(ui, "fonts", UI_COLOR_TEXT, "{} / {}", text.fontCount, text.maxFonts);
        eng_dbg_count_meter_(ui, "glyphs", text.glyphCount, text.maxGlyphs);
        eng_dbg_count_meter_(ui, "atlas rows", text.shelfY + text.shelfHeight, text.atlasHeight);
        eng_dbg_kv(ui, "runs hit / miss / evict / bypass", UI_COLOR_TEXT, "{} / {} / {} / {}",
                   text.runHits, text.runMisses, text.runEvictions, text.runBypasses);

        eng_dbg_section_(ui, "atlas");
        ui_atlas_preview(ui, ui_px(330.0f), ui_px(330.0f));
    }
}

// ////////////////////////
// Window

struct EngDbgTabDef {
    U32 tab;
    const char* label;
};

static U32 eng_dbg_visible_tabs_(EngDbgTabDef* out) {
    const EngProject* project = eng_project_();
    U32 count = 0u;
    out[count++] = {EngDbgTab_Overview, "main"};
    out[count++] = {EngDbgTab_Profiler, "prof"};
    out[count++] = {EngDbgTab_Graphics, "gfx"};
    out[count++] = {EngDbgTab_Memory, "mem"};
    out[count++] = {EngDbgTab_Assets, "files"};
    if (project->capabilities & ENG_CAP_SIM) {
        out[count++] = {EngDbgTab_Sim, "sim"};
    }
    if (project->capabilities & ENG_CAP_AUDIO) {
        out[count++] = {EngDbgTab_Audio, "audio"};
    }
    out[count++] = {EngDbgTab_UI, "ui"};
    if (project->debug_tab) {
        out[count++] = {EngDbgTab_Project, project->name};
    }
    return count;
}

static void eng_debug_window_(EngContext* ctx, UI_Context* ui) {
    EngState* state = ctx->engine;
    EngDebug* dbg = &state->debug;

    UI_PanelDesc desc = {};
    desc.anchorX = 1.0f;
    desc.anchorY = 0.0f;
    desc.offsetX = -16.0f;
    desc.offsetY = 16.0f;
    desc.width = ui_px(700.0f);
    desc.height = ui_px(MAX(360.0f, ui->viewportHeight - 32.0f));
    ui_panel_begin(ui, str8("debug###dbgwin"), &desc);

    EngDbgTabDef tabs[EngDbgTab_COUNT];
    U32 tabCount = eng_dbg_visible_tabs_(tabs);
    B32 activeVisible = 0;
    for (U32 i = 0u; i < tabCount; ++i) {
        if (tabs[i].tab == dbg->activeTab) {
            activeVisible = 1;
            break;
        }
    }
    if (!activeVisible) {
        dbg->activeTab = EngDbgTab_Overview;
    }

    ui_row_begin(ui, ui_grow(1.0f), ui_fit());
    for (U32 i = 0u; i < tabCount; ++i) {
        B32 active = (tabs[i].tab == dbg->activeTab);
        UI_Signal sig = ui_row_begin_keyed(ui,
                                           str8_fmt(ui->frameArena, "###dbgtab_{}", str8(tabs[i].label)),
                                           ui_fit(), ui_px(26.0f), active);
        ui_spacer(ui, ui_px(8.0f));
        ui_label_colored(ui, str8(tabs[i].label),
                         active ? UI_COLOR_TEXT_BRIGHT : UI_COLOR_TEXT_DIM);
        ui_spacer(ui, ui_px(8.0f));
        ui_row_end(ui);
        if (sig.clicked) {
            dbg->activeTab = tabs[i].tab;
        }
    }
    ui_spacer(ui, ui_grow(1.0f));
    ui_label_colored(ui, str8("[F1]"), ENG_DBG_COLOR_HEADER);
    ui_row_end(ui);

    ui_scroll_begin(ui, str8("###dbgcontent"), ui_grow(1.0f), ui_grow(1.0f));
    switch (dbg->activeTab) {
        case EngDbgTab_Overview: eng_dbg_tab_overview_(ctx, ui); break;
        case EngDbgTab_Profiler: eng_dbg_tab_profiler_(ctx, ui); break;
        case EngDbgTab_Graphics: eng_dbg_tab_graphics_(ctx, ui); break;
        case EngDbgTab_Memory: eng_dbg_tab_memory_(ctx, ui); break;
        case EngDbgTab_Assets: eng_dbg_tab_assets_(ctx, ui); break;
        case EngDbgTab_Sim: eng_dbg_tab_sim_(ctx, ui); break;
        case EngDbgTab_Audio: eng_dbg_tab_audio_(ctx, ui); break;
        case EngDbgTab_UI: eng_dbg_tab_ui_(ctx, ui); break;
        case EngDbgTab_Project: {
            const EngProject* project = eng_project_();
            if (project->debug_tab) {
                project->debug_tab(ctx, ui);
            }
        } break;
        default: break;
    }
    ui_scroll_end(ui);

    ui_panel_end(ui);
}

static void eng_panels_submit(EngContext* ctx, EngRendererFrame* rendererFrame, const EngInput* input) {
    EngState* state = ctx->engine;
    EngRender2D* render = &state->render2d;
    if (render->textContext == 0 || render->font.generation == 0u) {
        return;
    }

    eng_dbg_push_series_(ctx);

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
        if (state->debug.windowVisible) {
            PROF_SCOPE("debug window");
            eng_debug_window_(ctx, ui);
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
