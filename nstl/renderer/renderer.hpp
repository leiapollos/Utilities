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

struct MaterialData {
    Vec4F32 colorFactor;
    F32 metallicFactor;
    F32 roughnessFactor;
    U32 colorTextureIndex;
    U32 metalRoughTextureIndex;
    U32 samplerIndex;
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
    Vec4F32 ambientColor;
    Vec4F32 sunDirection;
    Vec4F32 sunColor;
};

// ////////////////////////
// GPU Handle Types

typedef U64 MeshHandle;
static const MeshHandle MESH_HANDLE_INVALID = 0;

typedef U64 TextureHandle;
static const TextureHandle TEXTURE_HANDLE_INVALID = 0;

typedef U64 MaterialHandle;
static const MaterialHandle MATERIAL_HANDLE_INVALID = 0;

struct RenderObject {
    MeshHandle mesh;
    MaterialHandle material;
    Mat4x4F32 transform;
    Vec4F32 color;
    U32 firstIndex;
    U32 indexCount;
};

typedef U64 ShaderHandle;
static const ShaderHandle SHADER_HANDLE_INVALID = 0ull;

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
};

B32 renderer_init(Arena* arena, Renderer* renderer);
void renderer_shutdown(Renderer* renderer);
void renderer_draw(Renderer* renderer, OS_WindowHandle window, const SceneData* scene,
                   const RenderObject* objects, U32 objectCount);
void renderer_compile_shaders(Renderer* renderer, Arena* arena, JobSystem* jobSystem,
                              const ShaderCompileRequest* requests, U32 requestCount);
B32 renderer_imgui_init(Renderer* renderer, OS_WindowHandle window);
void renderer_imgui_shutdown(Renderer* renderer);
void renderer_imgui_process_events(Renderer* renderer, const OS_GraphicsEvent* events, U32 eventCount);
void renderer_imgui_begin_frame(Renderer* renderer, F32 deltaSeconds);
void renderer_imgui_end_frame(Renderer* renderer);
void renderer_imgui_set_window_size(Renderer* renderer, U32 width, U32 height);
void renderer_on_window_resized(Renderer* renderer, U32 width, U32 height);

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

MaterialHandle renderer_upload_material(Renderer* renderer, const MaterialData* material,
                                        TextureHandle colorTexture, TextureHandle metalRoughTexture);
void renderer_destroy_material(Renderer* renderer, MaterialHandle material);

B32 renderer_upload_scene(Renderer* renderer, Arena* arena, const LoadedScene* scene, GPUSceneData* outGPU);
void renderer_destroy_scene(Renderer* renderer, GPUSceneData* gpu);

