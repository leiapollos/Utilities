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

// The 2D rendering state: text shaping/caching (CPU) plus the renderer-owned
// GPU resources that execute draw2d batches. This is the whole current app;
// the demo scene is a thin producer on top.
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

    B32 initialized;
    U32 loadLogMask;
};

struct AppCoreState {
    U32 windowWidth;
    U32 windowHeight;
    U64 frameCounter;
    U32 reloadCount;

    JobSystem* jobSystem;
    U32 workerCount;

    AppResourceState resources;
    AppGfxShaderBuildState gfxShaderBuild;
    AppRender2DState render2d;
};

struct APP_Context {
    AppHost* host;
    HOT_StateStore* store;
    AppCoreState* core;
};
