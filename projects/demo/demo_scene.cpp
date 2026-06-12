//
// Created by André Leite on 10/06/2026.
//

// Cell geometry comes from the shared classifier (demo_scene_kernels.hpp)
// so the rendered grid and the collision world can never drift; only the
// render-side dressing (spin, bob) lives here, gated on the cell's
// animate eligibility so ghosts and colliders stay honest.
// Last-tick contact normals as world-space whiskers: foot of the line at
// the contact point on the sphere surface, tip one unit along the normal.
static void demo_debug_draw_contacts_(EngContext* ctx, const DemoTickStats* stats) {
    EngState* state = ctx->engine;
    EngWorldState* world = &state->world;
    if (!world->frameOpen) {
        return;
    }
    F32 windowWidth = (F32)state->windowWidth;
    F32 windowHeight = (F32)state->windowHeight;
    U32 count = MIN(stats->contactCount, DEMO_GAME_RESOLVE_MAX_ITERATIONS);
    for (U32 at = 0u; at < count; ++at) {
        Vec3F32 point = stats->contactPoints[at];
        Vec3F32 normal = stats->contactNormals[at];
        F32 footX;
        F32 footY;
        F32 tipX;
        F32 tipY;
        if (!eng_debug_project_point_(&world->frameRecord, windowWidth, windowHeight,
                                      point.x, point.y, point.z, &footX, &footY) ||
            !eng_debug_project_point_(&world->frameRecord, windowWidth, windowHeight,
                                      point.x + normal.x, point.y + normal.y, point.z + normal.z,
                                      &tipX, &tipY)) {
            continue;
        }
        debug_draw_line(ctx, footX, footY, tipX, tipY, 2.0f, 0xE8843AFFu);
        debug_draw_box(ctx, footX - 3.0f, footY - 3.0f, footX + 3.0f, footY + 3.0f, 1.0f, 0xE8843AFFu);
    }
}


static Mat4x4F32 demo_scene_cell_transform_(const DemoSceneCell* cell, F32 time, B32 animate) {
    B32 animated = animate && cell->animateEligible;
    // Row-vector convention: compose left to right, scale -> rotate -> translate.
    Mat4x4F32 transform = mat4_scale(eng_world_vec3_(1.0f, cell->height, 1.0f));
    if (animated) {
        QuatF32 spin = quat_from_axis_angle(eng_world_vec3_(0.0f, 1.0f, 0.0f),
                                            time * (0.6f + (F32)(cell->cellSeed % 5u) * 0.25f));
        transform = transform * quat_to_mat4(spin);
    }
    transform = transform * mat4_translate(eng_world_vec3_(cell->worldX, cell->height * 0.5f, cell->worldZ));
    if (animated) {
        F32 bob = 0.35f * (0.5f + 0.5f * SIN_F32(time * 1.7f + (F32)cell->cellSeed * 0.61f));
        transform.v[3][1] += bob;
    }
    return transform;
}

struct AppSceneExtractParams {
    EngWorldState* world;
    U32 side;
    F32 time;
    B32 animate;
    U32 paletteMaterials[6];
    U32 alphaTestMaterial;
    U32 transparentMaterials[2];
};

// Allocates the scene's material slots from the world table once the world
// exists; the scene owns the palette, the world owns only the missing slot.
static void app_demo_scene_ensure_materials_(EngContext* ctx) {
    DemoSettings* demo = &demo_state_(ctx)->settings;
    EngWorldState* world = &ctx->engine->world;
    if (demo->materialsReady || !world->gpuResourcesCreated) {
        return;
    }

    static const F32 palette[6][4] = {
        {0.80f, 0.34f, 0.26f, 1.0f},
        {0.30f, 0.62f, 0.85f, 1.0f},
        {0.92f, 0.78f, 0.32f, 1.0f},
        {0.42f, 0.78f, 0.45f, 1.0f},
        {0.72f, 0.52f, 0.86f, 1.0f},
        {0.34f, 0.36f, 0.42f, 1.0f},
    };
    B32 ok = 1;
    for (U32 paletteIndex = 0u; paletteIndex < 6u; ++paletteIndex) {
        ok = ok && eng_world_material_alloc(world, &demo->paletteMaterials[paletteIndex]);
        if (ok) {
            ShdWorldMaterialRecord record = {};
            record.baseColor[0] = palette[paletteIndex][0];
            record.baseColor[1] = palette[paletteIndex][1];
            record.baseColor[2] = palette[paletteIndex][2];
            record.baseColor[3] = palette[paletteIndex][3];
            eng_world_material_set(world, demo->paletteMaterials[paletteIndex], &record);
        }
    }
    ok = ok && eng_world_material_alloc(world, &demo->alphaTestMaterial);
    if (ok) {
        ShdWorldMaterialRecord record = {};
        record.baseColor[0] = 0.95f;
        record.baseColor[1] = 0.95f;
        record.baseColor[2] = 0.95f;
        record.baseColor[3] = 1.0f;
        record.flags = ENG_WORLD_MATERIAL_FLAG_ALPHA_TEST;
        eng_world_material_set(world, demo->alphaTestMaterial, &record);
    }
    static const F32 transparentColors[2][4] = {
        {0.35f, 0.65f, 0.95f, 0.50f},
        {0.95f, 0.45f, 0.55f, 0.55f},
    };
    for (U32 transparentIndex = 0u; transparentIndex < 2u; ++transparentIndex) {
        ok = ok && eng_world_material_alloc(world, &demo->transparentMaterials[transparentIndex]);
        if (ok) {
            ShdWorldMaterialRecord record = {};
            record.baseColor[0] = transparentColors[transparentIndex][0];
            record.baseColor[1] = transparentColors[transparentIndex][1];
            record.baseColor[2] = transparentColors[transparentIndex][2];
            record.baseColor[3] = transparentColors[transparentIndex][3];
            eng_world_material_set(world, demo->transparentMaterials[transparentIndex], &record);
        }
    }
    ok = ok && eng_world_material_alloc(world, &demo->playerMaterial);
    if (ok) {
        ShdWorldMaterialRecord record = {};
        record.baseColor[0] = 1.0f;
        record.baseColor[1] = 0.55f;
        record.baseColor[2] = 0.15f;
        record.baseColor[3] = 1.0f;
        eng_world_material_set(world, demo->playerMaterial, &record);
    }
    demo->materialsReady = ok;
}

// Spawns every instance of a published model; the placement applies after
// each instance's model-space transform (row-vector: local first).
static void demo_scene_push_model_(EngWorldState* world, EngWorldLaneWriter* writer,
                                  const EngWorldModelResources* model, EngWorldBin bin,
                                  const Mat4x4F32* placement) {
    for (U32 at = 0u; at < model->instanceCount; ++at) {
        const EngWorldModelInstanceRef* instance = model->instances + at;
        Mat4x4F32 transform = instance->transform * (*placement);
        eng_world_writer_push_(world, writer, instance->mesh, instance->materialSlot, bin, &transform);
    }
}

// Uniform scale to a target world radius, spin, then land the model's
// bounds center exactly on position.
static Mat4x4F32 demo_scene_model_placement_(const EngWorldModelResources* model, F32 targetRadius,
                                            Vec3F32 position, F32 yawRadians) {
    F32 scale = (model->boundsRadius > 1e-6f) ? (targetRadius / model->boundsRadius) : 1.0f;
    Mat4x4F32 placement = mat4_scale(eng_world_vec3_(scale, scale, scale)) *
                          quat_to_mat4(quat_from_axis_angle(eng_world_vec3_(0.0f, 1.0f, 0.0f), yawRadians));
    Vec3F32 center = eng_world_vec3_(model->boundsCenter[0], model->boundsCenter[1], model->boundsCenter[2]);
    Vec3F32 placedCenter;
    placedCenter.x = center.x * placement.v[0][0] + center.y * placement.v[1][0] + center.z * placement.v[2][0];
    placedCenter.y = center.x * placement.v[0][1] + center.y * placement.v[1][1] + center.z * placement.v[2][1];
    placedCenter.z = center.x * placement.v[0][2] + center.y * placement.v[1][2] + center.z * placement.v[2][2];
    placement.v[3][0] = position.x - placedCenter.x;
    placement.v[3][1] = position.y - placedCenter.y;
    placement.v[3][2] = position.z - placedCenter.z;
    return placement;
}

// Per-item extraction body; laneId/laneCount explicit so the single-threaded
// path runs inline with no SPMD group.
static void demo_scene_extract_range_(const AppSceneExtractParams* params, U64 laneId, U64 laneCount) {
    EngWorldState* world = params->world;
    EngWorldLaneWriter* writer = world->laneWriters + laneId;
    U32 side = params->side;
    F32 time = params->time;
    B32 animate = params->animate;
    RangeU64 range = spmd_split_range_((U64)side * side, laneId, laneCount);

    for (U64 item = range.min; item < range.max; ++item) {
        U32 x = (U32)(item % side);
        U32 z = (U32)(item / side);
        DemoSceneCell cell = demo_scene_classify_cell_(x, z, side);

        if (cell.kind == DemoSceneCell_ModelProxy) {
            U32 modelIndex = (cell.lane == 9u) ? 0u : 1u;
            if (world->models[modelIndex]) {
                const EngWorldModelResources* model = world->models[modelIndex];
                Vec3F32 position = eng_world_vec3_(cell.worldX, DEMO_SCENE_MODEL_PROXY_Y, cell.worldZ);
                Mat4x4F32 placement = demo_scene_model_placement_(model, DEMO_SCENE_MODEL_PROXY_RADIUS,
                                                                 position,
                                                                 (F32)cell.cellSeed * ((cell.lane == 9u) ? 0.7f : 1.3f));
                demo_scene_push_model_(world, writer, model, EngWorldBin_Opaque, &placement);
                continue;
            }
            // Unpublished model: render the plain cell below while the
            // proxy collider already stands in — asset arrival timing is
            // not a sim input.
        }

        Mat4x4F32 transform = demo_scene_cell_transform_(&cell, time, animate);
        EngWorldMeshHandle mesh = cell.sphereMesh ? world->builtinMeshes[1]
                                                  : world->builtinMeshes[0];
        if (cell.lane == 3u) {
            eng_world_writer_push_(world, writer, world->builtinMeshes[0], params->alphaTestMaterial, EngWorldBin_AlphaTest, &transform);
        } else if (cell.lane == 5u || cell.lane == 7u) {
            eng_world_writer_push_(world, writer, mesh, params->transparentMaterials[(cell.lane == 5u) ? 0u : 1u], EngWorldBin_Transparent, &transform);
        } else {
            eng_world_writer_push_(world, writer, mesh, params->paletteMaterials[cell.cellSeed % 6u], EngWorldBin_Opaque, &transform);
        }
    }
}

static void demo_scene_extract_kernel_(void* kernelParameters) {
    PROF_SCOPE("scene extract");
    const AppSceneExtractParams* params = (const AppSceneExtractParams*)kernelParameters;
    demo_scene_extract_range_(params, spmd_lane_id(), spmd_lane_count());
}

static void demo_scene_submit(EngContext* ctx, EngRendererFrame* rendererFrame) {
    EngState* state = ctx->engine;
    DemoState* demoState = demo_state_(ctx);
    DemoSettings* demo = &demoState->settings;
    EngWorldState* world = &state->world;
    if (!world->frameOpen || world->meshCount == 0u) {
        return;
    }

    app_demo_scene_ensure_materials_(ctx);

    F32 time = (F32)state->simTimeSeconds;
    U32 side = CLAMP(demo->gridSide, DEMO_GRID_MIN, DEMO_GRID_MAX);

    F32 orbitRadius = (F32)side * DEMO_SCENE_GRID_SPACING * 0.62f;
    if (demo->playerMode) {
        Vec3F32 eye;
        Vec3F32 target;
        demo_game_camera_(ctx, state->lastDeltaSeconds, &eye, &target);
        eng_world_set_camera(ctx, eye, target, 1.0472f, 0.1f, orbitRadius * 4.0f);
    } else {
        // The spectator auto-orbit is load-bearing: headless captures
        // depend on its deterministic path.
        F32 yaw = time * 0.22f;
        Vec3F32 eye = eng_world_vec3_(COS_F32(yaw) * orbitRadius,
                                      orbitRadius * 0.45f,
                                      SIN_F32(yaw) * orbitRadius);
        eng_world_set_camera(ctx, eye, eng_world_vec3_(0.0f, 0.0f, 0.0f), 1.0472f, 0.1f,
                             orbitRadius * 4.0f);
    }

    // The plaza sits outside the grid edge; size the ground to cover it.
    F32 groundSpan = (F32)side * DEMO_SCENE_GRID_SPACING + 2.0f * (DEMO_SCENE_SPAWN_MARGIN + 20.0f);
    Mat4x4F32 groundTransform = mat4_scale(eng_world_vec3_(groundSpan, 1.0f, groundSpan)) *
                                mat4_translate(eng_world_vec3_(0.0f, -0.05f, 0.0f));
    eng_world_push(ctx, world->builtinMeshes[2], demo->paletteMaterials[5], EngWorldBin_Opaque, &groundTransform);

    // Spawn plaza, rendered straight from the collider table (the inverse
    // derivation from the grid: the table is the single source of truth).
    {
        DemoScenePlayground playground;
        F32 gridExtentForPlaza = demo_scene_grid_extent_(side);
        demo_scene_build_playground_(gridExtentForPlaza + DEMO_SCENE_SPAWN_MARGIN,
                                    -(gridExtentForPlaza + DEMO_SCENE_SPAWN_MARGIN), &playground);
        for (U32 at = 0u; at < playground.count; ++at) {
            const DemoCollider* collider = playground.colliders + at;
            Mat4x4F32 plazaTransform = mat4_scale(eng_world_vec3_(collider->halfExtents.x * 2.0f,
                                                                  collider->halfExtents.y * 2.0f,
                                                                  collider->halfExtents.z * 2.0f)) *
                                       quat_to_mat4(collider->orientation) *
                                       mat4_translate(collider->center);
            eng_world_push(ctx, world->builtinMeshes[0], demo->paletteMaterials[2], EngWorldBin_Opaque,
                           &plazaTransform);
        }
    }

    if (demo->playerMode) {
        // Unit sphere mesh has radius 0.5; scale to the player's radius.
        F32 playerScale = DEMO_GAME_PLAYER_RADIUS * 2.0f;
        Vec3F32 renderPosition = demo_game_render_position_(demoState->game.playerPrevPosition,
                                                           demoState->game.player.position,
                                                           state->simAccumulator);
        Mat4x4F32 playerTransform = mat4_scale(eng_world_vec3_(playerScale, playerScale, playerScale)) *
                                    mat4_translate(renderPosition);
        eng_world_push(ctx, world->builtinMeshes[1], demo->playerMaterial, EngWorldBin_Opaque, &playerTransform);
    }

    // A transparent cube nested inside a transparent sphere, both half-sunk
    // into the ground at the grid center: the opaque plane depth-rejects the
    // buried halves, the cube shows through the sphere shell (nearest-point
    // sort draws inner before outer), and the engulfed grid objects exercise
    // ordering against the shell every orbit.
    F32 groundY = -0.05f;
    Mat4x4F32 bigCube = mat4_scale(eng_world_vec3_(14.0f, 14.0f, 14.0f)) *
                        mat4_translate(eng_world_vec3_(0.0f, groundY, 0.0f));
    eng_world_push(ctx, world->builtinMeshes[0], demo->transparentMaterials[0], EngWorldBin_Transparent, &bigCube);
    Mat4x4F32 bigSphere = mat4_scale(eng_world_vec3_(26.0f, 26.0f, 26.0f)) *
                          mat4_translate(eng_world_vec3_(0.0f, groundY, 0.0f));
    eng_world_push(ctx, world->builtinMeshes[1], demo->transparentMaterials[1], EngWorldBin_Transparent, &bigSphere);

    // Showcase models at opposite grid edges: Lantern (node transforms) and
    // Buggy (148 deduped sections, 236 instances through mesh reuse).
    F32 gridExtent = (F32)side * DEMO_SCENE_GRID_SPACING * 0.5f;
    if (world->models[2]) {
        Mat4x4F32 placement = demo_scene_model_placement_(world->models[2], 12.0f,
                                                         eng_world_vec3_(gridExtent * 0.5f, 10.0f, gridExtent * 0.5f),
                                                         time * 0.25f);
        demo_scene_push_model_(world, world->laneWriters, world->models[2], EngWorldBin_Opaque, &placement);
    }
    if (world->models[3]) {
        Mat4x4F32 placement = demo_scene_model_placement_(world->models[3], 16.0f,
                                                         eng_world_vec3_(-gridExtent * 0.5f, 13.0f, -gridExtent * 0.5f),
                                                         time * -0.2f);
        demo_scene_push_model_(world, world->laneWriters, world->models[3], EngWorldBin_Opaque, &placement);
    }

    AppSceneExtractParams extractParams = {};
    extractParams.world = world;
    extractParams.side = side;
    extractParams.time = time;
    extractParams.animate = demo->animate;
    MEMCPY(extractParams.paletteMaterials, demo->paletteMaterials, sizeof(extractParams.paletteMaterials));
    extractParams.alphaTestMaterial = demo->alphaTestMaterial;
    extractParams.transparentMaterials[0] = demo->transparentMaterials[0];
    extractParams.transparentMaterials[1] = demo->transparentMaterials[1];

    U32 itemCount = side * side;
    U32 dispatchLanes = MIN(world->laneCount, MAX(1u, itemCount / 512u));
    if (dispatchLanes > 1u && state->jobSystem) {
        PROF_SCOPE("scene dispatch");
        SPMDGroup* group = spmd_dispatch(state->jobSystem, ctx->host->frameArena,
                                         .laneCount = dispatchLanes,
                                         .kernel = demo_scene_extract_kernel_,
                                         .kernelParameters = &extractParams);
        if (group) {
            spmd_destroy_group(group);
        }
    } else {
        demo_scene_extract_range_(&extractParams, 0u, 1u);
    }

    if (demo->showBounds) {
        eng_debug_draw_world_bounds(ctx, 256u);
        if (demo->playerMode) {
            demo_debug_draw_contacts_(ctx, &demo_state_(ctx)->game.lastTickStats);
        }
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
            eng_renderer_submit_text(ctx, rendererFrame, &title, Draw2DLayer_HUD);
        }
    }
}
