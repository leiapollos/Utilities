//
// Created by André Leite on 10/06/2026.
//

#define APP_SCENE_GRID_SPACING 2.2f

static Mat4x4F32 app_scene_object_transform_(U32 x, U32 z, U32 side, F32 time, B32 animate) {
    F32 half = (F32)(side - 1u) * 0.5f;
    F32 worldX = ((F32)x - half) * APP_SCENE_GRID_SPACING;
    F32 worldZ = ((F32)z - half) * APP_SCENE_GRID_SPACING;
    U32 cellSeed = x * 31u + z * 17u;
    F32 height = 0.5f + (F32)((cellSeed >> 2u) % 5u) * 0.22f;

    // Row-vector convention: compose left to right, scale -> rotate -> translate.
    Mat4x4F32 transform = mat4_scale(app_world_vec3_(1.0f, height, 1.0f));
    if (animate && (cellSeed % 7u) == 0u) {
        QuatF32 spin = quat_from_axis_angle(app_world_vec3_(0.0f, 1.0f, 0.0f),
                                            time * (0.6f + (F32)(cellSeed % 5u) * 0.25f));
        transform = transform * quat_to_mat4(spin);
    }
    transform = transform * mat4_translate(app_world_vec3_(worldX, height * 0.5f, worldZ));
    if (animate && (cellSeed % 7u) == 0u) {
        F32 bob = 0.35f * (0.5f + 0.5f * SIN_F32(time * 1.7f + (F32)cellSeed * 0.61f));
        transform.v[3][1] += bob;
    }
    return transform;
}

struct AppSceneExtractParams {
    AppWorldState* world;
    U32 side;
    F32 time;
    B32 animate;
};

// Per-item extraction body; laneId/laneCount explicit so the single-threaded
// path runs inline with no SPMD group.
static void app_scene_extract_range_(const AppSceneExtractParams* params, U64 laneId, U64 laneCount) {
    AppWorldState* world = params->world;
    AppWorldLaneWriter* writer = world->laneWriters + laneId;
    U32 side = params->side;
    F32 time = params->time;
    B32 animate = params->animate;
    RangeU64 range = spmd_split_range_((U64)side * side, laneId, laneCount);

    for (U64 item = range.min; item < range.max; ++item) {
        U32 x = (U32)(item % side);
        U32 z = (U32)(item / side);
        U32 cellSeed = x * 31u + z * 17u;
        Mat4x4F32 transform = app_scene_object_transform_(x, z, side, time, animate);

        AppWorldMeshHandle mesh = ((cellSeed & 3u) == 0u) ? world->builtinMeshes[1]
                                                          : world->builtinMeshes[0];
        U32 lane = cellSeed % 11u;
        if (lane == 9u && world->assetMeshes[0].generation != 0u) {
            F32 halfSide = (F32)(side - 1u) * 0.5f;
            Mat4x4F32 duckTransform = mat4_scale(app_world_vec3_(0.012f, 0.012f, 0.012f));
            QuatF32 duckSpin = quat_from_axis_angle(app_world_vec3_(0.0f, 1.0f, 0.0f),
                                                    (F32)cellSeed * 0.7f);
            duckTransform = duckTransform * quat_to_mat4(duckSpin);
            duckTransform = duckTransform * mat4_translate(app_world_vec3_(
                ((F32)x - halfSide) * APP_SCENE_GRID_SPACING, 0.0f,
                ((F32)z - halfSide) * APP_SCENE_GRID_SPACING));
            app_world_writer_push_(world, writer, world->assetMeshes[0], 9u, AppWorldBin_Opaque, &duckTransform);
            continue;
        }
        if (lane == 1u && world->assetMeshes[1].generation != 0u) {
            F32 halfSide = (F32)(side - 1u) * 0.5f;
            Mat4x4F32 avocadoTransform = mat4_scale(app_world_vec3_(22.0f, 22.0f, 22.0f));
            QuatF32 avocadoSpin = quat_from_axis_angle(app_world_vec3_(0.0f, 1.0f, 0.0f),
                                                       (F32)cellSeed * 1.3f);
            avocadoTransform = avocadoTransform * quat_to_mat4(avocadoSpin);
            avocadoTransform = avocadoTransform * mat4_translate(app_world_vec3_(
                ((F32)x - halfSide) * APP_SCENE_GRID_SPACING, 0.0f,
                ((F32)z - halfSide) * APP_SCENE_GRID_SPACING));
            app_world_writer_push_(world, writer, world->assetMeshes[1], 10u, AppWorldBin_Opaque, &avocadoTransform);
            continue;
        }
        if (lane == 3u) {
            app_world_writer_push_(world, writer, world->builtinMeshes[0], 6u, AppWorldBin_AlphaTest, &transform);
        } else if (lane == 5u || lane == 7u) {
            app_world_writer_push_(world, writer, mesh, (lane == 5u) ? 7u : 8u, AppWorldBin_Transparent, &transform);
        } else {
            app_world_writer_push_(world, writer, mesh, cellSeed % 6u, AppWorldBin_Opaque, &transform);
        }
    }
}

static void app_scene_extract_kernel_(void* kernelParameters) {
    PROF_SCOPE("scene extract");
    const AppSceneExtractParams* params = (const AppSceneExtractParams*)kernelParameters;
    app_scene_extract_range_(params, spmd_lane_id(), spmd_lane_count());
}

static void app_demo_scene_submit(APP_Context* ctx, AppRendererFrame* rendererFrame) {
    AppCoreState* state = ctx->core;
    AppDemoState* demo = &state->demo;
    AppWorldState* world = &state->world;
    if (!world->frameOpen || world->meshCount == 0u) {
        return;
    }

    F32 time = (F32)((F64)state->frameCounter / 60.0);
    U32 side = CLAMP(demo->gridSide, APP_DEMO_GRID_MIN, APP_DEMO_GRID_MAX);

    F32 orbitRadius = (F32)side * APP_SCENE_GRID_SPACING * 0.62f;
    F32 yaw = time * 0.22f;
    Vec3F32 eye = app_world_vec3_(COS_F32(yaw) * orbitRadius,
                                  orbitRadius * 0.45f,
                                  SIN_F32(yaw) * orbitRadius);
    app_world_set_camera(ctx, eye, app_world_vec3_(0.0f, 0.0f, 0.0f), 1.0472f, 0.1f,
                         orbitRadius * 4.0f);

    Mat4x4F32 groundTransform = mat4_scale(app_world_vec3_((F32)side * APP_SCENE_GRID_SPACING + 8.0f, 1.0f,
                                                            (F32)side * APP_SCENE_GRID_SPACING + 8.0f)) *
                                mat4_translate(app_world_vec3_(0.0f, -0.05f, 0.0f));
    app_world_push(ctx, world->builtinMeshes[2], 5u, AppWorldBin_Opaque, &groundTransform);

    AppSceneExtractParams extractParams = {};
    extractParams.world = world;
    extractParams.side = side;
    extractParams.time = time;
    extractParams.animate = demo->animate;

    U32 itemCount = side * side;
    U32 dispatchLanes = MIN(world->laneCount, MAX(1u, itemCount / 512u));
    if (dispatchLanes > 1u && state->jobSystem) {
        PROF_SCOPE("scene dispatch");
        SPMDGroup* group = spmd_dispatch(state->jobSystem, ctx->host->frameArena,
                                         .laneCount = dispatchLanes,
                                         .kernel = app_scene_extract_kernel_,
                                         .kernelParameters = &extractParams);
        if (group) {
            spmd_destroy_group(group);
        }
    } else {
        app_scene_extract_range_(&extractParams, 0u, 1u);
    }

    if (demo->showBounds) {
        app_debug_draw_world_bounds(ctx, 256u);
    }

    if (state->render2d.textContext != 0 && state->render2d.font.generation != 0u) {
        Temp scratch = get_scratch(0, 0);
        if (scratch.arena) {
            DEFER_REF(temp_end(&scratch));
            TextDrawDesc titleDesc = {};
            titleDesc.font = state->render2d.font;
            titleDesc.text = str8((U8*)demo->titleBuffer, demo->titleLength);
            titleDesc.x = 40.0f;
            titleDesc.y = 28.0f;
            titleDesc.pixelSize = demo->titleSize;
            titleDesc.rgba8 = 0xF4F1E8FFu;
            TextDrawData title = text_prepare_draw(state->render2d.textContext, scratch.arena, &titleDesc);
            app_renderer_submit_text(ctx, rendererFrame, &title, Draw2DLayer_HUD);
        }
    }
}
