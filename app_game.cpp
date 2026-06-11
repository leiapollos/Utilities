//
// Created by André Leite on 11/06/2026.
//
// The action layer and follow camera. Events fold into key state gated by
// the UI's device claims (one frame delayed, same contract as UI hit
// testing); one action sample per frame feeds every catch-up tick; the
// camera rig is render-time state and never enters the sim.
//

#define APP_GAME_CAMERA_SENSITIVITY 0.005f
#define APP_GAME_CAMERA_ZOOM_RATE 0.05f
#define APP_GAME_CAMERA_MIN_DISTANCE 6.0f
#define APP_GAME_CAMERA_MAX_DISTANCE 120.0f
#define APP_GAME_CAMERA_MIN_PITCH 0.08f
#define APP_GAME_CAMERA_MAX_PITCH 1.35f
#define APP_GAME_CAMERA_FOLLOW_SPEED 10.0f

static void app_game_fold_events_(AppCoreState* state, const AppInput* input) {
    AppGameState* game = &state->game;

    // The UI owns the keyboard (focused edit): drop every held key so
    // nothing sticks while typing.
    if (state->ui.wantKeyboard) {
        MEMSET(game->keyDown, 0, sizeof(game->keyDown));
    }

    for (U32 eventIndex = 0u; eventIndex < input->eventCount; ++eventIndex) {
        const OS_GraphicsEvent* event = input->events + eventIndex;
        switch (event->tag) {
            case OS_GraphicsEvent_Tag_KeyDown: {
                if (!state->ui.wantKeyboard && (U32)event->keyDown.keyCode < APP_GAME_MAX_KEYS) {
                    game->keyDown[event->keyDown.keyCode] = 1;
                }
            } break;
            case OS_GraphicsEvent_Tag_KeyUp: {
                if ((U32)event->keyUp.keyCode < APP_GAME_MAX_KEYS) {
                    game->keyDown[event->keyUp.keyCode] = 0;
                }
            } break;
            case OS_GraphicsEvent_Tag_MouseButtonDown: {
                if (!state->ui.wantMouse && event->mouseButtonDown.button == OS_MouseButton_Left) {
                    game->cameraDragging = 1;
                }
            } break;
            case OS_GraphicsEvent_Tag_MouseButtonUp: {
                if (event->mouseButtonUp.button == OS_MouseButton_Left) {
                    game->cameraDragging = 0;
                }
            } break;
            case OS_GraphicsEvent_Tag_MouseMove: {
                if (game->cameraDragging && state->demo.playerMode) {
                    game->cameraYaw += event->mouseMove.deltaX * APP_GAME_CAMERA_SENSITIVITY;
                    game->cameraPitch = CLAMP(game->cameraPitch +
                                                  event->mouseMove.deltaY * APP_GAME_CAMERA_SENSITIVITY,
                                              APP_GAME_CAMERA_MIN_PITCH, APP_GAME_CAMERA_MAX_PITCH);
                }
            } break;
            case OS_GraphicsEvent_Tag_MouseScroll: {
                if (!state->ui.wantMouse && state->demo.playerMode) {
                    game->cameraDistance = CLAMP(game->cameraDistance -
                                                     event->mouseScroll.deltaY * APP_GAME_CAMERA_ZOOM_RATE,
                                                 APP_GAME_CAMERA_MIN_DISTANCE, APP_GAME_CAMERA_MAX_DISTANCE);
                }
            } break;
            default: break;
        }
    }
}

// One sample per frame; catch-up ticks share it. The wish direction maps
// camera-relative keys into world space here so the tick stays pure.
static AppGameActions app_game_sample_actions_(AppCoreState* state) {
    AppGameState* game = &state->game;
    AppGameActions actions = {};

    F32 forwardInput = (game->keyDown[OS_KeyCode_W] || game->keyDown[OS_KeyCode_UpArrow]) ? 1.0f : 0.0f;
    F32 backInput = (game->keyDown[OS_KeyCode_S] || game->keyDown[OS_KeyCode_DownArrow]) ? 1.0f : 0.0f;
    F32 rightInput = (game->keyDown[OS_KeyCode_D] || game->keyDown[OS_KeyCode_RightArrow]) ? 1.0f : 0.0f;
    F32 leftInput = (game->keyDown[OS_KeyCode_A] || game->keyDown[OS_KeyCode_LeftArrow]) ? 1.0f : 0.0f;

    F32 forwardX = -COS_F32(game->cameraYaw);
    F32 forwardZ = -SIN_F32(game->cameraYaw);
    F32 rightX = SIN_F32(game->cameraYaw);
    F32 rightZ = -COS_F32(game->cameraYaw);

    actions.moveX = forwardX * (forwardInput - backInput) + rightX * (rightInput - leftInput);
    actions.moveZ = forwardZ * (forwardInput - backInput) + rightZ * (rightInput - leftInput);
    actions.jump = game->keyDown[OS_KeyCode_Space];
    return actions;
}

static void app_game_camera_(AppCoreState* state, F32 frameDt, Vec3F32* outEye, Vec3F32* outTarget) {
    AppGameState* game = &state->game;
    if (!game->cameraInitialized) {
        game->cameraInitialized = 1;
        game->cameraYaw = 0.0f;
        game->cameraPitch = 0.55f;
        game->cameraDistance = 26.0f;
        game->cameraTarget = game->player.position;
    }

    // Follow the interpolated render position; follow lag alone does not
    // hide tick aliasing, it only rounds the steps (measured as the
    // every-~0.6s double-step snap).
    Vec3F32 renderPosition = app_game_render_position_(game->playerPrevPosition,
                                                       game->player.position,
                                                       state->simAccumulator);
    F32 rate = CLAMP(APP_GAME_CAMERA_FOLLOW_SPEED * frameDt, 0.0f, 1.0f);
    game->cameraTarget.x += (renderPosition.x - game->cameraTarget.x) * rate;
    game->cameraTarget.y += (renderPosition.y - game->cameraTarget.y) * rate;
    game->cameraTarget.z += (renderPosition.z - game->cameraTarget.z) * rate;

    F32 horizontal = COS_F32(game->cameraPitch) * game->cameraDistance;
    Vec3F32 eye;
    eye.x = game->cameraTarget.x + COS_F32(game->cameraYaw) * horizontal;
    eye.y = game->cameraTarget.y + SIN_F32(game->cameraPitch) * game->cameraDistance;
    eye.z = game->cameraTarget.z + SIN_F32(game->cameraYaw) * horizontal;

    *outEye = eye;
    *outTarget = game->cameraTarget;
}

// ////////////////////////
// Persistence + input replay (U16)
//
// Save/load round-trips the serialized subset through a versioned blob —
// forward-discard on mismatch, the hot-state reseed discipline aimed at
// disk. Replay records (relative tick -> actions) from a captured start
// state with periodic player checksums; playback always reads back from
// disk so every replay validates the whole pipe. Both formats are
// runtime-owned, not cooked assets.

#define APP_SAVE_MAGIC 0x56415355u  // "USAV"
#define APP_SAVE_VERSION 1u
#define APP_SAVE_PATH "saves/player.usav"
#define APP_REPLAY_MAGIC 0x50455255u // "UREP"
#define APP_REPLAY_VERSION 1u
#define APP_REPLAY_PATH "saves/session.urep"

struct AppSaveFileHeader {
    U32 magic;
    U32 version;
};

struct AppReplayFileHeader {
    U32 magic;
    U32 version;
    U32 tickCount;
    U32 checkInterval;
};

static AppGameSaveState app_game_save_capture_(const AppCoreState* state) {
    AppGameSaveState save = {};
    save.player = state->game.player;
    save.cameraYaw = state->game.cameraYaw;
    save.cameraPitch = state->game.cameraPitch;
    save.cameraDistance = state->game.cameraDistance;
    save.cameraTarget = state->game.cameraTarget;
    save.simTickCounter = state->simTickCounter;
    return save;
}

static void app_game_save_apply_(AppCoreState* state, const AppGameSaveState* save) {
    state->game.player = save->player;
    state->game.playerPrevPosition = save->player.position;
    state->game.cameraYaw = save->cameraYaw;
    state->game.cameraPitch = save->cameraPitch;
    state->game.cameraDistance = save->cameraDistance;
    state->game.cameraTarget = save->cameraTarget;
    state->game.cameraInitialized = 1;
    state->simTickCounter = save->simTickCounter;
    state->simAccumulator = 0.0f;
}

static void app_game_save_write_(AppCoreState* state) {
    OS_create_directory("saves");
    OS_Handle file = OS_file_open(APP_SAVE_PATH, OS_FileOpenMode_Create);
    if (!file.handle) {
        LOG_ERROR("save", "Cannot open '{}' for write", str8(APP_SAVE_PATH));
        return;
    }
    AppSaveFileHeader header = {};
    header.magic = APP_SAVE_MAGIC;
    header.version = APP_SAVE_VERSION;
    AppGameSaveState save = app_game_save_capture_(state);
    OS_file_write(file, sizeof(header), &header);
    OS_file_write(file, sizeof(save), &save);
    OS_file_close(file);
    LOG_INFO("save", "Saved: pos ({}, {}, {}) tick {}",
             (F64)save.player.position.x, (F64)save.player.position.y,
             (F64)save.player.position.z, save.simTickCounter);
}

static void app_game_save_read_(AppCoreState* state) {
    if (state->replay.mode != AppReplayMode_Idle) {
        LOG_ERROR("save", "Load ignored while replay is {}",
                  state->replay.mode == AppReplayMode_Recording ? str8("recording") : str8("playing"));
        return;
    }
    OS_Handle file = OS_file_open(APP_SAVE_PATH, OS_FileOpenMode_Read);
    if (!file.handle) {
        LOG_ERROR("save", "No save at '{}'", str8(APP_SAVE_PATH));
        return;
    }
    // Seekable files take the range overload; the size overload is the
    // pipe/stream path and asserts on regular files.
    AppSaveFileHeader header = {};
    AppGameSaveState save = {};
    RangeU64 headerRange = {0ull, sizeof(header)};
    RangeU64 bodyRange = {sizeof(header), sizeof(header) + sizeof(save)};
    U64 readHeader = OS_file_read(file, headerRange, &header);
    U64 readBody = OS_file_read(file, bodyRange, &save);
    OS_file_close(file);
    if (readHeader != sizeof(header) || readBody != sizeof(save) ||
        header.magic != APP_SAVE_MAGIC || header.version != APP_SAVE_VERSION) {
        // Forward-discard: an old or foreign blob is dead data, not an
        // upgrade project.
        LOG_ERROR("save", "Discarding save (magic/version/size mismatch)");
        return;
    }
    app_game_save_apply_(state, &save);
    LOG_INFO("save", "Loaded: pos ({}, {}, {}) tick {}",
             (F64)save.player.position.x, (F64)save.player.position.y,
             (F64)save.player.position.z, save.simTickCounter);
}

static void app_game_record_start_(AppCoreState* state) {
    AppReplayState* replay = &state->replay;
    if (replay->mode != AppReplayMode_Idle) {
        return;
    }
    replay->mode = AppReplayMode_Recording;
    replay->cursor = 0u;
    replay->tickCount = 0u;
    replay->checkCount = 0u;
    replay->divergedAtTick = 0ull;
    replay->initial = app_game_save_capture_(state);
    LOG_INFO("replay", "Recording from tick {}", replay->initial.simTickCounter);
}

static void app_game_record_stop_(AppCoreState* state) {
    AppReplayState* replay = &state->replay;
    if (replay->mode != AppReplayMode_Recording) {
        return;
    }
    replay->mode = AppReplayMode_Idle;
    replay->tickCount = replay->cursor;
    replay->checkCount = replay->cursor / APP_REPLAY_CHECK_INTERVAL;

    OS_create_directory("saves");
    OS_Handle file = OS_file_open(APP_REPLAY_PATH, OS_FileOpenMode_Create);
    if (!file.handle) {
        LOG_ERROR("replay", "Cannot open '{}' for write", str8(APP_REPLAY_PATH));
        return;
    }
    AppReplayFileHeader header = {};
    header.magic = APP_REPLAY_MAGIC;
    header.version = APP_REPLAY_VERSION;
    header.tickCount = replay->tickCount;
    header.checkInterval = APP_REPLAY_CHECK_INTERVAL;
    OS_file_write(file, sizeof(header), &header);
    OS_file_write(file, sizeof(replay->initial), &replay->initial);
    OS_file_write(file, (U64)replay->tickCount * sizeof(AppGameActions), replay->actions);
    OS_file_write(file, (U64)replay->checkCount * sizeof(U64), replay->checksums);
    OS_file_close(file);
    LOG_INFO("replay", "Recorded {} ticks ({} checksums) -> '{}'",
             replay->tickCount, replay->checkCount, str8(APP_REPLAY_PATH));
}

static void app_game_replay_start_(AppCoreState* state) {
    AppReplayState* replay = &state->replay;
    if (replay->mode != AppReplayMode_Idle) {
        return;
    }
    OS_Handle file = OS_file_open(APP_REPLAY_PATH, OS_FileOpenMode_Read);
    if (!file.handle) {
        LOG_ERROR("replay", "No replay at '{}'", str8(APP_REPLAY_PATH));
        return;
    }
    AppReplayFileHeader header = {};
    RangeU64 headerRange = {0ull, sizeof(header)};
    U64 readHeader = OS_file_read(file, headerRange, &header);
    if (readHeader != sizeof(header) ||
        header.magic != APP_REPLAY_MAGIC || header.version != APP_REPLAY_VERSION ||
        header.tickCount > APP_REPLAY_MAX_TICKS ||
        header.checkInterval != APP_REPLAY_CHECK_INTERVAL) {
        LOG_ERROR("replay", "Discarding replay (header mismatch)");
        OS_file_close(file);
        return;
    }
    U64 actionBytes = (U64)header.tickCount * sizeof(AppGameActions);
    U32 checkCount = header.tickCount / APP_REPLAY_CHECK_INTERVAL;
    U64 checkBytes = (U64)checkCount * sizeof(U64);
    U64 at = sizeof(header);
    RangeU64 initialRange = {at, at + sizeof(replay->initial)};
    U64 readInitial = OS_file_read(file, initialRange, &replay->initial);
    at += sizeof(replay->initial);
    RangeU64 actionsRange = {at, at + actionBytes};
    U64 readActions = OS_file_read(file, actionsRange, replay->actions);
    at += actionBytes;
    RangeU64 checksRange = {at, at + checkBytes};
    U64 readChecks = OS_file_read(file, checksRange, replay->checksums);
    OS_file_close(file);
    if (readInitial != sizeof(replay->initial) || readActions != actionBytes ||
        readChecks != checkBytes) {
        LOG_ERROR("replay", "Discarding replay (truncated)");
        return;
    }
    replay->mode = AppReplayMode_Playing;
    replay->cursor = 0u;
    replay->tickCount = header.tickCount;
    replay->checkCount = checkCount;
    replay->divergedAtTick = 0ull;
    app_game_save_apply_(state, &replay->initial);
    LOG_INFO("replay", "Playing {} ticks from tick {}",
             replay->tickCount, replay->initial.simTickCounter);
}

static void app_game_replay_stop_(AppCoreState* state) {
    AppReplayState* replay = &state->replay;
    if (replay->mode == AppReplayMode_Recording) {
        app_game_record_stop_(state);
        return;
    }
    if (replay->mode == AppReplayMode_Playing) {
        replay->mode = AppReplayMode_Idle;
        LOG_INFO("replay", "Playback aborted at relative tick {}", replay->cursor);
    }
}

// Per-tick hooks for the drain loop. The playback override replaces the
// live action sample; the post-tick hook records actions/checksums or
// verifies them. Returns whether playback supplied the actions.
static B32 app_game_replay_tick_actions_(AppCoreState* state, AppGameActions* actions) {
    AppReplayState* replay = &state->replay;
    if (replay->mode != AppReplayMode_Playing) {
        return 0;
    }
    if (replay->cursor >= replay->tickCount) {
        replay->mode = AppReplayMode_Idle;
        LOG_INFO("replay", "Playback finished: {}",
                 replay->divergedAtTick == 0ull ? str8("checksums clean")
                                                : str8("DIVERGED (see first bad tick above)"));
        return 0;
    }
    *actions = replay->actions[replay->cursor];
    return 1;
}

static void app_game_replay_post_tick_(AppCoreState* state, const AppGameActions* actions) {
    AppReplayState* replay = &state->replay;
    if (replay->mode == AppReplayMode_Recording) {
        if (replay->cursor >= APP_REPLAY_MAX_TICKS) {
            LOG_INFO("replay", "Recording buffer full; stopping");
            app_game_record_stop_(state);
            return;
        }
        replay->actions[replay->cursor] = *actions;
        replay->cursor += 1u;
        if ((replay->cursor % APP_REPLAY_CHECK_INTERVAL) == 0u) {
            replay->checksums[replay->cursor / APP_REPLAY_CHECK_INTERVAL - 1u] =
                app_game_state_checksum_(&state->game.player);
        }
    } else if (replay->mode == AppReplayMode_Playing) {
        replay->cursor += 1u;
        if ((replay->cursor % APP_REPLAY_CHECK_INTERVAL) == 0u) {
            U32 checkIndex = replay->cursor / APP_REPLAY_CHECK_INTERVAL - 1u;
            if (checkIndex < replay->checkCount) {
                U64 expected = replay->checksums[checkIndex];
                U64 got = app_game_state_checksum_(&state->game.player);
                if (expected != got && replay->divergedAtTick == 0ull) {
                    replay->divergedAtTick = replay->initial.simTickCounter + replay->cursor;
                    LOG_ERROR("replay",
                              "DIVERGENCE at relative tick {} (absolute {}): expected {} got {}",
                              replay->cursor, replay->divergedAtTick, expected, got);
                }
            }
        }
    }
}
