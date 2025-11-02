//
// Created by André Leite on 02/11/2025.
//

#pragma once

#include "nstl/base/base_include.hpp"

#define APP_INTERFACE_VERSION 2u

struct AppHostContext {
    B32 shouldQuit;
    U32 reloadCount;
    B32 windowIsOpen;
    void* userData;
    U32 logicalCoreCount;
};

struct AppFrameInput {
    B32 windowCloseRequested;
    B32 windowResized;
    U32 newWidth;
    U32 newHeight;
    B32 mouseMoved;
    F32 mouseX;
    F32 mouseY;
};

struct AppWindowDesc {
    U32 width;
    U32 height;
    const char* title;
};

struct AppWindowState {
    B32 isOpen;
    U32 width;
    U32 height;
};

struct AppWindowCommand {
    B32 requestOpen;
    B32 requestClose;
    B32 requestFocus;
    B32 requestSize;
    B32 requestTitle;
    AppWindowDesc desc;
};

struct AppPlatform {
    void* userData;
    void (*issue_window_command)(void* userData, const AppWindowCommand* command);
    void (*request_quit)(void* userData);
};

struct AppMemory {
    B32 isInitialized;
    void* permanentStorage;
    U64 permanentStorageSize;
    void* transientStorage;
    U64 transientStorageSize;
    Arena* programArena;
    AppPlatform* platform;
    AppHostContext* hostContext;
    const AppFrameInput* frameInput;
    AppWindowState* windowState;
};

struct AppRuntime {
    AppMemory* memory;
    AppPlatform* platform;
    AppHostContext* host;
    const AppFrameInput* input;
    AppWindowState* window;
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

