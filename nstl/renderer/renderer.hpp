//
// Created by André Leite on 03/11/2025.
//

#pragma once

// ////////////////////////
// Renderer System

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
void renderer_draw_color(Renderer* renderer, OS_WindowHandle window, Vec4F32 color);
void renderer_compile_shaders(Renderer* renderer, Arena* arena, JobSystem* jobSystem,
                              const ShaderCompileRequest* requests, U32 requestCount);
B32 renderer_imgui_init(Renderer* renderer, OS_WindowHandle window);
void renderer_imgui_shutdown(Renderer* renderer);
void renderer_imgui_process_events(Renderer* renderer, const OS_GraphicsEvent* events, U32 eventCount);
void renderer_imgui_begin_frame(Renderer* renderer, F32 deltaSeconds);
void renderer_imgui_end_frame(Renderer* renderer);
void renderer_imgui_set_window_size(Renderer* renderer, U32 width, U32 height);
