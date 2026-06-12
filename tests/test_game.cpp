//
// Player sim determinism + movement properties. The tick is a pure
// function of (state, actions, tickIndex); running the same script twice
// from the same initial state must be bit-identical — the property U16's
// input replay depends on.
//

// Statics: an DemoColliderSet is half a megabyte — too big for a stack
// frame. Empty set = the pre-U17 plane-only world, bit-identical.
static DemoColliderSet test_game_noColliders_;
static DemoTickStats test_game_tickStats_;

static void test_game_tick_(DemoPlayerState* player, const DemoActions* actions, U64 tick) {
    demo_game_tick_(player, actions, &test_game_noColliders_, tick, &test_game_tickStats_);
}

static DemoActions test_game_script_(U64 tick) {
    DemoActions actions = {};
    if (tick < 60u) {
        actions.moveX = 1.0f;
    } else if (tick < 120u) {
        actions.moveX = -0.5f;
        actions.moveZ = 1.0f;
    }
    actions.jump = (tick >= 30u && tick < 34u) || (tick >= 90u && tick < 92u);
    return actions;
}

static void test_game_run_script_(DemoPlayerState* player, U64 tickCount) {
    MEMSET(player, 0, sizeof(*player));
    for (U64 tick = 0u; tick < tickCount; ++tick) {
        DemoActions actions = test_game_script_(tick);
        test_game_tick_(player, &actions, tick);
    }
}

static void test_game_(void) {
    // Determinism: identical script, bit-identical end state.
    DemoPlayerState first;
    DemoPlayerState second;
    test_game_run_script_(&first, 240u);
    test_game_run_script_(&second, 240u);
    TEST_CHECK(MEMCMP(&first, &second, sizeof(first)) == 0);

    // At rest the player settles grounded on the plane.
    DemoPlayerState player = {};
    DemoActions idle = {};
    for (U32 tick = 0u; tick < 120u; ++tick) {
        test_game_tick_(&player, &idle, tick);
    }
    TEST_CHECK(player.grounded);
    TEST_CHECK_NEAR(player.position.y, DEMO_GAME_GROUND_Y + DEMO_GAME_PLAYER_RADIUS, 1e-5f);
    TEST_CHECK_NEAR(player.velocity.x, 0.0f, 1e-5f);

    // Sustained input reaches but never exceeds the ground speed cap.
    DemoActions run = {};
    run.moveX = 1.0f;
    F32 maxSpeed = 0.0f;
    for (U32 tick = 0u; tick < 240u; ++tick) {
        test_game_tick_(&player, &run, tick);
        F32 speed = SQRT_F32(player.velocity.x * player.velocity.x +
                             player.velocity.z * player.velocity.z);
        maxSpeed = MAX(maxSpeed, speed);
    }
    TEST_CHECK(maxSpeed <= DEMO_GAME_MOVE_SPEED + 1e-3f);
    TEST_CHECK(maxSpeed > DEMO_GAME_MOVE_SPEED * 0.95f);
    TEST_CHECK(player.position.x > 0.0f);

    // Jump leaves the ground, peaks, and lands back on it.
    DemoActions jump = {};
    jump.jump = 1;
    test_game_tick_(&player, &jump, 0u);
    TEST_CHECK(!player.grounded);
    F32 peak = player.position.y;
    B32 landed = 0;
    for (U32 tick = 0u; tick < 240u && !landed; ++tick) {
        test_game_tick_(&player, &idle, tick);
        peak = MAX(peak, player.position.y);
        landed = player.grounded;
    }
    TEST_CHECK(landed);
    TEST_CHECK(peak > DEMO_GAME_GROUND_Y + DEMO_GAME_PLAYER_RADIUS + 1.0f);

    // Friction alone stops a grounded run.
    for (U32 tick = 0u; tick < 240u; ++tick) {
        test_game_tick_(&player, &idle, tick);
    }
    TEST_CHECK_NEAR(player.velocity.x, 0.0f, 1e-4f);
    TEST_CHECK_NEAR(player.velocity.z, 0.0f, 1e-4f);

    // Render interpolation (the every-~0.6s movement snap): the sampled
    // position must span the last tick interval and be continuous across
    // the tick boundary, where alpha wraps 1 -> 0 as prev jumps to cur.
    Vec3F32 prevTick = test_vec3_(1.0f, 2.0f, 3.0f);
    Vec3F32 curTick = test_vec3_(2.0f, 2.5f, 1.0f);
    Vec3F32 nextTick = test_vec3_(5.0f, -1.0f, 0.0f);
    Vec3F32 atStart = demo_game_render_position_(prevTick, curTick, 0.0f);
    TEST_CHECK_NEAR(atStart.x, prevTick.x, 1e-6f);
    TEST_CHECK_NEAR(atStart.z, prevTick.z, 1e-6f);
    Vec3F32 atMid = demo_game_render_position_(prevTick, curTick, ENG_SIM_TICK_DT * 0.5f);
    TEST_CHECK_NEAR(atMid.x, 1.5f, 1e-5f);
    TEST_CHECK_NEAR(atMid.y, 2.25f, 1e-5f);
    Vec3F32 beforeBoundary = demo_game_render_position_(prevTick, curTick,
                                                       ENG_SIM_TICK_DT * 0.999f);
    Vec3F32 afterBoundary = demo_game_render_position_(curTick, nextTick, 0.0f);
    TEST_CHECK_NEAR(beforeBoundary.x, afterBoundary.x, 2e-3f);
    TEST_CHECK_NEAR(beforeBoundary.y, afterBoundary.y, 2e-3f);
    TEST_CHECK_NEAR(beforeBoundary.z, afterBoundary.z, 3e-3f);

    // Replay checksum: equal states hash equal, any canonical field flips
    // the hash (the divergence detector U16's replay leans on).
    DemoPlayerState hashA;
    DemoPlayerState hashB;
    test_game_run_script_(&hashA, 100u);
    test_game_run_script_(&hashB, 100u);
    TEST_CHECK(demo_game_state_checksum_(&hashA) == demo_game_state_checksum_(&hashB));
    DemoPlayerState perturbed = hashA;
    perturbed.position.x += 1e-6f;
    TEST_CHECK(demo_game_state_checksum_(&perturbed) != demo_game_state_checksum_(&hashA));
    perturbed = hashA;
    perturbed.velocity.z -= 1e-6f;
    TEST_CHECK(demo_game_state_checksum_(&perturbed) != demo_game_state_checksum_(&hashA));
    perturbed = hashA;
    perturbed.grounded = !perturbed.grounded;
    TEST_CHECK(demo_game_state_checksum_(&perturbed) != demo_game_state_checksum_(&hashA));
}
