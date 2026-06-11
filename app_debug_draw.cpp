//
// Created by André Leite on 10/06/2026.
//

static void debug_draw_rect(APP_Context* ctx, F32 minX, F32 minY, F32 maxX, F32 maxY, U32 rgba8) {
    draw2d_rect(&ctx->core->render2d.draw2d, Draw2DLayer_Debug, minX, minY, maxX, maxY, rgba8);
}

static void debug_draw_box(APP_Context* ctx, F32 minX, F32 minY, F32 maxX, F32 maxY, F32 thickness, U32 rgba8) {
    draw2d_box(&ctx->core->render2d.draw2d, Draw2DLayer_Debug, minX, minY, maxX, maxY, thickness, rgba8);
}

static void debug_draw_line(APP_Context* ctx, F32 x0, F32 y0, F32 x1, F32 y1, F32 thickness, U32 rgba8) {
    draw2d_line(&ctx->core->render2d.draw2d, Draw2DLayer_Debug, x0, y0, x1, y1, thickness, rgba8);
}

static B32 app_debug_project_point_(const ShdWorldFrameRecord* frame, F32 windowWidth, F32 windowHeight,
                                    F32 x, F32 y, F32 z, F32* outX, F32* outY) {
    const F32* m = frame->viewProj;
    F32 clipX = m[0] * x + m[4] * y + m[8] * z + m[12];
    F32 clipY = m[1] * x + m[5] * y + m[9] * z + m[13];
    F32 clipW = m[3] * x + m[7] * y + m[11] * z + m[15];
    if (clipW <= 0.0001f) {
        return 0;
    }
    *outX = (clipX / clipW * 0.5f + 0.5f) * windowWidth;
    *outY = (1.0f - (clipY / clipW * 0.5f + 0.5f)) * windowHeight;
    return 1;
}

static void app_debug_draw_record_bounds_(APP_Context* ctx, const AppWorldState* world,
                                          const ShdWorldRenderableRecord* record,
                                          F32 windowWidth, F32 windowHeight) {
    F32 cornerX[8];
    F32 cornerY[8];
    for (U32 corner = 0u; corner < 8u; ++corner) {
        F32 x = record->boundsCenter[0] + ((corner & 1u) ? record->boundsExtents[0] : -record->boundsExtents[0]);
        F32 y = record->boundsCenter[1] + ((corner & 2u) ? record->boundsExtents[1] : -record->boundsExtents[1]);
        F32 z = record->boundsCenter[2] + ((corner & 4u) ? record->boundsExtents[2] : -record->boundsExtents[2]);
        if (!app_debug_project_point_(&world->frameRecord, windowWidth, windowHeight,
                                      x, y, z, cornerX + corner, cornerY + corner)) {
            return;
        }
    }
    static const U32 edges[12][2] = {
        {0u, 1u}, {1u, 3u}, {3u, 2u}, {2u, 0u},
        {4u, 5u}, {5u, 7u}, {7u, 6u}, {6u, 4u},
        {0u, 4u}, {1u, 5u}, {2u, 6u}, {3u, 7u},
    };
    for (U32 edge = 0u; edge < 12u; ++edge) {
        debug_draw_line(ctx, cornerX[edges[edge][0]], cornerY[edges[edge][0]],
                        cornerX[edges[edge][1]], cornerY[edges[edge][1]], 1.0f, 0x4F8A6A90u);
    }
}

static void app_debug_draw_world_bounds(APP_Context* ctx, U32 maxBounds) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
    if (!world->frameOpen || world->laneCount == 0u) {
        return;
    }

    F32 windowWidth = (F32)state->windowWidth;
    F32 windowHeight = (F32)state->windowHeight;
    U32 drawn = 0u;
    for (U32 lane = 0u; lane < world->laneCount && drawn < maxBounds; ++lane) {
        const AppWorldLaneWriter* writer = world->laneWriters + lane;
        U32 count = MIN(writer->count, maxBounds - drawn);
        for (U32 renderableIndex = 0u; renderableIndex < count; ++renderableIndex) {
            app_debug_draw_record_bounds_(ctx, world, writer->records + renderableIndex,
                                          windowWidth, windowHeight);
        }
        drawn += count;
    }
}

// Last-tick contact normals as world-space whiskers: foot of the line at
// the contact point on the sphere surface, tip one unit along the normal.
static void app_debug_draw_contacts(APP_Context* ctx, const AppGameTickStats* stats) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
    if (!world->frameOpen) {
        return;
    }
    F32 windowWidth = (F32)state->windowWidth;
    F32 windowHeight = (F32)state->windowHeight;
    U32 count = MIN(stats->contactCount, APP_GAME_RESOLVE_MAX_ITERATIONS);
    for (U32 at = 0u; at < count; ++at) {
        Vec3F32 point = stats->contactPoints[at];
        Vec3F32 normal = stats->contactNormals[at];
        F32 footX;
        F32 footY;
        F32 tipX;
        F32 tipY;
        if (!app_debug_project_point_(&world->frameRecord, windowWidth, windowHeight,
                                      point.x, point.y, point.z, &footX, &footY) ||
            !app_debug_project_point_(&world->frameRecord, windowWidth, windowHeight,
                                      point.x + normal.x, point.y + normal.y, point.z + normal.z,
                                      &tipX, &tipY)) {
            continue;
        }
        debug_draw_line(ctx, footX, footY, tipX, tipY, 2.0f, 0xE8843AFFu);
        debug_draw_box(ctx, footX - 3.0f, footY - 3.0f, footX + 3.0f, footY + 3.0f, 1.0f, 0xE8843AFFu);
    }
}

static void debug_draw_text(APP_Context* ctx, AppRendererFrame* rendererFrame, F32 x, F32 y, F32 pixelSize, U32 rgba8, StringU8 text) {
    AppRender2DState* render = &ctx->core->render2d;
    if (render->textContext == 0 || render->font.generation == 0u || text.size == 0u) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena == 0) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    TextDrawDesc desc = {};
    desc.font = render->font;
    desc.text = text;
    desc.x = x;
    desc.y = y;
    desc.pixelSize = pixelSize;
    desc.rgba8 = rgba8;
    TextDrawData drawData = text_prepare_draw(render->textContext, scratch.arena, &desc);
    app_renderer_submit_text(ctx, rendererFrame, &drawData, Draw2DLayer_Debug);
}
