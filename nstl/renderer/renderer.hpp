//
// Created by André Leite on 03/11/2025.
//

#pragma once

// ////////////////////////
// Mesh Data Types

struct Vertex {
    Vec3F32 position;
    F32 uvX;
    Vec3F32 normal;
    F32 uvY;
    Vec4F32 color;
};

struct MeshData {
    U32* indices;
    U32 indexCount;
    Vertex* vertices;
    U32 vertexCount;
    StringU8 name;
};

// ////////////////////////
// Bounding Volume

struct Bounds {
    Vec3F32 origin;
    Vec3F32 extents;
    F32 sphereRadius;
};

// ////////////////////////
// Material Data

static const U32 MATERIAL_NO_TEXTURE = 0xFFFFFFFF;

typedef enum AlphaMode {
    AlphaMode_Opaque,
    AlphaMode_Mask,
    AlphaMode_Blend,
} AlphaMode;

struct MaterialData {
    Vec4F32 colorFactor;
    F32 metallicFactor;
    F32 roughnessFactor;
    U32 colorTextureIndex;
    U32 metalRoughTextureIndex;
    U32 samplerIndex;
    AlphaMode alphaMode;
    F32 alphaCutoff;
};

// ////////////////////////
// Mesh Surface (per-primitive)

struct MeshSurface {
    U32 startIndex;
    U32 count;
    U32 materialIndex;
    Bounds bounds;
};

struct MeshAssetData {
    MeshData data;
    MeshSurface* surfaces;
    U32 surfaceCount;
};

// ////////////////////////
// Scene Graph Types

struct SceneNode {
    StringU8 name;
    Mat4x4F32 localTransform;
    Mat4x4F32 worldTransform;
    S32 parentIndex;
    S32 meshIndex;
    U32* childIndices;
    U32 childCount;
};

struct LoadedImage {
    U8* pixels;
    U32 width;
    U32 height;
    U32 channels;
};

B32 image_load_from_memory(const U8* data, U64 size, LoadedImage* out);
B32 image_load_from_file(const char* path, LoadedImage* out);
void image_free(LoadedImage* img);

struct LoadedScene {
    MeshAssetData* meshes;
    U32 meshCount;
    
    SceneNode* nodes;
    U32 nodeCount;
    
    MaterialData* materials;
    U32 materialCount;
    
    LoadedImage* images;
    U32 imageCount;
    
    U32* topNodeIndices;
    U32 topNodeCount;
};

struct SceneData {
    Mat4x4F32 view;
    Mat4x4F32 proj;
    Mat4x4F32 viewproj;
    Mat4x4F32 lightSpaceMatrix;
    Vec4F32 ambientColor;
    Vec4F32 sunDirection;
    Vec4F32 sunColor;
};

// ////////////////////////
// GPU Handle Types

struct MeshHandle {
    U32 slot;
    U32 generation;
};

struct TextureHandle {
    U32 slot;
    U32 generation;
};

struct MaterialHandle {
    U32 slot;
    U32 generation;
};

static const MeshHandle MESH_HANDLE_INVALID = {0u, 0u};
static const TextureHandle TEXTURE_HANDLE_INVALID = {0u, 0u};
static const MaterialHandle MATERIAL_HANDLE_INVALID = {0u, 0u};

#define MESH_HANDLE_IS_INVALID(handle) (((handle).slot == MESH_HANDLE_INVALID.slot) && \
                                        ((handle).generation == MESH_HANDLE_INVALID.generation))
#define MESH_HANDLE_IS_VALID(handle) (!(MESH_HANDLE_IS_INVALID(handle)))

#define TEXTURE_HANDLE_IS_INVALID(handle) (((handle).slot == TEXTURE_HANDLE_INVALID.slot) && \
                                           ((handle).generation == TEXTURE_HANDLE_INVALID.generation))
#define TEXTURE_HANDLE_IS_VALID(handle) (!(TEXTURE_HANDLE_IS_INVALID(handle)))

#define MATERIAL_HANDLE_IS_INVALID(handle) (((handle).slot == MATERIAL_HANDLE_INVALID.slot) && \
                                            ((handle).generation == MATERIAL_HANDLE_INVALID.generation))
#define MATERIAL_HANDLE_IS_VALID(handle) (!(MATERIAL_HANDLE_IS_INVALID(handle)))

struct RenderObject {
    MeshHandle mesh;
    MaterialHandle material;
    Mat4x4F32 transform;
    Vec4F32 color;
    U32 firstIndex;
    U32 indexCount;
};

struct ShaderHandle {
    U32 slot;
    U32 generation;
};

static const ShaderHandle SHADER_HANDLE_INVALID = {0u, 0u};

#define SHADER_HANDLE_IS_INVALID(handle) (((handle).slot == SHADER_HANDLE_INVALID.slot) && \
                                          ((handle).generation == SHADER_HANDLE_INVALID.generation))
#define SHADER_HANDLE_IS_VALID(handle) (!(SHADER_HANDLE_IS_INVALID(handle)))

struct ShaderCompileRequest {
    StringU8 shaderPath;
    ShaderHandle* outHandle;
};

struct ShaderCompileResult {
    void* module;
    StringU8 path;
    ShaderHandle handle;
    B32 valid;
};

typedef B32 (*RendererCompileShaderFunc)(void* backendData, Arena* arena, StringU8 shaderPath,
                                         ShaderCompileResult* outResult);
typedef void (*RendererMergeShaderResultsFunc)(void* backendData, Arena* arena, const ShaderCompileResult* results,
                                               U32 resultCount, const ShaderCompileRequest* requests, U32 requestCount);

struct Renderer {
    void* backendData;
    RendererCompileShaderFunc compileShader;
    RendererMergeShaderResultsFunc mergeShaderResults;
    B32 frameInProgress;
    OS_WindowHandle activeWindow;
    const SceneData* activeScene;
};

static const U32 RENDERER_API_VERSION = 2u;

struct RendererCreateDesc {
    U32 structSize;
    U32 apiVersion;
    const void* next;
    Arena* arena;
};

struct RendererFrameBeginDesc {
    U32 structSize;
    U32 apiVersion;
    const void* next;
    OS_WindowHandle window;
    const SceneData* scene;
};

struct RendererSubmitDesc {
    U32 structSize;
    U32 apiVersion;
    const void* next;
    const RenderObject* objects;
    U32 objectCount;
};

struct RendererEndFrameDesc {
    U32 structSize;
    U32 apiVersion;
    const void* next;
};

struct RendererResizeDesc {
    U32 structSize;
    U32 apiVersion;
    const void* next;
    U32 width;
    U32 height;
};

struct RendererRadiance2DDesc {
    U32 structSize;
    U32 apiVersion;
    const void* next;
    TextureHandle emissiveTexture;
    TextureHandle occluderTexture;
    U32 gridWidth;
    U32 gridHeight;
    U32 cascadeCount;
    U32 raysPerProbeBase;
    U32 maxSteps;
    F32 intensity;
    F32 exposure;
};

inline RendererCreateDesc renderer_create_desc(Arena* arena) {
    RendererCreateDesc desc = {};
    desc.structSize = sizeof(RendererCreateDesc);
    desc.apiVersion = RENDERER_API_VERSION;
    desc.next = 0;
    desc.arena = arena;
    return desc;
}

inline RendererFrameBeginDesc renderer_frame_begin_desc(OS_WindowHandle window, const SceneData* scene) {
    RendererFrameBeginDesc desc = {};
    desc.structSize = sizeof(RendererFrameBeginDesc);
    desc.apiVersion = RENDERER_API_VERSION;
    desc.next = 0;
    desc.window = window;
    desc.scene = scene;
    return desc;
}

inline RendererSubmitDesc renderer_submit_desc(const RenderObject* objects, U32 objectCount) {
    RendererSubmitDesc desc = {};
    desc.structSize = sizeof(RendererSubmitDesc);
    desc.apiVersion = RENDERER_API_VERSION;
    desc.next = 0;
    desc.objects = objects;
    desc.objectCount = objectCount;
    return desc;
}

inline RendererEndFrameDesc renderer_end_frame_desc(void) {
    RendererEndFrameDesc desc = {};
    desc.structSize = sizeof(RendererEndFrameDesc);
    desc.apiVersion = RENDERER_API_VERSION;
    desc.next = 0;
    return desc;
}

inline RendererResizeDesc renderer_resize_desc(U32 width, U32 height) {
    RendererResizeDesc desc = {};
    desc.structSize = sizeof(RendererResizeDesc);
    desc.apiVersion = RENDERER_API_VERSION;
    desc.next = 0;
    desc.width = width;
    desc.height = height;
    return desc;
}

inline RendererRadiance2DDesc renderer_radiance_2d_desc(TextureHandle emissiveTexture,
                                                         TextureHandle occluderTexture,
                                                         U32 gridWidth,
                                                         U32 gridHeight,
                                                         U32 cascadeCount,
                                                         U32 raysPerProbeBase,
                                                         U32 maxSteps,
                                                         F32 intensity,
                                                         F32 exposure) {
    RendererRadiance2DDesc desc = {};
    desc.structSize = sizeof(RendererRadiance2DDesc);
    desc.apiVersion = RENDERER_API_VERSION;
    desc.next = 0;
    desc.emissiveTexture = emissiveTexture;
    desc.occluderTexture = occluderTexture;
    desc.gridWidth = gridWidth;
    desc.gridHeight = gridHeight;
    desc.cascadeCount = cascadeCount;
    desc.raysPerProbeBase = raysPerProbeBase;
    desc.maxSteps = maxSteps;
    desc.intensity = intensity;
    desc.exposure = exposure;
    return desc;
}

B32 renderer_create(const RendererCreateDesc* createDesc, Renderer* outRenderer);
B32 renderer_begin_frame(Renderer* renderer, const RendererFrameBeginDesc* frameBeginDesc);
void renderer_submit(Renderer* renderer, const RendererSubmitDesc* submitDesc);
void renderer_submit_radiance_2d(Renderer* renderer, const RendererRadiance2DDesc* radianceDesc);
void renderer_end_frame(Renderer* renderer, const RendererEndFrameDesc* endFrameDesc);
void renderer_resize(Renderer* renderer, const RendererResizeDesc* resizeDesc);

void renderer_shutdown(Renderer* renderer);
B32 renderer_imgui_init(Renderer* renderer, OS_WindowHandle window);
void renderer_imgui_shutdown(Renderer* renderer);
void renderer_imgui_process_events(Renderer* renderer, const OS_GraphicsEvent* events, U32 eventCount);
void renderer_imgui_begin_frame(Renderer* renderer, F32 deltaSeconds);
void renderer_imgui_end_frame(Renderer* renderer);
void renderer_imgui_set_window_size(Renderer* renderer, U32 width, U32 height);

MeshHandle renderer_upload_mesh(Renderer* renderer, const MeshAssetData* meshData);
void renderer_destroy_mesh(Renderer* renderer, MeshHandle mesh);

B32 scene_load_from_file(Arena* arena, const char* path, LoadedScene* outScene);

// ////////////////////////
// GPU Scene Data

struct GPUSceneData {
    TextureHandle* textures;
    U32 textureCount;
    
    MaterialHandle* materials;
    U32 materialCount;
    
    MeshHandle* meshes;
    U32 meshCount;
};

TextureHandle renderer_upload_texture(Renderer* renderer, const LoadedImage* image);
void renderer_destroy_texture(Renderer* renderer, TextureHandle texture);
B32 renderer_update_texture(Renderer* renderer, TextureHandle texture, const LoadedImage* image);

MaterialHandle renderer_upload_material(Renderer* renderer, const MaterialData* material,
                                        TextureHandle colorTexture, TextureHandle metalRoughTexture);
void renderer_destroy_material(Renderer* renderer, MaterialHandle material);

B32 renderer_upload_scene(Renderer* renderer, Arena* arena, const LoadedScene* scene, GPUSceneData* outGPU);
void renderer_destroy_scene(Renderer* renderer, GPUSceneData* gpu);
