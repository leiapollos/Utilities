//
// Created by André Leite on 31/10/2025.
//

#include "nstl/base/base_include.hpp"
#include "nstl/base/base_log.hpp"
#include "app_interface.hpp"

#define APP_PERMANENT_STORAGE_SIZE MB(64)

static U64 app_total_permanent_size(void) {
    return APP_PERMANENT_STORAGE_SIZE;
}

static B32 app_initialize(AppMemory* memory) {
    if (!memory || !memory->permanentStorage) {
        return 0;
    }

    if (!memory->isInitialized) {
        MEMSET(memory->permanentStorage, 0, memory->permanentStorageSize);
        memory->isInitialized = 1;
        LOG_INFO("app", "App initialized");
    }

    return 1;
}

static void app_tick(AppMemory* memory, F32 deltaSeconds) {
    (void)memory;
    (void)deltaSeconds;
}

static void app_shutdown(AppMemory* memory) {
    (void)memory;
    LOG_INFO("app", "App shutdown");
}

APP_MODULE_EXPORT B32 app_get_entry_points(AppModuleExports* outExports) {
    if (!outExports) {
        return 0;
    }

    MEMSET(outExports, 0, sizeof(AppModuleExports));
    outExports->interfaceVersion = APP_INTERFACE_VERSION;
    outExports->requiredPermanentMemory = app_total_permanent_size();
    outExports->requiredTransientMemory = 0;
    outExports->initialize = app_initialize;
    outExports->tick = app_tick;
    outExports->shutdown = app_shutdown;
    return 1;
}

