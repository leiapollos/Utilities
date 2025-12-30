//
// Created by André Leite on 02/11/2025.
//

#pragma once

#include "nstl/base/base_include.hpp"
#include "nstl/os/core/os_core.hpp"
#include "nstl/os/graphics/os_graphics.hpp"
#include "nstl/renderer/renderer.hpp"

#define APP_INTERFACE_VERSION 4u

#define PLATFORM_OS_FUNCTIONS(X) \
    X(OS_graphics_init) \
    X(OS_graphics_shutdown) \
    X(OS_graphics_pump_events) \
    X(OS_graphics_poll_events) \
    X(OS_window_create) \
    X(OS_window_destroy) \
    X(OS_window_is_open) \
    X(OS_window_get_surface_info) \
    X(OS_reserve) \
    X(OS_commit) \
    X(OS_decommit) \
    X(OS_release) \
    X(OS_set_environment_variable) \
    X(OS_get_environment_variable) \
    X(OS_execute) \
    X(OS_library_open) \
    X(OS_library_close) \
    X(OS_library_load_symbol) \
    X(OS_library_last_error) \
    X(OS_get_time_microseconds) \
    X(OS_get_time_nanoseconds) \
    X(OS_sleep_milliseconds) \
    X(OS_thread_create) \
    X(OS_thread_join) \
    X(OS_thread_detach) \
    X(OS_thread_yield) \
    X(OS_cpu_pause) \
    X(OS_get_thread_id_u32) \
    X(OS_mutex_create) \
    X(OS_mutex_destroy) \
    X(OS_mutex_lock) \
    X(OS_mutex_unlock) \
    X(OS_condition_variable_create) \
    X(OS_condition_variable_destroy) \
    X(OS_condition_variable_wait) \
    X(OS_condition_variable_signal) \
    X(OS_condition_variable_broadcast) \
    X(OS_barrier_create) \
    X(OS_barrier_destroy) \
    X(OS_barrier_wait) \
    X(OS_get_system_info) \
    X(OS_abort)

struct PlatformOSApi {
#define PLATFORM_DECLARE_OS_FN(name) decltype(&name) name;
    PLATFORM_OS_FUNCTIONS(PLATFORM_DECLARE_OS_FN)
#undef PLATFORM_DECLARE_OS_FN
};

#define PLATFORM_OS_FN(platform, name) ((platform)->os.name)
#define PLATFORM_OS_CALL(platform, name, ...) ((platform)->os.name(__VA_ARGS__))

#define PLATFORM_RENDERER_FUNCTIONS(X) \
    X(renderer_compile_shaders) \
    X(renderer_draw) \
    X(renderer_imgui_init) \
    X(renderer_imgui_shutdown) \
    X(renderer_imgui_process_events) \
    X(renderer_imgui_begin_frame) \
    X(renderer_imgui_end_frame) \
    X(renderer_imgui_set_window_size) \
    X(renderer_on_window_resized) \
    X(renderer_upload_mesh) \
    X(renderer_destroy_mesh)

struct PlatformRendererApi {
#define PLATFORM_DECLARE_RENDERER_FN(name) decltype(&name) name;
    PLATFORM_RENDERER_FUNCTIONS(PLATFORM_DECLARE_RENDERER_FN)
#undef PLATFORM_DECLARE_RENDERER_FN
};

#define PLATFORM_RENDERER_FN(platform, name) ((platform)->renderer.name)
#define PLATFORM_RENDERER_CALL(platform, name, ...) ((platform)->renderer.name(__VA_ARGS__))

struct AppHostContext {
    B32 shouldQuit;
    U32 reloadCount;
    void* userData;
    U32 logicalCoreCount;
    Renderer* renderer;
    Arena* frameArena;
};

struct AppInput {
    F32 deltaSeconds;
    const OS_GraphicsEvent* events;
    U32 eventCount;
};

struct AppWindowDesc {
    U32 width;
    U32 height;
    const char* title;
};

struct AppPlatform {
    void* userData;
    PlatformOSApi os;
    PlatformRendererApi renderer;
};

struct AppMemory {
    B32 isInitialized;
    void* permanentStorage;
    U64 permanentStorageSize;
    void* transientStorage;
    U64 transientStorageSize;
    Arena* programArena;
};

struct AppModuleExports {
    U32 interfaceVersion;
    U64 requiredPermanentMemory;
    U64 requiredTransientMemory;
    U64 requiredProgramArenaSize;
    B32 (*initialize)(AppPlatform* platform, AppMemory* memory, AppHostContext* host);
    void (*reload)(AppPlatform* platform, AppMemory* memory, AppHostContext* host);
    void (*update)(AppPlatform* platform, AppMemory* memory, AppHostContext* host, const AppInput* input,
                   F32 deltaSeconds);
    void (*shutdown)(AppPlatform* platform, AppMemory* memory, AppHostContext* host);
};

#if defined(COMPILER_CLANG) || defined(COMPILER_GCC)
#define APP_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define APP_MODULE_EXPORT extern "C"
#endif

APP_MODULE_EXPORT B32 app_get_entry_points(AppModuleExports* outExports);
