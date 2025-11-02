//
// Created by André Leite on 02/11/2025.
//

#include "nstl/base/base_include.hpp"
#include "nstl/base/base_log.hpp"
#include "nstl/os/os_include.hpp"
#include "app_interface.hpp"

#include <dlfcn.h>
#include <sys/stat.h>

#define APP_MODULE_SOURCE_PATH "hot/utilities_app.dylib"

typedef B32 (*AppGetEntryPointsProc)(AppModuleExports* outExports);

typedef struct LoadedModule {
    void* handle;
    AppModuleExports exports;
    B32 isValid;
} LoadedModule;

typedef struct HostState {
    LoadedModule module;
    AppMemory memory;
    void* permanentStorage;
    U64 permanentStorageSize;
    B32 shouldQuit;
    U64 moduleTimestamp;
} HostState;

static U64 host_get_file_timestamp(const char* path) {
    struct stat fileStat;
    if (stat(path, &fileStat) != 0) {
        return 0;
    }

#if defined(PLATFORM_OS_MACOS)
    U64 seconds = (U64) fileStat.st_mtimespec.tv_sec;
    U64 nanos = (U64) fileStat.st_mtimespec.tv_nsec;
#elif defined(PLATFORM_OS_LINUX)
    U64 seconds = (U64) fileStat.st_mtim.tv_sec;
    U64 nanos = (U64) fileStat.st_mtim.tv_nsec;
#else
    U64 seconds = (U64) fileStat.st_mtime;
    U64 nanos = 0;
#endif

    return seconds * 1000000000ull + nanos;
}

static B32 host_load_module(HostState* state) {
    if (!state) {
        return 0;
    }

    const char* sourcePath = APP_MODULE_SOURCE_PATH;
    void* handle = dlopen(sourcePath, RTLD_NOW | RTLD_LOCAL);
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

    if (exports.requiredPermanentMemory > state->permanentStorageSize) {
        LOG_ERROR("host", "Module permanent memory requirement too large ({} > {})", exports.requiredPermanentMemory, state->permanentStorageSize);
        dlclose(handle);
        return 0;
    }

    state->module.handle = handle;
    state->module.exports = exports;
    state->module.isValid = 1;
    state->moduleTimestamp = host_get_file_timestamp(sourcePath);

    state->memory.isInitialized = 0;
    state->memory.permanentStorage = state->permanentStorage;
    state->memory.permanentStorageSize = state->permanentStorageSize;
    state->memory.transientStorage = 0;
    state->memory.transientStorageSize = 0;

    if (state->module.exports.initialize) {
        B32 initOk = state->module.exports.initialize(&state->memory);
        if (!initOk) {
            LOG_ERROR("host", "Module initialize() reported failure");
            state->module.isValid = 0;
            dlclose(handle);
            return 0;
        }
    }

    LOG_INFO("host", "Module loaded from '{}'", sourcePath);
    return 1;
}

static void host_unload_module(HostState* state) {
    if (!state || !state->module.handle) {
        return;
    }

    if (state->module.isValid && state->module.exports.shutdown) {
        state->module.exports.shutdown(&state->memory);
    }

    dlclose(state->module.handle);
    MEMSET(&state->module, 0, sizeof(state->module));
}

int host_main_loop(int argc, char** argv) {
    (void)argc;
    (void)argv;

    thread_context_alloc();

    log_init();
    set_log_level(LogLevel_Info);

    HostState state = {};
    state.permanentStorageSize = MB(64);
    state.permanentStorage = OS_reserve(state.permanentStorageSize);
    if (!state.permanentStorage) {
        LOG_ERROR("host", "Failed to reserve permanent storage");
        thread_context_release();
        return 1;
    }
    if (!OS_commit(state.permanentStorage, state.permanentStorageSize)) {
        LOG_ERROR("host", "Failed to commit permanent storage");
        OS_release(state.permanentStorage, state.permanentStorageSize);
        thread_context_release();
        return 1;
    }
    MEMSET(state.permanentStorage, 0, state.permanentStorageSize);

    if (!host_load_module(&state)) {
        LOG_ERROR("host", "Initial module load failed");
        OS_release(state.permanentStorage, state.permanentStorageSize);
        thread_context_release();
        return 1;
    }

    LOG_INFO("host", "Entering main loop");

    U64 lastTickTime = OS_get_time_microseconds();

    while (!state.shouldQuit) {
        U64 currentTimestamp = host_get_file_timestamp(APP_MODULE_SOURCE_PATH);
        if (currentTimestamp > 0 && currentTimestamp > state.moduleTimestamp) {
            LOG_INFO("host", "Detected module change ({} -> {}), reloading", state.moduleTimestamp, currentTimestamp);
            state.moduleTimestamp = currentTimestamp;
        }

        if (state.module.isValid && state.module.exports.tick) {
            U64 now = OS_get_time_microseconds();
            U64 deltaMicro = now - lastTickTime;
            lastTickTime = now;
            F32 deltaSeconds = (F32)((F64) deltaMicro / (F64)MILLION(1ULL));
            state.module.exports.tick(&state.memory, deltaSeconds);
        } else {
            OS_sleep_milliseconds(100);
        }

        OS_sleep_milliseconds(16);
    }

    host_unload_module(&state);
    OS_release(state.permanentStorage, state.permanentStorageSize);

    thread_context_release();
    return 0;
}

