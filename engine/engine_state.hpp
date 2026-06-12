//
// Created by André Leite on 02/11/2025.
//
// Engine-owned hot state: one store slot, engine-versioned, independent
// of any project's slot. Everything here is framework state — resources,
// the 2D overlay, the world renderer tables, cooked-audio handles, the
// sim clock and replay buffers, and the debug overlay toggles.
//

#pragma once

#define ENG_STATE_ID(a, b, c, d) ((((U64)(a)) << 56u) | (((U64)(b)) << 48u) | (((U64)(c)) << 40u) | (((U64)(d)) << 32u) | 0x53544154u)

#define ENG_STATE_VERSION 3u

struct EngState;

struct EngResources {
    Arena* arena;
    ContentStore* contentStore;
    FileStream* fileStream;
    ArtifactCache* artifactCache;
};

struct EngShaderBuild {
    U64 sourceTimestamp;
    B32 initialized;
};

struct EngRender2D {
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

#define ENG_WORLD_MAX_RENDERABLES 16384u
#define ENG_WORLD_MAX_TRANSPARENTS (ENG_WORLD_MAX_RENDERABLES / 4u)
#define ENG_WORLD_MAX_MESHES 512u
#define ENG_WORLD_MAX_MATERIALS 64u
#define ENG_WORLD_BIN_COUNT 3u
// Transparents are CPU-direct since U8; only opaque + alpha-test own GPU
// cells.
#define ENG_WORLD_GPU_BIN_COUNT 2u
#define ENG_WORLD_CELL_COUNT (ENG_WORLD_GPU_BIN_COUNT * ENG_WORLD_MAX_MESHES)
#define ENG_WORLD_FRAME_BUFFER_COUNT 2u
#define ENG_WORLD_SHADER_COUNT 7u
#define ENG_WORLD_MAX_LANES 16u
// Engine budgets, not mirrors of any project's counts; boot asserts
// project desc tables fit.
#define ENG_WORLD_MAX_MODELS 16u
#define ENG_WORLD_MAX_ASSET_TEXTURES 64u
#define ENG_WORLD_MODEL_MAX_TEXTURES 4u
// Material slot 0 is the builtin "missing" material (magenta); the allocator
// never hands it out, so zero-initialized indices fail loudly on screen.
#define ENG_WORLD_MATERIAL_MISSING 0u

enum EngWorldBin {
    EngWorldBin_Opaque = 0,
    EngWorldBin_AlphaTest,
    EngWorldBin_Transparent,
};

struct EngWorldMeshHandle {
    U32 index;
    U32 generation;
};

struct EngWorldMesh {
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

struct EngAssetBridge {
    GfxDevice* device;
    AudioSystem* audioSystem;
    EngState* state;
    GfxFrame* frame;
};

struct EngWorldModelMaterialRef {
    U32 worldSlot;
    U32 textureLocal; // model-local texture index, ASSET_MODEL_NO_TEXTURE if none
};

struct EngWorldModelInstanceRef {
    EngWorldMeshHandle mesh;
    U32 materialSlot; // world material table index
    Mat4x4F32 transform; // model space
};

// One published model generation, owned by an arena carried in the artifact
// value; world->models[] points at the current generation and the artifact
// destroy releases everything (sections, materials, buffers, arena).
struct EngWorldModelResources {
    GfxBuffer vertexBuffer;
    GfxBuffer indexBuffer;
    GfxResourceId vertexBufferId;
    U32 sectionCount;
    EngWorldMeshHandle* sections;
    U32 materialCount;
    EngWorldModelMaterialRef* materials;
    U32 instanceCount;
    EngWorldModelInstanceRef* instances;
    U32 textureCount;
    F32 boundsCenter[3];
    F32 boundsRadius;
};

// One per extraction lane; slices of the per-frame arrays, no sharing.
// Transparent depth/cull happen at merge time on the main thread.
struct EngWorldLaneWriter {
    ShdWorldRenderableRecord* records;
    U32 count;
    U32 cap;
    ShdWorldRenderableRecord* transparents;
    U32 transparentCount;
    U32 transparentCap;
    U32 dropped;
};

struct EngWorldState {
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
    GfxBuffer frameRecordBuffers[ENG_WORLD_FRAME_BUFFER_COUNT];
    GfxResourceId frameRecordBufferIds[ENG_WORLD_FRAME_BUFFER_COUNT];
    GfxBuffer renderableBuffers[ENG_WORLD_FRAME_BUFFER_COUNT];
    GfxResourceId renderableBufferIds[ENG_WORLD_FRAME_BUFFER_COUNT];
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

    FileHandle shaderFiles[ENG_WORLD_SHADER_COUNT];
    ContentHash shaderHashes[ENG_WORLD_SHADER_COUNT];
    GfxPipeline opaquePipeline;
    GfxPipeline transparentPipeline;
    GfxPipeline computePipelines[5];

    EngWorldMeshHandle builtinMeshes[3];

    GfxSampler worldSampler;
    GfxResourceId worldSamplerId;
    ShdWorldMaterialRecord materialRecords[ENG_WORLD_MAX_MATERIALS];
    B32 meshRecordsDirty;
    B32 materialsDirty;

    GfxTexture assetTextures[ENG_WORLD_MAX_ASSET_TEXTURES];
    U32 assetTextureCount;

    FileHandle assetModelFiles[ENG_WORLD_MAX_MODELS];
    FileHandle assetModelTextureFiles[ENG_WORLD_MAX_MODELS][ENG_WORLD_MODEL_MAX_TEXTURES];
    EngWorldModelResources* models[ENG_WORLD_MAX_MODELS];
    B32 assetsSettled;

    ShdWorldFrameRecord frameRecord;
    Vec3F32 cameraForward;
    EngWorldLaneWriter* laneWriters;
    U32 laneCount;
    U32 requestedLaneCount; // project policy, set in pre_frame; 0 = single lane
    U32 lastRenderableCount;
    U32 lastDroppedCount;
    U32 lastTransparentDraws;
    U32 failLogMask;
    B32 frameOpen;
};

// Cooked sounds, published into the host's audio buffer table through the
// artifact cache (the GfxBuffer pattern: the module owns handles, the host
// owns the PCM, so playback survives module reloads). Indexed by the
// project's sound table; generations bump per publish so the project can
// react to republishes (e.g. restart a loop) without engine policy.
#define ENG_AUDIO_MAX_SOUNDS 16u

struct EngAudio {
    FileHandle soundFiles[ENG_AUDIO_MAX_SOUNDS];
    AudioBufferHandle sounds[ENG_AUDIO_MAX_SOUNDS];
    U32 soundGenerations[ENG_AUDIO_MAX_SOUNDS];
    B32 settled;
};

struct EngState {
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
    U64 lastSimTickNanos;
    EngReplay replay;
    B32 debugOverlayVisible;
    B32 profilerVisible;
    B32 profFlatView;
    U32 profSelectedSite;

    JobSystem* jobSystem;
    U32 workerCount;

    EngResources resources;
    EngShaderBuild gfxShaderBuild;
    EngRender2D render2d;
    EngWorldState world;
    EngAudio audio;
    EngAssetBridge assetBridge;
    UI_State ui;
};
