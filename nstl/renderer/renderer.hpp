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

struct MeshSurface {
    U32 startIndex;
    U32 count;
};

struct MeshAssetData {
    MeshData data;
    MeshSurface* surfaces;
    U32 surfaceCount;
};

struct SceneData {
    Mat4x4F32 view;
    Mat4x4F32 proj;
    Mat4x4F32 viewproj;
    Vec4F32 ambientColor;
    Vec4F32 sunDirection;
    Vec4F32 sunColor;
};

struct GPUMesh;
typedef GPUMesh* MeshHandle;
static const MeshHandle MESH_HANDLE_INVALID = 0;

struct RenderObject {
    MeshHandle mesh;
    Mat4x4F32 transform;
    F32 alpha;
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

U32 mesh_load_from_file(Arena* arena, const char* path, MeshAssetData** outMeshes);

