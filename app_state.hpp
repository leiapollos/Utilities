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

    B32 initialized;
    U32 loadLogMask;
};

#define APP_WORLD_MAX_RENDERABLES 16384u
#define APP_WORLD_MAX_MESHES 8u
#define APP_WORLD_MAX_MATERIALS 16u
#define APP_WORLD_BIN_COUNT 3u
#define APP_WORLD_FRAME_BUFFER_COUNT 2u
#define APP_WORLD_SHADER_COUNT 7u

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
    Vec3F32 boundsCenter;
    Vec3F32 boundsExtents;
    F32 boundsRadius;
};

struct AppWorldState {
    B32 gpuResourcesCreated;

    SlotMap meshes;
    U32 meshCount;
    U32 materialCount;

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

    ShdWorldFrameRecord frameRecord;
    ShdWorldRenderableRecord* renderables;
    ShdWorldRenderableRecord* transparents;
    F32* transparentDepths;
    U32 renderableCount;
    U32 transparentCount;
    U32 lastRenderableCount;
    B32 frameOpen;
};

struct AppDemoState {
    U8 titleBuffer[128];
    U32 titleLength;
    F32 titleSize;
    B32 showBounds;
    B32 animate;
    U32 gridSide;
};

struct AppCoreState {
    U32 windowWidth;
    U32 windowHeight;
    U64 frameCounter;
    U32 reloadCount;
    F32 lastDeltaSeconds;
    F32 averageDeltaSeconds;
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
