//
// Created by André Leite on 11/06/2026.
//
// Pure player simulation. A tick consumes (actions, tickIndex) and nothing
// else — no wall clock, no frame dt, no render state — which is the
// property input replay (U16) buys with. Actions carry the wish direction
// already in world space; camera-relative mapping happens at sample time
// in the action layer, so replay captures world-space intent.
// Self-contained on base types so `./sob test` runs the same code.
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

static void app_game_tick_(AppPlayerState* player, const AppGameActions* actions, U64 tickIndex) {
    (void)tickIndex;
    F32 dt = APP_SIM_TICK_DT;

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
    player->position.x += player->velocity.x * dt;
    player->position.y += player->velocity.y * dt;
    player->position.z += player->velocity.z * dt;

    F32 standHeight = APP_GAME_GROUND_Y + APP_GAME_PLAYER_RADIUS;
    if (player->position.y <= standHeight) {
        player->position.y = standHeight;
        if (player->velocity.y < 0.0f) {
            player->velocity.y = 0.0f;
        }
        player->grounded = 1;
    } else {
        player->grounded = 0;
    }
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
