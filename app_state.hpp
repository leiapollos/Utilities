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
    U32 maxLanes;
    U32 gridSide;

    // Scene-owned material slots, allocated from the world table once the
    // world's GPU resources exist.
    B32 materialsReady;
    U32 paletteMaterials[6];
    U32 alphaTestMaterial;
    U32 transparentMaterials[2];
};

#define APP_SIM_TICK_HZ 60u
#define APP_SIM_TICK_DT (1.0f / (F32)APP_SIM_TICK_HZ)
#define APP_SIM_MAX_FRAME_DT 0.25f

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
    UI_State ui;
};

struct APP_Context {
    AppHost* host;
    HOT_StateStore* store;
    AppCoreState* core;
};
