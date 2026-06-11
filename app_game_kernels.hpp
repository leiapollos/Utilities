//
// Created by André Leite on 11/06/2026.
//
// Pure player simulation. A tick consumes (actions, colliders, tickIndex)
// and nothing else — no wall clock, no frame dt, no render state — which
// is the property input replay (U16) buys with. Actions carry the wish
// direction already in world space; the collider set derives from the
// grid side captured in the save state, so both tick inputs replay
// bit-identically. Self-contained on base types so `./sob test` runs the
// same code.
//

#pragma once

#define APP_SIM_TICK_HZ 60u
#define APP_SIM_TICK_DT (1.0f / (F32)APP_SIM_TICK_HZ)
#define APP_SIM_MAX_FRAME_DT 0.25f

#define APP_GAME_GRAVITY 30.0f
#define APP_GAME_JUMP_SPEED 11.0f
#define APP_GAME_MOVE_SPEED 14.0f
#define APP_GAME_GROUND_ACCEL 10.0f
#define APP_GAME_AIR_ACCEL 2.0f
#define APP_GAME_GROUND_FRICTION 8.0f
#define APP_GAME_PLAYER_RADIUS 1.0f
#define APP_GAME_GROUND_Y (-0.05f)
#define APP_GAME_MAX_SPEED 50.0f
#define APP_GAME_GROUND_NORMAL_Y 0.7f
#define APP_GAME_RESOLVE_MAX_ITERATIONS 4u

// No-tunneling invariant: one tick of travel at the speed cap stays under
// the player radius, so discrete resolve cannot step through a collider.
static_assert(APP_GAME_MAX_SPEED * APP_SIM_TICK_DT < APP_GAME_PLAYER_RADIUS,
              "speed cap breaks the discrete-collision no-tunneling bound");

#define APP_COLLIDER_CAP 16384u

struct AppGameActions {
    F32 moveX; // world-space wish direction, unnormalized
    F32 moveZ;
    B32 jump;
};

struct AppPlayerState {
    Vec3F32 position;
    Vec3F32 velocity;
    B32 grounded;
};

enum AppColliderKind {
    AppCollider_Box = 0u,
    AppCollider_Sphere = 1u,
    AppCollider_OrientedBox = 2u,
};

struct AppCollider {
    U32 kind;
    F32 radius;          // Sphere
    Vec3F32 center;
    Vec3F32 halfExtents; // Box, OrientedBox
    QuatF32 orientation; // OrientedBox only; must be unit length (builders
                         // write identity for the axis-aligned kinds)
};

// Static collision world, derived data (rebuilt from the grid side, never
// saved). Iteration order is part of the determinism contract.
struct AppColliderSet {
    U32 count;
    U32 dropped;
    AppCollider colliders[APP_COLLIDER_CAP];
};

struct AppContact {
    Vec3F32 normal; // unit, pushes the player out of the collider
    F32 depth;      // penetration along the normal, > 0 when returned
};

struct AppGameTickStats {
    U32 contactCount;
    F32 deepestDepth;
    Vec3F32 contactPoints[APP_GAME_RESOLVE_MAX_ITERATIONS];
    Vec3F32 contactNormals[APP_GAME_RESOLVE_MAX_ITERATIONS];
};

// Rotate v by the unit quaternion q — the same rotation quat_to_mat4
// expresses (row-vector v·M); the suite pins the two against each other.
static Vec3F32 app_collider_quat_rotate_(QuatF32 q, Vec3F32 v) {
    F32 tx = 2.0f * (q.y * v.z - q.z * v.y + q.w * v.x);
    F32 ty = 2.0f * (q.z * v.x - q.x * v.z + q.w * v.y);
    F32 tz = 2.0f * (q.x * v.y - q.y * v.x + q.w * v.z);
    Vec3F32 result;
    result.x = v.x + q.y * tz - q.z * ty;
    result.y = v.y + q.z * tx - q.x * tz;
    result.z = v.z + q.x * ty - q.y * tx;
    return result;
}

static Vec3F32 app_collider_quat_unrotate_(QuatF32 q, Vec3F32 v) {
    QuatF32 conjugate;
    conjugate.x = -q.x;
    conjugate.y = -q.y;
    conjugate.z = -q.z;
    conjugate.w = q.w;
    return app_collider_quat_rotate_(conjugate, v);
}

// Box-local sphere contact: delta is the sphere center in the box frame.
// Shared verbatim by the axis-aligned and oriented kinds.
static B32 app_collider_box_contact_local_(F32 dx, F32 dy, F32 dz, Vec3F32 halfExtents,
                                           F32 radius, AppContact* outContact) {
    F32 cx = CLAMP(dx, -halfExtents.x, halfExtents.x);
    F32 cy = CLAMP(dy, -halfExtents.y, halfExtents.y);
    F32 cz = CLAMP(dz, -halfExtents.z, halfExtents.z);
    F32 ox = dx - cx;
    F32 oy = dy - cy;
    F32 oz = dz - cz;
    F32 distSq = ox * ox + oy * oy + oz * oz;
    if (distSq >= radius * radius) {
        return 0;
    }
    if (distSq > 1e-12f) {
        F32 dist = SQRT_F32(distSq);
        F32 inverse = 1.0f / dist;
        outContact->normal.x = ox * inverse;
        outContact->normal.y = oy * inverse;
        outContact->normal.z = oz * inverse;
        outContact->depth = radius - dist;
        return 1;
    }
    // Center inside the box: push out the nearest face; ties break to
    // the lowest axis and the positive side so the contact is a pure
    // function of the inputs.
    F32 faceX = halfExtents.x - ((dx < 0.0f) ? -dx : dx);
    F32 faceY = halfExtents.y - ((dy < 0.0f) ? -dy : dy);
    F32 faceZ = halfExtents.z - ((dz < 0.0f) ? -dz : dz);
    outContact->normal.x = 0.0f;
    outContact->normal.y = 0.0f;
    outContact->normal.z = 0.0f;
    F32 face;
    if (faceX <= faceY && faceX <= faceZ) {
        outContact->normal.x = (dx >= 0.0f) ? 1.0f : -1.0f;
        face = faceX;
    } else if (faceY <= faceZ) {
        outContact->normal.y = (dy >= 0.0f) ? 1.0f : -1.0f;
        face = faceY;
    } else {
        outContact->normal.z = (dz >= 0.0f) ? 1.0f : -1.0f;
        face = faceZ;
    }
    outContact->depth = radius + face;
    return 1;
}

static B32 app_collider_sphere_contact_(const AppCollider* collider, Vec3F32 center, F32 radius,
                                        AppContact* outContact) {
    F32 dx = center.x - collider->center.x;
    F32 dy = center.y - collider->center.y;
    F32 dz = center.z - collider->center.z;
    if (collider->kind == AppCollider_Box) {
        return app_collider_box_contact_local_(dx, dy, dz, collider->halfExtents, radius, outContact);
    }
    if (collider->kind == AppCollider_OrientedBox) {
        Vec3F32 delta;
        delta.x = dx;
        delta.y = dy;
        delta.z = dz;
        Vec3F32 local = app_collider_quat_unrotate_(collider->orientation, delta);
        if (!app_collider_box_contact_local_(local.x, local.y, local.z, collider->halfExtents,
                                             radius, outContact)) {
            return 0;
        }
        outContact->normal = app_collider_quat_rotate_(collider->orientation, outContact->normal);
        return 1;
    }

    F32 combined = radius + collider->radius;
    F32 distSq = dx * dx + dy * dy + dz * dz;
    if (distSq >= combined * combined) {
        return 0;
    }
    if (distSq > 1e-12f) {
        F32 dist = SQRT_F32(distSq);
        F32 inverse = 1.0f / dist;
        outContact->normal.x = dx * inverse;
        outContact->normal.y = dy * inverse;
        outContact->normal.z = dz * inverse;
        outContact->depth = combined - dist;
        return 1;
    }
    // Concentric degenerate: up is the only defensible arbitrary choice.
    outContact->normal.x = 0.0f;
    outContact->normal.y = 1.0f;
    outContact->normal.z = 0.0f;
    outContact->depth = combined;
    return 1;
}

static void app_game_tick_(AppPlayerState* player, const AppGameActions* actions,
                           const AppColliderSet* colliders, U64 tickIndex,
                           AppGameTickStats* outStats) {
    (void)tickIndex;
    F32 dt = APP_SIM_TICK_DT;
    outStats->contactCount = 0u;
    outStats->deepestDepth = 0.0f;

    // Quake-style ground move: friction first, then accelerate toward the
    // wish direction up to the speed cap along it.
    if (player->grounded) {
        F32 speed = SQRT_F32(player->velocity.x * player->velocity.x +
                             player->velocity.z * player->velocity.z);
        if (speed > 0.0f) {
            F32 drop = speed * APP_GAME_GROUND_FRICTION * dt;
            F32 scale = MAX(speed - drop, 0.0f) / speed;
            player->velocity.x *= scale;
            player->velocity.z *= scale;
        }
    }

    F32 wishX = actions->moveX;
    F32 wishZ = actions->moveZ;
    F32 wishLength = SQRT_F32(wishX * wishX + wishZ * wishZ);
    if (wishLength > 1e-6f) {
        wishX /= wishLength;
        wishZ /= wishLength;
        F32 accel = player->grounded ? APP_GAME_GROUND_ACCEL : APP_GAME_AIR_ACCEL;
        F32 current = player->velocity.x * wishX + player->velocity.z * wishZ;
        F32 missing = APP_GAME_MOVE_SPEED - current;
        if (missing > 0.0f) {
            F32 gain = MIN(accel * APP_GAME_MOVE_SPEED * dt, missing);
            player->velocity.x += wishX * gain;
            player->velocity.z += wishZ * gain;
        }
    }

    if (actions->jump && player->grounded) {
        player->velocity.y = APP_GAME_JUMP_SPEED;
        player->grounded = 0;
    }

    player->velocity.y -= APP_GAME_GRAVITY * dt;

    // Direction-preserving speed cap; enforces the no-tunneling invariant
    // and reads as terminal velocity in play.
    F32 speedSq = player->velocity.x * player->velocity.x +
                  player->velocity.y * player->velocity.y +
                  player->velocity.z * player->velocity.z;
    if (speedSq > APP_GAME_MAX_SPEED * APP_GAME_MAX_SPEED) {
        F32 scale = APP_GAME_MAX_SPEED / SQRT_F32(speedSq);
        player->velocity.x *= scale;
        player->velocity.y *= scale;
        player->velocity.z *= scale;
    }

    player->position.x += player->velocity.x * dt;
    player->position.y += player->velocity.y * dt;
    player->position.z += player->velocity.z * dt;

    // Discrete resolve, deepest contact first (strict > keeps the lowest
    // collider index on ties, so the result is scan-order independent):
    // push out along the normal, clip the velocity component into the
    // surface (slide), repeat until separated or the iteration cap.
    B32 contactGrounded = 0;
    for (U32 iteration = 0u; iteration < APP_GAME_RESOLVE_MAX_ITERATIONS; ++iteration) {
        AppContact deepest = {};
        for (U32 at = 0u; at < colliders->count; ++at) {
            AppContact contact;
            if (app_collider_sphere_contact_(colliders->colliders + at, player->position,
                                             APP_GAME_PLAYER_RADIUS, &contact) &&
                contact.depth > deepest.depth) {
                deepest = contact;
            }
        }
        if (deepest.depth <= 0.0f) {
            break;
        }
        player->position.x += deepest.normal.x * deepest.depth;
        player->position.y += deepest.normal.y * deepest.depth;
        player->position.z += deepest.normal.z * deepest.depth;
        F32 into = player->velocity.x * deepest.normal.x +
                   player->velocity.y * deepest.normal.y +
                   player->velocity.z * deepest.normal.z;
        if (into < 0.0f) {
            player->velocity.x -= deepest.normal.x * into;
            player->velocity.y -= deepest.normal.y * into;
            player->velocity.z -= deepest.normal.z * into;
        }
        if (deepest.normal.y > APP_GAME_GROUND_NORMAL_Y) {
            contactGrounded = 1;
        }
        outStats->contactPoints[outStats->contactCount].x =
            player->position.x - deepest.normal.x * APP_GAME_PLAYER_RADIUS;
        outStats->contactPoints[outStats->contactCount].y =
            player->position.y - deepest.normal.y * APP_GAME_PLAYER_RADIUS;
        outStats->contactPoints[outStats->contactCount].z =
            player->position.z - deepest.normal.z * APP_GAME_PLAYER_RADIUS;
        outStats->contactNormals[outStats->contactCount] = deepest.normal;
        outStats->contactCount += 1u;
        outStats->deepestDepth = MAX(outStats->deepestDepth, deepest.depth);
    }

    // The analytic ground plane stays beneath everything — exact and free.
    F32 standHeight = APP_GAME_GROUND_Y + APP_GAME_PLAYER_RADIUS;
    B32 planeGrounded = 0;
    if (player->position.y <= standHeight) {
        player->position.y = standHeight;
        if (player->velocity.y < 0.0f) {
            player->velocity.y = 0.0f;
        }
        planeGrounded = 1;
    }
    player->grounded = (planeGrounded || contactGrounded) ? 1 : 0;
}

// FNV-1a over the player's canonical fields (never the raw struct —
// padding would poison it). Recorded every N ticks during capture and
// compared during playback; a mismatch names the first impure tick.
static U64 app_game_state_checksum_(const AppPlayerState* player) {
    const F32 fields[6] = {
        player->position.x, player->position.y, player->position.z,
        player->velocity.x, player->velocity.y, player->velocity.z,
    };
    U64 hash = 14695981039346656037ull;
    const U8* bytes = (const U8*)fields;
    for (U64 at = 0u; at < sizeof(fields); ++at) {
        hash = (hash ^ (U64)bytes[at]) * 1099511628211ull;
    }
    hash = (hash ^ (U64)(player->grounded ? 1u : 0u)) * 1099511628211ull;
    return hash;
}

// Render-side tick interpolation. Frames and ticks beat against each
// other (measured: ~17.1ms paced frames vs 16.67ms ticks ran 2 ticks in
// one frame every ~35 frames — a visible double-step snap every ~0.6s),
// so rendered motion samples lerp(prevTick, curTick, accumulator/tickDt)
// and becomes a continuous function of wall time. At a tick boundary
// alpha wraps 1 -> 0 exactly as prev jumps to cur, so the sampled
// position is continuous — that property is pinned in the test suite.
static Vec3F32 app_game_render_position_(Vec3F32 previous, Vec3F32 current, F32 accumulator) {
    F32 alpha = accumulator * (1.0f / APP_SIM_TICK_DT);
    Vec3F32 result;
    result.x = previous.x + (current.x - previous.x) * alpha;
    result.y = previous.y + (current.y - previous.y) * alpha;
    result.z = previous.z + (current.z - previous.z) * alpha;
    return result;
}
