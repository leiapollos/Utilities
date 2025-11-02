//
// Created by André Leite on 02/11/2025.
//

#pragma once

#include "nstl/base/base_typedefs.hpp"

#define APP_INTERFACE_VERSION 2u

typedef struct AppMemory {
    B32 isInitialized;
    void* permanentStorage;
    U64 permanentStorageSize;
    void* transientStorage;
    U64 transientStorageSize;
} AppMemory;

typedef struct AppModuleExports {
    U32 interfaceVersion;
    U64 requiredPermanentMemory;
    U64 requiredTransientMemory;
    B32 (*initialize)(AppMemory* memory);
    void (*reload)(AppMemory* memory);
    void (*tick)(AppMemory* memory, F32 deltaSeconds);
    void (*shutdown)(AppMemory* memory);
} AppModuleExports;

#if defined(__clang__) || defined(__GNUC__)
#define APP_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define APP_MODULE_EXPORT extern "C"
#endif

APP_MODULE_EXPORT B32 app_get_entry_points(AppModuleExports* outExports);

