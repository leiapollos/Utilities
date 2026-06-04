//
// Created by André Leite on 02/11/2025.
//

#include "nstl/base/base_include.hpp"
#include "nstl/os/os_include.hpp"
#include "nstl/gfx/gfx_include.hpp"
#include "app_interface.hpp"

#include "nstl/base/base_include.cpp"
#include "nstl/os/os_include.cpp"
#include "nstl/gfx/gfx_include.cpp"


#define HOT_MODULE_HISTORY_MAX 32
#define HOST_WINDOW_TITLE "Utilities"
#define HOST_WINDOW_WIDTH 1280u
#define HOST_WINDOW_HEIGHT 720u
#define HOST_DEFAULT_TARGET_FPS_FOCUSED 60u
#define HOST_DEFAULT_TARGET_FPS_UNFOCUSED 30u
#define HOST_MIN_TARGET_FPS 5u
#define HOST_MAX_TARGET_FPS 240u
#define HOST_DEFAULT_MIN_SLEEP_MS 1u
#define HOST_MAX_MIN_SLEEP_MS 16u
#define HOST_GFX_FRAMES_IN_FLIGHT 2u
#define HOST_GFX_TEMP_BUFFER_SIZE MB(8)

#if !defined(UTILITIES_STATIC_APP)
#define UTILITIES_STATIC_APP 0
#endif

static const char* HOST_MODULE_BUILD_INPUTS[] = {
    "app.cpp",
    "app_interface.hpp",
    "app_state.hpp",
    "app/shaders/triangle.metal",
    "nstl/artifact/artifact.hpp",
    "nstl/artifact/artifact.cpp",
    "nstl/artifact/artifact_include.hpp",
    "nstl/artifact/artifact_include.cpp",
    "nstl/gfx/gfx.hpp",
    "nstl/gfx/gfx_include.hpp",
};

typedef B32 (*AppLoadProc)(AppLoadParams* params, AppCode* outCode);

struct LoadedModule {
    OS_SharedLibrary library;
    AppCode code;
};

struct ProgramMemory {
    void* permanentBase;
    U64 permanentSize;
    void* transientBase;
    U64 transientSize;
};

static AppOSApi host_build_os_api(void) {
    AppOSApi api = {};
#define HOST_ASSIGN_OS_FN(name) api.name = name;
    PLATFORM_OS_FUNCTIONS(HOST_ASSIGN_OS_FN)
#undef HOST_ASSIGN_OS_FN
    return api;
}

static U32 host_parse_u32_env_value(StringU8 value) {
    if (!value.data || value.size == 0u) {
        return 0u;
    }

    U64 parsedValue = 0ull;
    for (U64 i = 0u; i < value.size; ++i) {
        U8 ch = value.data[i];
        if (ch < (U8)'0' || ch > (U8)'9') {
            break;
        }

        U64 digit = (U64)(ch - (U8)'0');
        if (parsedValue > ((U64)-1) / 10ull) {
            parsedValue = (U64)-1;
            break;
        }
        parsedValue = (parsedValue * 10ull) + digit;
    }

    if (parsedValue > (U64)0xFFFFFFFFu) {
        parsedValue = (U64)0xFFFFFFFFu;
    }
    return (U32)parsedValue;
}

static U32 host_clamp_u32(U32 value, U32 minValue, U32 maxValue) {
    if (value < minValue) {
        value = minValue;
    }
    if (value > maxValue) {
        value = maxValue;
    }
    return value;
}

static U32 host_parse_u32_env_value_clamped(StringU8 value, U32 defaultValue, U32 minValue, U32 maxValue) {
    if (!value.data || value.size == 0u) {
        return defaultValue;
    }

    U32 parsedValue = host_parse_u32_env_value(value);
    return host_clamp_u32(parsedValue, minValue, maxValue);
}

static B32 host_env_flag_enabled(StringU8 value) {
    if (!value.data || value.size == 0u) {
        return 0;
    }

    U8 first = value.data[0];
    if (first == (U8)'1' || first == (U8)'t' || first == (U8)'T' ||
        first == (U8)'y' || first == (U8)'Y') {
        return 1;
    }
    return 0;
}

struct HostState {
    LoadedModule module;
    ProgramMemory storage;
    HOT_StateStore store;
    Arena* programArena;
    Arena* frameArena;
    Arena* pathArena;
    AppHost host;
    AppInput input;
    OS_WindowHandle window;
    U32 windowWidth;
    U32 windowHeight;
    B32 graphicsInitialized;
    U64 moduleTimestamp;
    U64 sourceTimestamp;
    U64 moduleGeneration;
    StringU8 currentModulePath;
    OS_SharedLibrary retiredModules[HOT_MODULE_HISTORY_MAX];
    StringU8 retiredModulePaths[HOT_MODULE_HISTORY_MAX];
    U32 retiredModuleCount;
    B32 buildFailed;
    B32 windowFocused;
    U32 targetFpsFocused;
    U32 targetFpsUnfocused;
    U32 minSleepMs;
    B32 framePacingEnabled;
    U32 exitAfterFrames;
    U32 framesRun;
};

static B32 host_allocate_memory(HostState* state);
static void host_release_memory(HostState* state);
static B32 host_ensure_graphics_initialized(HostState* state);
static B32 host_create_main_window(HostState* state);
static void host_destroy_main_window(HostState* state);
static B32 host_create_gfx_device(HostState* state);
static void host_destroy_gfx_device(HostState* state);
static StringU8 host_build_module_path(StringU8 relativePath, Arena* arena);
static void host_record_retired_module(HostState* state, OS_SharedLibrary library, StringU8 path);
static B32 host_copy_file(StringU8 srcPath, StringU8 dstPath);
static U64 host_get_newest_module_input_timestamp(void);
static StringU8 host_export_status_string(B32 passed);
static B32 host_validate_app_code(const AppCode* code, StringU8 modulePath);
static B32 host_load_candidate(HostState* state, LoadedModule* outModule, StringU8* outPath);
static B32 host_commit_candidate(HostState* state, LoadedModule* candidate, StringU8 candidatePath, B32 isReload);
#if UTILITIES_STATIC_APP
static B32 host_load_static_app(HostState* state);
#endif
static B32 host_load_initial_module(HostState* state);
static void host_unload_module(HostState* state);
static void host_close_module(LoadedModule* module);
static void host_try_reload_module(HostState* state);
static void host_update_input(HostState* state, F32 deltaSeconds);
static void host_cleanup_retired_modules(HostState* state);

static B32 host_init(HostState* state) {
    ASSERT_ALWAYS(state != 0);

    MEMSET(state, 0, sizeof(*state));

    B32 memoryOk = host_allocate_memory(state);
    ASSERT_ALWAYS(memoryOk);
    if (!memoryOk) {
        return 0;
    }

    LOG_INFO("host", "Allocated program memory (perm={} bytes, trans={} bytes)",
             state->storage.permanentSize, state->storage.transientSize);

    state->sourceTimestamp = host_get_newest_module_input_timestamp();
    state->windowFocused = 1;
    state->targetFpsFocused = HOST_DEFAULT_TARGET_FPS_FOCUSED;
    state->targetFpsUnfocused = HOST_DEFAULT_TARGET_FPS_UNFOCUSED;
    state->minSleepMs = HOST_DEFAULT_MIN_SLEEP_MS;
    state->framePacingEnabled = 1;

    {
        Temp scratch = get_scratch(0, 0);
        if (scratch.arena) {
            StringU8 focusedFpsText = OS_get_environment_variable(scratch.arena,
                                                                  str8("UTILITIES_MAX_FPS_FOCUSED"));
            state->targetFpsFocused = host_parse_u32_env_value_clamped(focusedFpsText,
                                                                        HOST_DEFAULT_TARGET_FPS_FOCUSED,
                                                                        HOST_MIN_TARGET_FPS,
                                                                        HOST_MAX_TARGET_FPS);

            StringU8 unfocusedFpsText = OS_get_environment_variable(scratch.arena,
                                                                    str8("UTILITIES_MAX_FPS_UNFOCUSED"));
            state->targetFpsUnfocused = host_parse_u32_env_value_clamped(unfocusedFpsText,
                                                                          HOST_DEFAULT_TARGET_FPS_UNFOCUSED,
                                                                          HOST_MIN_TARGET_FPS,
                                                                          HOST_MAX_TARGET_FPS);

            StringU8 minSleepText = OS_get_environment_variable(scratch.arena,
                                                               str8("UTILITIES_MIN_SLEEP_MS"));
            state->minSleepMs = host_parse_u32_env_value_clamped(minSleepText,
                                                                  HOST_DEFAULT_MIN_SLEEP_MS,
                                                                  HOST_DEFAULT_MIN_SLEEP_MS,
                                                                  HOST_MAX_MIN_SLEEP_MS);

            StringU8 disablePacingText = OS_get_environment_variable(scratch.arena,
                                                                     str8("UTILITIES_DISABLE_FRAME_PACING"));
            if (host_env_flag_enabled(disablePacingText)) {
                state->framePacingEnabled = 0;
            }

            StringU8 exitAfterFramesText = OS_get_environment_variable(scratch.arena,
                                                                       str8("UTILITIES_EXIT_AFTER_FRAMES"));
            state->exitAfterFrames = host_parse_u32_env_value(exitAfterFramesText);
            temp_end(&scratch);
        }
    }

    LOG_INFO("host", "Frame pacing: enabled={}, focusedFps={}, unfocusedFps={}, minSleepMs={}",
             state->framePacingEnabled ? 1u : 0u,
             state->targetFpsFocused,
             state->targetFpsUnfocused,
             state->minSleepMs);

    B32 graphicsOk = host_ensure_graphics_initialized(state);
    ASSERT_ALWAYS(graphicsOk);
    if (!graphicsOk) {
        LOG_ERROR("host", "Failed to initialize graphics before loading module");
        return 0;
    }

    B32 windowOk = host_create_main_window(state);
    ASSERT_ALWAYS(windowOk);
    if (!windowOk) {
        LOG_ERROR("host", "Failed to create main window");
        return 0;
    }

    B32 gfxOk = host_create_gfx_device(state);
    ASSERT_ALWAYS(gfxOk);
    if (!gfxOk) {
        LOG_ERROR("host", "Failed to create gfx device");
        return 0;
    }

    B32 loadOk = host_load_initial_module(state);
    ASSERT_ALWAYS(loadOk);
    if (!loadOk) {
        LOG_ERROR("host", "Initial module load failed");
        return 0;
    }

    return 1;
}

static void host_update(HostState* state, F32 deltaSeconds) {
    ASSERT_ALWAYS(state != 0);
    ASSERT_ALWAYS(state->frameArena != 0);

    arena_pop_to(state->frameArena, 0);

    host_try_reload_module(state);
    host_update_input(state, deltaSeconds);
    if (state->host.shouldQuit) {
        return;
    }

    ASSERT_ALWAYS(state->module.code.frame != 0);

    state->module.code.frame(&state->host, &state->store, &state->input);
}

static void host_shutdown(HostState* state) {
    ASSERT_ALWAYS(state != 0);

    host_unload_module(state);

    if (state->graphicsInitialized) {
        host_destroy_gfx_device(state);
        host_destroy_main_window(state);
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

    state->currentModulePath = STR8_NIL;
    for (U32 index = 0; index < HOT_MODULE_HISTORY_MAX; ++index) {
        state->retiredModulePaths[index] = STR8_NIL;
        state->retiredModules[index].handle = 0;
    }
    state->retiredModuleCount = 0;
    MEMSET(&state->store, 0, sizeof(state->store));
    state->host.frameArena = 0;
    state->host.stateArena = 0;
    state->host.window.handle = 0;
    state->host.gfxDevice = 0;
    state->host.windowWidth = 0;
    state->host.windowHeight = 0;
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
        .committedSize = MB(8),
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

    hot_state_store_init(&state->store, state->storage.permanentBase, state->storage.permanentSize);

    MEMSET(&state->host, 0, sizeof(state->host));
    OS_SystemInfo* sysInfo = OS_get_system_info();
    state->host.logicalCoreCount = sysInfo ? sysInfo->logicalCores : 1u;
    state->host.frameArena = state->frameArena;
    state->host.stateArena = state->programArena;
    state->host.windowWidth = HOST_WINDOW_WIDTH;
    state->host.windowHeight = HOST_WINDOW_HEIGHT;
    state->host.os = host_build_os_api();

    return 1;
}

// ////////////////////////
// Host Module Management

static StringU8 host_build_module_path(StringU8 relativePath, Arena* arena) {
    if (!arena || !relativePath.data) {
        return STR8_NIL;
    }

    StringU8 executableDir = OS_get_executable_directory(arena);
    if (!executableDir.data || executableDir.size == 0) {
        return STR8_NIL;
    }

    return str8_concat(arena, executableDir, str8("/"), relativePath);
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

static B32 host_validate_app_code(const AppCode* code, StringU8 modulePath) {
    ASSERT_ALWAYS(code != 0);

    const B32 sizeOk = (code->size == sizeof(AppCode));
    const B32 abiOk = (code->abiVersion == APP_ABI_VERSION);
    const B32 schemaOk = (code->schemaVersion == APP_STATE_SCHEMA_VERSION);
    const B32 entryOk = (code->boot != 0 && code->before_reload != 0 && code->after_reload != 0 &&
                         code->frame != 0 && code->shutdown != 0);

    StringU8 pathForLog = (modulePath.data && modulePath.size > 0) ? modulePath : str8("<unknown>");
    LOG_INFO("host", "AppCode checks for '{}': size={}, abi={}, schema={}, entrypoints={}",
             pathForLog,
             host_export_status_string(sizeOk),
             host_export_status_string(abiOk),
             host_export_status_string(schemaOk),
             host_export_status_string(entryOk));

    B32 allOk = sizeOk && abiOk && schemaOk && entryOk;
    if (!allOk) {
        LOG_ERROR("host", "AppCode requirements not satisfied");
    }
    return allOk;
}

static B32 host_load_candidate(HostState* state, LoadedModule* outModule, StringU8* outPath) {
    ASSERT_ALWAYS(state != 0);
    ASSERT_ALWAYS(outModule != 0);
    ASSERT_ALWAYS(outPath != 0);

    MEMSET(outModule, 0, sizeof(*outModule));
    *outPath = STR8_NIL;

    Temp scratch = get_scratch(0, 0);
    ASSERT_ALWAYS(scratch.arena != 0);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));
    Arena* arena = scratch.arena;

    StringU8 sourcePath = host_build_module_path(str8(APP_MODULE_SOURCE_RELATIVE), arena);
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

    StringU8 loadPath = host_build_module_path(loadPathRelative, arena);
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

    AppLoadProc appLoad = (AppLoadProc) OS_library_load_symbol(library, str8("app_load"));
    if (!appLoad) {
        StringU8 error = OS_library_last_error(arena);
        if (error.size == 0) {
            error = str8("<unknown>");
        }
        LOG_ERROR("host", "OS_library_load_symbol('app_load') failed: {}", error);
        OS_library_close(library);
        return 0;
    }

    AppLoadParams params = {};
    params.size = sizeof(params);
    params.abiVersion = APP_ABI_VERSION;
    params.moduleGeneration = state->moduleGeneration;
    params.host = &state->host;
    params.store = &state->store;

    AppCode code = {};
    if (!appLoad(&params, &code)) {
        LOG_ERROR("host", "app_load returned failure");
        OS_library_close(library);
        return 0;
    }

    if (!host_validate_app_code(&code, loadPath)) {
        OS_library_close(library);
        return 0;
    }

    outModule->library = library;
    outModule->code = code;
    if (state->pathArena == 0) {
        LOG_ERROR("host", "No path arena available for candidate path");
        host_close_module(outModule);
        return 0;
    }
    *outPath = str8_cpy(state->pathArena, loadPath);
    LOG_INFO("host", "Candidate module loaded from '{}'", loadPath);
    return 1;
}

static B32 host_commit_candidate(HostState* state, LoadedModule* candidate, StringU8 candidatePath, B32 isReload) {
    ASSERT_ALWAYS(state != 0);
    ASSERT_ALWAYS(candidate != 0);
    ASSERT_ALWAYS(candidate->code.frame != 0);

    B32 commitOk = 1;

    if (!isReload) {
        if (!candidate->code.boot(&state->host, &state->store)) {
            LOG_ERROR("host", "App boot reported failure");
            commitOk = 0;
        }
    } else {
        if (state->module.code.before_reload) {
            state->module.code.before_reload(&state->host, &state->store);
        }
        if (!candidate->code.after_reload(&state->host, &state->store)) {
            LOG_ERROR("host", "App after_reload reported failure; keeping active module");
            if (state->module.code.after_reload) {
                if (!state->module.code.after_reload(&state->host, &state->store)) {
                    LOG_ERROR("host", "Active module failed to restore after rejected reload; quitting");
                    state->host.shouldQuit = 1;
                }
            }
            commitOk = 0;
        }
    }

    if (!commitOk) {
        host_close_module(candidate);
        if (candidatePath.data && candidatePath.size > 0) {
            unlink((const char*) candidatePath.data);
        }
        return 0;
    }

    LoadedModule oldModule = state->module;
    StringU8 oldPath = state->currentModulePath;

    state->module = *candidate;
    MEMSET(candidate, 0, sizeof(*candidate));

    state->currentModulePath = STR8_NIL;
    if (state->pathArena && candidatePath.data && candidatePath.size > 0) {
        state->currentModulePath = str8_cpy(state->pathArena, candidatePath);
    }

    if (isReload && oldModule.library.handle) {
        host_record_retired_module(state, oldModule.library, oldPath);
    }

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena) {
        StringU8 moduleSourcePath = host_build_module_path(str8(APP_MODULE_SOURCE_RELATIVE), scratch.arena);
        OS_FileInfo moduleInfo = OS_get_file_info((const char*) moduleSourcePath.data);
        state->moduleTimestamp = moduleInfo.exists ? moduleInfo.lastWriteTimestampNs : state->moduleTimestamp;
        temp_end(&scratch);
    }

    LOG_INFO("host", "Committed module from '{}'", state->currentModulePath);
    return 1;
}

static B32 host_load_initial_module(HostState* state) {
#if UTILITIES_STATIC_APP
    return host_load_static_app(state);
#else
    LoadedModule candidate = {};
    StringU8 candidatePath = STR8_NIL;
    if (!host_load_candidate(state, &candidate, &candidatePath)) {
        return 0;
    }
    return host_commit_candidate(state, &candidate, candidatePath, 0);
#endif
}

#if UTILITIES_STATIC_APP
static B32 host_load_static_app(HostState* state) {
    ASSERT_ALWAYS(state != 0);

    AppLoadParams params = {};
    params.size = sizeof(params);
    params.abiVersion = APP_ABI_VERSION;
    params.moduleGeneration = 1u;
    params.host = &state->host;
    params.store = &state->store;

    AppCode code = {};
    if (!app_load(&params, &code)) {
        LOG_ERROR("host", "Static app_load returned failure");
        return 0;
    }
    if (!host_validate_app_code(&code, str8("static-app"))) {
        return 0;
    }
    if (!code.boot(&state->host, &state->store)) {
        LOG_ERROR("host", "Static app boot reported failure");
        return 0;
    }

    state->module.code = code;
    state->moduleGeneration = 1u;
    state->moduleTimestamp = 0u;
    if (state->pathArena) {
        state->currentModulePath = str8_cpy(state->pathArena, str8("static-app"));
    }
    LOG_INFO("host", "Static app committed");
    return 1;
}
#endif

static void host_close_module(LoadedModule* module) {
    ASSERT_ALWAYS(module != 0);
    if (module->library.handle) {
        OS_library_close(module->library);
    }
    MEMSET(module, 0, sizeof(*module));
}

static void host_unload_module(HostState* state) {
    ASSERT_ALWAYS(state != 0);
    if (!state->module.library.handle && state->module.code.shutdown == 0) {
        return;
    }

    if (state->module.code.shutdown) {
        state->module.code.shutdown(&state->host, &state->store);
    }
    if (state->module.library.handle) {
        OS_library_close(state->module.library);
        if (state->currentModulePath.data && state->currentModulePath.size > 0) {
            unlink((const char*) state->currentModulePath.data);
        }
    }

    MEMSET(&state->module, 0, sizeof(state->module));
    state->currentModulePath = STR8_NIL;
}

static U64 host_get_newest_module_input_timestamp(void) {
    U64 newestTimestamp = 0;
    for (U32 index = 0; index < ARRAY_COUNT(HOST_MODULE_BUILD_INPUTS); ++index) {
        OS_FileInfo info = OS_get_file_info(HOST_MODULE_BUILD_INPUTS[index]);
        if (info.exists && info.lastWriteTimestampNs > newestTimestamp) {
            newestTimestamp = info.lastWriteTimestampNs;
        }
    }
    return newestTimestamp;
}

static void host_try_build_module(HostState* state) {
    U64 sourceTimestamp = host_get_newest_module_input_timestamp();
    if (sourceTimestamp == 0) {
        return;
    }

    if (sourceTimestamp <= state->sourceTimestamp) {
        return;
    }

    LOG_INFO("host", "Detected app module source changes -> rebuilding module");
    S32 buildResult = APP_OS_CALL(&state->host, OS_execute, str8("./sob module debug"));
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

#if UTILITIES_STATIC_APP
    (void) state;
    return;
#else
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

    StringU8 moduleSourcePath = host_build_module_path(str8(APP_MODULE_SOURCE_RELATIVE), scratch.arena);
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

    LOG_INFO("host", "Detected module change ({} -> {}), loading candidate", state->moduleTimestamp, timestamp);
    LoadedModule candidate = {};
    StringU8 candidatePath = STR8_NIL;
    if (!host_load_candidate(state, &candidate, &candidatePath)) {
        LOG_ERROR("host", "Candidate load failed; active module remains running");
        return;
    }
    if (!host_commit_candidate(state, &candidate, candidatePath, 1)) {
        LOG_ERROR("host", "Candidate swap failed; active module remains running");
    }
#endif
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

    state->graphicsInitialized = 1;
    return 1;
}

static B32 host_create_main_window(HostState* state) {
    ASSERT_ALWAYS(state != 0);
    ASSERT_ALWAYS(state->graphicsInitialized);

    state->windowWidth = HOST_WINDOW_WIDTH;
    state->windowHeight = HOST_WINDOW_HEIGHT;

    OS_WindowDesc desc = {};
    desc.title = HOST_WINDOW_TITLE;
    desc.width = state->windowWidth;
    desc.height = state->windowHeight;

    state->window = OS_window_create(desc);
    if (!state->window.handle) {
        LOG_ERROR("host", "OS_window_create failed");
        return 0;
    }

    OS_WindowSurfaceInfo surface = OS_window_get_surface_info(state->window);
    if (surface.drawableWidth != 0u && surface.drawableHeight != 0u) {
        state->windowWidth = surface.drawableWidth;
        state->windowHeight = surface.drawableHeight;
    }

    state->host.window = state->window;
    state->host.windowWidth = state->windowWidth;
    state->host.windowHeight = state->windowHeight;
    return 1;
}

static void host_destroy_main_window(HostState* state) {
    ASSERT_ALWAYS(state != 0);

    if (state->window.handle) {
        OS_window_destroy(state->window);
        state->window.handle = 0;
    }

    state->windowWidth = 0;
    state->windowHeight = 0;
    state->host.window.handle = 0;
    state->host.windowWidth = 0;
    state->host.windowHeight = 0;
}

static B32 host_create_gfx_device(HostState* state) {
    ASSERT_ALWAYS(state != 0);
    ASSERT_ALWAYS(state->window.handle != 0);
    ASSERT_ALWAYS(state->programArena != 0);

    if (state->host.gfxDevice) {
        return 1;
    }

    GfxDeviceDesc desc = {};
    desc.backend = GfxBackend_Metal;
    desc.window = state->window;
    desc.framesInFlight = HOST_GFX_FRAMES_IN_FLIGHT;
    desc.tempBufferSize = HOST_GFX_TEMP_BUFFER_SIZE;
    desc.enableValidation = 1;

    GfxDevice* device = 0;
    if (!gfx_device_create(&desc, state->programArena, &device)) {
        return 0;
    }

    gfx_device_resize(device, state->windowWidth, state->windowHeight);
    state->host.gfxDevice = device;
    return 1;
}

static void host_destroy_gfx_device(HostState* state) {
    if (!state || !state->host.gfxDevice) {
        return;
    }

    gfx_device_destroy(state->host.gfxDevice);
    state->host.gfxDevice = 0;
}

static void host_update_input(HostState* state, F32 deltaSeconds) {
    ASSERT_ALWAYS(state != 0);
    ASSERT_ALWAYS(state->frameArena != 0);
    ASSERT_ALWAYS(state->graphicsInitialized);

    AppInput* input = &state->input;
    input->deltaSeconds = deltaSeconds;
    input->events = 0;
    input->eventCount = 0;

    ASSERT_ALWAYS(APP_OS_FN(&state->host, OS_graphics_pump_events) != 0);
    APP_OS_CALL(&state->host, OS_graphics_pump_events);

    ASSERT_ALWAYS(APP_OS_FN(&state->host, OS_graphics_poll_events) != 0);

    Arena* arena = state->frameArena;
    OS_GraphicsEvent* firstEvent = 0;
    U32 eventCount = 0;

    OS_GraphicsEvent tempEvents[64];
    while (1) {
        U32 fetched = APP_OS_CALL(&state->host, OS_graphics_poll_events,
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
        const OS_GraphicsEvent* event = firstEvent + index;
        switch (event->tag) {
            case OS_GraphicsEvent_Tag_WindowShown: {
                if (event->window.handle == state->window.handle &&
                    event->windowShown.width != 0u &&
                    event->windowShown.height != 0u) {
                    state->windowWidth = event->windowShown.width;
                    state->windowHeight = event->windowShown.height;
                    state->host.windowWidth = state->windowWidth;
                    state->host.windowHeight = state->windowHeight;
                    if (state->host.gfxDevice) {
                        gfx_device_resize(state->host.gfxDevice, state->windowWidth, state->windowHeight);
                    }
                }
            }
            break;

            case OS_GraphicsEvent_Tag_WindowResized: {
                if (event->window.handle == state->window.handle &&
                    event->windowResized.width != 0u &&
                    event->windowResized.height != 0u) {
                    state->windowWidth = event->windowResized.width;
                    state->windowHeight = event->windowResized.height;
                    state->host.windowWidth = state->windowWidth;
                    state->host.windowHeight = state->windowHeight;
                    if (state->host.gfxDevice) {
                        gfx_device_resize(state->host.gfxDevice, state->windowWidth, state->windowHeight);
                    }
                }
            }
            break;

            case OS_GraphicsEvent_Tag_WindowClosed:
            case OS_GraphicsEvent_Tag_WindowDestroyed: {
                if (event->window.handle == state->window.handle) {
                    state->host.shouldQuit = 1;
                }
            }
            break;

            case OS_GraphicsEvent_Tag_WindowFocused: {
                state->windowFocused = 1;
            }
            break;

            case OS_GraphicsEvent_Tag_WindowUnfocused: {
                state->windowFocused = 0;
            }
            break;

            default: {
            }
            break;
        }
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
        return 1;
    }

    LOG_INFO("host", "Entering main loop");

    U64 lastTickTime = OS_get_time_microseconds();

    while (!state.host.shouldQuit) {
        U64 frameStartTime = OS_get_time_microseconds();
        U64 deltaMicro = frameStartTime - lastTickTime;
        lastTickTime = frameStartTime;
        F32 deltaSeconds = (F32) ((F64) deltaMicro / (F64) MILLION(1ULL));

        host_update(&state, deltaSeconds);
        state.framesRun += 1u;
        if (state.exitAfterFrames != 0u && state.framesRun >= state.exitAfterFrames) {
            state.host.shouldQuit = 1;
        }

        if (state.framePacingEnabled && !state.host.shouldQuit) {
            U32 targetFps = state.windowFocused ? state.targetFpsFocused : state.targetFpsUnfocused;
            targetFps = host_clamp_u32(targetFps, HOST_MIN_TARGET_FPS, HOST_MAX_TARGET_FPS);
            U64 targetFrameMicros = MILLION(1ULL) / targetFps;

            U64 frameEndTime = OS_get_time_microseconds();
            U64 workMicros = frameEndTime - frameStartTime;
            if (workMicros < targetFrameMicros) {
                U64 remainingMicros = targetFrameMicros - workMicros;
                U32 sleepMilliseconds = (U32)(remainingMicros / 1000ull);
                if (sleepMilliseconds >= 1u) {
                    if (sleepMilliseconds < state.minSleepMs) {
                        sleepMilliseconds = state.minSleepMs;
                    }
                    OS_sleep_milliseconds(sleepMilliseconds);
                }
            }
        }
    }

    host_shutdown(&state);
    return 0;
}
