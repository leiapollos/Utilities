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

struct AppGfxDemoObject;
struct AppGfxMaterial;

struct AppCoreState {
    U32 windowWidth;
    U32 windowHeight;
    U64 frameCounter;
    U32 reloadCount;
    F32 gfxDemoAnimationSeconds;

    JobSystem* jobSystem;
    U32 workerCount;

    B32 gfxDemoInitialized;
    B32 gfxDemoGeometryCreated;
    B32 gfxDemoGeometryUploaded;
    B32 gfxDemoReady;
    U32 gfxDemoLoadLogMask;
    Arena* resourceArena;
    ContentStore* contentStore;
    FileStream* fileStream;
    ArtifactCache* artifactCache;
    U64 gfxShaderSourceTimestamp;
    B32 gfxShaderBuildInitialized;
    FileHandle gfxTriangleVertexShader;
    FileHandle gfxTriangleFragmentShader;
    FileHandle gfxDemoComputeShader;
    FileHandle gfxDemoTextureSource;
    ArtifactKey gfxTrianglePipelineArtifactKey;
    ArtifactKey gfxDemoComputePipelineArtifactKey;
    ArtifactKey gfxDemoTextureDecodeArtifactKey;
    GfxBuffer gfxTriangleVertexBuffer;
    GfxBuffer gfxTriangleIndexBuffer;
    GfxPipeline gfxTrianglePipeline;
    GfxPipeline gfxDemoComputePipeline;
    GfxTexture gfxDemoTexture;
    GfxTexture gfxDemoOffscreenColor;
    GfxTexture gfxDemoDepth;
    GfxSampler gfxDemoSampler;
    AppGfxDemoObject* gfxDemoObjects;
    U32 gfxDemoObjectCount;
    AppGfxMaterial* gfxDemoMaterialSources;
    U32 gfxDemoMaterialSourceCount;
    GfxBuffer gfxDemoMaterialSourceBuffer;
    GfxResourceId gfxDemoMaterialSourceBufferId;
    GfxBuffer gfxDemoMaterialBuffer;
    GfxResourceId gfxDemoMaterialBufferId;
    GfxResourceId gfxDemoTextureId;
    GfxResourceId gfxDemoSamplerId;
    U32 gfxDemoTargetWidth;
    U32 gfxDemoTargetHeight;
    U32 gfxDemoMaterialCount;
    B32 gfxDemoMaterialSourceUploaded;
    B32 gfxDemoMaterialSourceDirty;
    B32 gfxDemoTextureUploaded;
    B32 gfxDemoMaterialsReady;
    B32 gfxDemoMaterialDirty;
    ContentHash gfxDemoDecodedTextureHash;
    U64 gfxDemoTextureGeneration;
    U64 gfxDemoTextureFailedGeneration;
};

struct APP_Context {
    AppHost* host;
    HOT_StateStore* store;
    AppCoreState* core;
};
