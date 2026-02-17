static F32 app_scene_sponza_wrap_degrees_(F32 degrees) {
    while (degrees >= 180.0f) {
        degrees -= 360.0f;
    }
    while (degrees < -180.0f) {
        degrees += 360.0f;
    }
    return degrees;
}

static Vec3F32 app_scene_sponza_direction_from_angles_(F32 azimuthDeg, F32 elevationDeg) {
    F32 azimuthRad = DEG_TO_RAD(azimuthDeg);
    F32 elevationRad = DEG_TO_RAD(elevationDeg);
    F32 cosElevation = COS_F32(elevationRad);

    Vec3F32 dir = {{
        cosElevation * COS_F32(azimuthRad),
        SIN_F32(elevationRad),
        cosElevation * SIN_F32(azimuthRad),
    }};
    return vec3_normalize(dir);
}

static Vec3F32 app_scene_sponza_transform_point_(Mat4x4F32 transform, Vec3F32 point) {
    Vec4F32 p4 = {{point.x, point.y, point.z, 1.0f}};
    Vec4F32 transformed = transform * p4;
    F32 invW = 1.0f;
    if (ABS_F32(transformed.w) > 0.000001f) {
        invW = 1.0f / transformed.w;
    }

    Vec3F32 result = {{
        transformed.x * invW,
        transformed.y * invW,
        transformed.z * invW,
    }};
    return result;
}

static F32 app_scene_sponza_max_basis_scale_(Mat4x4F32 transform) {
    F32 sx = SQRT_F32(transform.v[0][0] * transform.v[0][0]
                      + transform.v[1][0] * transform.v[1][0]
                      + transform.v[2][0] * transform.v[2][0]);
    F32 sy = SQRT_F32(transform.v[0][1] * transform.v[0][1]
                      + transform.v[1][1] * transform.v[1][1]
                      + transform.v[2][1] * transform.v[2][1]);
    F32 sz = SQRT_F32(transform.v[0][2] * transform.v[0][2]
                      + transform.v[1][2] * transform.v[1][2]
                      + transform.v[2][2] * transform.v[2][2]);
    return MAX(sx, MAX(sy, sz));
}

static void app_scene_sponza_compute_scene_bounds_(const SponzaSceneState* sceneState, Vec3F32* outCenter, F32* outRadius) {
    ASSERT_ALWAYS(sceneState != 0);
    ASSERT_ALWAYS(outCenter != 0);
    ASSERT_ALWAYS(outRadius != 0);

    Vec3F32 minPos = {{1000000000.0f, 1000000000.0f, 1000000000.0f}};
    Vec3F32 maxPos = {{-1000000000.0f, -1000000000.0f, -1000000000.0f}};
    B32 foundBounds = 0;

    for (U32 nodeIndex = 0; nodeIndex < sceneState->scene.nodeCount; ++nodeIndex) {
        const SceneNode* node = &sceneState->scene.nodes[nodeIndex];
        if (node->meshIndex < 0 || (U32) node->meshIndex >= sceneState->scene.meshCount) {
            continue;
        }

        const MeshAssetData* mesh = &sceneState->scene.meshes[node->meshIndex];
        if (mesh->surfaceCount == 0u) {
            continue;
        }

        F32 maxScale = app_scene_sponza_max_basis_scale_(node->worldTransform);
        for (U32 surfaceIndex = 0; surfaceIndex < mesh->surfaceCount; ++surfaceIndex) {
            const MeshSurface* surface = &mesh->surfaces[surfaceIndex];
            Vec3F32 centerWS = app_scene_sponza_transform_point_(node->worldTransform, surface->bounds.origin);
            F32 radiusWS = surface->bounds.sphereRadius * maxScale;

            if (!foundBounds) {
                minPos = {{centerWS.x - radiusWS, centerWS.y - radiusWS, centerWS.z - radiusWS}};
                maxPos = {{centerWS.x + radiusWS, centerWS.y + radiusWS, centerWS.z + radiusWS}};
                foundBounds = 1;
            } else {
                minPos.x = MIN(minPos.x, centerWS.x - radiusWS);
                minPos.y = MIN(minPos.y, centerWS.y - radiusWS);
                minPos.z = MIN(minPos.z, centerWS.z - radiusWS);
                maxPos.x = MAX(maxPos.x, centerWS.x + radiusWS);
                maxPos.y = MAX(maxPos.y, centerWS.y + radiusWS);
                maxPos.z = MAX(maxPos.z, centerWS.z + radiusWS);
            }
        }
    }

    if (!foundBounds) {
        *outCenter = {{0.0f, 0.0f, 0.0f}};
        *outRadius = 1.0f;
        return;
    }

    Vec3F32 center = {{
        (minPos.x + maxPos.x) * 0.5f,
        (minPos.y + maxPos.y) * 0.5f,
        (minPos.z + maxPos.z) * 0.5f,
    }};
    Vec3F32 extents = {{
        (maxPos.x - minPos.x) * 0.5f,
        (maxPos.y - minPos.y) * 0.5f,
        (maxPos.z - minPos.z) * 0.5f,
    }};
    F32 radius = SQRT_F32(extents.x * extents.x + extents.y * extents.y + extents.z * extents.z);

    *outCenter = center;
    *outRadius = MAX(radius, 0.25f);
}

static void app_scene_sponza_release_cpu_images_(SponzaSceneState* sceneState) {
    ASSERT_ALWAYS(sceneState != 0);
    if (!sceneState->scene.images || sceneState->scene.imageCount == 0u) {
        return;
    }

    for (U32 i = 0; i < sceneState->scene.imageCount; ++i) {
        image_free(&sceneState->scene.images[i]);
    }
}

static void app_scene_sponza_release_cpu_scene_(SponzaSceneState* sceneState) {
    ASSERT_ALWAYS(sceneState != 0);

    if (sceneState->sceneArena) {
        arena_release(sceneState->sceneArena);
        sceneState->sceneArena = 0;
    }

    MEMSET(&sceneState->scene, 0, sizeof(sceneState->scene));
}

static B32 app_scene_sponza_build_render_units_(Arena* arena, const LoadedScene* scene,
                                                SponzaRenderUnit** outRenderUnits, U32* outRenderUnitCount) {
    ASSERT_ALWAYS(arena != 0);
    ASSERT_ALWAYS(scene != 0);
    ASSERT_ALWAYS(outRenderUnits != 0);
    ASSERT_ALWAYS(outRenderUnitCount != 0);

    *outRenderUnits = 0;
    *outRenderUnitCount = 0;

    U32 totalRenderUnits = 0;
    for (U32 nodeIndex = 0; nodeIndex < scene->nodeCount; ++nodeIndex) {
        const SceneNode* node = &scene->nodes[nodeIndex];
        if (node->meshIndex < 0 || (U32)node->meshIndex >= scene->meshCount) {
            continue;
        }

        const MeshAssetData* mesh = &scene->meshes[node->meshIndex];
        totalRenderUnits += (mesh->surfaceCount > 0u) ? mesh->surfaceCount : 1u;
    }

    if (totalRenderUnits == 0u) {
        return 1;
    }

    SponzaRenderUnit* renderUnits = ARENA_PUSH_ARRAY(arena, SponzaRenderUnit, totalRenderUnits);
    if (!renderUnits) {
        return 0;
    }

    U32 renderUnitCount = 0;
    for (U32 nodeIndex = 0; nodeIndex < scene->nodeCount; ++nodeIndex) {
        const SceneNode* node = &scene->nodes[nodeIndex];
        if (node->meshIndex < 0 || (U32)node->meshIndex >= scene->meshCount) {
            continue;
        }

        const MeshAssetData* mesh = &scene->meshes[node->meshIndex];
        if (mesh->surfaceCount == 0u) {
            SponzaRenderUnit* unit = &renderUnits[renderUnitCount++];
            unit->meshIndex = (U32)node->meshIndex;
            unit->materialIndex = (U32)-1;
            unit->firstIndex = 0u;
            unit->indexCount = 0u;
            unit->transform = node->worldTransform;
        } else {
            for (U32 surfaceIndex = 0; surfaceIndex < mesh->surfaceCount; ++surfaceIndex) {
                const MeshSurface* surface = &mesh->surfaces[surfaceIndex];
                SponzaRenderUnit* unit = &renderUnits[renderUnitCount++];
                unit->meshIndex = (U32)node->meshIndex;
                unit->materialIndex = surface->materialIndex;
                unit->firstIndex = surface->startIndex;
                unit->indexCount = surface->count;
                unit->transform = node->worldTransform;
            }
        }
    }

    *outRenderUnits = renderUnits;
    *outRenderUnitCount = renderUnitCount;
    return 1;
}

static void app_scene_sponza_init(AppPlatform* platform, AppMemory* memory, AppHostContext* host, AppCoreState* state) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(memory != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(host->renderer != 0);
    ASSERT_ALWAYS(state != 0);

    SponzaSceneState* sceneState = &state->sponza;
    if (sceneState->sceneLoaded) {
        return;
    }

    sceneState->sceneArena = arena_alloc();
    if (!sceneState->sceneArena) {
        LOG_ERROR("app", "Failed to allocate temporary scene arena");
        return;
    }

    if (scene_load_from_file(sceneState->sceneArena, "assets/sponza.glb", &sceneState->scene)) {
        LOG_INFO("app", "Scene loaded: {} meshes, {} materials, {} nodes, {} images",
                 sceneState->scene.meshCount,
                 sceneState->scene.materialCount,
                 sceneState->scene.nodeCount,
                 sceneState->scene.imageCount);

        if (PLATFORM_RENDERER_CALL(platform,
                                   renderer_upload_scene,
                                   host->renderer,
                                   memory->programArena,
                                   &sceneState->scene,
                                   &sceneState->gpuScene)) {
            app_scene_sponza_release_cpu_images_(sceneState);
            if (!app_scene_sponza_build_render_units_(memory->programArena,
                                                      &sceneState->scene,
                                                      &sceneState->renderUnits,
                                                      &sceneState->renderUnitCount)) {
                LOG_ERROR("app", "Failed to allocate Sponza render units");
                PLATFORM_RENDERER_CALL(platform, renderer_destroy_scene, host->renderer, &sceneState->gpuScene);
                app_scene_sponza_release_cpu_scene_(sceneState);
                return;
            }

            app_scene_sponza_compute_scene_bounds_(sceneState, &sceneState->sceneBoundsCenter, &sceneState->sceneBoundsRadius);
            sceneState->sceneLoaded = 1;
            sceneState->meshScale = 0.01f;
            sceneState->meshColor = {{1.0f, 1.0f, 1.0f, 1.0f}};
            sceneState->shadowLightAzimuthDeg = 90.0f;
            sceneState->shadowLightElevationDeg = 63.4349f;
            sceneState->shadowLightAnimate = 1;
            sceneState->shadowLightAnimateSpeedDegPerSec = 45.0f;

            app_scene_sponza_release_cpu_scene_(sceneState);
        } else {
            app_scene_sponza_release_cpu_images_(sceneState);
            app_scene_sponza_release_cpu_scene_(sceneState);
            LOG_ERROR("app", "Failed to upload scene to GPU");
        }
    } else {
        app_scene_sponza_release_cpu_scene_(sceneState);
        LOG_ERROR("app", "Failed to load Sponza scene");
    }
}

static void app_scene_sponza_shutdown(AppPlatform* platform, AppHostContext* host, AppCoreState* state) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(host->renderer != 0);
    ASSERT_ALWAYS(state != 0);

    SponzaSceneState* sceneState = &state->sponza;
    if (sceneState->sceneLoaded) {
        PLATFORM_RENDERER_CALL(platform, renderer_destroy_scene, host->renderer, &sceneState->gpuScene);
        sceneState->sceneLoaded = 0;
    }
    app_scene_sponza_release_cpu_scene_(sceneState);
    sceneState->renderUnits = 0;
    sceneState->renderUnitCount = 0;
}

static void app_scene_sponza_on_enter(AppCoreState* state) {
    ASSERT_ALWAYS(state != 0);
}

static void app_scene_sponza_handle_event(const OS_GraphicsEvent* evt, AppCoreState* state, B32 imguiWantCaptureMouse) {
    ASSERT_ALWAYS(evt != 0);
    ASSERT_ALWAYS(state != 0);

    switch (evt->tag) {
        case OS_GraphicsEvent_Tag_MouseMove: {
            if (!imguiWantCaptureMouse) {
                F32 deltaPitch = -evt->mouseMove.deltaY * state->camera.sensitivity;
                F32 deltaYaw = evt->mouseMove.deltaX * state->camera.sensitivity;
                camera_rotate(&state->camera, deltaPitch, deltaYaw);
            }
        }
        break;

        case OS_GraphicsEvent_Tag_KeyDown: {
            OS_KeyCode key = evt->keyDown.keyCode;
            if (key == OS_KeyCode_W) {
                state->camera.velocity.z = -1.0f;
            } else if (key == OS_KeyCode_S) {
                state->camera.velocity.z = 1.0f;
            } else if (key == OS_KeyCode_A) {
                state->camera.velocity.x = -1.0f;
            } else if (key == OS_KeyCode_D) {
                state->camera.velocity.x = 1.0f;
            }
        }
        break;

        case OS_GraphicsEvent_Tag_KeyUp: {
            OS_KeyCode key = evt->keyUp.keyCode;
            if (key == OS_KeyCode_W || key == OS_KeyCode_S) {
                state->camera.velocity.z = 0.0f;
            } else if (key == OS_KeyCode_A || key == OS_KeyCode_D) {
                state->camera.velocity.x = 0.0f;
            }
        }
        break;

        default: {
        }
        break;
    }
}

static void app_scene_sponza_build_ui(AppCoreState* state) {
    ASSERT_ALWAYS(state != 0);

    SponzaSceneState* sceneState = &state->sponza;

    ImGui::SeparatorText("Sponza");
    ImGui::SliderFloat("Mesh Scale", &sceneState->meshScale, 0.001f, 1.0f, "%.3f");
    ImGui::ColorEdit4("Mesh Color", sceneState->meshColor.v);

    ImGui::SeparatorText("Shadow Debug");
    ImGui::SliderFloat("Light Azimuth", &sceneState->shadowLightAzimuthDeg, -180.0f, 180.0f, "%.1f deg");
    ImGui::SliderFloat("Light Elevation", &sceneState->shadowLightElevationDeg, -89.0f, 89.0f, "%.1f deg");
    bool shadowLightAnimate = sceneState->shadowLightAnimate != 0;
    if (ImGui::Checkbox("Animate Light Azimuth", &shadowLightAnimate)) {
        sceneState->shadowLightAnimate = shadowLightAnimate ? 1 : 0;
    }
    ImGui::SliderFloat("Animation Speed", &sceneState->shadowLightAnimateSpeedDegPerSec, -360.0f, 360.0f, "%.1f deg/s");

    ImGui::SeparatorText("Camera");
    ImGui::Text("Position: (%.2f, %.2f, %.2f)",
                (double) state->camera.position.x,
                (double) state->camera.position.y,
                (double) state->camera.position.z);
    QuatF32 q = state->camera.orientation;
    ImGui::Text("Orientation: (%.3f, %.3f, %.3f, %.3f)", (double) q.x, (double) q.y, (double) q.z, (double) q.w);
    ImGui::SliderFloat("Sensitivity", &state->camera.sensitivity, 0.001f, 0.02f, "%.4f");
    ImGui::SliderFloat("Move Speed", &state->camera.moveSpeed, 0.001f, 2.0f, "%.3f");
}

static void app_scene_sponza_render(AppPlatform* platform, AppHostContext* host, AppCoreState* state, F32 deltaSeconds) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(host->renderer != 0);
    ASSERT_ALWAYS(state != 0);

    SponzaSceneState* sceneState = &state->sponza;

    camera_update(&state->camera, deltaSeconds);

    if (state->desiredWindow.width == 0u || state->desiredWindow.height == 0u) {
        return;
    }

    F32 fovY = DEG_TO_RAD(70.0f);
    F32 aspect = (F32) state->desiredWindow.width / (F32) state->desiredWindow.height;
    F32 zNear = 0.001f;
    F32 zFar = 100.0f;

    Mat4x4F32 projection = mat4_perspective(fovY, aspect, zNear, zFar);
    Mat4x4F32 view = camera_get_view_matrix(&state->camera);

    SceneData scene = {};
    scene.view = view;
    scene.proj = projection;
    scene.viewproj = view * projection;

    if (sceneState->shadowLightAnimate) {
        sceneState->shadowLightAzimuthDeg += sceneState->shadowLightAnimateSpeedDegPerSec * deltaSeconds;
    }
    sceneState->shadowLightAzimuthDeg = app_scene_sponza_wrap_degrees_(sceneState->shadowLightAzimuthDeg);
    sceneState->shadowLightElevationDeg = CLAMP(sceneState->shadowLightElevationDeg, -89.0f, 89.0f);
    Vec3F32 sunDir = app_scene_sponza_direction_from_angles_(sceneState->shadowLightAzimuthDeg,
                                                              sceneState->shadowLightElevationDeg);

    scene.ambientColor.r = 0.1f;
    scene.ambientColor.g = 0.1f;
    scene.ambientColor.b = 0.1f;
    scene.ambientColor.a = 1.0f;
    scene.sunDirection.x = sunDir.x;
    scene.sunDirection.y = sunDir.y;
    scene.sunDirection.z = sunDir.z;
    scene.sunDirection.w = 0.0f;
    scene.sunColor.r = 1.0f;
    scene.sunColor.g = 1.0f;
    scene.sunColor.b = 1.0f;
    scene.sunColor.a = 1.0f;

    {
        Vec3F32 sceneCenter = {{
            sceneState->sceneBoundsCenter.x * sceneState->meshScale,
            sceneState->sceneBoundsCenter.y * sceneState->meshScale,
            sceneState->sceneBoundsCenter.z * sceneState->meshScale,
        }};
        F32 sceneRadius = MAX(sceneState->sceneBoundsRadius * sceneState->meshScale, 0.25f);
        F32 orthoSize = sceneRadius * 1.25f;
        F32 lightDistance = MAX(sceneRadius * 2.5f, 2.0f);
        F32 lightNear = MAX(lightDistance - sceneRadius * 2.0f, 0.01f);
        F32 lightFar = lightDistance + sceneRadius * 2.0f;
        if (lightFar <= lightNear + 0.01f) {
            lightFar = lightNear + 0.01f;
        }

        Vec3F32 lightPos = {{sceneCenter.x + sunDir.x * lightDistance,
                             sceneCenter.y + sunDir.y * lightDistance,
                             sceneCenter.z + sunDir.z * lightDistance}};
        Vec3F32 up = {{0.0f, 1.0f, 0.0f}};
        if (ABS_F32(vec3_dot(sunDir, up)) > 0.99f) {
            up = {{0.0f, 0.0f, 1.0f}};
        }
        Mat4x4F32 lightView = mat4_look_at(lightPos, sceneCenter, up);
        Mat4x4F32 lightProj = mat4_ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, lightNear, lightFar);
        scene.lightSpaceMatrix = lightView * lightProj;
    }

    RenderObject* renderObjects = 0;
    U32 renderObjectCount = 0;

    if (sceneState->sceneLoaded && sceneState->renderUnitCount > 0u) {
        renderObjects = ARENA_PUSH_ARRAY(host->frameArena, RenderObject, sceneState->renderUnitCount);
        if (renderObjects != 0) {
            F32 scale = sceneState->meshScale;
            Mat4x4F32 scaleMatrix = mat4_identity();
            scaleMatrix.v[0][0] = scale;
            scaleMatrix.v[1][1] = scale;
            scaleMatrix.v[2][2] = scale;

            for (U32 i = 0; i < sceneState->renderUnitCount; ++i) {
                const SponzaRenderUnit* unit = &sceneState->renderUnits[i];
                if (unit->meshIndex >= sceneState->gpuScene.meshCount) {
                    continue;
                }

                RenderObject* obj = &renderObjects[renderObjectCount++];
                obj->mesh = sceneState->gpuScene.meshes[unit->meshIndex];
                obj->transform = scaleMatrix * unit->transform;
                obj->color = sceneState->meshColor;
                obj->firstIndex = unit->firstIndex;
                obj->indexCount = unit->indexCount;
                obj->material = MATERIAL_HANDLE_INVALID;
                if (unit->materialIndex < sceneState->gpuScene.materialCount) {
                    obj->material = sceneState->gpuScene.materials[unit->materialIndex];
                }
            }
        }
    }

    RendererFrameBeginDesc beginDesc = renderer_frame_begin_desc(state->windowHandle, &scene);
    if (PLATFORM_RENDERER_CALL(platform, renderer_begin_frame, host->renderer, &beginDesc)) {
        RendererSubmitDesc submitDesc = renderer_submit_desc(renderObjects, renderObjectCount);
        PLATFORM_RENDERER_CALL(platform, renderer_submit, host->renderer, &submitDesc);
        RendererEndFrameDesc endDesc = renderer_end_frame_desc();
        PLATFORM_RENDERER_CALL(platform, renderer_end_frame, host->renderer, &endDesc);
    }
}
