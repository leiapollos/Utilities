//
// Created by André Leite on 02/11/2025.
//

#include "nstl/base/base_include.hpp"
#include "nstl/os/os_include.hpp"
#include "app_interface.hpp"

#define RENDERER_BACKEND_VULKAN
#include "nstl/renderer/renderer_include.hpp"

#include "nstl/base/base_include.cpp"
#include "nstl/os/os_include.cpp"
#include "nstl/renderer/renderer_include.cpp"


#define APP_MODULE_SOURCE_RELATIVE "hot/utilities_app.dylib"
#define APP_SOURCE_PATH "app.cpp"
#define HOT_MODULE_NAME_PATTERN "hot/utilities_app_loaded_%llu.dylib"
#define HOT_MODULE_HISTORY_MAX 32

typedef B32 (*AppGetEntryPointsProc)(AppModuleExports* outExports);

struct LoadedModule {
    OS_SharedLibrary library;
    AppModuleExports exports;
    B32 isValid;
};

struct ProgramMemory {
    void* permanentBase;
    U64 permanentSize;
    void* transientBase;
    U64 transientSize;
};

#if !defined(UTILITIES_ICD_FILENAME)
#define UTILITIES_ICD_FILENAME ""
#endif

static void host_apply_environment_defaults(void) {
#if defined(PLATFORM_OS_MACOS)
    StringU8 envName = str8("VK_ICD_FILENAMES");
    StringU8 icdFilename = str8(UTILITIES_ICD_FILENAME);
    if (!icdFilename.data || icdFilename.size == 0) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    StringU8 currentValue = OS_get_environment_variable(scratch.arena, envName);
    if (currentValue.size != 0) {
        return;
    }

    StringU8 execDir = OS_get_executable_directory(scratch.arena);
    if (execDir.size == 0) {
        return;
    }

    StringU8 icdPath = str8_concat(scratch.arena, execDir, str8("/"), icdFilename);
    if (icdPath.size == 0) {
        return;
    }

    OS_set_environment_variable(envName, icdPath);
#endif
}

static PlatformOSApi host_build_os_api(void) {
    PlatformOSApi api = {};
#define HOST_ASSIGN_OS_FN(name) api.name = name;
    PLATFORM_OS_FUNCTIONS(HOST_ASSIGN_OS_FN)
#undef HOST_ASSIGN_OS_FN
    return api;
}

static PlatformRendererApi host_build_renderer_api(void) {
    PlatformRendererApi api = {};
#define HOST_ASSIGN_RENDERER_FN(name) api.name = name;
    PLATFORM_RENDERER_FUNCTIONS(HOST_ASSIGN_RENDERER_FN)
#undef HOST_ASSIGN_RENDERER_FN
    return api;
}

static B32 renderer_imgui_init_stub(Renderer* renderer, OS_WindowHandle window) {
    (void) renderer;
    (void) window;
    return 1;
}

static void renderer_imgui_shutdown_stub(Renderer* renderer) {
    (void) renderer;
}

static void renderer_imgui_process_events_stub(Renderer* renderer, const OS_GraphicsEvent* events, U32 eventCount) {
    (void) renderer;
    (void) events;
    (void) eventCount;
}

static void renderer_imgui_begin_frame_stub(Renderer* renderer, F32 deltaSeconds) {
    (void) renderer;
    (void) deltaSeconds;
}

static void renderer_imgui_end_frame_stub(Renderer* renderer) {
    (void) renderer;
}

static void renderer_imgui_set_window_size_stub(Renderer* renderer, U32 width, U32 height) {
    (void) renderer;
    (void) width;
    (void) height;
}

static PlatformRendererApi host_build_renderer_api_imgui_stub(void) {
    PlatformRendererApi api = host_build_renderer_api();
    api.renderer_imgui_init = renderer_imgui_init_stub;
    api.renderer_imgui_shutdown = renderer_imgui_shutdown_stub;
    api.renderer_imgui_process_events = renderer_imgui_process_events_stub;
    api.renderer_imgui_begin_frame = renderer_imgui_begin_frame_stub;
    api.renderer_imgui_end_frame = renderer_imgui_end_frame_stub;
    api.renderer_imgui_set_window_size = renderer_imgui_set_window_size_stub;
    return api;
}

static B32 host_should_enable_imgui(void) {
    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return 1;
    }
    DEFER_REF(temp_end(&scratch));

    StringU8 value = OS_get_environment_variable(scratch.arena, str8("UTILITIES_IMGUI"));
    if (value.size == 0) {
        return 1;
    }

    U8 first = value.data[0];
    if (first == '0' || first == 'f' || first == 'F' || first == 'n' || first == 'N') {
        return 0;
    }
    return 1;
}

struct HostState {
    LoadedModule module;
    AppMemory memory;
    ProgramMemory storage;
    Arena* programArena;
    Arena* frameArena;
    Arena* pathArena;
    AppHostContext hostContext;
    AppPlatform platformAPI;
    PlatformRendererApi rendererApiReal;
    PlatformRendererApi rendererApiImguiStub;
    B32 imguiEnabled;
    AppInput input;
    Renderer renderer;
    B32 graphicsInitialized;
    U64 moduleTimestamp;
    U64 sourceTimestamp;
    U64 moduleGeneration;
    StringU8 currentModulePath;
    StringU8 moduleBasePath;
    OS_SharedLibrary retiredModules[HOT_MODULE_HISTORY_MAX];
    StringU8 retiredModulePaths[HOT_MODULE_HISTORY_MAX];
    U32 retiredModuleCount;
    B32 buildFailed;
};

static void host_set_imgui_enabled(HostState* state, B32 enabled) {
    ASSERT_ALWAYS(state != 0);
    PlatformRendererApi* target = enabled ? &state->rendererApiReal : &state->rendererApiImguiStub;
    state->platformAPI.renderer = *target;
    state->imguiEnabled = enabled ? 1 : 0;
}

static B32 host_allocate_memory(HostState* state);
static void host_release_memory(HostState* state);
static B32 host_ensure_graphics_initialized(HostState* state);
static StringU8 host_build_module_path(HostState* state, StringU8 relativePath, Arena* arena);
static void host_record_retired_module(HostState* state, OS_SharedLibrary library, StringU8 path);
static B32 host_copy_file(StringU8 srcPath, StringU8 dstPath);
static StringU8 host_export_status_string(B32 passed);
static B32 host_validate_module_exports(HostState* state, const AppModuleExports* exports, StringU8 modulePath);
static B32 host_load_module(HostState* state, B32 isReload);
static void host_unload_module(HostState* state, B32 callShutdown, B32 retainHandle);
static void host_try_reload_module(HostState* state);
static void host_update_input(HostState* state, F32 deltaSeconds);
static void host_cleanup_retired_modules(HostState* state);

static B32 host_init(HostState* state) {
    ASSERT_ALWAYS(state != 0);

    MEMSET(state, 0, sizeof(*state));
    host_apply_environment_defaults();

    B32 memoryOk = host_allocate_memory(state);
    ASSERT_ALWAYS(memoryOk);
    if (!memoryOk) {
        return 0;
    }

    LOG_INFO("host", "Allocated program memory (perm={} bytes, trans={} bytes)",
             state->storage.permanentSize, state->storage.transientSize);

    state->moduleBasePath = STR8_NIL;
    state->currentModulePath = STR8_NIL;
    for (U32 i = 0; i < HOT_MODULE_HISTORY_MAX; ++i) {
        state->retiredModulePaths[i] = STR8_NIL;
        state->retiredModules[i].handle = 0;
    }

    state->moduleGeneration = 0;
    OS_FileInfo initialSourceInfo = OS_get_file_info(APP_SOURCE_PATH);
    state->sourceTimestamp = initialSourceInfo.exists ? initialSourceInfo.lastWriteTimestampNs : 0;
    state->buildFailed = 0;

    if (!host_should_enable_imgui()) {
        host_set_imgui_enabled(state, 0);
    }

    B32 graphicsOk = host_ensure_graphics_initialized(state);
    ASSERT_ALWAYS(graphicsOk);
    if (!graphicsOk) {
        LOG_ERROR("host", "Failed to initialize graphics before loading module");
        return 0;
    }

    B32 loadOk = host_load_module(state, 0);
    ASSERT_ALWAYS(loadOk);
    if (!loadOk) {
        LOG_ERROR("host", "Initial module load failed");
        return 0;
    }

    return 1;
}

static void host_window_resize_callback(OS_WindowHandle window, U32 width, U32 height, void* userData) {
    HostState* state = (HostState*) userData;
    if (!state || !state->module.isValid || !state->module.exports.update) {
        return;
    }

    AppInput input = {};
    input.deltaSeconds = 0.016f;
    
    OS_GraphicsEvent resizeEvent = OS_GraphicsEvent::window_resized(window, width, height);
    
    input.events = &resizeEvent;
    input.eventCount = 1;

    state->module.exports.update(&state->platformAPI, &state->memory, &state->hostContext, &input, input.deltaSeconds);
}

static void host_update(HostState* state, F32 deltaSeconds) {
    ASSERT_ALWAYS(state != 0);
    ASSERT_ALWAYS(state->frameArena != 0);

    arena_pop_to(state->frameArena, 0);

    host_try_reload_module(state);
    host_update_input(state, deltaSeconds);

    ASSERT_ALWAYS(state->module.isValid);
    ASSERT_ALWAYS(state->module.exports.update != 0);
    ASSERT_ALWAYS(state->memory.isInitialized);

    state->module.exports.update(&state->platformAPI, &state->memory, &state->hostContext, &state->input,
                                 deltaSeconds);
}

static void host_shutdown(HostState* state) {
    ASSERT_ALWAYS(state != 0);

    host_unload_module(state, 1, 0);

    if (state->graphicsInitialized) {
        renderer_shutdown(&state->renderer);
        state->hostContext.renderer = 0;
        OS_graphics_shutdown();
        state->graphicsInitialized = 0;
    }

    host_cleanup_retired_modules(state);
    host_release_memory(state);
}

// ////////////////////////
// Host Memory

static void host_release_memory(HostState* state) {
    if (state->pathArena) {
        arena_release(state->pathArena);
        state->pathArena = 0;
    }

    arena_release(state->frameArena);
    state->frameArena = 0;

    if (state->programArena) {
        arena_release(state->programArena);
        state->programArena = 0;
    }

    if (state->storage.permanentBase) {
        OS_release(state->storage.permanentBase, state->storage.permanentSize);
        state->storage.permanentBase = 0;
    }

    if (state->storage.transientBase) {
        OS_release(state->storage.transientBase, state->storage.transientSize);
        state->storage.transientBase = 0;
    }

    state->moduleBasePath = STR8_NIL;
    state->currentModulePath = STR8_NIL;
    for (U32 index = 0; index < HOT_MODULE_HISTORY_MAX; ++index) {
        state->retiredModulePaths[index] = STR8_NIL;
        state->retiredModules[index].handle = 0;
    }
    state->retiredModuleCount = 0;
    state->hostContext.frameArena = 0;
}

static B32 host_allocate_memory(HostState* state) {
    state->storage.permanentSize = MB(64);
    state->storage.transientSize = MB(16);

    state->storage.permanentBase = OS_reserve(state->storage.permanentSize);
    if (!state->storage.permanentBase) {
        LOG_ERROR("host", "Failed to reserve permanent storage ({} bytes)", state->storage.permanentSize);
        return 0;
    }
    if (!OS_commit(state->storage.permanentBase, state->storage.permanentSize)) {
        LOG_ERROR("host", "Failed to commit permanent storage");
        host_release_memory(state);
        return 0;
    }

    state->storage.transientBase = OS_reserve(state->storage.transientSize);
    if (!state->storage.transientBase) {
        LOG_ERROR("host", "Failed to reserve transient storage ({} bytes)", state->storage.transientSize);
        host_release_memory(state);
        return 0;
    }
    if (!OS_commit(state->storage.transientBase, state->storage.transientSize)) {
        LOG_ERROR("host", "Failed to commit transient storage");
        host_release_memory(state);
        return 0;
    }

    MEMSET(state->storage.permanentBase, 0, state->storage.permanentSize);
    MEMSET(state->storage.transientBase, 0, state->storage.transientSize);

    state->programArena = arena_alloc(
        .arenaSize = MB(256),
        .committedSize = MB(256),
        .flags = ArenaFlags_DoChain
    );
    if (!state->programArena) {
        LOG_ERROR("host", "Failed to allocate program arena ({} bytes)", MB(256));
        host_release_memory(state);
        return 0;
    }

    state->frameArena = arena_alloc(
        .arenaSize = MB(16),
        .committedSize = MB(1),
        .flags = ArenaFlags_DoChain
    );
    if (!state->frameArena) {
        LOG_ERROR("host", "Failed to allocate frame arena");
        host_release_memory(state);
        return 0;
    }

    state->pathArena = arena_alloc(
        .arenaSize = KB(64),
        .committedSize = KB(64),
        .flags = ArenaFlags_DoChain
    );
    if (!state->pathArena) {
        LOG_ERROR("host", "Failed to allocate path arena");
        host_release_memory(state);
        return 0;
    }

    state->memory.isInitialized = 0;
    state->memory.permanentStorage = state->storage.permanentBase;
    state->memory.permanentStorageSize = state->storage.permanentSize;
    state->memory.transientStorage = state->storage.transientBase;
    state->memory.transientStorageSize = state->storage.transientSize;
    state->memory.programArena = state->programArena;

    MEMSET(&state->hostContext, 0, sizeof(state->hostContext));
    state->hostContext.userData = state;
    OS_SystemInfo* sysInfo = OS_get_system_info();
    state->hostContext.logicalCoreCount = sysInfo ? sysInfo->logicalCores : 1u;
    state->hostContext.frameArena = state->frameArena;

    state->platformAPI.userData = state;
    state->platformAPI.os = host_build_os_api();
    state->rendererApiReal = host_build_renderer_api();
    state->rendererApiImguiStub = host_build_renderer_api_imgui_stub();
    host_set_imgui_enabled(state, 1);

    MEMSET(&state->input, 0, sizeof(state->input));
    state->input.eventCount = 0;
    state->input.deltaSeconds = 0.0f;

    state->graphicsInitialized = 0;

    OS_set_window_resize_callback(host_window_resize_callback, state);

    return 1;
}

// ////////////////////////
// Host Module Management

static StringU8 host_build_module_path(HostState* state, StringU8 relativePath, Arena* arena) {
    if (!arena || !relativePath.data) {
        return STR8_NIL;
    }

    if (!state->moduleBasePath.data || state->moduleBasePath.size == 0) {
        StringU8 execDirTemp = OS_get_executable_directory(arena);
        if (execDirTemp.size > 0 && state->pathArena) {
            state->moduleBasePath = str8_cpy(state->pathArena, execDirTemp);
        } else {
            state->moduleBasePath = STR8_NIL;
        }
    }

    if (!state->moduleBasePath.data || state->moduleBasePath.size == 0) {
        return relativePath;
    }

    return str8_concat(arena, state->moduleBasePath, str8("/"), relativePath);
}

static B32 host_copy_file(StringU8 srcPath, StringU8 dstPath) {
    if (!srcPath.data || srcPath.size == 0 || !dstPath.data || dstPath.size == 0) {
        LOG_ERROR("host", "Invalid path for file copy (src='{}', dst='{}')", srcPath, dstPath);
        return 0;
    }

    if (OS_file_copy_contents((const char*) srcPath.data, (const char*) dstPath.data)) {
        return 1;
    }

    int errorCode = errno;
    const char* errorText = strerror(errorCode);
    StringU8 errorMsg = errorText ? str8(errorText) : str8("<unknown>");
    LOG_ERROR("host", "Failed to copy module from '{}' to '{}' (errno={} '{}')", srcPath, dstPath, errorCode,
              errorMsg);
    return 0;
}

static void host_record_retired_module(HostState* state, OS_SharedLibrary library, StringU8 path) {
    if (!state || !library.handle) {
        return;
    }

    if (state->retiredModuleCount >= HOT_MODULE_HISTORY_MAX) {
        OS_SharedLibrary oldLibrary = state->retiredModules[0];
        if (oldLibrary.handle) {
            OS_library_close(oldLibrary);
        }
        if (state->retiredModulePaths[0].data && state->retiredModulePaths[0].size > 0) {
            unlink((const char*) state->retiredModulePaths[0].data);
        }
        for (U32 i = 1; i < state->retiredModuleCount; ++i) {
            state->retiredModules[i - 1] = state->retiredModules[i];
            state->retiredModulePaths[i - 1] = state->retiredModulePaths[i];
        }
        state->retiredModuleCount -= 1;
    }

    state->retiredModules[state->retiredModuleCount] = library;
    StringU8 storedPath = STR8_NIL;
    if (path.data && path.size > 0 && state->pathArena) {
        storedPath = str8_cpy(state->pathArena, path);
    }
    state->retiredModulePaths[state->retiredModuleCount] = storedPath;
    state->retiredModuleCount += 1;
}

static StringU8 host_export_status_string(B32 passed) {
    return passed ? str8("pass") : str8("fail");
}

static B32 host_validate_module_exports(HostState* state, const AppModuleExports* exports, StringU8 modulePath) {
    const B32 interfaceOk = (exports->interfaceVersion == APP_INTERFACE_VERSION);
    const B32 permanentOk = (exports->requiredPermanentMemory <= state->memory.permanentStorageSize);
    const B32 transientOk = (exports->requiredTransientMemory <= state->memory.transientStorageSize);
    const B32 arenaOk = (exports->requiredProgramArenaSize <= MB(256));

    StringU8 pathForLog = (modulePath.data && modulePath.size > 0) ? modulePath : str8("<unknown>");
    LOG_INFO("host", "Module export checks for '{}': interface={}, permanent={}, transient={}, programArena={}",
             pathForLog,
             host_export_status_string(interfaceOk),
             host_export_status_string(permanentOk),
             host_export_status_string(transientOk),
             host_export_status_string(arenaOk));

    B32 allOk = interfaceOk && permanentOk && transientOk && arenaOk;
    if (!allOk) {
        LOG_ERROR("host", "Module export requirements not satisfied");
    }
    return allOk;
}

static B32 host_load_module(HostState* state, B32 isReload) {
    ASSERT_ALWAYS(state != 0);

    Temp scratch = get_scratch(0, 0);
    ASSERT_ALWAYS(scratch.arena != 0);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));
    Arena* arena = scratch.arena;

    StringU8 sourcePath = host_build_module_path(state, str8(APP_MODULE_SOURCE_RELATIVE), arena);
    if (!sourcePath.data || sourcePath.size == 0) {
        LOG_ERROR("host", "Failed to resolve source module path");
        return 0;
    }

    OS_Handle probeHandle = OS_file_open((const char*) sourcePath.data, OS_FileOpenMode_Read);
    if (!probeHandle.handle) {
        LOG_ERROR("host", "Module '{}' not available", sourcePath);
        return 0;
    }
    OS_file_close(probeHandle);

    state->moduleGeneration += 1;

    StringU8 loadPathRelative = str8_concat(arena,
                                            str8("hot/utilities_app_loaded_"),
                                            str8_from_U64(arena, state->moduleGeneration, 10),
                                            str8(".dylib"));
    if (!loadPathRelative.data || loadPathRelative.size == 0) {
        LOG_ERROR("host", "Failed to generate module load name");
        return 0;
    }

    StringU8 loadPath = host_build_module_path(state, loadPathRelative, arena);
    if (!loadPath.data || loadPath.size == 0) {
        LOG_ERROR("host", "Failed to resolve module load path");
        return 0;
    }

    if (!host_copy_file(sourcePath, loadPath)) {
        return 0;
    }

    LOG_INFO("host", "Attempting to load module '{}'", loadPath);

    OS_SharedLibrary library = {};
    if (!OS_library_open(loadPath, &library)) {
        StringU8 error = OS_library_last_error(arena);
        if (error.size == 0) {
            error = str8("<unknown>");
        }
        LOG_ERROR("host", "OS_library_open('{}') failed: {}", loadPath, error);
        return 0;
    }

    AppGetEntryPointsProc getEntryPoints = (AppGetEntryPointsProc) OS_library_load_symbol(library, str8("app_get_entry_points"));
    if (!getEntryPoints) {
        StringU8 error = OS_library_last_error(arena);
        if (error.size == 0) {
            error = str8("<unknown>");
        }
        LOG_ERROR("host", "OS_library_load_symbol('app_get_entry_points') failed: {}", error);
        OS_library_close(library);
        return 0;
    }

    AppModuleExports exports = {};
    if (!getEntryPoints(&exports)) {
        LOG_ERROR("host", "app_get_entry_points returned failure");
        OS_library_close(library);
        return 0;
    }

    if (!host_validate_module_exports(state, &exports, loadPath)) {
        OS_library_close(library);
        return 0;
    }

    state->module.library = library;
    state->module.exports = exports;
    state->module.isValid = 1;

    OS_FileInfo moduleInfo = OS_get_file_info((const char*) sourcePath.data);
    state->moduleTimestamp = moduleInfo.exists ? moduleInfo.lastWriteTimestampNs : 0;

    state->currentModulePath = STR8_NIL;
    if (state->pathArena) {
        state->currentModulePath = str8_cpy(state->pathArena, loadPath);
    }

    if (!isReload) {
        if (state->module.exports.initialize) {
            B32 initOk = state->module.exports.initialize(&state->platformAPI, &state->memory, &state->hostContext);
            if (!initOk) {
                LOG_ERROR("host", "Module initialize() reported failure");
                state->module.isValid = 0;
                OS_library_close(library);
                state->module.library.handle = 0;
                MEMSET(&state->module.exports, 0, sizeof(state->module.exports));
                return 0;
            }
        }
    } else {
        if (state->module.exports.reload) {
            state->module.exports.reload(&state->platformAPI, &state->memory, &state->hostContext);
        }
    }

    LOG_INFO("host", "Module loaded from '{}' (timestamp {})", loadPath, state->moduleTimestamp);
    return 1;
}

static void host_unload_module(HostState* state, B32 callShutdown, B32 retainHandle) {
    ASSERT_ALWAYS(state != 0);
    if (!state->module.library.handle) {
        return;
    }

    if (callShutdown && state->module.isValid && state->module.exports.shutdown) {
        state->module.exports.shutdown(&state->platformAPI, &state->memory, &state->hostContext);
    }
    if (retainHandle) {
        host_record_retired_module(state, state->module.library, state->currentModulePath);
    } else {
        OS_library_close(state->module.library);
        if (state->currentModulePath.data && state->currentModulePath.size > 0) {
            unlink((const char*) state->currentModulePath.data);
        }
    }

    state->module.library.handle = 0;
    MEMSET(&state->module.exports, 0, sizeof(state->module.exports));
    state->module.isValid = 0;
    state->currentModulePath = STR8_NIL;
}

static void host_try_build_module(HostState* state) {
    OS_FileInfo sourceInfo = OS_get_file_info(APP_SOURCE_PATH);
    if (!sourceInfo.exists) {
        return;
    }

    U64 sourceTimestamp = sourceInfo.lastWriteTimestampNs;
    if (sourceTimestamp <= state->sourceTimestamp) {
        return;
    }

    LOG_INFO("host", "Detected source change in '{}' -> rebuilding module", APP_SOURCE_PATH);
    S32 buildResult = PLATFORM_OS_CALL(&state->platformAPI, OS_execute, str8("sh build.sh debug module"));
    if (buildResult != 0) {
        LOG_ERROR("host", "Module rebuild failed (exit code {})", buildResult);
        state->sourceTimestamp = sourceTimestamp;
        state->buildFailed = 1;
        return;
    } else {
        state->sourceTimestamp = sourceTimestamp;
        state->buildFailed = 0;
    }
}

static void host_try_reload_module(HostState* state) {
    ASSERT_ALWAYS(state != 0);

    host_try_build_module(state);

    if (state->buildFailed) {
        return;
    }

    Temp scratch = {};
    if (state->frameArena) {
        scratch = temp_begin(state->frameArena);
    } else {
        scratch = get_scratch(0, 0);
    }
    ASSERT_ALWAYS(scratch.arena != 0);
    if (!scratch.arena) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    StringU8 moduleSourcePath = host_build_module_path(state, str8(APP_MODULE_SOURCE_RELATIVE), scratch.arena);
    if (!moduleSourcePath.data || moduleSourcePath.size == 0) {
        return;
    }

    OS_FileInfo moduleInfo = OS_get_file_info((const char*) moduleSourcePath.data);
    if (!moduleInfo.exists) {
        return;
    }

    U64 timestamp = moduleInfo.lastWriteTimestampNs;
    if (timestamp <= state->moduleTimestamp) {
        return;
    }

    LOG_INFO("host", "Detected module change ({} -> {}), reloading", state->moduleTimestamp, timestamp);
    host_unload_module(state, 0, 1);
    if (!host_load_module(state, 1)) {
        LOG_ERROR("host", "Reload failed; module remains unloaded");
    }
}

static void host_cleanup_retired_modules(HostState* state) {
    ASSERT_ALWAYS(state != 0);

    for (U32 index = 0; index < state->retiredModuleCount; ++index) {
        OS_SharedLibrary library = state->retiredModules[index];
        if (library.handle) {
            OS_library_close(library);
            state->retiredModules[index].handle = 0;
        }

        if (state->retiredModulePaths[index].data && state->retiredModulePaths[index].size > 0) {
            unlink((const char*) state->retiredModulePaths[index].data);
        }
        state->retiredModulePaths[index] = STR8_NIL;
    }

    state->retiredModuleCount = 0;
}

// ////////////////////////
// Host Graphics & Input

static B32 host_ensure_graphics_initialized(HostState* state) {
    ASSERT_ALWAYS(state != 0);

    if (state->graphicsInitialized) {
        return 1;
    }

    if (!OS_graphics_init()) {
        LOG_ERROR("host", "Failed to initialize graphics subsystem");
        return 0;
    }

    MEMSET(&state->renderer, 0, sizeof(state->renderer));
    if (!renderer_init(state->programArena, &state->renderer)) {
        LOG_ERROR("host", "Failed to initialize renderer");
        return 0;
    }

    state->hostContext.renderer = &state->renderer;
    state->graphicsInitialized = 1;
    return 1;
}

static void host_update_input(HostState* state, F32 deltaSeconds) {
    ASSERT_ALWAYS(state != 0);
    ASSERT_ALWAYS(state->frameArena != 0);
    ASSERT_ALWAYS(state->graphicsInitialized);

    AppInput* input = &state->input;
    input->deltaSeconds = deltaSeconds;
    input->events = 0;
    input->eventCount = 0;

    ASSERT_ALWAYS(PLATFORM_OS_FN(&state->platformAPI, OS_graphics_pump_events) != 0);
    PLATFORM_OS_CALL(&state->platformAPI, OS_graphics_pump_events);

    ASSERT_ALWAYS(PLATFORM_OS_FN(&state->platformAPI, OS_graphics_poll_events) != 0);

    Arena* arena = state->frameArena;
    OS_GraphicsEvent* firstEvent = 0;
    U32 eventCount = 0;

    OS_GraphicsEvent tempEvents[64];
    while (1) {
        U32 fetched = PLATFORM_OS_CALL(&state->platformAPI, OS_graphics_poll_events,
                                       tempEvents, (U32)ARRAY_COUNT(tempEvents));
        if (fetched == 0u) {
            break;
        }

        for (U32 index = 0; index < fetched; ++index) {
            OS_GraphicsEvent* dst = ARENA_PUSH_STRUCT(arena, OS_GraphicsEvent);
            *dst = tempEvents[index];
            if (!firstEvent) {
                firstEvent = dst;
            }
            eventCount += 1u;
        }
    }

    input->events = firstEvent;
    input->eventCount = eventCount;

    for (U32 index = 0; index < eventCount; ++index) {
        (void) state;
    }
}

int host_main_loop(int argc, char** argv) {
    (void) argc;
    (void) argv;

    log_init();
    set_log_level(LogLevel_Info);

    HostState state;
    if (!host_init(&state)) {
        LOG_ERROR("host", "Host initialization failed");
        host_shutdown(&state);
        thread_context_release();
        return 1;
    }

    LOG_INFO("host", "Entering main loop");

    U64 lastTickTime = OS_get_time_microseconds();

    while (!state.hostContext.shouldQuit) {
        U64 now = OS_get_time_microseconds();
        U64 deltaMicro = now - lastTickTime;
        lastTickTime = now;
        F32 deltaSeconds = (F32) ((F64) deltaMicro / (F64) MILLION(1ULL));

        host_update(&state, deltaSeconds);
    }

    host_shutdown(&state);
    return 0;
}
