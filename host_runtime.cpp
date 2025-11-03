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

#define HOST_WINDOW_TITLE_MAX 128

struct HostState {
    LoadedModule module;
    AppMemory memory;
    ProgramMemory storage;
    Arena* programArena;
    AppHostContext hostContext;
    AppPlatform platformAPI;
    AppFrameInput frameInput;
    AppWindowState windowState;
    AppWindowCommand pendingWindowCommand;
    AppRuntime runtime;
    OS_WindowHandle window;
    char windowTitle[HOST_WINDOW_TITLE_MAX];
    OS_WindowDesc windowDesc;
    B32 graphicsInitialized;
    U64 moduleTimestamp;
    U64 moduleGeneration;
    char currentModulePath[HOT_MODULE_PATH_MAX];
    void* retiredModuleHandles[HOT_MODULE_HISTORY_MAX];
    char retiredModulePaths[HOT_MODULE_HISTORY_MAX][HOT_MODULE_PATH_MAX];
    U32 retiredModuleCount;
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
        U64 len = C_STR_LEN(path);
        if (len >= HOT_MODULE_PATH_MAX) {
            len = HOT_MODULE_PATH_MAX - 1;
        }
        MEMMOVE(state->retiredModulePaths[state->retiredModuleCount], path, len);
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
    if (!state) {
        return;
    }

    if (state->window.handle) {
        OS_window_destroy(state->window);
        state->window.handle = 0;
    }

    state->windowState.isOpen = 0;
    state->windowState.width = 0;
    state->windowState.height = 0;
    state->hostContext.windowIsOpen = 0;
}

static void host_reset_window_command(AppWindowCommand* command) {
    if (!command) {
        return;
    }
    MEMSET(command, 0, sizeof(AppWindowCommand));
}

static void host_platform_request_quit_(void* userData) {
    if (!userData) {
        return;
    }

    HostState* state = (HostState*) userData;
    state->hostContext.shouldQuit = 1;
}

static void host_platform_issue_window_command_(void* userData, const AppWindowCommand* command) {
    if (!userData || !command) {
        return;
    }

    HostState* state = (HostState*) userData;
    AppWindowCommand* pending = &state->pendingWindowCommand;

    if (command->requestOpen) {
        pending->requestOpen = 1;
    }
    if (command->requestClose) {
        pending->requestClose = 1;
    }
    if (command->requestFocus) {
        pending->requestFocus = 1;
    }
    if (command->requestSize) {
        pending->requestSize = 1;
        pending->desc.width = command->desc.width;
        pending->desc.height = command->desc.height;
    }
    if (command->requestTitle) {
        pending->requestTitle = 1;
        pending->desc.title = command->desc.title;
    }
}

static void host_apply_window_command(HostState* state, AppWindowCommand* command) {
    if (!state || !command) {
        return;
    }

    if (command->requestSize) {
        state->windowDesc.width = command->desc.width;
        state->windowDesc.height = command->desc.height;
        state->windowState.width = command->desc.width;
        state->windowState.height = command->desc.height;
    }

    if (command->requestTitle) {
        if (command->desc.title) {
            U64 length = C_STR_LEN(command->desc.title);
            if (length >= HOST_WINDOW_TITLE_MAX) {
                length = HOST_WINDOW_TITLE_MAX - 1;
            }
            MEMMOVE(state->windowTitle, command->desc.title, length);
            state->windowTitle[length] = '\0';
        } else {
            state->windowTitle[0] = '\0';
        }
        state->windowDesc.title = state->windowTitle;
    }

    if (command->requestClose) {
        host_destroy_window(state);
    }

    if (command->requestOpen) {
        if (!state->windowState.isOpen) {
            if (!host_ensure_graphics_initialized(state)) {
                LOG_ERROR("host", "Unable to open window: graphics initialization failed");
            } else {
                OS_WindowDesc desc = state->windowDesc;
                if (!desc.title || desc.title[0] == '\0') {
                    desc.title = "Utilities";
                }
                if (desc.width == 0u) {
                    desc.width = 1280u;
                }
                if (desc.height == 0u) {
                    desc.height = 720u;
                }

                state->window = OS_window_create(desc);
                if (!state->window.handle) {
                    LOG_ERROR("host", "Failed to create window {}x{}", desc.width, desc.height);
                } else {
                    if (desc.title != state->windowTitle) {
                        U64 length = C_STR_LEN(desc.title);
                        if (length >= HOST_WINDOW_TITLE_MAX) {
                            length = HOST_WINDOW_TITLE_MAX - 1;
                        }
                        MEMMOVE(state->windowTitle, desc.title, length);
                        state->windowTitle[length] = '\0';
                    }
                    state->windowDesc.title = state->windowTitle;
                    state->windowDesc.width = desc.width;
                    state->windowDesc.height = desc.height;
                    state->windowState.isOpen = 1;
                    state->windowState.width = desc.width;
                    state->windowState.height = desc.height;
                    state->hostContext.windowIsOpen = 1;
                    LOG_INFO("host", "Window opened ({}x{})", desc.width, desc.height);
                }
            }
        }
    }

    if (!state->windowState.isOpen) {
        state->hostContext.windowIsOpen = 0;
    }

    host_reset_window_command(command);
}

static void host_flush_window_command(HostState* state) {
    if (!state) {
        return;
    }

    AppWindowCommand* command = &state->pendingWindowCommand;
    if (!command->requestOpen && !command->requestClose &&
        !command->requestSize && !command->requestTitle && !command->requestFocus) {
        return;
    }

    host_apply_window_command(state, command);
}

static void host_update_frame_input(HostState* state) {
    if (!state) {
        return;
    }

    MEMSET(&state->frameInput, 0, sizeof(state->frameInput));

    if (!state->graphicsInitialized) {
        state->windowState.isOpen = 0;
        state->hostContext.windowIsOpen = 0;
        return;
    }

    OS_graphics_pump_events();

    OS_GraphicsEvent events[32];
    U32 eventCount = OS_graphics_poll_events(events, ARRAY_COUNT(events));
    for (U32 index = 0; index < eventCount; ++index) {
        OS_GraphicsEvent* evt = &events[index];
        if (state->window.handle && evt->window.handle != state->window.handle) {
            continue;
        }

        switch (evt->type) {
            case OS_GraphicsEventType_WindowShown: {
                state->windowState.isOpen = 1;
                state->hostContext.windowIsOpen = 1;
                state->frameInput.windowResized = 1;
                state->frameInput.newWidth = evt->windowEvent.width;
                state->frameInput.newHeight = evt->windowEvent.height;
            } break;

            case OS_GraphicsEventType_WindowClosed:
            case OS_GraphicsEventType_WindowDestroyed: {
                state->frameInput.windowCloseRequested = 1;
                host_destroy_window(state);
            } break;

            case OS_GraphicsEventType_MouseMove: {
                state->frameInput.mouseMoved = 1;
                state->frameInput.mouseX = evt->mouse.x;
                state->frameInput.mouseY = evt->mouse.y;
            } break;

            default: {
            } break;
        }
    }

    if (state->window.handle) {
        if (!OS_window_is_open(state->window)) {
            host_destroy_window(state);
        } else {
            state->windowState.isOpen = 1;
            state->hostContext.windowIsOpen = 1;
            if (state->windowDesc.width != 0u) {
                state->windowState.width = state->windowDesc.width;
            }
            if (state->windowDesc.height != 0u) {
                state->windowState.height = state->windowDesc.height;
            }
        }
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
    state->memory.platform = &state->platformAPI;
    state->memory.hostContext = &state->hostContext;
    state->memory.frameInput = &state->frameInput;
    state->memory.windowState = &state->windowState;

    MEMSET(&state->hostContext, 0, sizeof(state->hostContext));
    state->hostContext.userData = state;
    OS_SystemInfo* sysInfo = OS_get_system_info();
    state->hostContext.logicalCoreCount = sysInfo ? sysInfo->logicalCores : 1u;

    state->platformAPI.userData = state;
    state->platformAPI.issue_window_command = host_platform_issue_window_command_;
    state->platformAPI.request_quit = host_platform_request_quit_;

    MEMSET(&state->frameInput, 0, sizeof(state->frameInput));
    MEMSET(&state->windowState, 0, sizeof(state->windowState));
    host_reset_window_command(&state->pendingWindowCommand);

    state->window.handle = 0;
    MEMSET(state->windowTitle, 0, sizeof(state->windowTitle));
    state->windowDesc.title = state->windowTitle;
    state->windowDesc.width = 0;
    state->windowDesc.height = 0;
    state->graphicsInitialized = 0;

    state->runtime.memory = &state->memory;
    state->runtime.platform = &state->platformAPI;
    state->runtime.host = &state->hostContext;
    state->runtime.input = &state->frameInput;
    state->runtime.window = &state->windowState;

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
        host_flush_window_command(state);
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

    host_flush_window_command(&state);

    LOG_INFO("host", "Entering main loop");

    U64 lastTickTime = OS_get_time_microseconds();

    while (!state.hostContext.shouldQuit) {
        host_try_reload_module(&state);
        host_flush_window_command(&state);
        host_update_frame_input(&state);

        if (state.module.isValid && state.module.exports.tick) {
            U64 now = OS_get_time_microseconds();
            U64 deltaMicro = now - lastTickTime;
            lastTickTime = now;
            F32 deltaSeconds = (F32)((F64) deltaMicro / (F64)MILLION(1ULL));
            state.module.exports.tick(&state.runtime, deltaSeconds);
        } else {
            OS_sleep_milliseconds(100);
        }

        host_flush_window_command(&state);
        OS_sleep_milliseconds(16);
    }

    host_unload_module(&state, 1, 0);
    host_flush_window_command(&state);
    host_destroy_window(&state);

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

    thread_context_release();
    return 0;
}

