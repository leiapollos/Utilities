//
// Created by André Leite on 02/11/2025.
//

#pragma once

#include "app_interface.hpp"

struct AppCoreState {
    U32 version;
    AppWindowDesc desiredWindow;
    U64 frameCounter;
    U32 reloadCount;
    B32 keepWindowVisible;
    B32 mouseMovedLogged;
    F32 lastMouseX;
    F32 lastMouseY;

    JobSystem* jobSystem;
    U32 workerCount;
};

