//
// Created by André Leite on 12/06/2026.
//
// Save/load + input replay over opaque project blobs (U16 machinery made
// generic in U19). The engine owns the clock, the files, the recording
// buffers, and the divergence detector; the project supplies capture/
// apply/checksum and what a tick means. Forward-discard on any version
// or size mismatch — old blobs are dead data, not an upgrade project.
//

static void eng_sim_save_write_(EngContext* ctx) {
    const EngProject* project = eng_project_();
    if (!project->sim_capture) {
        return;
    }
    OS_create_directory("saves");
    OS_Handle file = OS_file_open(ENG_SAVE_PATH, OS_FileOpenMode_Create);
    if (!file.handle) {
        LOG_ERROR("save", "Cannot open '{}' for write", str8(ENG_SAVE_PATH));
        return;
    }
    EngSaveFileHeader header = {};
    header.magic = ENG_SAVE_MAGIC;
    header.formatVersion = ENG_SAVE_FORMAT_VERSION;
    header.saveVersion = project->saveVersion;
    header.saveSize = project->saveSize;
    U8 save[ENG_SIM_MAX_SAVE_SIZE] = {};
    project->sim_capture(ctx, save);
    OS_file_write(file, sizeof(header), &header);
    OS_file_write(file, (U64)project->saveSize, save);
    OS_file_close(file);
    LOG_INFO("save", "Saved: {} bytes (save v{}) tick {}",
             project->saveSize, project->saveVersion, ctx->engine->simTickCounter);
}

static void eng_sim_save_read_(EngContext* ctx) {
    const EngProject* project = eng_project_();
    EngReplay* replay = &ctx->engine->replay;
    if (!project->sim_apply) {
        return;
    }
    if (replay->mode != EngReplayMode_Idle) {
        LOG_ERROR("save", "Load ignored while replay is {}",
                  replay->mode == EngReplayMode_Recording ? str8("recording") : str8("playing"));
        return;
    }
    OS_Handle file = OS_file_open(ENG_SAVE_PATH, OS_FileOpenMode_Read);
    if (!file.handle) {
        LOG_ERROR("save", "No save at '{}'", str8(ENG_SAVE_PATH));
        return;
    }
    // Seekable files take the range overload; the size overload is the
    // pipe/stream path and asserts on regular files.
    EngSaveFileHeader header = {};
    U8 save[ENG_SIM_MAX_SAVE_SIZE] = {};
    RangeU64 headerRange = {0ull, sizeof(header)};
    U64 readHeader = OS_file_read(file, headerRange, &header);
    B32 headerOk = readHeader == sizeof(header) &&
                   header.magic == ENG_SAVE_MAGIC &&
                   header.formatVersion == ENG_SAVE_FORMAT_VERSION &&
                   header.saveVersion == project->saveVersion &&
                   header.saveSize == project->saveSize;
    U64 readBody = 0u;
    if (headerOk) {
        RangeU64 bodyRange = {sizeof(header), sizeof(header) + (U64)header.saveSize};
        readBody = OS_file_read(file, bodyRange, save);
    }
    OS_file_close(file);
    if (!headerOk || readBody != header.saveSize) {
        // Forward-discard: an old or foreign blob is dead data, not an
        // upgrade project.
        LOG_ERROR("save", "Discarding save (magic/version/size mismatch)");
        return;
    }
    project->sim_apply(ctx, save);
    ctx->engine->simAccumulator = 0.0f;
    LOG_INFO("save", "Loaded: {} bytes (save v{}) tick {}",
             header.saveSize, header.saveVersion, ctx->engine->simTickCounter);
}

static void eng_sim_record_start_(EngContext* ctx) {
    const EngProject* project = eng_project_();
    EngReplay* replay = &ctx->engine->replay;
    if (replay->mode != EngReplayMode_Idle || !project->sim_capture) {
        return;
    }
    replay->mode = EngReplayMode_Recording;
    replay->cursor = 0u;
    replay->tickCount = 0u;
    replay->checkCount = 0u;
    replay->divergedAtTick = 0ull;
    MEMSET(replay->initial, 0, sizeof(replay->initial));
    project->sim_capture(ctx, replay->initial);
    LOG_INFO("replay", "Recording from tick {}", ctx->engine->simTickCounter);
}

static void eng_sim_record_stop_(EngContext* ctx) {
    const EngProject* project = eng_project_();
    EngReplay* replay = &ctx->engine->replay;
    if (replay->mode != EngReplayMode_Recording) {
        return;
    }
    replay->mode = EngReplayMode_Idle;
    replay->tickCount = replay->cursor;
    replay->checkCount = replay->cursor / ENG_REPLAY_CHECK_INTERVAL;

    OS_create_directory("saves");
    OS_Handle file = OS_file_open(ENG_REPLAY_PATH, OS_FileOpenMode_Create);
    if (!file.handle) {
        LOG_ERROR("replay", "Cannot open '{}' for write", str8(ENG_REPLAY_PATH));
        return;
    }
    EngReplayFileHeader header = {};
    header.magic = ENG_REPLAY_MAGIC;
    header.formatVersion = ENG_REPLAY_FORMAT_VERSION;
    header.saveVersion = project->saveVersion;
    header.saveSize = project->saveSize;
    header.actionSize = project->actionSize;
    header.tickCount = replay->tickCount;
    header.checkInterval = ENG_REPLAY_CHECK_INTERVAL;
    OS_file_write(file, sizeof(header), &header);
    OS_file_write(file, (U64)project->saveSize, replay->initial);
    OS_file_write(file, (U64)replay->tickCount * project->actionSize, replay->actions);
    OS_file_write(file, (U64)replay->checkCount * sizeof(U64), replay->checksums);
    OS_file_close(file);
    LOG_INFO("replay", "Recorded {} ticks ({} checksums) -> '{}'",
             replay->tickCount, replay->checkCount, str8(ENG_REPLAY_PATH));
}

static void eng_sim_replay_start_(EngContext* ctx) {
    const EngProject* project = eng_project_();
    EngReplay* replay = &ctx->engine->replay;
    if (replay->mode != EngReplayMode_Idle || !project->sim_apply) {
        return;
    }
    OS_Handle file = OS_file_open(ENG_REPLAY_PATH, OS_FileOpenMode_Read);
    if (!file.handle) {
        LOG_ERROR("replay", "No replay at '{}'", str8(ENG_REPLAY_PATH));
        return;
    }
    EngReplayFileHeader header = {};
    RangeU64 headerRange = {0ull, sizeof(header)};
    U64 readHeader = OS_file_read(file, headerRange, &header);
    if (readHeader != sizeof(header) ||
        header.magic != ENG_REPLAY_MAGIC ||
        header.formatVersion != ENG_REPLAY_FORMAT_VERSION ||
        header.saveVersion != project->saveVersion ||
        header.saveSize != project->saveSize ||
        header.actionSize != project->actionSize ||
        header.tickCount > ENG_REPLAY_MAX_TICKS ||
        header.checkInterval != ENG_REPLAY_CHECK_INTERVAL) {
        LOG_ERROR("replay", "Discarding replay (header mismatch)");
        OS_file_close(file);
        return;
    }
    U64 actionBytes = (U64)header.tickCount * header.actionSize;
    U32 checkCount = header.tickCount / ENG_REPLAY_CHECK_INTERVAL;
    U64 checkBytes = (U64)checkCount * sizeof(U64);
    U64 at = sizeof(header);
    RangeU64 initialRange = {at, at + header.saveSize};
    U64 readInitial = OS_file_read(file, initialRange, replay->initial);
    at += header.saveSize;
    RangeU64 actionsRange = {at, at + actionBytes};
    U64 readActions = OS_file_read(file, actionsRange, replay->actions);
    at += actionBytes;
    RangeU64 checksRange = {at, at + checkBytes};
    U64 readChecks = OS_file_read(file, checksRange, replay->checksums);
    OS_file_close(file);
    if (readInitial != header.saveSize || readActions != actionBytes ||
        readChecks != checkBytes) {
        LOG_ERROR("replay", "Discarding replay (truncated)");
        return;
    }
    replay->mode = EngReplayMode_Playing;
    replay->cursor = 0u;
    replay->tickCount = header.tickCount;
    replay->checkCount = checkCount;
    replay->divergedAtTick = 0ull;
    project->sim_apply(ctx, replay->initial);
    ctx->engine->simAccumulator = 0.0f;
    LOG_INFO("replay", "Playing {} ticks from tick {}",
             replay->tickCount, ctx->engine->simTickCounter);
}

static void eng_sim_replay_stop_(EngContext* ctx) {
    EngReplay* replay = &ctx->engine->replay;
    if (replay->mode == EngReplayMode_Recording) {
        eng_sim_record_stop_(ctx);
        return;
    }
    if (replay->mode == EngReplayMode_Playing) {
        replay->mode = EngReplayMode_Idle;
        LOG_INFO("replay", "Playback aborted at relative tick {}", replay->cursor);
    }
}

// Per-tick hooks for the drain loop. The playback override replaces the
// live action sample; the post-tick hook records actions/checksums or
// verifies them. Returns whether playback supplied the actions.
static B32 eng_sim_replay_tick_actions_(EngContext* ctx, void* actions) {
    const EngProject* project = eng_project_();
    EngReplay* replay = &ctx->engine->replay;
    if (replay->mode != EngReplayMode_Playing) {
        return 0;
    }
    if (replay->cursor >= replay->tickCount) {
        replay->mode = EngReplayMode_Idle;
        LOG_INFO("replay", "Playback finished: {}",
                 replay->divergedAtTick == 0ull ? str8("checksums clean")
                                                : str8("DIVERGED (see first bad tick above)"));
        return 0;
    }
    MEMCPY(actions, replay->actions + (U64)replay->cursor * project->actionSize,
           project->actionSize);
    return 1;
}

static void eng_sim_replay_post_tick_(EngContext* ctx, const void* actions) {
    const EngProject* project = eng_project_();
    EngReplay* replay = &ctx->engine->replay;
    if (replay->mode == EngReplayMode_Recording) {
        if (replay->cursor >= ENG_REPLAY_MAX_TICKS) {
            LOG_INFO("replay", "Recording buffer full; stopping");
            eng_sim_record_stop_(ctx);
            return;
        }
        MEMCPY(replay->actions + (U64)replay->cursor * project->actionSize, actions,
               project->actionSize);
        replay->cursor += 1u;
        if ((replay->cursor % ENG_REPLAY_CHECK_INTERVAL) == 0u) {
            replay->checksums[replay->cursor / ENG_REPLAY_CHECK_INTERVAL - 1u] =
                project->sim_checksum(ctx);
        }
    } else if (replay->mode == EngReplayMode_Playing) {
        replay->cursor += 1u;
        if ((replay->cursor % ENG_REPLAY_CHECK_INTERVAL) == 0u) {
            U32 checkIndex = replay->cursor / ENG_REPLAY_CHECK_INTERVAL - 1u;
            if (checkIndex < replay->checkCount) {
                U64 expected = replay->checksums[checkIndex];
                U64 got = project->sim_checksum(ctx);
                if (expected != got && replay->divergedAtTick == 0ull) {
                    replay->divergedAtTick = ctx->engine->simTickCounter;
                    LOG_ERROR("replay",
                              "DIVERGENCE at relative tick {} (absolute {}): expected {} got {}",
                              replay->cursor, replay->divergedAtTick, expected, got);
                }
            }
        }
    }
}
