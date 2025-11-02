//
// Created by André Leite on 02/11/2025.
//

#pragma once

#include "nstl/base/base_include.hpp"

#define APP_INTERFACE_VERSION 2u

struct AppMemory {
    B32 isInitialized;
    void* permanentStorage;
    U64 permanentStorageSize;
    void* transientStorage;
    U64 transientStorageSize;
    Arena* programArena;
};

struct AppRuntime {
    AppMemory* memory;
};

struct AppModuleExports {
    U32 interfaceVersion;
    U64 requiredPermanentMemory;
    U64 requiredTransientMemory;
    U64 requiredProgramArenaSize;
    B32 (*initialize)(AppRuntime* runtime);
    void (*reload)(AppRuntime* runtime);
    void (*tick)(AppRuntime* runtime, F32 deltaSeconds);
    void (*shutdown)(AppRuntime* runtime);
};

#if defined(__clang__) || defined(__GNUC__)
#define APP_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define APP_MODULE_EXPORT extern "C"
#endif

APP_MODULE_EXPORT B32 app_get_entry_points(AppModuleExports* outExports);

