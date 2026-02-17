//
// Created by André Leite on 02/11/2025.
//

#pragma once

#include "app_interface.hpp"

#include "app/app_camera.hpp"

struct AppTestsState;
struct ArtifactCache;

static const U32 APP_RADIANCE_2D_GRID_WIDTH = 512u;
static const U32 APP_RADIANCE_2D_GRID_HEIGHT = 512u;
static const U32 APP_RADIANCE_2D_MAX_CASCADES = 8u;

enum AppSceneKind {
    AppSceneKind_Sponza = 0,
    AppSceneKind_Radiance2D,
    AppSceneKind_COUNT,
};

struct SponzaRenderUnit {
    U32 meshIndex;
    U32 materialIndex;
    U32 firstIndex;
    U32 indexCount;
    Mat4x4F32 transform;
};

struct SponzaSceneState {
    LoadedScene scene;
    Arena* sceneArena;
    SponzaRenderUnit* renderUnits;
    U32 renderUnitCount;
    GPUSceneData gpuScene;
    B32 sceneLoaded;

    F32 meshScale;
    Vec4F32 meshColor;

    Vec3F32 sceneBoundsCenter;
    F32 sceneBoundsRadius;

    F32 shadowLightAzimuthDeg;
    F32 shadowLightElevationDeg;
    B32 shadowLightAnimate;
    F32 shadowLightAnimateSpeedDegPerSec;
};

struct Radiance2DState {
    U32 gridWidth;
    U32 gridHeight;
    U32 requestedGridResolution;
    B32 applyGridResolution;

    U8* emissivePixels;
    U8* occluderPixels;
    U64 pixelBufferSize;

    LoadedImage emissiveImage;
    LoadedImage occluderImage;

    TextureHandle emissiveTexture;
    TextureHandle occluderTexture;

    B32 initialized;
    B32 emissiveDirty;
    B32 occluderDirty;

    B32 leftMouseDown;
    B32 rightMouseDown;
    F32 mouseX;
    F32 mouseY;

    F32 brushRadius;
    Vec4F32 brushColor;

    U32 cascadeCount;
    U32 raysPerProbeBase;
    U32 maxSteps;
    F32 intensity;
    F32 exposure;
};

struct AppCoreState {
    U32 version;
    AppWindowDesc desiredWindow;
    OS_WindowHandle windowHandle;
    U64 frameCounter;
    U32 reloadCount;

    JobSystem* jobSystem;
    U32 workerCount;
    AppTestsState* tests;
    ArtifactCache* artifactCache;

    Camera camera;

    AppSceneKind activeScene;
    AppSceneKind pendingSceneSwitch;

    SponzaSceneState sponza;
    Radiance2DState radiance2D;
};
