static U8 app_scene_radiance_2d_color_channel_to_u8_(F32 value) {
    F32 clamped = CLAMP(value, 0.0f, 1.0f);
    return (U8) (clamped * 255.0f);
}

static B32 app_scene_radiance_2d_calc_buffer_size_(U32 resolution, U64* outSize) {
    ASSERT_ALWAYS(outSize != 0);
    if (resolution == 0u) {
        return 0;
    }

    U64 res64 = (U64) resolution;
    U64 maxFactor = ((U64) -1) / 4ull;
    if (res64 > 0ull && res64 > (maxFactor / res64)) {
        return 0;
    }

    *outSize = res64 * res64 * 4ull;
    return 1;
}

static B32 app_scene_radiance_2d_recreate_grid_(AppPlatform* platform,
                                                AppHostContext* host,
                                                AppCoreState* state,
                                                U32 requestedResolution) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(host->renderer != 0);
    ASSERT_ALWAYS(state != 0);

    Radiance2DState* radiance = &state->radiance2D;

    U32 resolution = (requestedResolution > 0u) ? requestedResolution : 1u;
    U64 newBufferSize = 0;
    if (!app_scene_radiance_2d_calc_buffer_size_(resolution, &newBufferSize)) {
        LOG_ERROR("app", "Radiance2D resolution {} is too large", resolution);
        return 0;
    }

    U8* newEmissivePixels = (U8*) PLATFORM_OS_CALL(platform, OS_reserve, newBufferSize);
    if (newEmissivePixels == 0) {
        LOG_ERROR("app", "Failed to reserve Radiance2D emissive buffer ({} bytes)", newBufferSize);
        return 0;
    }
    if (!PLATFORM_OS_CALL(platform, OS_commit, newEmissivePixels, newBufferSize)) {
        PLATFORM_OS_CALL(platform, OS_release, newEmissivePixels, newBufferSize);
        LOG_ERROR("app", "Failed to commit Radiance2D emissive buffer ({} bytes)", newBufferSize);
        return 0;
    }

    U8* newOccluderPixels = (U8*) PLATFORM_OS_CALL(platform, OS_reserve, newBufferSize);
    if (newOccluderPixels == 0) {
        PLATFORM_OS_CALL(platform, OS_release, newEmissivePixels, newBufferSize);
        LOG_ERROR("app", "Failed to reserve Radiance2D occluder buffer ({} bytes)", newBufferSize);
        return 0;
    }
    if (!PLATFORM_OS_CALL(platform, OS_commit, newOccluderPixels, newBufferSize)) {
        PLATFORM_OS_CALL(platform, OS_release, newOccluderPixels, newBufferSize);
        PLATFORM_OS_CALL(platform, OS_release, newEmissivePixels, newBufferSize);
        LOG_ERROR("app", "Failed to commit Radiance2D occluder buffer ({} bytes)", newBufferSize);
        return 0;
    }

    MEMSET(newEmissivePixels, 0, newBufferSize);
    MEMSET(newOccluderPixels, 0, newBufferSize);

    LoadedImage newEmissiveImage = {};
    newEmissiveImage.pixels = newEmissivePixels;
    newEmissiveImage.width = resolution;
    newEmissiveImage.height = resolution;
    newEmissiveImage.channels = 4u;

    LoadedImage newOccluderImage = {};
    newOccluderImage.pixels = newOccluderPixels;
    newOccluderImage.width = resolution;
    newOccluderImage.height = resolution;
    newOccluderImage.channels = 4u;

    TextureHandle newEmissiveTexture = PLATFORM_RENDERER_CALL(platform,
                                                              renderer_upload_texture,
                                                              host->renderer,
                                                              &newEmissiveImage);
    if (TEXTURE_HANDLE_IS_INVALID(newEmissiveTexture)) {
        PLATFORM_OS_CALL(platform, OS_release, newOccluderPixels, newBufferSize);
        PLATFORM_OS_CALL(platform, OS_release, newEmissivePixels, newBufferSize);
        LOG_ERROR("app", "Failed to upload Radiance2D emissive texture for {}x{}", resolution, resolution);
        return 0;
    }

    TextureHandle newOccluderTexture = PLATFORM_RENDERER_CALL(platform,
                                                              renderer_upload_texture,
                                                              host->renderer,
                                                              &newOccluderImage);
    if (TEXTURE_HANDLE_IS_INVALID(newOccluderTexture)) {
        PLATFORM_RENDERER_CALL(platform, renderer_destroy_texture, host->renderer, newEmissiveTexture);
        PLATFORM_OS_CALL(platform, OS_release, newOccluderPixels, newBufferSize);
        PLATFORM_OS_CALL(platform, OS_release, newEmissivePixels, newBufferSize);
        LOG_ERROR("app", "Failed to upload Radiance2D occluder texture for {}x{}", resolution, resolution);
        return 0;
    }

    TextureHandle oldEmissiveTexture = radiance->emissiveTexture;
    TextureHandle oldOccluderTexture = radiance->occluderTexture;
    U8* oldEmissivePixels = radiance->emissivePixels;
    U8* oldOccluderPixels = radiance->occluderPixels;
    U64 oldBufferSize = radiance->pixelBufferSize;

    radiance->gridWidth = resolution;
    radiance->gridHeight = resolution;
    radiance->emissivePixels = newEmissivePixels;
    radiance->occluderPixels = newOccluderPixels;
    radiance->pixelBufferSize = newBufferSize;
    radiance->emissiveImage = newEmissiveImage;
    radiance->occluderImage = newOccluderImage;
    radiance->emissiveTexture = newEmissiveTexture;
    radiance->occluderTexture = newOccluderTexture;
    radiance->emissiveDirty = 0;
    radiance->occluderDirty = 0;
    radiance->leftMouseDown = 0;
    radiance->rightMouseDown = 0;

    if (TEXTURE_HANDLE_IS_VALID(oldEmissiveTexture)) {
        PLATFORM_RENDERER_CALL(platform, renderer_destroy_texture, host->renderer, oldEmissiveTexture);
    }
    if (TEXTURE_HANDLE_IS_VALID(oldOccluderTexture)) {
        PLATFORM_RENDERER_CALL(platform, renderer_destroy_texture, host->renderer, oldOccluderTexture);
    }

    if (oldEmissivePixels != 0 && oldBufferSize > 0ull) {
        PLATFORM_OS_CALL(platform, OS_release, oldEmissivePixels, oldBufferSize);
    }
    if (oldOccluderPixels != 0 && oldBufferSize > 0ull) {
        PLATFORM_OS_CALL(platform, OS_release, oldOccluderPixels, oldBufferSize);
    }

    return 1;
}

static B32 app_scene_radiance_2d_window_to_grid_(const AppCoreState* state, F32 mouseX, F32 mouseY,
                                                 S32* outX, S32* outY) {
    ASSERT_ALWAYS(state != 0);
    ASSERT_ALWAYS(outX != 0);
    ASSERT_ALWAYS(outY != 0);

    const Radiance2DState* radiance = &state->radiance2D;
    if (state->desiredWindow.width == 0u || state->desiredWindow.height == 0u) {
        return 0;
    }

    F32 normX = mouseX / (F32) state->desiredWindow.width;
    F32 normY = 1.0f - (mouseY / (F32) state->desiredWindow.height);

    normX = CLAMP(normX, 0.0f, 0.999999f);
    normY = CLAMP(normY, 0.0f, 0.999999f);

    *outX = (S32) (normX * (F32) radiance->gridWidth);
    *outY = (S32) (normY * (F32) radiance->gridHeight);
    return 1;
}

static void app_scene_radiance_2d_paint_(AppCoreState* state, F32 mouseX, F32 mouseY, B32 paintEmissive,
                                         B32 paintOccluder) {
    ASSERT_ALWAYS(state != 0);

    Radiance2DState* radiance = &state->radiance2D;
    if (!radiance->initialized) {
        return;
    }

    S32 centerX = 0;
    S32 centerY = 0;
    if (!app_scene_radiance_2d_window_to_grid_(state, mouseX, mouseY, &centerX, &centerY)) {
        return;
    }

    S32 radius = (S32) CLAMP(radiance->brushRadius, 1.0f, 64.0f);
    S32 minX = CLAMP(centerX - radius, 0, (S32) radiance->gridWidth - 1);
    S32 maxX = CLAMP(centerX + radius, 0, (S32) radiance->gridWidth - 1);
    S32 minY = CLAMP(centerY - radius, 0, (S32) radiance->gridHeight - 1);
    S32 maxY = CLAMP(centerY + radius, 0, (S32) radiance->gridHeight - 1);
    S32 radiusSq = radius * radius;

    U8 emissiveR = app_scene_radiance_2d_color_channel_to_u8_(radiance->brushColor.r);
    U8 emissiveG = app_scene_radiance_2d_color_channel_to_u8_(radiance->brushColor.g);
    U8 emissiveB = app_scene_radiance_2d_color_channel_to_u8_(radiance->brushColor.b);
    U8 emissiveA = app_scene_radiance_2d_color_channel_to_u8_(radiance->brushColor.a);

    for (S32 y = minY; y <= maxY; ++y) {
        for (S32 x = minX; x <= maxX; ++x) {
            S32 dx = x - centerX;
            S32 dy = y - centerY;
            if ((dx * dx + dy * dy) > radiusSq) {
                continue;
            }

            U32 pixelIndex = ((U32) y * radiance->gridWidth + (U32) x) * 4u;
            if (paintEmissive) {
                radiance->emissivePixels[pixelIndex + 0u] = emissiveR;
                radiance->emissivePixels[pixelIndex + 1u] = emissiveG;
                radiance->emissivePixels[pixelIndex + 2u] = emissiveB;
                radiance->emissivePixels[pixelIndex + 3u] = emissiveA;
                radiance->emissiveDirty = 1;
            }
            if (paintOccluder) {
                radiance->occluderPixels[pixelIndex + 0u] = 0u;
                radiance->occluderPixels[pixelIndex + 1u] = 0u;
                radiance->occluderPixels[pixelIndex + 2u] = 0u;
                radiance->occluderPixels[pixelIndex + 3u] = 255u;
                radiance->occluderDirty = 1;
            }
        }
    }
}

static void app_scene_radiance_2d_init(AppPlatform* platform, AppMemory* memory, AppHostContext* host,
                                       AppCoreState* state) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(memory != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(host->renderer != 0);
    ASSERT_ALWAYS(state != 0);

    Radiance2DState* radiance = &state->radiance2D;
    if (radiance->initialized) {
        return;
    }

    radiance->gridWidth = APP_RADIANCE_2D_GRID_WIDTH;
    radiance->gridHeight = APP_RADIANCE_2D_GRID_HEIGHT;
    radiance->requestedGridResolution = APP_RADIANCE_2D_GRID_WIDTH;
    radiance->applyGridResolution = 0;

    radiance->emissiveTexture = TEXTURE_HANDLE_INVALID;
    radiance->occluderTexture = TEXTURE_HANDLE_INVALID;

    if (!app_scene_radiance_2d_recreate_grid_(platform, host, state, radiance->gridWidth)) {
        LOG_ERROR("app", "Failed to initialize Radiance2D resources");
        return;
    }

    radiance->brushRadius = 4.0f;
    radiance->brushColor = {{1.0f, 0.8f, 0.3f, 1.0f}};
    radiance->cascadeCount = 5u;
    radiance->raysPerProbeBase = 12u;
    radiance->maxSteps = 8u;
    radiance->intensity = 0.25f;
    radiance->exposure = 0.3f;

    radiance->emissiveDirty = 0;
    radiance->occluderDirty = 0;
    radiance->leftMouseDown = 0;
    radiance->rightMouseDown = 0;

    radiance->initialized = 1;
}

static void app_scene_radiance_2d_shutdown(AppPlatform* platform, AppHostContext* host, AppCoreState* state) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(host->renderer != 0);
    ASSERT_ALWAYS(state != 0);

    Radiance2DState* radiance = &state->radiance2D;

    if (TEXTURE_HANDLE_IS_VALID(radiance->emissiveTexture)) {
        PLATFORM_RENDERER_CALL(platform, renderer_destroy_texture, host->renderer, radiance->emissiveTexture);
        radiance->emissiveTexture = TEXTURE_HANDLE_INVALID;
    }

    if (TEXTURE_HANDLE_IS_VALID(radiance->occluderTexture)) {
        PLATFORM_RENDERER_CALL(platform, renderer_destroy_texture, host->renderer, radiance->occluderTexture);
        radiance->occluderTexture = TEXTURE_HANDLE_INVALID;
    }

    if (radiance->emissivePixels != 0 && radiance->pixelBufferSize > 0ull) {
        PLATFORM_OS_CALL(platform, OS_release, radiance->emissivePixels, radiance->pixelBufferSize);
        radiance->emissivePixels = 0;
    }

    if (radiance->occluderPixels != 0 && radiance->pixelBufferSize > 0ull) {
        PLATFORM_OS_CALL(platform, OS_release, radiance->occluderPixels, radiance->pixelBufferSize);
        radiance->occluderPixels = 0;
    }

    radiance->pixelBufferSize = 0ull;
    radiance->initialized = 0;
}

static void app_scene_radiance_2d_on_enter(AppCoreState* state) {
    ASSERT_ALWAYS(state != 0);
    state->camera.velocity = {{0.0f, 0.0f, 0.0f}};
}

static void app_scene_radiance_2d_handle_event(const OS_GraphicsEvent* evt, AppCoreState* state,
                                               B32 imguiWantCaptureMouse) {
    ASSERT_ALWAYS(evt != 0);
    ASSERT_ALWAYS(state != 0);

    Radiance2DState* radiance = &state->radiance2D;
    if (!radiance->initialized) {
        return;
    }

    switch (evt->tag) {
        case OS_GraphicsEvent_Tag_MouseMove: {
            radiance->mouseX = evt->mouseMove.x;
            radiance->mouseY = evt->mouseMove.y;
            if (!imguiWantCaptureMouse) {
                if (radiance->leftMouseDown) {
                    B32 paintOccluder = ((evt->mouseMove.modifiers & OS_KeyModifiers_Control) != 0u) ? 1 : 0;
                    B32 paintEmissive = paintOccluder ? 0 : 1;
                    app_scene_radiance_2d_paint_(state,
                                                 radiance->mouseX,
                                                 radiance->mouseY,
                                                 paintEmissive,
                                                 paintOccluder);
                }
                if (radiance->rightMouseDown) {
                    app_scene_radiance_2d_paint_(state, radiance->mouseX, radiance->mouseY, 0, 1);
                }
            }
        }
        break;

        case OS_GraphicsEvent_Tag_MouseButtonDown: {
            radiance->mouseX = evt->mouseButtonDown.x;
            radiance->mouseY = evt->mouseButtonDown.y;
            if (evt->mouseButtonDown.button == OS_MouseButton_Left) {
                radiance->leftMouseDown = 1;
                if (!imguiWantCaptureMouse) {
                    B32 paintOccluder = ((evt->mouseButtonDown.modifiers & OS_KeyModifiers_Control) != 0u) ? 1 : 0;
                    B32 paintEmissive = paintOccluder ? 0 : 1;
                    app_scene_radiance_2d_paint_(state,
                                                 radiance->mouseX,
                                                 radiance->mouseY,
                                                 paintEmissive,
                                                 paintOccluder);
                }
            } else if (evt->mouseButtonDown.button == OS_MouseButton_Right) {
                radiance->rightMouseDown = 1;
                if (!imguiWantCaptureMouse) {
                    app_scene_radiance_2d_paint_(state, radiance->mouseX, radiance->mouseY, 0, 1);
                }
            }
        }
        break;

        case OS_GraphicsEvent_Tag_MouseButtonUp: {
            if (evt->mouseButtonUp.button == OS_MouseButton_Left) {
                radiance->leftMouseDown = 0;
            } else if (evt->mouseButtonUp.button == OS_MouseButton_Right) {
                radiance->rightMouseDown = 0;
            }
        }
        break;

        default: {
        }
        break;
    }
}

static void app_scene_radiance_2d_build_ui(AppCoreState* state) {
    ASSERT_ALWAYS(state != 0);

    Radiance2DState* radiance = &state->radiance2D;

    ImGui::SeparatorText("Radiance 2D");
    ImGui::Text("Paint emissive: LMB");
    ImGui::Text("Paint occluders: RMB or Ctrl+LMB");

    int requestedGridResolution = (int) radiance->requestedGridResolution;
    ImGui::InputInt("Grid Resolution", &requestedGridResolution);
    requestedGridResolution = MAX(requestedGridResolution, 1);
    radiance->requestedGridResolution = (U32) requestedGridResolution;

    if (ImGui::Button("Apply Grid Resolution")) {
        radiance->applyGridResolution = 1;
    }
    ImGui::SameLine();
    ImGui::Text("Current: %ux%u", radiance->gridWidth, radiance->gridHeight);

    ImGui::ColorEdit4("Brush Color", radiance->brushColor.v);
    ImGui::SliderFloat("Brush Radius", &radiance->brushRadius, 1.0f, 32.0f, "%.1f");

    if (ImGui::Button("Clear Emissive")) {
        U64 size = (U64) radiance->gridWidth * (U64) radiance->gridHeight * 4u;
        MEMSET(radiance->emissivePixels, 0, size);
        radiance->emissiveDirty = 1;
    }

    if (ImGui::Button("Clear Occluders")) {
        U64 size = (U64) radiance->gridWidth * (U64) radiance->gridHeight * 4u;
        MEMSET(radiance->occluderPixels, 0, size);
        radiance->occluderDirty = 1;
    }

    int cascadeCount = (int) radiance->cascadeCount;
    int raysPerProbeBase = (int) radiance->raysPerProbeBase;
    int maxSteps = (int) radiance->maxSteps;

    ImGui::InputInt("Cascade Count", &cascadeCount);
    ImGui::InputInt("Rays/Probe Base", &raysPerProbeBase);
    ImGui::InputInt("Max Steps", &maxSteps);

    cascadeCount = CLAMP(cascadeCount, 1, (int) APP_RADIANCE_2D_MAX_CASCADES);
    raysPerProbeBase = CLAMP(raysPerProbeBase, 1, 64);
    maxSteps = CLAMP(maxSteps, 1, 256);

    radiance->cascadeCount = (U32) cascadeCount;
    radiance->raysPerProbeBase = (U32) raysPerProbeBase;
    radiance->maxSteps = (U32) maxSteps;

    ImGui::SliderFloat("GI Intensity", &radiance->intensity, 0.0f, 4.0f, "%.2f");
    ImGui::SliderFloat("Exposure", &radiance->exposure, 0.1f, 4.0f, "%.2f");
}

static void app_scene_radiance_2d_render(AppPlatform* platform, AppHostContext* host, AppCoreState* state,
                                         F32 deltaSeconds) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(host->renderer != 0);
    ASSERT_ALWAYS(state != 0);
    (void) deltaSeconds;

    Radiance2DState* radiance = &state->radiance2D;
    if (!radiance->initialized) {
        return;
    }

    if (radiance->applyGridResolution) {
        app_scene_radiance_2d_recreate_grid_(platform, host, state, radiance->requestedGridResolution);
        radiance->applyGridResolution = 0;
    }

    if (radiance->emissiveDirty) {
        if (PLATFORM_RENDERER_CALL(platform,
                                   renderer_update_texture,
                                   host->renderer,
                                   radiance->emissiveTexture,
                                   &radiance->emissiveImage)) {
            radiance->emissiveDirty = 0;
        }
    }

    if (radiance->occluderDirty) {
        if (PLATFORM_RENDERER_CALL(platform,
                                   renderer_update_texture,
                                   host->renderer,
                                   radiance->occluderTexture,
                                   &radiance->occluderImage)) {
            radiance->occluderDirty = 0;
        }
    }

    RendererRadiance2DDesc radianceDesc = renderer_radiance_2d_desc(radiance->emissiveTexture,
                                                                     radiance->occluderTexture,
                                                                     radiance->gridWidth,
                                                                     radiance->gridHeight,
                                                                     radiance->cascadeCount,
                                                                     radiance->raysPerProbeBase,
                                                                     radiance->maxSteps,
                                                                     radiance->intensity,
                                                                     radiance->exposure);

    RendererFrameBeginDesc beginDesc = renderer_frame_begin_desc(state->windowHandle, 0);
    if (PLATFORM_RENDERER_CALL(platform, renderer_begin_frame, host->renderer, &beginDesc)) {
        PLATFORM_RENDERER_CALL(platform, renderer_submit_radiance_2d, host->renderer, &radianceDesc);
        RendererEndFrameDesc endDesc = renderer_end_frame_desc();
        PLATFORM_RENDERER_CALL(platform, renderer_end_frame, host->renderer, &endDesc);
    }
}
