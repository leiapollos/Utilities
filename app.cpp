//
// Created by André Leite on 31/10/2025.
//

#include "nstl/base/base_include.hpp"
#include "nstl/os/os_include.hpp"

#include "app_interface.hpp"
#include "app_tests.hpp"
#include "app_state.hpp"

#include "app_tests.cpp"

#define APP_CORE_STATE_VERSION 3u

static U64 app_total_permanent_size(void) {
    return sizeof(AppCoreState) + app_tests_permanent_size();
}

static AppCoreState* app_get_state(AppMemory* memory) {
    ASSERT_DEBUG(memory != 0);
    ASSERT_DEBUG(memory->permanentStorage != 0);
    ASSERT_DEBUG(memory->permanentStorageSize >= app_total_permanent_size());
    return (AppCoreState*) memory->permanentStorage;
}

static void app_assign_tests_state(AppCoreState* state) {
    if (!state) {
        return;
    }
    U8* base = (U8*) state;
    state->tests = (AppTestsState*) (base + sizeof(AppCoreState));
}

static AppTestsState* app_get_tests(AppCoreState* state) {
    return state ? state->tests : 0;
}

static U32 app_select_worker_count(const AppHostContext* host) {
    U32 logicalCores = 1u;
    if (host && host->logicalCoreCount > 0u) {
        logicalCores = host->logicalCoreCount;
    }
    U32 workers = (logicalCores > 1u) ? (logicalCores - 1u) : 1u;
    return workers;
}

static B32 app_initialize(AppPlatform* platform, AppMemory* memory, AppHostContext* host) {
    if (!memory) {
        return 0;
    }

    AppCoreState* state = app_get_state(memory);
    B32 needsReset = (!memory->isInitialized) || (state->version != APP_CORE_STATE_VERSION);

    if (needsReset) {
        U64 totalSize = app_total_permanent_size();
        totalSize = (totalSize > memory->permanentStorageSize) ? memory->permanentStorageSize : totalSize;
        MEMSET(memory->permanentStorage, 0, totalSize);
        memory->isInitialized = 1;
    }

    state = app_get_state(memory);
    app_assign_tests_state(state);
    AppTestsState* tests = app_get_tests(state);

    thread_context_alloc();
    log_init();
    if (needsReset) {
        state->version = APP_CORE_STATE_VERSION;
        state->desiredWindow.title = "Utilities Hot Reload";
        state->desiredWindow.width = 1280u;
        state->desiredWindow.height = 720u;
        state->windowHandle.handle = 0;
        state->frameCounter = 0;
        state->reloadCount = 0;
        set_log_level(LogLevel_Info);
        StringU8 eventsDomain = str8((const char*)"events", 6);
        set_log_domain_level(eventsDomain, LogLevel_Debug);
    }

    if (!state->jobSystem) {
        state->workerCount = app_select_worker_count(host);
        state->jobSystem = job_system_create(memory->programArena, state->workerCount);
        if (!state->jobSystem) {
            LOG_ERROR("jobs", "Failed to create job system (workers={})", state->workerCount);
            state->workerCount = 0;
        } else {
            LOG_INFO("jobs", "Job system ready (workers={})", state->workerCount);
        }
    }

    if (needsReset) {
        app_tests_initialize(memory, state, tests);
    }

    if (host) {
        host->reloadCount = state->reloadCount;
    }

    if (platform && !state->windowHandle.handle) {
        OS_WindowDesc desc = {};
        desc.title = state->desiredWindow.title;
        desc.width = state->desiredWindow.width;
        desc.height = state->desiredWindow.height;
        state->windowHandle = PLATFORM_OS_CALL(platform, OS_window_create, desc);
    }

    return 1;
}

static void app_reload(AppPlatform* platform, AppMemory* memory, AppHostContext* host) {
    if (!memory) {
        return;
    }

    AppCoreState* state = app_get_state(memory);
    app_assign_tests_state(state);
    AppTestsState* tests = app_get_tests(state);

    state->desiredWindow.title = "Utilities Hot Reload";
    state->reloadCount += 1;
    if (host) {
        host->reloadCount = state->reloadCount;
    }

    if (!state->jobSystem) {
        state->workerCount = app_select_worker_count(host);
        state->jobSystem = job_system_create(memory->programArena, state->workerCount);
        if (state->jobSystem) {
            LOG_INFO("jobs", "Job system re-created on reload (workers={})", state->workerCount);
        } else {
            LOG_ERROR("jobs", "Failed to recreate job system on reload");
        }
    }

    app_tests_reload(memory, state, tests);
}

static void app_update(AppPlatform* platform, AppMemory* memory, AppHostContext* host, const AppInput* input, F32 deltaSeconds) {
    if (!memory || !memory->permanentStorage) {
        return;
    }

    AppCoreState* state = app_get_state(memory);
    AppTestsState* tests = app_get_tests(state);

    state->frameCounter += 1ull;

    if (input) {
        for (U32 eventIndex = 0; eventIndex < input->eventCount; ++eventIndex) {
            const OS_GraphicsEvent* evt = input->events + eventIndex;
            if (!evt) {
                continue;
            }

            switch (evt->type) {
                case OS_GraphicsEventType_WindowShown: {
                    if (!state->windowHandle.handle) {
                        state->windowHandle = evt->window;
                    }
                    if (evt->windowEvent.width != 0u && evt->windowEvent.height != 0u) {
                        state->desiredWindow.width = evt->windowEvent.width;
                        state->desiredWindow.height = evt->windowEvent.height;
                    }
                } break;

                case OS_GraphicsEventType_WindowClosed:
                case OS_GraphicsEventType_WindowDestroyed: {
                    if (state->windowHandle.handle == evt->window.handle) {
                        OS_WindowHandle closedHandle = state->windowHandle;
                        state->windowHandle.handle = 0;
                        if (host) {
                            host->shouldQuit = 1;
                        }
                        if (platform) {
                            PLATFORM_OS_CALL(platform, OS_window_destroy, closedHandle);
                        }
                    }
                } break;

                case OS_GraphicsEventType_MouseMove: {
                    LOG_INFO("app", "Mouse moved to ({}, {})", evt->mouse.x, evt->mouse.y);
                } break;

                case OS_GraphicsEventType_MouseButtonDown:
                case OS_GraphicsEventType_MouseButtonUp:
                case OS_GraphicsEventType_MouseScroll:
                case OS_GraphicsEventType_KeyDown:
                case OS_GraphicsEventType_KeyUp:
                case OS_GraphicsEventType_TextInput:
                default: {
                } break;
            }
        }
    }

    app_tests_tick(memory, state, tests, deltaSeconds);
}

static void app_shutdown(AppPlatform* platform, AppMemory* memory, AppHostContext* host) {
    (void) host;
    if (!memory || !memory->permanentStorage) {
        return;
    }

    AppCoreState* state = app_get_state(memory);
    AppTestsState* tests = app_get_tests(state);

    app_tests_shutdown(memory, state, tests);

    if (state->jobSystem) {
        job_system_destroy(state->jobSystem);
        state->jobSystem = 0;
        state->workerCount = 0;
    }

    if (platform && state->windowHandle.handle) {
        PLATFORM_OS_CALL(platform, OS_window_destroy, state->windowHandle);
        state->windowHandle.handle = 0;
    }
}

APP_MODULE_EXPORT B32 app_get_entry_points(AppModuleExports* outExports) {
    if (!outExports) {
        return 0;
    }

    MEMSET(outExports, 0, sizeof(AppModuleExports));
    outExports->interfaceVersion = APP_INTERFACE_VERSION;
    outExports->requiredPermanentMemory = app_total_permanent_size();
    outExports->requiredTransientMemory = 0;
    outExports->requiredProgramArenaSize = 0;
    outExports->initialize = app_initialize;
    outExports->reload = app_reload;
    outExports->update = app_update;
    outExports->shutdown = app_shutdown;
    return 1;
}
