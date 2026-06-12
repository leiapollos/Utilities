//
// Created by André Leite on 12/06/2026.
//
// The demo's hot state: its own store slot, its own version, independent
// of the engine slot. Holds the game, the collision world, the demo
// settings, and the save payload — nothing the engine needs to know.
//

#pragma once

#define DEMO_STATE_VERSION 1u

#define DEMO_GRID_MIN 4u
#define DEMO_GRID_MAX 120u

struct DemoSettings {
    U8 titleBuffer[128];
    U32 titleLength;
    F32 titleSize;
    B32 showBounds;
    B32 animate;
    B32 threadedExtract;
    B32 playerMode;
    U32 maxLanes;
    U32 gridSide;

    // Scene-owned material slots, allocated from the world table once the
    // world's GPU resources exist.
    B32 materialsReady;
    U32 paletteMaterials[6];
    U32 alphaTestMaterial;
    U32 transparentMaterials[2];
    U32 playerMaterial;
};

// Indexes the project sound table registered with the engine.
enum DemoSound {
    DemoSound_Jump = 0u,
    DemoSound_Land = 1u,
    DemoSound_Click = 2u,
    DemoSound_Ambience = 3u,
    DemoSound_Count = 4u,
};

#define DEMO_SOUND_GAIN_JUMP 0.5f
#define DEMO_SOUND_GAIN_LAND 0.7f
#define DEMO_SOUND_GAIN_CLICK 0.9f
#define DEMO_SOUND_GAIN_AMBIENCE 0.6f

#define DEMO_GAME_MAX_KEYS 256u

// Action layer + player sim + follow camera. The player ticks inside the
// engine's fixed-tick drain when player mode is on; the camera rig is
// render-time state.
struct DemoGameState {
    DemoPlayerState player;
    Vec3F32 playerPrevPosition; // position at the previous tick, for render interpolation
    B32 keyDown[DEMO_GAME_MAX_KEYS];
    B32 cameraDragging;
    B32 cameraInitialized;
    F32 cameraYaw;
    F32 cameraPitch;
    F32 cameraDistance;
    Vec3F32 cameraTarget; // smoothed look target
    DemoTickStats lastTickStats; // observability only, never sim input
};

// The serialized subset: the closure of the sim's inputs (player + camera
// rig + sim clock + the grid side the colliders derive from). Explicit
// struct rather than DemoGameState so transient input (keyDown, drag)
// never reaches disk; also the payload of the save file and the replay's
// initial state. Every future input the tick reads must enter this struct
// the same day it is added, or replay rots.
struct DemoSaveState {
    DemoPlayerState player;
    F32 cameraYaw;
    F32 cameraPitch;
    F32 cameraDistance;
    Vec3F32 cameraTarget;
    U32 gridSide; // world-defining sim input: the collision world derives from it
    U64 simTickCounter;
};

static_assert(sizeof(DemoSaveState) <= ENG_SIM_MAX_SAVE_SIZE, "save payload exceeds the engine blob cap");
static_assert(sizeof(DemoActions) <= ENG_SIM_MAX_ACTION_SIZE, "actions exceed the engine blob cap");

struct DemoState {
    DemoGameState game;
    DemoColliderSet colliders; // derived from gridSide; rebuilt, never saved
    U32 colliderBuiltSide;     // 0 = never built
    DemoSettings settings;
    B32 ambienceOn; // outside settings on purpose: survives the demo reset button
    U32 reloadSeen;             // engine reloadCount last handled (collider rebuild)
    U32 ambienceGenerationSeen; // engine sound generation last handled (loop restart)
};

// The project slot, typed. Valid wherever an EngContext is.
static DemoState* demo_state_(EngContext* ctx) {
    return (DemoState*)ctx->project;
}
