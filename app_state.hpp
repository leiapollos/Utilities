//
// Created by André Leite on 02/11/2025.
//

#pragma once

#include "app_interface.hpp"
#include "nstl/artifact/artifact_include.hpp"

#define APP_STATE_ID(a, b, c, d) ((((U64)(a)) << 56u) | (((U64)(b)) << 48u) | (((U64)(c)) << 40u) | (((U64)(d)) << 32u) | 0x53544154u)

enum APP_StateKind {
    APP_State_Core = 0,
    APP_State_COUNT,
};

struct APP_StateDesc {
    U64 id;
    const char* name;
    U32 version;
    U64 size;
    U64 alignment;
};

struct AppCoreState {
    U32 windowWidth;
    U32 windowHeight;
    U64 frameCounter;
    U32 reloadCount;

    JobSystem* jobSystem;
    U32 workerCount;

    B32 gfxDemoInitialized;
    Arena* resourceArena;
    ArtifactCache* resourceCache;
    ArtifactHandle gfxTriangleShader;
    GfxBuffer gfxTriangleVertexBuffer;
    GfxBuffer gfxTriangleIndexBuffer;
    GfxPipeline gfxTrianglePipeline;
};

struct APP_Context {
    AppHost* host;
    HOT_StateStore* store;
    AppCoreState* core;
};
