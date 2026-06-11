//
// Created by André Leite on 11/06/2026.
//
// Pure scene-cell classification, shared by the render extraction and the
// collider build so the two can never drift. A cell is a function of
// (x, z, side) and NOTHING else — never asset publish state, never render
// toggles, never time — because the colliders feed the deterministic tick
// and must replay bit-identically. Consequences pinned in the tests:
// animation-eligible cells are ghosts even while the animate toggle is
// off (the sim cannot read render toggles), and model cells carry their
// proxy collider even before the model publishes (asset arrival timing is
// not a sim input).
//

#pragma once

#include "app_game_kernels.hpp"

#define APP_SCENE_GRID_SPACING 2.2f
#define APP_SCENE_CELL_HALF_WIDTH 0.5f
#define APP_SCENE_MODEL_PROXY_RADIUS 1.3f
#define APP_SCENE_MODEL_PROXY_Y 0.9f
#define APP_SCENE_SPAWN_MARGIN 8.0f
#define APP_SCENE_PLAYGROUND_CAP 24u

static F32 app_scene_grid_extent_(U32 side) {
    return (F32)side * APP_SCENE_GRID_SPACING * 0.5f;
}

enum AppSceneCellKind {
    AppSceneCell_SolidBox = 0u,
    AppSceneCell_Ghost = 1u,
    AppSceneCell_ModelProxy = 2u,
};

struct AppSceneCell {
    U32 kind;
    U32 cellSeed;
    U32 lane;            // cellSeed % 11: render bin/material/model choice
    B32 sphereMesh;      // renders the builtin sphere; still collides as its box
    B32 animateEligible; // may spin/bob when the render toggle is on
    F32 height;
    F32 worldX;
    F32 worldZ;
};

static AppSceneCell app_scene_classify_cell_(U32 x, U32 z, U32 side) {
    AppSceneCell cell = {};
    F32 half = (F32)(side - 1u) * 0.5f;
    cell.cellSeed = x * 31u + z * 17u;
    cell.lane = cell.cellSeed % 11u;
    cell.sphereMesh = ((cell.cellSeed & 3u) == 0u) ? 1 : 0;
    cell.animateEligible = ((cell.cellSeed % 7u) == 0u) ? 1 : 0;
    cell.height = 0.5f + (F32)((cell.cellSeed >> 2u) % 5u) * 0.22f;
    cell.worldX = ((F32)x - half) * APP_SCENE_GRID_SPACING;
    cell.worldZ = ((F32)z - half) * APP_SCENE_GRID_SPACING;
    if (cell.lane == 9u || cell.lane == 1u) {
        cell.kind = AppSceneCell_ModelProxy;
    } else if (cell.animateEligible || cell.lane == 5u || cell.lane == 7u) {
        cell.kind = AppSceneCell_Ghost;
    } else {
        cell.kind = AppSceneCell_SolidBox;
    }
    return cell;
}

// ////////////////////////
// Spawn plaza (U18): a static authored table anchored to the
// grid-relative spawn — a walled court open toward the grid, with a
// platform reachable two ways (a 30-degree ramp whose surface normal
// sits inside the grounded cone, and a staircase of adjacent 0.6-risers
// that exercises both the curb-climb ratchet and the box-seam hazard on
// purpose), pillars to slalom, and one 45-degree wall for
// non-axis-aligned slide feel. The render derives from this table — one
// source of truth in each direction, never two.

struct AppScenePlayground {
    U32 count;
    AppCollider colliders[APP_SCENE_PLAYGROUND_CAP];
};

static AppCollider* app_scene_playground_slot_(AppScenePlayground* playground) {
    ASSERT_DEBUG(playground->count < APP_SCENE_PLAYGROUND_CAP);
    AppCollider* collider = playground->colliders + playground->count;
    playground->count += 1u;
    collider->kind = AppCollider_Box;
    collider->radius = 0.0f;
    collider->orientation.x = 0.0f;
    collider->orientation.y = 0.0f;
    collider->orientation.z = 0.0f;
    collider->orientation.w = 1.0f;
    return collider;
}

// Box flush with the ground plane: centerY/halfY derive from the top.
static void app_scene_playground_box_(AppScenePlayground* playground, F32 centerX, F32 centerZ,
                                      F32 topY, F32 halfX, F32 halfZ) {
    AppCollider* collider = app_scene_playground_slot_(playground);
    F32 halfY = (topY - APP_GAME_GROUND_Y) * 0.5f;
    collider->center.x = centerX;
    collider->center.y = topY - halfY;
    collider->center.z = centerZ;
    collider->halfExtents.x = halfX;
    collider->halfExtents.y = halfY;
    collider->halfExtents.z = halfZ;
}

static void app_scene_build_playground_(F32 originX, F32 originZ, AppScenePlayground* playground) {
    playground->count = 0u;

    // Court walls, +z side open toward the grid. [0..2]
    app_scene_playground_box_(playground, originX + 18.0f, originZ, 2.5f, 0.5f, 18.5f);
    app_scene_playground_box_(playground, originX - 18.0f, originZ, 2.5f, 0.5f, 18.5f);
    app_scene_playground_box_(playground, originX, originZ - 18.0f, 2.5f, 18.5f, 0.5f);

    // Platform, top at 3.0. [3]
    app_scene_playground_box_(playground, originX - 10.0f, originZ - 10.0f, 3.0f, 4.0f, 4.0f);

    // Ramp onto the platform's +x face: 30 degrees, surface running from
    // the platform top edge (x = origin-6, y = 3) down toward +x. The
    // surface normal is (sin30, cos30, 0) = (0.5, 0.866, 0) — inside the
    // grounded cone, pinned in the suite. [4]
    {
        AppCollider* ramp = app_scene_playground_slot_(playground);
        ramp->kind = AppCollider_OrientedBox;
        F32 sin30 = 0.5f;
        F32 cos30 = 0.8660254f;
        F32 slopeHalf = 3.0f;
        F32 thicknessHalf = 0.25f;
        Vec3F32 zAxis;
        zAxis.x = 0.0f;
        zAxis.y = 0.0f;
        zAxis.z = 1.0f;
        // The engine's polynomial sin/cos leave quat_from_axis_angle a few
        // 1e-4 off unit length; the contact math requires unit (the
        // conjugate stands in for the inverse), so normalize at build.
        ramp->orientation = quat_normalize(quat_from_axis_angle(zAxis, -(PI_F32 / 6.0f)));
        // Surface midpoint = top edge + downslope direction (cos30, -sin30)
        // times the half length; the slab center sits one half-thickness
        // beneath the surface along the normal.
        ramp->center.x = (originX - 6.0f) + slopeHalf * cos30 - thicknessHalf * sin30;
        ramp->center.y = 3.0f - slopeHalf * sin30 - thicknessHalf * cos30;
        ramp->center.z = originZ - 10.0f;
        ramp->halfExtents.x = slopeHalf;
        ramp->halfExtents.y = thicknessHalf;
        ramp->halfExtents.z = 2.0f;
    }

    // Staircase up the platform's +z face: five adjacent treads, 0.6
    // risers (tops always below the sphere center, so held input ratchets
    // up), the top tread meeting the platform top exactly. [5..9]
    for (U32 step = 0u; step < 5u; ++step) {
        F32 topY = 0.6f * (F32)(step + 1u);
        F32 centerZ = (originZ - 5.2f) + (F32)(4u - step) * 1.6f;
        app_scene_playground_box_(playground, originX - 10.0f, centerZ, topY, 2.0f, 0.8f);
    }

    // Pillars in the east half, tops at 3.5 (reachable from the platform,
    // not from the ground). [10..13]
    app_scene_playground_box_(playground, originX + 6.0f, originZ + 4.0f, 3.5f, 0.6f, 0.6f);
    app_scene_playground_box_(playground, originX + 10.0f, originZ + 1.0f, 3.5f, 0.6f, 0.6f);
    app_scene_playground_box_(playground, originX + 7.0f, originZ - 3.0f, 3.5f, 0.6f, 0.6f);
    app_scene_playground_box_(playground, originX + 12.0f, originZ - 6.0f, 3.5f, 0.6f, 0.6f);

    // A 45-degree wall segment for non-axis-aligned slide feel. [14]
    {
        AppCollider* wall = app_scene_playground_slot_(playground);
        wall->kind = AppCollider_OrientedBox;
        Vec3F32 yAxis;
        yAxis.x = 0.0f;
        yAxis.y = 1.0f;
        yAxis.z = 0.0f;
        wall->orientation = quat_normalize(quat_from_axis_angle(yAxis, PI_F32 / 4.0f));
        wall->center.x = originX + 8.0f;
        wall->center.y = 1.225f;
        wall->center.z = originZ - 8.0f;
        wall->halfExtents.x = 4.0f;
        wall->halfExtents.y = 1.275f;
        wall->halfExtents.z = 0.5f;
    }
}

// The grid's collision world plus the spawn plaza. Box cells mirror the
// render transform (unit cube scaled by (1, height, 1), translated to
// rest on the ground); model cells mirror the render placement (bounds
// radius landed at the proxy height). Iteration order — grid cells in
// scan order, then the playground table — is part of the determinism
// contract.
static void app_scene_build_colliders_(U32 side, AppColliderSet* set) {
    set->count = 0u;
    set->dropped = 0u;
    for (U32 z = 0u; z < side; ++z) {
        for (U32 x = 0u; x < side; ++x) {
            AppSceneCell cell = app_scene_classify_cell_(x, z, side);
            if (cell.kind == AppSceneCell_Ghost) {
                continue;
            }
            if (set->count >= APP_COLLIDER_CAP) {
                set->dropped += 1u;
                continue;
            }
            AppCollider* collider = set->colliders + set->count;
            collider->orientation.x = 0.0f;
            collider->orientation.y = 0.0f;
            collider->orientation.z = 0.0f;
            collider->orientation.w = 1.0f;
            if (cell.kind == AppSceneCell_ModelProxy) {
                collider->kind = AppCollider_Sphere;
                collider->radius = APP_SCENE_MODEL_PROXY_RADIUS;
                collider->center.x = cell.worldX;
                collider->center.y = APP_SCENE_MODEL_PROXY_Y;
                collider->center.z = cell.worldZ;
                collider->halfExtents.x = 0.0f;
                collider->halfExtents.y = 0.0f;
                collider->halfExtents.z = 0.0f;
            } else {
                collider->kind = AppCollider_Box;
                collider->radius = 0.0f;
                collider->center.x = cell.worldX;
                collider->center.y = cell.height * 0.5f;
                collider->center.z = cell.worldZ;
                collider->halfExtents.x = APP_SCENE_CELL_HALF_WIDTH;
                collider->halfExtents.y = cell.height * 0.5f;
                collider->halfExtents.z = APP_SCENE_CELL_HALF_WIDTH;
            }
            set->count += 1u;
        }
    }

    AppScenePlayground playground;
    F32 extent = app_scene_grid_extent_(side);
    app_scene_build_playground_(extent + APP_SCENE_SPAWN_MARGIN, -(extent + APP_SCENE_SPAWN_MARGIN),
                                &playground);
    for (U32 at = 0u; at < playground.count; ++at) {
        if (set->count >= APP_COLLIDER_CAP) {
            set->dropped += 1u;
            continue;
        }
        set->colliders[set->count] = playground.colliders[at];
        set->count += 1u;
    }
}
