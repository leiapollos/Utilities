//
// Created by André Leite on 02/11/2025.
//

#pragma once

#include "app_interface.hpp"

struct AppTestsState;

struct AppCoreState {
    U32 version;
    AppWindowDesc desiredWindow;
    OS_WindowHandle windowHandle;
    U64 frameCounter;
    U32 reloadCount;

    JobSystem* jobSystem;
    U32 workerCount;
    AppTestsState* tests;

    MeshHandle meshHandle;
    F32 meshScale;
    F32 meshAlpha;
    F32 meshSpacing;
    S32 meshCount;
    B32 meshLoaded;
};
