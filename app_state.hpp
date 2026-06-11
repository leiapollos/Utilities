//
// Created by André Leite on 02/11/2025.
//

#pragma once

#define APP_STATE_ID(a, b, c, d) ((((U64)(a)) << 56u) | (((U64)(b)) << 48u) | (((U64)(c)) << 40u) | (((U64)(d)) << 32u) | 0x53544154u)

enum APP_StateKind {
    APP_State_Core = 0,
    APP_State_COUNT,
};

struct APP_StateDesc {
    U64 id;
    const char* name;
    U32 version;
    U64 size;
    U64 alignment;
};

struct AppResourceState {
    Arena* arena;
    ContentStore* contentStore;
    FileStream* fileStream;
    ArtifactCache* artifactCache;
};

struct AppGfxShaderBuildState {
    U64 sourceTimestamp;
    B32 initialized;
};

struct AppRender2DState {
    TextContext* textContext;
    TextFont font;
    FileHandle fontFile;
    U64 failedFontGeneration;

    FileHandle vertexShaderFile;
    FileHandle fragmentShaderFile;
    ContentHash vertexShaderHash;
    ContentHash fragmentShaderHash;
    GfxPipeline pipeline;

    GfxTexture atlasTexture;
    GfxResourceId atlasTextureId;
    GfxSampler atlasSampler;
    GfxResourceId atlasSamplerId;
    GfxBuffer indexBuffer;
    GfxBuffer quadBuffers[2];
    GfxResourceId quadBufferIds[2];
    B32 gpuResourcesCreated;
    B32 atlasSeeded;

    Draw2DContext draw2d;
    GfxStats lastGfxStats;
    Draw2DStats lastDraw2DStats;

    U32 lastFilePublishCount;
    B32 initialized;
    U32 loadLogMask;
};

struct AppCoreState;

#define APP_WORLD_MAX_RENDERABLES 16384u
#define APP_WORLD_MAX_TRANSPARENTS (APP_WORLD_MAX_RENDERABLES / 4u)
#define APP_WORLD_MAX_MESHES 512u
#define APP_WORLD_MAX_MATERIALS 64u
#define APP_WORLD_BIN_COUNT 3u
// Transparents are CPU-direct since U8; only opaque + alpha-test own GPU
// cells.
#define APP_WORLD_GPU_BIN_COUNT 2u
#define APP_WORLD_CELL_COUNT (APP_WORLD_GPU_BIN_COUNT * APP_WORLD_MAX_MESHES)
#define APP_WORLD_FRAME_BUFFER_COUNT 2u
#define APP_WORLD_SHADER_COUNT 7u
#define APP_WORLD_MAX_LANES 16u
#define APP_WORLD_DEMO_ASSET_COUNT 4u
#define APP_WORLD_MAX_ASSET_TEXTURES 64u
#define APP_WORLD_MODEL_MAX_TEXTURES 4u
// Material slot 0 is the builtin "missing" material (magenta); the allocator
// never hands it out, so zero-initialized indices fail loudly on screen.
#define APP_WORLD_MATERIAL_MISSING 0u

enum AppWorldBin {
    AppWorldBin_Opaque = 0,
    AppWorldBin_AlphaTest,
    AppWorldBin_Transparent,
};

struct AppWorldMeshHandle {
    U32 index;
    U32 generation;
};

struct AppWorldMesh {
    U32 indexCount;
    U32 firstIndex;
    U32 baseVertex;
    GfxBuffer vertexBuffer;
    GfxResourceId vertexBufferId;
    U32 vertexByteOffset;
    GfxBuffer indexBuffer;
    U32 indexByteOffset;
    B32 ownsBuffers;
    Vec3F32 boundsCenter;
    Vec3F32 boundsExtents;
    F32 boundsRadius;
};

struct AppWorldArtifactBridge {
    GfxDevice* device;
    AudioSystem* audioSystem;
    AppCoreState* state;
    GfxFrame* frame;
};

struct AppWorldModelMaterialRef {
    U32 worldSlot;
    U32 textureLocal; // model-local texture index, ASSET_MODEL_NO_TEXTURE if none
};

struct AppWorldModelInstanceRef {
    AppWorldMeshHandle mesh;
    U32 materialSlot; // world material table index
    Mat4x4F32 transform; // model space
};

// One published model generation, owned by an arena carried in the artifact
// value; world->models[] points at the current generation and the artifact
// destroy releases everything (sections, materials, buffers, arena).
struct AppWorldModelResources {
    GfxBuffer vertexBuffer;
    GfxBuffer indexBuffer;
    GfxResourceId vertexBufferId;
    U32 sectionCount;
    AppWorldMeshHandle* sections;
    U32 materialCount;
    AppWorldModelMaterialRef* materials;
    U32 instanceCount;
    AppWorldModelInstanceRef* instances;
    U32 textureCount;
    F32 boundsCenter[3];
    F32 boundsRadius;
};

// One per extraction lane; slices of the per-frame arrays, no sharing.
// Transparent depth/cull happen at merge time on the main thread.
struct AppWorldLaneWriter {
    ShdWorldRenderableRecord* records;
    U32 count;
    U32 cap;
    ShdWorldRenderableRecord* transparents;
    U32 transparentCount;
    U32 transparentCap;
    U32 dropped;
};

struct AppWorldState {
    B32 gpuResourcesCreated;

    SlotMap meshes;
    U32 meshCount;
    U64 materialUsedMask;

    GfxBuffer vertexBuffer;
    GfxResourceId vertexBufferId;
    GfxBuffer indexBuffer;
    GfxBuffer meshRecordBuffer;
    GfxResourceId meshRecordBufferId;
    GfxBuffer materialBuffer;
    GfxResourceId materialBufferId;
    GfxBuffer frameRecordBuffers[APP_WORLD_FRAME_BUFFER_COUNT];
    GfxResourceId frameRecordBufferIds[APP_WORLD_FRAME_BUFFER_COUNT];
    GfxBuffer renderableBuffers[APP_WORLD_FRAME_BUFFER_COUNT];
    GfxResourceId renderableBufferIds[APP_WORLD_FRAME_BUFFER_COUNT];
    GfxBuffer flagsBuffer;
    GfxResourceId flagsBufferId;
    GfxBuffer cellCountBuffer;
    GfxResourceId cellCountBufferId;
    GfxBuffer cellOffsetBuffer;
    GfxResourceId cellOffsetBufferId;
    GfxBuffer cellCursorBuffer;
    GfxResourceId cellCursorBufferId;
    GfxBuffer visibleBuffer;
    GfxResourceId visibleBufferId;
    GfxBuffer argsBuffer;
    GfxResourceId argsBufferId;

    GfxTexture depthTexture;
    U32 depthWidth;
    U32 depthHeight;

    FileHandle shaderFiles[APP_WORLD_SHADER_COUNT];
    ContentHash shaderHashes[APP_WORLD_SHADER_COUNT];
    GfxPipeline opaquePipeline;
    GfxPipeline transparentPipeline;
    GfxPipeline computePipelines[5];

    AppWorldMeshHandle builtinMeshes[3];

    GfxSampler worldSampler;
    GfxResourceId worldSamplerId;
    ShdWorldMaterialRecord materialRecords[APP_WORLD_MAX_MATERIALS];
    B32 meshRecordsDirty;
    B32 materialsDirty;

    GfxTexture assetTextures[APP_WORLD_MAX_ASSET_TEXTURES];
    U32 assetTextureCount;

    FileHandle assetModelFiles[APP_WORLD_DEMO_ASSET_COUNT];
    FileHandle assetModelTextureFiles[APP_WORLD_DEMO_ASSET_COUNT][APP_WORLD_MODEL_MAX_TEXTURES];
    AppWorldModelResources* models[APP_WORLD_DEMO_ASSET_COUNT];
    B32 assetsSettled;
    AppWorldArtifactBridge artifactBridge;

    ShdWorldFrameRecord frameRecord;
    Vec3F32 cameraForward;
    AppWorldLaneWriter* laneWriters;
    U32 laneCount;
    U32 lastRenderableCount;
    U32 lastDroppedCount;
    U32 lastTransparentDraws;
    U32 failLogMask;
    B32 frameOpen;
};

#define APP_DEMO_GRID_MIN 4u
#define APP_DEMO_GRID_MAX 120u

struct AppDemoState {
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

// APP_SIM_TICK_* live in app_game_kernels.hpp with the tick they drive.

// Cooked sounds, published into the host's audio buffer table through the
// artifact cache (the GfxBuffer pattern: the module owns handles, the host
// owns the PCM, so playback survives module reloads).
enum AppSound {
    AppSound_Jump = 0u,
    AppSound_Land = 1u,
    AppSound_Click = 2u,
    AppSound_Ambience = 3u,
    AppSound_Count = 4u,
};

#define APP_SOUND_GAIN_JUMP 0.5f
#define APP_SOUND_GAIN_LAND 0.7f
#define APP_SOUND_GAIN_CLICK 0.9f
#define APP_SOUND_GAIN_AMBIENCE 0.6f

struct AppAudioState {
    FileHandle soundFiles[AppSound_Count];
    AudioBufferHandle sounds[AppSound_Count];
    B32 settled;
    B32 ambienceOn;
};

#define APP_GAME_MAX_KEYS 256u

// Action layer + player sim + follow camera. The player ticks inside the
// fixed-tick drain when the demo's player mode is on; the camera rig is
// render-time state.
struct AppGameState {
    AppPlayerState player;
    Vec3F32 playerPrevPosition; // position at the previous tick, for render interpolation
    B32 keyDown[APP_GAME_MAX_KEYS];
    B32 cameraDragging;
    B32 cameraInitialized;
    F32 cameraYaw;
    F32 cameraPitch;
    F32 cameraDistance;
    Vec3F32 cameraTarget; // smoothed look target
    AppGameTickStats lastTickStats; // observability only, never sim input
    U64 lastTickNanos;
};

// The serialized subset: the closure of the sim's inputs (player + camera
// rig + sim clock + the grid side the colliders derive from). Explicit
// struct rather than AppGameState so transient input (keyDown, drag)
// never reaches disk; also the payload of the save file and the replay's
// initial state. Every future input the tick reads must enter this struct
// the same day it is added, or replay rots.
struct AppGameSaveState {
    AppPlayerState player;
    F32 cameraYaw;
    F32 cameraPitch;
    F32 cameraDistance;
    Vec3F32 cameraTarget;
    U32 gridSide; // world-defining sim input: the collision world derives from it
    U64 simTickCounter;
};

// Input replay: actions per tick from a recorded start state, with a
// player checksum every APP_REPLAY_CHECK_INTERVAL ticks. Playback feeds
// the drain loop instead of live input; the first checksum mismatch
// names the exact diverging tick. Indexed by ticks relative to start, so
// recording and playback are frame-rate independent by construction.
#define APP_REPLAY_MAX_TICKS 18000u // 5 minutes at 60 Hz
#define APP_REPLAY_CHECK_INTERVAL 60u
#define APP_REPLAY_MAX_CHECKS (APP_REPLAY_MAX_TICKS / APP_REPLAY_CHECK_INTERVAL)

enum AppReplayMode {
    AppReplayMode_Idle = 0u,
    AppReplayMode_Recording = 1u,
    AppReplayMode_Playing = 2u,
};

struct AppReplayState {
    U32 mode;
    U32 cursor;    // ticks recorded / consumed, relative to start
    U32 tickCount; // loaded replay length (playback only)
    U32 checkCount;
    U64 divergedAtTick; // absolute sim tick of first mismatch; 0 = clean
    AppGameSaveState initial;
    AppGameActions actions[APP_REPLAY_MAX_TICKS];
    U64 checksums[APP_REPLAY_MAX_CHECKS];
};

struct AppCoreState {
    U32 windowWidth;
    U32 windowHeight;
    U64 frameCounter;
    U32 reloadCount;
    F32 lastDeltaSeconds;
    F32 averageDeltaSeconds;
    U64 simTickCounter;
    F32 simAccumulator;
    F32 simForcedDt;
    F64 simTimeSeconds;
    U32 simClampCount;
    AppGameState game;
    AppReplayState replay;
    AppColliderSet colliders; // derived from gridSide; rebuilt, never saved
    U32 colliderBuiltSide;    // 0 = never built
    B32 debugOverlayVisible;
    B32 profilerVisible;
    B32 profFlatView;
    U32 profSelectedSite;

    JobSystem* jobSystem;
    U32 workerCount;

    AppResourceState resources;
    AppGfxShaderBuildState gfxShaderBuild;
    AppRender2DState render2d;
    AppWorldState world;
    AppDemoState demo;
    AppAudioState audio;
    UI_State ui;
};

struct APP_Context {
    AppHost* host;
    HOT_StateStore* store;
    AppCoreState* core;
};
