//
// Created by André Leite on 31/10/2025.
//

#include "nstl/base/base_include.hpp"
#include "nstl/os/os_include.hpp"

#include "app_interface.hpp"
#include "app_tests.hpp"
#include "app_state.hpp"

#include "app_tests.cpp"

#define APP_CORE_STATE_VERSION 2u

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

static U32 app_select_worker_count(const AppRuntime* runtime) {
    U32 logicalCores = 1u;
    if (runtime && runtime->host && runtime->host->logicalCoreCount > 0u) {
        logicalCores = runtime->host->logicalCoreCount;
    }
    U32 workers = (logicalCores > 1u) ? (logicalCores - 1u) : 1u;
    return workers;
}

static B32 app_initialize(AppRuntime* runtime) {
    if (!runtime || !runtime->memory) {
        return 0;
    }

    AppMemory* memory = runtime->memory;
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
        state->keepWindowVisible = 1;
        state->frameCounter = 0;
        state->reloadCount = 0;
        set_log_level(LogLevel_Info);
        StringU8 eventsDomain = str8((const char*)"events", 6);
        set_log_domain_level(eventsDomain, LogLevel_Debug);
    }

    if (!state->jobSystem) {
        state->workerCount = app_select_worker_count(runtime);
        state->jobSystem = job_system_create(memory->programArena, state->workerCount);
        if (!state->jobSystem) {
            LOG_ERROR("jobs", "Failed to create job system (workers={})", state->workerCount);
            state->workerCount = 0;
        } else {
            LOG_INFO("jobs", "Job system ready (workers={})", state->workerCount);
        }
    }

    if (needsReset) {
        app_tests_initialize(runtime, state, tests);
    }

    state->keepWindowVisible = 1;
    if (memory->platform && memory->platform->issue_window_command) {
        AppWindowCommand command = {};
        command.requestOpen = 1;
        command.desc = state->desiredWindow;
        if (command.desc.width != 0u || command.desc.height != 0u) {
            command.requestSize = 1;
        }
        if (command.desc.title) {
            command.requestTitle = 1;
        }
        command.requestFocus = 1;
        memory->platform->issue_window_command(memory->platform->userData, &command);
    }
    return 1;
}

static void app_reload(AppRuntime* runtime) {
    if (!runtime || !runtime->memory) {
        return;
    }

    AppCoreState* state = app_get_state(runtime->memory);
    app_assign_tests_state(state);
    AppTestsState* tests = app_get_tests(state);

    state->desiredWindow.title = "Utilities Hot Reload";
    state->reloadCount += 1;
    if (runtime->memory->hostContext) {
        runtime->memory->hostContext->reloadCount = state->reloadCount;
    }

    if (!state->jobSystem) {
        state->workerCount = app_select_worker_count(runtime);
        state->jobSystem = job_system_create(runtime->memory->programArena, state->workerCount);
        if (state->jobSystem) {
            LOG_INFO("jobs", "Job system re-created on reload (workers={})", state->workerCount);
        } else {
            LOG_ERROR("jobs", "Failed to recreate job system on reload");
        }
    }

    app_tests_reload(runtime, state, tests);
    if (runtime->memory->platform && runtime->memory->platform->issue_window_command) {
        AppWindowCommand command = {};
        command.requestTitle = 1;
        command.desc.title = state->desiredWindow.title;
        runtime->memory->platform->issue_window_command(runtime->memory->platform->userData, &command);
    }
}

static void app_tick(AppRuntime* runtime, F32 deltaSeconds) {
    if (!runtime || !runtime->memory || !runtime->memory->permanentStorage) {
        return;
    }

    AppCoreState* state = app_get_state(runtime->memory);
    AppTestsState* tests = app_get_tests(state);

    state->frameCounter += 1ull;
    
    if (runtime->input && runtime->input->mouseMoved) {
        LOG_INFO("app", "Mouse moved to ({}, {})", runtime->input->mouseX, runtime->input->mouseY);
    }
    
    app_tests_tick(runtime, state, tests, deltaSeconds);
}

static void app_shutdown(AppRuntime* runtime) {
    if (!runtime || !runtime->memory || !runtime->memory->permanentStorage) {
        return;
    }

    AppCoreState* state = app_get_state(runtime->memory);
    AppTestsState* tests = app_get_tests(state);

    app_tests_shutdown(runtime, state, tests);

    if (state->jobSystem) {
        job_system_destroy(state->jobSystem);
        state->jobSystem = 0;
        state->workerCount = 0;
    }

    if (state->keepWindowVisible && runtime->memory->platform && runtime->memory->platform->issue_window_command) {
        AppWindowCommand command = {};
        command.requestClose = 1;
        runtime->memory->platform->issue_window_command(runtime->memory->platform->userData, &command);
        state->keepWindowVisible = 0;
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
    outExports->tick = app_tick;
    outExports->shutdown = app_shutdown;
    return 1;
}
