//
// Created by André Leite on 11/06/2026.
//
// The action layer, follow camera, and the demo's sim hooks. Events fold
// into key state gated by the UI's device claims (one frame delayed, same
// contract as UI hit testing); one action sample per frame feeds every
// catch-up tick; the camera rig is render-time state and never enters the
// sim. The save payload and tick body live here — the engine drains the
// clock and runs the replay machinery over them as opaque blobs.
//

#define DEMO_GAME_CAMERA_SENSITIVITY 0.005f
#define DEMO_GAME_CAMERA_ZOOM_RATE 0.05f
#define DEMO_GAME_CAMERA_MIN_DISTANCE 6.0f
#define DEMO_GAME_CAMERA_MAX_DISTANCE 120.0f
#define DEMO_GAME_CAMERA_MIN_PITCH 0.08f
#define DEMO_GAME_CAMERA_MAX_PITCH 1.35f
#define DEMO_GAME_CAMERA_FOLLOW_SPEED 10.0f

static void demo_game_fold_events_(EngContext* ctx) {
    DemoState* demo = demo_state_(ctx);
    DemoGameState* game = &demo->game;
    const EngInput* input = ctx->input;

    // The UI owns the keyboard (focused edit): drop every held key so
    // nothing sticks while typing.
    if (ctx->engine->ui.wantKeyboard) {
        MEMSET(game->keyDown, 0, sizeof(game->keyDown));
    }

    for (U32 eventIndex = 0u; eventIndex < input->eventCount; ++eventIndex) {
        const OS_GraphicsEvent* event = input->events + eventIndex;
        switch (event->tag) {
            case OS_GraphicsEvent_Tag_KeyDown: {
                if (!ctx->engine->ui.wantKeyboard && (U32)event->keyDown.keyCode < DEMO_GAME_MAX_KEYS) {
                    game->keyDown[event->keyDown.keyCode] = 1;
                }
            } break;
            case OS_GraphicsEvent_Tag_KeyUp: {
                if ((U32)event->keyUp.keyCode < DEMO_GAME_MAX_KEYS) {
                    game->keyDown[event->keyUp.keyCode] = 0;
                }
            } break;
            case OS_GraphicsEvent_Tag_MouseButtonDown: {
                if (!ctx->engine->ui.wantMouse && event->mouseButtonDown.button == OS_MouseButton_Left) {
                    game->cameraDragging = 1;
                }
            } break;
            case OS_GraphicsEvent_Tag_MouseButtonUp: {
                if (event->mouseButtonUp.button == OS_MouseButton_Left) {
                    game->cameraDragging = 0;
                }
            } break;
            case OS_GraphicsEvent_Tag_MouseMove: {
                if (game->cameraDragging && demo->settings.playerMode) {
                    game->cameraYaw += event->mouseMove.deltaX * DEMO_GAME_CAMERA_SENSITIVITY;
                    game->cameraPitch = CLAMP(game->cameraPitch +
                                                  event->mouseMove.deltaY * DEMO_GAME_CAMERA_SENSITIVITY,
                                              DEMO_GAME_CAMERA_MIN_PITCH, DEMO_GAME_CAMERA_MAX_PITCH);
                }
            } break;
            case OS_GraphicsEvent_Tag_MouseScroll: {
                if (!ctx->engine->ui.wantMouse && demo->settings.playerMode) {
                    game->cameraDistance = CLAMP(game->cameraDistance -
                                                     event->mouseScroll.deltaY * DEMO_GAME_CAMERA_ZOOM_RATE,
                                                 DEMO_GAME_CAMERA_MIN_DISTANCE, DEMO_GAME_CAMERA_MAX_DISTANCE);
                }
            } break;
            default: break;
        }
    }
}

static void demo_game_camera_(EngContext* ctx, F32 frameDt, Vec3F32* outEye, Vec3F32* outTarget) {
    DemoState* demo = demo_state_(ctx);
    DemoGameState* game = &demo->game;
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
    Vec3F32 renderPosition = demo_game_render_position_(game->playerPrevPosition,
                                                       game->player.position,
                                                       ctx->engine->simAccumulator);
    F32 rate = CLAMP(DEMO_GAME_CAMERA_FOLLOW_SPEED * frameDt, 0.0f, 1.0f);
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
// Sim hooks (the EngProject contract)

// One sample per frame; catch-up ticks share it. The wish direction maps
// camera-relative keys into world space here so the tick stays pure.
// Returns whether the sim should tick at all this frame (player mode).
static B32 demo_sim_sample_(EngContext* ctx, void* outActions) {
    DemoState* demo = demo_state_(ctx);
    DemoGameState* game = &demo->game;
    DemoActions* actions = (DemoActions*)outActions;
    *actions = {};

    F32 forwardInput = (game->keyDown[OS_KeyCode_W] || game->keyDown[OS_KeyCode_UpArrow]) ? 1.0f : 0.0f;
    F32 backInput = (game->keyDown[OS_KeyCode_S] || game->keyDown[OS_KeyCode_DownArrow]) ? 1.0f : 0.0f;
    F32 rightInput = (game->keyDown[OS_KeyCode_D] || game->keyDown[OS_KeyCode_RightArrow]) ? 1.0f : 0.0f;
    F32 leftInput = (game->keyDown[OS_KeyCode_A] || game->keyDown[OS_KeyCode_LeftArrow]) ? 1.0f : 0.0f;

    F32 forwardX = -COS_F32(game->cameraYaw);
    F32 forwardZ = -SIN_F32(game->cameraYaw);
    F32 rightX = SIN_F32(game->cameraYaw);
    F32 rightZ = -COS_F32(game->cameraYaw);

    actions->moveX = forwardX * (forwardInput - backInput) + rightX * (rightInput - leftInput);
    actions->moveZ = forwardZ * (forwardInput - backInput) + rightZ * (rightInput - leftInput);
    actions->jump = game->keyDown[OS_KeyCode_Space];
    return demo->settings.playerMode;
}

// Sounds key off grounded transitions so the tick stays pure: the sim
// never knows audio exists.
static void demo_sim_tick_(EngContext* ctx, const void* actionsBlob) {
    DemoState* demo = demo_state_(ctx);
    const DemoActions* actions = (const DemoActions*)actionsBlob;
    demo->game.playerPrevPosition = demo->game.player.position;
    B32 wasGrounded = demo->game.player.grounded;
    DemoTickStats tickStats = {};
    demo_game_tick_(&demo->game.player, actions, &demo->colliders,
                    ctx->engine->simTickCounter, &tickStats);
    demo->game.lastTickStats = tickStats;
    if (wasGrounded && !demo->game.player.grounded &&
        demo->game.player.velocity.y > 0.0f) {
        audio_play(ctx->host->audioSystem, ctx->engine->audio.sounds[DemoSound_Jump],
                   DEMO_SOUND_GAIN_JUMP, 0);
    }
    if (!wasGrounded && demo->game.player.grounded) {
        audio_play(ctx->host->audioSystem, ctx->engine->audio.sounds[DemoSound_Land],
                   DEMO_SOUND_GAIN_LAND, 0);
    }
}

static void demo_sim_capture_(EngContext* ctx, void* outSave) {
    DemoState* demo = demo_state_(ctx);
    DemoSaveState* save = (DemoSaveState*)outSave;
    *save = {};
    save->player = demo->game.player;
    save->cameraYaw = demo->game.cameraYaw;
    save->cameraPitch = demo->game.cameraPitch;
    save->cameraDistance = demo->game.cameraDistance;
    save->cameraTarget = demo->game.cameraTarget;
    save->gridSide = demo->settings.gridSide;
    save->simTickCounter = ctx->engine->simTickCounter;
}

static void demo_sim_apply_(EngContext* ctx, const void* saveBlob) {
    DemoState* demo = demo_state_(ctx);
    const DemoSaveState* save = (const DemoSaveState*)saveBlob;
    demo->game.player = save->player;
    demo->game.playerPrevPosition = save->player.position;
    demo->game.cameraYaw = save->cameraYaw;
    demo->game.cameraPitch = save->cameraPitch;
    demo->game.cameraDistance = save->cameraDistance;
    demo->game.cameraTarget = save->cameraTarget;
    demo->game.cameraInitialized = 1;
    ctx->engine->simTickCounter = save->simTickCounter;
    // The grid side is a sim input (the collision world derives from it):
    // restoring the state means restoring the world, immediately — the
    // first replayed tick must not run against the old colliders.
    demo->settings.gridSide = CLAMP(save->gridSide, DEMO_GRID_MIN, DEMO_GRID_MAX);
    demo_scene_build_colliders_(demo->settings.gridSide, &demo->colliders);
    demo->colliderBuiltSide = demo->settings.gridSide;
    LOG_INFO("save", "Loaded: pos ({}, {}, {}) tick {}",
             (F64)save->player.position.x, (F64)save->player.position.y,
             (F64)save->player.position.z, save->simTickCounter);
}

static U64 demo_sim_checksum_(EngContext* ctx) {
    DemoState* demo = demo_state_(ctx);
    return demo_game_state_checksum_(&demo->game.player);
}
