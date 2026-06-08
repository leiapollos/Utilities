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
struct AppGfxGpuObject;
struct AppGfxGpuCullSource;
struct AppGfxGpuCullObject;
struct AppGfxMaterial;
struct AppGfxVisibilityBin;
struct AppGfxVisibilityGroup;

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

struct AppGfxDemoShaderFiles {
    FileHandle triangleVertex;
    FileHandle triangleFragment;
    FileHandle materialCompute;
    FileHandle cullBoundsCompute;
    FileHandle visibilityCountCompute;
    FileHandle visibilityPrefixCompute;
    FileHandle visibilityCompactCompute;
    FileHandle textureSource;
};

struct AppGfxDemoPipelineState {
    ArtifactKey triangleOpaqueArtifactKey;
    ArtifactKey triangleTransparentArtifactKey;
    ArtifactKey materialComputeArtifactKey;
    ArtifactKey cullBoundsComputeArtifactKey;
    ArtifactKey visibilityCountComputeArtifactKey;
    ArtifactKey visibilityPrefixComputeArtifactKey;
    ArtifactKey visibilityCompactComputeArtifactKey;
    GfxPipeline triangleOpaque;
    GfxPipeline triangleTransparent;
    GfxPipeline materialCompute;
    GfxPipeline cullBoundsCompute;
    GfxPipeline visibilityCountCompute;
    GfxPipeline visibilityPrefixCompute;
    GfxPipeline visibilityCompactCompute;
};

struct AppGfxDemoRendererData {
    F32 animationSeconds;
    AppGfxDemoObject* objects;
    U32 objectCount;
    AppGfxGpuObject* gpuObjects;
    U32 gpuObjectCount;
    AppGfxGpuCullSource* gpuCullSources;
    U32 gpuCullSourceCount;
    AppGfxMaterial* materialSources;
    U32 materialSourceCount;
    U32* visibilitySourceIndices;
    U32 visibilitySourceIndexCount;
    AppGfxVisibilityBin* visibilityBins;
    U32 visibilityBinCount;
    AppGfxVisibilityGroup* visibilityGroups;
    U32 visibilityGroupCount;
    U32 dataVersion;
    U32 materialCount;
};

struct AppGfxDemoGpuResources {
    GfxBuffer triangleVertexBuffer;
    GfxResourceId triangleVertexBufferId;
    GfxBuffer triangleIndexBuffer;
    GfxBuffer objectBuffer;
    GfxResourceId objectBufferId;
    GfxBuffer materialSourceBuffer;
    GfxResourceId materialSourceBufferId;
    GfxBuffer materialBuffer;
    GfxResourceId materialBufferId;
    GfxBuffer visibilitySourceIndexBuffer;
    GfxResourceId visibilitySourceIndexBufferId;
    GfxBuffer cullSourceBuffer;
    GfxResourceId cullSourceBufferId;
    GfxBuffer cullObjectBuffer;
    GfxResourceId cullObjectBufferId;
    GfxBuffer visibilityBinBuffer;
    GfxResourceId visibilityBinBufferId;
    GfxBuffer visibilityGroupBuffer;
    GfxResourceId visibilityGroupBufferId;
    GfxBuffer visibilityGroupCountBuffer;
    GfxResourceId visibilityGroupCountBufferId;
    GfxBuffer visibilityGroupOffsetBuffer;
    GfxResourceId visibilityGroupOffsetBufferId;
    GfxBuffer visibleIndexBuffer;
    GfxResourceId visibleIndexBufferId;
    GfxBuffer indirectArgsBuffer;
    GfxResourceId indirectArgsBufferId;
    GfxTexture texture;
    GfxTexture offscreenColor;
    GfxTexture depth;
    GfxSampler sampler;
    GfxResourceId textureId;
    GfxResourceId samplerId;
    U32 targetWidth;
    U32 targetHeight;
};

struct AppGfxDemoUploadState {
    ArtifactKey textureDecodeArtifactKey;
    ContentHash decodedTextureHash;
    U64 textureGeneration;
    U64 textureFailedGeneration;
    B32 materialSourceUploaded;
    B32 materialSourceDirty;
    B32 textureUploaded;
    B32 materialsReady;
    B32 materialDirty;
    B32 objectUploaded;
    B32 objectDirty;
    B32 cullSourceUploaded;
    B32 cullSourceDirty;
    B32 visibilitySourceUploaded;
    B32 visibilitySourceDirty;
    B32 visibilityBinUploaded;
    B32 visibilityBinDirty;
    B32 visibilityGroupUploaded;
    B32 visibilityGroupDirty;
};

struct AppGfxDemoRuntimeState {
    B32 initialized;
    B32 geometryCreated;
    B32 geometryUploaded;
    B32 ready;
    U32 loadLogMask;
};

struct AppGfxDemoTextState {
    TextContext* context;
    TextFont font;
    FileHandle fontFile;
    FileHandle vertexShader;
    FileHandle fragmentShader;
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
    U64 loadedFontGeneration;
    U64 failedFontGeneration;
    B32 gpuResourcesCreated;
};

struct AppGfxDemoState {
    AppGfxDemoShaderFiles shaders;
    AppGfxDemoPipelineState pipelines;
    AppGfxDemoRendererData renderer;
    AppGfxDemoGpuResources gpu;
    AppGfxDemoUploadState upload;
    AppGfxDemoRuntimeState runtime;
    AppGfxDemoTextState text;
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
    AppGfxDemoState gfxDemo;
};

struct APP_Context {
    AppHost* host;
    HOT_StateStore* store;
    AppCoreState* core;
};
