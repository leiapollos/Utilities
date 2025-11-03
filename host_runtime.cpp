//
// Created by André Leite on 02/11/2025.
//

#include "nstl/base/base_include.hpp"
#include "nstl/base/base_log.hpp"
#include "nstl/os/os_include.hpp"
#include "app_interface.hpp"

#include <dlfcn.h>

#define APP_MODULE_SOURCE_RELATIVE "hot/utilities_app.dylib"
#define APP_SOURCE_PATH "app.cpp"
#define HOT_MODULE_NAME_PATTERN "hot/utilities_app_loaded_%llu.dylib"
#define HOT_MODULE_PATH_MAX 256
#define HOT_MODULE_HISTORY_MAX 32

typedef B32 (*AppGetEntryPointsProc)(AppModuleExports* outExports);

struct LoadedModule {
    void* handle;
    AppModuleExports exports;
    B32 isValid;
};

struct ProgramMemory {
    void* permanentBase;
    U64 permanentSize;
    void* transientBase;
    U64 transientSize;
};

#define HOT_MODULE_PATH_MAX 256
#define HOT_MODULE_HISTORY_MAX 32
#define HOST_EVENT_CAP 64

static PlatformOSApi host_build_os_api(void) {
    PlatformOSApi api = {};
#define HOST_ASSIGN_OS_FN(name) api.name = name;
    PLATFORM_OS_FUNCTIONS(HOST_ASSIGN_OS_FN)
#undef HOST_ASSIGN_OS_FN
    return api;
}

struct HostState {
    LoadedModule module;
    AppMemory memory;
    ProgramMemory storage;
    Arena* programArena;
    AppHostContext hostContext;
    AppPlatform platformAPI;
    AppInput input;
    B32 graphicsInitialized;
    OS_GraphicsEvent eventBuffer[HOST_EVENT_CAP];
    U64 moduleTimestamp;
    U64 sourceTimestamp;
    U64 moduleGeneration;
    char currentModulePath[HOT_MODULE_PATH_MAX];
    char moduleBasePath[HOT_MODULE_PATH_MAX];
    void* retiredModuleHandles[HOT_MODULE_HISTORY_MAX];
    char retiredModulePaths[HOT_MODULE_HISTORY_MAX][HOT_MODULE_PATH_MAX];
    U32 retiredModuleCount;
    B32 buildFailed;
};

static void host_build_module_path(HostState* state, const char* relativePath, char* outPath, U64 maxLen) {
    Temp scratch = get_scratch(0, 0);
    DEFER_REF(temp_end(&scratch));
    Arena* arena = scratch.arena;

    if (state->moduleBasePath[0] == '\0') {
        StringU8 execDir = OS_get_executable_directory(arena);
        if (!str8_is_nil(execDir) && execDir.size > 0) {
            U64 baseLen = execDir.size;
            if (baseLen >= sizeof(state->moduleBasePath)) {
                baseLen = sizeof(state->moduleBasePath) - 1u;
            }
            MEMMOVE(state->moduleBasePath, execDir.data, baseLen);
            state->moduleBasePath[baseLen] = '\0';
        } else {
            state->moduleBasePath[0] = '\0';
        }
    }

    StringU8 relativeStr = str8(relativePath);
    StringU8 result;
    
    if (state->moduleBasePath[0] == '\0') {
        result = relativeStr;
    } else {
        StringU8 baseStr = str8(state->moduleBasePath);
        StringU8 separator = str8("/");
        StringU8 pieces[] = {baseStr, separator, relativeStr};
        result = str8_concat_n(arena, pieces, ARRAY_COUNT(pieces));
    }
    
    U64 len = result.size;
    if (len >= maxLen) {
        len = maxLen - 1;
    }
    MEMMOVE(outPath, result.data, len);
    outPath[len] = '\0';
}

static void host_record_retired_module(HostState* state, void* handle, const char* path) {
    if (!handle || !state) {
        return;
    }

    if (state->retiredModuleCount >= HOT_MODULE_HISTORY_MAX) {
        void* oldHandle = state->retiredModuleHandles[0];
        if (oldHandle) {
            dlclose(oldHandle);
        }
        if (state->retiredModulePaths[0][0] != '\0') {
            unlink(state->retiredModulePaths[0]);
        }
        for (U32 i = 1; i < state->retiredModuleCount; ++i) {
            state->retiredModuleHandles[i - 1] = state->retiredModuleHandles[i];
            MEMMOVE(state->retiredModulePaths[i - 1], state->retiredModulePaths[i], HOT_MODULE_PATH_MAX);
        }
        state->retiredModuleCount -= 1;
    }

    state->retiredModuleHandles[state->retiredModuleCount] = handle;
    if (path && path[0] != '\0') {
        StringU8 pathStr = str8(path);
        U64 len = pathStr.size;
        if (len >= HOT_MODULE_PATH_MAX) {
            len = HOT_MODULE_PATH_MAX - 1;
        }
        MEMMOVE(state->retiredModulePaths[state->retiredModuleCount], pathStr.data, len);
        state->retiredModulePaths[state->retiredModuleCount][len] = '\0';
    } else {
        state->retiredModulePaths[state->retiredModuleCount][0] = '\0';
    }
    state->retiredModuleCount += 1;
}

static B32 host_ensure_graphics_initialized(HostState* state) {
    if (!state) {
        return 0;
    }

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

static void host_destroy_window(HostState* state) {
}

static void host_update_input(HostState* state, F32 deltaSeconds) {
    if (!state) {
        return;
    }

    AppInput* input = &state->input;
    input->deltaSeconds = deltaSeconds;
    input->events = state->eventBuffer;
    input->eventCount = 0;

    if (!state->graphicsInitialized) {
        return;
    }

    if (PLATFORM_OS_FN(&state->platformAPI, OS_graphics_pump_events)) {
        PLATFORM_OS_CALL(&state->platformAPI, OS_graphics_pump_events);
    }

    U32 eventCount = 0;
    if (PLATFORM_OS_FN(&state->platformAPI, OS_graphics_poll_events)) {
        eventCount = PLATFORM_OS_CALL(&state->platformAPI, OS_graphics_poll_events,
                                      state->eventBuffer, (U32)ARRAY_COUNT(state->eventBuffer));
    }

    input->eventCount = eventCount;

    for (U32 index = 0; index < eventCount; ++index) {
        (void)state;
    }
}

static void host_release_memory(HostState* state) {
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
}

static B32 host_copy_file(const char* srcPath, const char* dstPath) {
    if (OS_file_copy_contents(srcPath, dstPath)) {
        return 1;
    }

    int errorCode = errno;
    const char* errorText = strerror(errorCode);
    LOG_ERROR("host", "Failed to copy module from '{}' to '{}' (errno={} '{}')", srcPath, dstPath, errorCode, errorText ? errorText : "<unknown>");
    return 0;
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

    state->platformAPI.userData = state;
    state->platformAPI.os = host_build_os_api();

    MEMSET(&state->input, 0, sizeof(state->input));
    state->input.events = state->eventBuffer;
    state->input.eventCount = 0;
    state->input.deltaSeconds = 0.0f;

    state->graphicsInitialized = 0;

    return 1;
}

static B32 host_load_module(HostState* state, B32 isReload) {
    if (!state) {
        return 0;
    }

    char sourcePath[HOT_MODULE_PATH_MAX];
    host_build_module_path(state, APP_MODULE_SOURCE_RELATIVE, sourcePath, sizeof(sourcePath));
    OS_Handle probeHandle = OS_file_open(sourcePath, OS_FileOpenMode_Read);
    if (!probeHandle.handle) {
        LOG_ERROR("host", "Module '{}' not available", sourcePath);
        return 0;
    }
    OS_file_close(probeHandle);

    state->moduleGeneration += 1;
    char loadPath[HOT_MODULE_PATH_MAX];
    
    Temp scratch = get_scratch(0, 0);
    DEFER_REF(temp_end(&scratch));
    Arena* arena = scratch.arena;
    
    StringU8 baseName = str8("hot/utilities_app_loaded_");
    StringU8 generationStr = str8_from_U64(arena, state->moduleGeneration, 10);
    StringU8 suffix = str8(".dylib");
    StringU8 pieces[] = {baseName, generationStr, suffix};
    StringU8 loadPathRelativeStr = str8_concat_n(arena, pieces, ARRAY_COUNT(pieces));
    
    char loadPathRelative[HOT_MODULE_PATH_MAX];
    U64 len = loadPathRelativeStr.size;
    if (len >= sizeof(loadPathRelative)) {
        len = sizeof(loadPathRelative) - 1;
    }
    MEMMOVE(loadPathRelative, loadPathRelativeStr.data, len);
    loadPathRelative[len] = '\0';
    
    host_build_module_path(state, loadPathRelative, loadPath, sizeof(loadPath));

    if (!host_copy_file(sourcePath, loadPath)) {
        LOG_ERROR("host", "Failed to copy module from '{}' to '{}'", sourcePath, loadPath);
        return 0;
    }

    LOG_INFO("host", "Attempting to dlopen '{}'", loadPath);

    void* handle = dlopen(loadPath, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        const char* errorStr = dlerror();
        LOG_ERROR("host", "dlopen failed: {}", errorStr ? errorStr : "<unknown>");
        return 0;
    }

    AppGetEntryPointsProc getEntryPoints = (AppGetEntryPointsProc) dlsym(handle, "app_get_entry_points");
    if (!getEntryPoints) {
        LOG_ERROR("host", "dlsym(app_get_entry_points) failed");
        dlclose(handle);
        return 0;
    }

    AppModuleExports exports = {};
    if (!getEntryPoints(&exports)) {
        LOG_ERROR("host", "app_get_entry_points returned failure");
        dlclose(handle);
        return 0;
    }

    if (exports.interfaceVersion != APP_INTERFACE_VERSION) {
        LOG_ERROR("host", "Module interface mismatch (expected {}, got {})", APP_INTERFACE_VERSION, exports.interfaceVersion);
        dlclose(handle);
        return 0;
    }

    if (exports.requiredPermanentMemory > state->memory.permanentStorageSize) {
        LOG_ERROR("host", "Module permanent memory requirement too large ({} > {})", exports.requiredPermanentMemory, state->memory.permanentStorageSize);
        dlclose(handle);
        return 0;
    }

    if (exports.requiredTransientMemory > state->memory.transientStorageSize) {
        LOG_ERROR("host", "Module transient memory requirement too large ({} > {})", exports.requiredTransientMemory, state->memory.transientStorageSize);
        dlclose(handle);
        return 0;
    }

    if (exports.requiredProgramArenaSize > MB(256)) {
        LOG_ERROR("host", "Module program arena requirement too large ({} > {})", exports.requiredProgramArenaSize, MB(256));
        dlclose(handle);
        return 0;
    }

    state->module.handle = handle;
    state->module.exports = exports;
    state->module.isValid = 1;
    OS_FileInfo moduleInfo = OS_get_file_info(sourcePath);
    state->moduleTimestamp = moduleInfo.exists ? moduleInfo.lastWriteTimestampNs : 0;

    StringU8 loadPathStr = str8(loadPath);
    U64 pathLen = loadPathStr.size;
    if (pathLen >= HOT_MODULE_PATH_MAX) {
        pathLen = HOT_MODULE_PATH_MAX - 1;
    }
    MEMMOVE(state->currentModulePath, loadPathStr.data, pathLen);
    state->currentModulePath[pathLen] = '\0';

    if (!isReload) {
        if (state->module.exports.initialize) {
            B32 initOk = state->module.exports.initialize(&state->platformAPI, &state->memory, &state->hostContext);
            if (!initOk) {
                LOG_ERROR("host", "Module initialize() reported failure");
                state->module.isValid = 0;
                dlclose(handle);
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
    if (!state || !state->module.handle) {
        return;
    }

    if (callShutdown && state->module.isValid && state->module.exports.shutdown) {
        state->module.exports.shutdown(&state->platformAPI, &state->memory, &state->hostContext);
    }
    if (retainHandle) {
        host_record_retired_module(state, state->module.handle, state->currentModulePath);
    } else {
        dlclose(state->module.handle);
        if (state->currentModulePath[0] != '\0') {
            unlink(state->currentModulePath);
        }
    }

    MEMSET(&state->module, 0, sizeof(state->module));
    state->currentModulePath[0] = '\0';
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
    int buildResult = system("sh build.sh debug module");
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
    host_try_build_module(state);

    if (state->buildFailed) {
        return;
    }

    char moduleSourcePath[HOT_MODULE_PATH_MAX];
    host_build_module_path(state, APP_MODULE_SOURCE_RELATIVE, moduleSourcePath, sizeof(moduleSourcePath));
    OS_FileInfo moduleInfo = OS_get_file_info(moduleSourcePath);
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

int host_main_loop(int argc, char** argv) {
    (void)argc;
    (void)argv;

    log_init();
    set_log_level(LogLevel_Info);

    HostState state = {};
    if (!host_allocate_memory(&state)) {
        LOG_ERROR("host", "Failed to allocate app memory");
        thread_context_release();
        return 1;
    }
    LOG_INFO("host", "Allocated program memory (perm={} bytes, trans={} bytes)",
             state.storage.permanentSize, state.storage.transientSize);

    state.moduleGeneration = 0;
    state.currentModulePath[0] = '\0';
    state.moduleBasePath[0] = '\0';
    OS_FileInfo initialSourceInfo = OS_get_file_info(APP_SOURCE_PATH);
    state.sourceTimestamp = initialSourceInfo.exists ? initialSourceInfo.lastWriteTimestampNs : 0;
    state.buildFailed = 0;

    if (!host_ensure_graphics_initialized(&state)) {
        LOG_ERROR("host", "Failed to initialize graphics before loading module");
    }

    if (!host_load_module(&state, 0)) {
        LOG_ERROR("host", "Initial module load failed");
    }

    LOG_INFO("host", "Entering main loop");

    U64 lastTickTime = OS_get_time_microseconds();

    while (!state.hostContext.shouldQuit) {
        U64 now = OS_get_time_microseconds();
        U64 deltaMicro = now - lastTickTime;
        lastTickTime = now;
        F32 deltaSeconds = (F32)((F64) deltaMicro / (F64)MILLION(1ULL));

        host_try_reload_module(&state);
        host_update_input(&state, deltaSeconds);

        if (state.module.isValid && state.module.exports.update) {
            state.module.exports.update(&state.platformAPI, &state.memory, &state.hostContext, &state.input, deltaSeconds);
        } else {
            OS_sleep_milliseconds(100);
        }
        OS_sleep_milliseconds(16);
    }

    host_unload_module(&state, 1, 0);
    if (state.graphicsInitialized) {
        OS_graphics_shutdown();
        state.graphicsInitialized = 0;
    }

    for (U32 i = 0; i < state.retiredModuleCount; ++i) {
        if (state.retiredModuleHandles[i]) {
            dlclose(state.retiredModuleHandles[i]);
            state.retiredModuleHandles[i] = 0;
        }
        if (state.retiredModulePaths[i][0] != '\0') {
            unlink(state.retiredModulePaths[i]);
            state.retiredModulePaths[i][0] = '\0';
        }
    }
    state.retiredModuleCount = 0;

    host_release_memory(&state);
    return 0;
}

