//
// Created by André Leite on 02/11/2025.
//

#include "nstl/base/base_include.hpp"
#include "nstl/base/base_log.hpp"
#include "nstl/os/os_include.hpp"
#include "app_interface.hpp"

#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define APP_MODULE_SOURCE_PATH "hot/utilities_app.dylib"
#define HOT_MODULE_NAME_PATTERN "hot/utilities_app_loaded_%llu.dylib"
#define HOT_MODULE_PATH_MAX 256

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

struct HostState {
    LoadedModule module;
    AppMemory memory;
    ProgramMemory storage;
    Arena* programArena;
    AppRuntime runtime;
    B32 shouldQuit;
    U64 moduleTimestamp;
    U64 moduleGeneration;
    char currentModulePath[HOT_MODULE_PATH_MAX];
};

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

    state->runtime.memory = &state->memory;

    return 1;
}

static B32 host_copy_file(const char* srcPath, const char* dstPath) {
    const U64 chunkSize = KB(64);
    U8 buffer[KB(64)];

    OS_Handle src = OS_file_open(srcPath, OS_FileOpenMode_Read);
    if (!src.handle) {
        LOG_ERROR("host", "Failed to open module '{}' for reading", srcPath);
        return 0;
    }

    OS_Handle dst = OS_file_open(dstPath, OS_FileOpenMode_Create);
    if (!dst.handle) {
        LOG_ERROR("host", "Failed to open module copy '{}' for writing", dstPath);
        OS_file_close(src);
        return 0;
    }

    U64 size = OS_file_size(src);
    U64 offset = 0;
    B32 ok = 1;

    while (offset < size) {
        U64 remaining = size - offset;
        U64 toTransfer = (remaining > chunkSize) ? chunkSize : remaining;
        RangeU64 range;
        range.min = offset;
        range.max = offset + toTransfer;

        U64 readBytes = OS_file_read(src, range, buffer);
        if (readBytes != toTransfer) {
            LOG_ERROR("host", "Short read when copying module ({} vs {})", readBytes, toTransfer);
            ok = 0;
            break;
        }

        U64 written = OS_file_write(dst, range, buffer);
        if (written != toTransfer) {
            LOG_ERROR("host", "Short write when copying module ({} vs {})", written, toTransfer);
            ok = 0;
            break;
        }

        offset += toTransfer;
    }

    OS_file_close(dst);
    OS_file_close(src);

    return ok;
}

static B32 host_load_module(HostState* state, B32 isReload) {
    if (!state) {
        return 0;
    }

    const char* sourcePath = APP_MODULE_SOURCE_PATH;
    const U32 maxAttempts = 100;
    B32 sourceReady = 0;
    for (U32 attempt = 0; attempt < maxAttempts; ++attempt) {
        OS_Handle handle = OS_file_open(sourcePath, OS_FileOpenMode_Read);
        if (handle.handle) {
            OS_file_close(handle);
            sourceReady = 1;
            break;
        }
        OS_sleep_milliseconds(10);
    }

    if (!sourceReady) {
        LOG_ERROR("host", "Module '{}' not available", sourcePath);
        return 0;
    }

    state->moduleGeneration += 1;
    char loadPath[HOT_MODULE_PATH_MAX];
    MEMSET(loadPath, 0, sizeof(loadPath));
    int written = snprintf(loadPath, sizeof(loadPath), HOT_MODULE_NAME_PATTERN, (unsigned long long) state->moduleGeneration);
    if (written < 0 || written >= (int) sizeof(loadPath)) {
        LOG_ERROR("host", "Failed to build module load path (%d)", written);
        return 0;
    }

    if (!host_copy_file(sourcePath, loadPath)) {
        return 0;
    }

    void* handle = dlopen(loadPath, RTLD_NOW | RTLD_LOCAL);
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
    state->moduleTimestamp = host_get_file_timestamp(sourcePath);

    U64 pathLen = C_STR_LEN(loadPath);
    if (pathLen >= HOT_MODULE_PATH_MAX) {
        pathLen = HOT_MODULE_PATH_MAX - 1;
    }
    MEMMOVE(state->currentModulePath, loadPath, pathLen);
    state->currentModulePath[pathLen] = '\0';

    if (!isReload) {
        if (state->module.exports.initialize) {
            B32 initOk = state->module.exports.initialize(&state->runtime);
            if (!initOk) {
                LOG_ERROR("host", "Module initialize() reported failure");
                state->module.isValid = 0;
                dlclose(handle);
                return 0;
            }
        }
    } else {
        if (state->module.exports.reload) {
            state->module.exports.reload(&state->runtime);
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
        state->module.exports.shutdown(&state->runtime);
    }

    if (retainHandle) {
    } else {
        dlclose(state->module.handle);
        if (state->currentModulePath[0] != '\0') {
            unlink(state->currentModulePath);
        }
    }

    MEMSET(&state->module, 0, sizeof(state->module));
    state->currentModulePath[0] = '\0';
}

static void host_try_reload_module(HostState* state) {
    U64 timestamp = host_get_file_timestamp(APP_MODULE_SOURCE_PATH);
    if (timestamp == 0) {
        return;
    }

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

    thread_context_alloc();

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

    if (!host_load_module(&state, 0)) {
        LOG_ERROR("host", "Initial module load failed");
    }

    LOG_INFO("host", "Entering main loop");

    U64 lastTickTime = OS_get_time_microseconds();

    while (!state.shouldQuit) {
        host_try_reload_module(&state);

        if (state.module.isValid && state.module.exports.tick) {
            U64 now = OS_get_time_microseconds();
            U64 deltaMicro = now - lastTickTime;
            lastTickTime = now;
            F32 deltaSeconds = (F32)((F64) deltaMicro / (F64)MILLION(1ULL));
            state.module.exports.tick(&state.runtime, deltaSeconds);
        } else {
            OS_sleep_milliseconds(100);
        }

        OS_sleep_milliseconds(16);
    }

    host_unload_module(&state, 1, 0);
    host_release_memory(&state);

    thread_context_release();
    return 0;
}

