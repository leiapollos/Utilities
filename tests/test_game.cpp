//
// Player sim determinism + movement properties. The tick is a pure
// function of (state, actions, tickIndex); running the same script twice
// from the same initial state must be bit-identical — the property U16's
// input replay depends on.
//

static AppGameActions test_game_script_(U64 tick) {
    AppGameActions actions = {};
    if (tick < 60u) {
        actions.moveX = 1.0f;
    } else if (tick < 120u) {
        actions.moveX = -0.5f;
        actions.moveZ = 1.0f;
    }
    actions.jump = (tick >= 30u && tick < 34u) || (tick >= 90u && tick < 92u);
    return actions;
}

static void test_game_run_script_(AppPlayerState* player, U64 tickCount) {
    MEMSET(player, 0, sizeof(*player));
    for (U64 tick = 0u; tick < tickCount; ++tick) {
        AppGameActions actions = test_game_script_(tick);
        app_game_tick_(player, &actions, tick);
    }
}

static void test_game_(void) {
    // Determinism: identical script, bit-identical end state.
    AppPlayerState first;
    AppPlayerState second;
    test_game_run_script_(&first, 240u);
    test_game_run_script_(&second, 240u);
    TEST_CHECK(MEMCMP(&first, &second, sizeof(first)) == 0);

    // At rest the player settles grounded on the plane.
    AppPlayerState player = {};
    AppGameActions idle = {};
    for (U32 tick = 0u; tick < 120u; ++tick) {
        app_game_tick_(&player, &idle, tick);
    }
    TEST_CHECK(player.grounded);
    TEST_CHECK_NEAR(player.position.y, APP_GAME_GROUND_Y + APP_GAME_PLAYER_RADIUS, 1e-5f);
    TEST_CHECK_NEAR(player.velocity.x, 0.0f, 1e-5f);

    // Sustained input reaches but never exceeds the ground speed cap.
    AppGameActions run = {};
    run.moveX = 1.0f;
    F32 maxSpeed = 0.0f;
    for (U32 tick = 0u; tick < 240u; ++tick) {
        app_game_tick_(&player, &run, tick);
        F32 speed = SQRT_F32(player.velocity.x * player.velocity.x +
                             player.velocity.z * player.velocity.z);
        maxSpeed = MAX(maxSpeed, speed);
    }
    TEST_CHECK(maxSpeed <= APP_GAME_MOVE_SPEED + 1e-3f);
    TEST_CHECK(maxSpeed > APP_GAME_MOVE_SPEED * 0.95f);
    TEST_CHECK(player.position.x > 0.0f);

    // Jump leaves the ground, peaks, and lands back on it.
    AppGameActions jump = {};
    jump.jump = 1;
    app_game_tick_(&player, &jump, 0u);
    TEST_CHECK(!player.grounded);
    F32 peak = player.position.y;
    B32 landed = 0;
    for (U32 tick = 0u; tick < 240u && !landed; ++tick) {
        app_game_tick_(&player, &idle, tick);
        peak = MAX(peak, player.position.y);
        landed = player.grounded;
    }
    TEST_CHECK(landed);
    TEST_CHECK(peak > APP_GAME_GROUND_Y + APP_GAME_PLAYER_RADIUS + 1.0f);

    // Friction alone stops a grounded run.
    for (U32 tick = 0u; tick < 240u; ++tick) {
        app_game_tick_(&player, &idle, tick);
    }
    TEST_CHECK_NEAR(player.velocity.x, 0.0f, 1e-4f);
    TEST_CHECK_NEAR(player.velocity.z, 0.0f, 1e-4f);

    // Render interpolation (the every-~0.6s movement snap): the sampled
    // position must span the last tick interval and be continuous across
    // the tick boundary, where alpha wraps 1 -> 0 as prev jumps to cur.
    Vec3F32 prevTick = test_vec3_(1.0f, 2.0f, 3.0f);
    Vec3F32 curTick = test_vec3_(2.0f, 2.5f, 1.0f);
    Vec3F32 nextTick = test_vec3_(5.0f, -1.0f, 0.0f);
    Vec3F32 atStart = app_game_render_position_(prevTick, curTick, 0.0f);
    TEST_CHECK_NEAR(atStart.x, prevTick.x, 1e-6f);
    TEST_CHECK_NEAR(atStart.z, prevTick.z, 1e-6f);
    Vec3F32 atMid = app_game_render_position_(prevTick, curTick, APP_SIM_TICK_DT * 0.5f);
    TEST_CHECK_NEAR(atMid.x, 1.5f, 1e-5f);
    TEST_CHECK_NEAR(atMid.y, 2.25f, 1e-5f);
    Vec3F32 beforeBoundary = app_game_render_position_(prevTick, curTick,
                                                       APP_SIM_TICK_DT * 0.999f);
    Vec3F32 afterBoundary = app_game_render_position_(curTick, nextTick, 0.0f);
    TEST_CHECK_NEAR(beforeBoundary.x, afterBoundary.x, 2e-3f);
    TEST_CHECK_NEAR(beforeBoundary.y, afterBoundary.y, 2e-3f);
    TEST_CHECK_NEAR(beforeBoundary.z, afterBoundary.z, 3e-3f);

    // Replay checksum: equal states hash equal, any canonical field flips
    // the hash (the divergence detector U16's replay leans on).
    AppPlayerState hashA;
    AppPlayerState hashB;
    test_game_run_script_(&hashA, 100u);
    test_game_run_script_(&hashB, 100u);
    TEST_CHECK(app_game_state_checksum_(&hashA) == app_game_state_checksum_(&hashB));
    AppPlayerState perturbed = hashA;
    perturbed.position.x += 1e-6f;
    TEST_CHECK(app_game_state_checksum_(&perturbed) != app_game_state_checksum_(&hashA));
    perturbed = hashA;
    perturbed.velocity.z -= 1e-6f;
    TEST_CHECK(app_game_state_checksum_(&perturbed) != app_game_state_checksum_(&hashA));
    perturbed = hashA;
    perturbed.grounded = !perturbed.grounded;
    TEST_CHECK(app_game_state_checksum_(&perturbed) != app_game_state_checksum_(&hashA));
}
