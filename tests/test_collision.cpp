//
// U17 collision: the shared cell classifier (render and colliders derive
// from one function, pinned here so drift is a test failure), the
// sphere-vs-collider contact kernels with their deterministic degenerate
// rules, and the resolve loop's gameplay properties (wall stop, slide,
// landing, ghosts) including bit-determinism through a contact-heavy
// script — the property the replay theater test exercises live.
//

static AppColliderSet test_collision_set_;
static AppColliderSet test_collision_setSecond_;
static AppGameTickStats test_collision_stats_;

static void test_collision_tick_(AppPlayerState* player, const AppGameActions* actions, U64 tick) {
    app_game_tick_(player, actions, &test_collision_set_, tick, &test_collision_stats_);
}

static AppCollider test_collision_box_(F32 cx, F32 cy, F32 cz, F32 hx, F32 hy, F32 hz) {
    AppCollider collider = {};
    collider.kind = AppCollider_Box;
    collider.center = test_vec3_(cx, cy, cz);
    collider.halfExtents = test_vec3_(hx, hy, hz);
    collider.orientation.w = 1.0f;
    return collider;
}

static AppGameActions test_collision_script_(U64 tick) {
    AppGameActions actions = {};
    if (tick < 150u) {
        actions.moveZ = 1.0f;
    } else if (tick < 300u) {
        actions.moveX = 1.0f;
        actions.jump = (tick % 60u) < 2u;
    } else if (tick < 450u) {
        actions.moveX = -1.0f;
        actions.moveZ = -0.3f;
    } else {
        actions.moveZ = -1.0f;
        actions.jump = (tick % 45u) < 2u;
    }
    return actions;
}

static void test_collision_(void) {
    // ── Classification: the side-4 census, hand-computed from the seed
    // formula (seeds x*31 + z*17; lanes mod 11; ghosts mod 7 or lanes 5/7).
    {
        U32 solids = 0u;
        U32 proxies = 0u;
        U32 ghosts = 0u;
        for (U32 z = 0u; z < 4u; ++z) {
            for (U32 x = 0u; x < 4u; ++x) {
                AppSceneCell cell = app_scene_classify_cell_(x, z, 4u);
                solids += (cell.kind == AppSceneCell_SolidBox) ? 1u : 0u;
                proxies += (cell.kind == AppSceneCell_ModelProxy) ? 1u : 0u;
                ghosts += (cell.kind == AppSceneCell_Ghost) ? 1u : 0u;
            }
        }
        TEST_CHECK(solids == 8u);
        TEST_CHECK(proxies == 3u);
        TEST_CHECK(ghosts == 5u);
    }
    {
        // (0,0): seed 0 — animation-eligible sphere-mesh cell is a ghost
        // even though the animate toggle is render-side state.
        AppSceneCell cell = app_scene_classify_cell_(0u, 0u, 4u);
        TEST_CHECK(cell.kind == AppSceneCell_Ghost);
        TEST_CHECK(cell.animateEligible);
        TEST_CHECK(cell.sphereMesh);

        // (1,0): seed 31, lane 9 — model cell.
        cell = app_scene_classify_cell_(1u, 0u, 4u);
        TEST_CHECK(cell.kind == AppSceneCell_ModelProxy);
        TEST_CHECK(cell.lane == 9u);

        // (0,1): seed 17, lane 6 — solid box with exact geometry: height
        // 0.5 + ((17>>2)%5)*0.22 = 1.38, world (-3.3, -1.1) at side 4.
        cell = app_scene_classify_cell_(0u, 1u, 4u);
        TEST_CHECK(cell.kind == AppSceneCell_SolidBox);
        TEST_CHECK_NEAR(cell.height, 1.38f, 1e-5f);
        TEST_CHECK_NEAR(cell.worldX, -3.3f, 1e-5f);
        TEST_CHECK_NEAR(cell.worldZ, -1.1f, 1e-5f);

        // (3,0): seed 93, lane 5 — transparent cell is a ghost without
        // being animation-eligible.
        cell = app_scene_classify_cell_(3u, 0u, 4u);
        TEST_CHECK(cell.kind == AppSceneCell_Ghost);
        TEST_CHECK(!cell.animateEligible);

        // (0,7) at side 8: seed 119 is BOTH lane 9 and mod-7 zero — the
        // model proxy wins, because the model render path never animates.
        cell = app_scene_classify_cell_(0u, 7u, 8u);
        TEST_CHECK(cell.kind == AppSceneCell_ModelProxy);
        TEST_CHECK(cell.animateEligible);
    }

    // ── Collider build: census-consistent (11 grid cells + the 15-piece
    // plaza), pure (two builds bytewise identical), geometry mirrors the
    // render transform, plaza appended after the grid in table order.
    {
        app_scene_build_colliders_(4u, &test_collision_set_);
        TEST_CHECK(test_collision_set_.count == 26u);
        TEST_CHECK(test_collision_set_.dropped == 0u);
        U32 boxes = 0u;
        U32 spheres = 0u;
        U32 oriented = 0u;
        for (U32 at = 0u; at < test_collision_set_.count; ++at) {
            boxes += (test_collision_set_.colliders[at].kind == AppCollider_Box) ? 1u : 0u;
            spheres += (test_collision_set_.colliders[at].kind == AppCollider_Sphere) ? 1u : 0u;
            oriented += (test_collision_set_.colliders[at].kind == AppCollider_OrientedBox) ? 1u : 0u;
        }
        TEST_CHECK(boxes == 21u);
        TEST_CHECK(spheres == 3u);
        TEST_CHECK(oriented == 2u);

        // The plaza tail matches a locally built table at the side-4
        // origin (extent 4.4 + margin 8 = 12.4).
        AppScenePlayground playground;
        app_scene_build_playground_(12.4f, -12.4f, &playground);
        TEST_CHECK(playground.count == 15u);
        TEST_CHECK(MEMCMP(test_collision_set_.colliders + 11u, playground.colliders,
                          playground.count * sizeof(AppCollider)) == 0);

        app_scene_build_colliders_(4u, &test_collision_setSecond_);
        TEST_CHECK(MEMCMP(&test_collision_set_, &test_collision_setSecond_,
                          sizeof(test_collision_set_)) == 0);

        // Scan order is z-outer/x-inner: the first collider is (1,0)'s
        // proxy sphere, the second is (0,1)'s box.
        const AppCollider* proxy = test_collision_set_.colliders + 0u;
        TEST_CHECK(proxy->kind == AppCollider_Sphere);
        TEST_CHECK_NEAR(proxy->radius, APP_SCENE_MODEL_PROXY_RADIUS, 1e-6f);
        TEST_CHECK_NEAR(proxy->center.y, APP_SCENE_MODEL_PROXY_Y, 1e-6f);
        const AppCollider* box = test_collision_set_.colliders + 1u;
        TEST_CHECK(box->kind == AppCollider_Box);
        TEST_CHECK_NEAR(box->center.x, -3.3f, 1e-5f);
        TEST_CHECK_NEAR(box->center.y, 0.69f, 1e-5f);
        TEST_CHECK_NEAR(box->center.z, -1.1f, 1e-5f);
        TEST_CHECK_NEAR(box->halfExtents.x, 0.5f, 1e-6f);
        TEST_CHECK_NEAR(box->halfExtents.y, 0.69f, 1e-5f);
        TEST_CHECK_NEAR(box->halfExtents.z, 0.5f, 1e-6f);
    }

    // ── Contact kernel: closest-point cases against a unit-ish box and a
    // sphere, including the deterministic degenerate rules.
    {
        AppCollider box = test_collision_box_(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
        AppContact contact = {};

        // Face. Depth/normal tolerances are 1e-5 wherever a sqrt is in
        // play: the engine's sqrt_f32 is the two-iteration Newton rsqrt
        // (deterministic, libm-free), accurate to ~1e-6 relative.
        TEST_CHECK(app_collider_sphere_contact_(&box, test_vec3_(1.5f, 0.0f, 0.0f), 1.0f, &contact));
        TEST_CHECK_NEAR(contact.depth, 0.5f, 1e-5f);
        TEST_CHECK_NEAR(contact.normal.x, 1.0f, 1e-5f);
        TEST_CHECK_NEAR(contact.normal.y, 0.0f, 1e-6f);

        // Edge.
        TEST_CHECK(app_collider_sphere_contact_(&box, test_vec3_(1.6f, 1.6f, 0.0f), 1.0f, &contact));
        TEST_CHECK_NEAR(contact.depth, 0.151472f, 1e-5f);
        TEST_CHECK_NEAR(contact.normal.x, 0.707107f, 1e-5f);
        TEST_CHECK_NEAR(contact.normal.y, 0.707107f, 1e-5f);

        // Corner.
        TEST_CHECK(app_collider_sphere_contact_(&box, test_vec3_(1.5f, 1.5f, 1.5f), 1.0f, &contact));
        TEST_CHECK_NEAR(contact.depth, 0.133975f, 1e-5f);
        TEST_CHECK_NEAR(contact.normal.x, 0.577350f, 1e-5f);
        TEST_CHECK_NEAR(contact.normal.z, 0.577350f, 1e-5f);

        // Center inside: nearest face wins (y here), depth spans radius
        // plus the remaining face distance.
        TEST_CHECK(app_collider_sphere_contact_(&box, test_vec3_(0.2f, 0.8f, 0.0f), 1.0f, &contact));
        TEST_CHECK_NEAR(contact.normal.y, 1.0f, 1e-6f);
        TEST_CHECK_NEAR(contact.normal.x, 0.0f, 1e-6f);
        TEST_CHECK_NEAR(contact.depth, 1.2f, 1e-5f);

        // Center inside, negative side.
        TEST_CHECK(app_collider_sphere_contact_(&box, test_vec3_(-0.9f, 0.0f, 0.1f), 1.0f, &contact));
        TEST_CHECK_NEAR(contact.normal.x, -1.0f, 1e-6f);
        TEST_CHECK_NEAR(contact.depth, 1.1f, 1e-5f);

        // Dead center: ties break to the lowest axis, positive side.
        TEST_CHECK(app_collider_sphere_contact_(&box, test_vec3_(0.0f, 0.0f, 0.0f), 1.0f, &contact));
        TEST_CHECK_NEAR(contact.normal.x, 1.0f, 1e-6f);
        TEST_CHECK_NEAR(contact.depth, 2.0f, 1e-5f);

        // Exact touch and clear miss are both no-contact.
        TEST_CHECK(!app_collider_sphere_contact_(&box, test_vec3_(2.0f, 0.0f, 0.0f), 1.0f, &contact));
        TEST_CHECK(!app_collider_sphere_contact_(&box, test_vec3_(3.0f, 0.0f, 0.0f), 1.0f, &contact));

        AppCollider sphere = {};
        sphere.kind = AppCollider_Sphere;
        sphere.radius = 1.0f;
        sphere.center = test_vec3_(0.0f, 0.0f, 0.0f);
        TEST_CHECK(app_collider_sphere_contact_(&sphere, test_vec3_(1.5f, 0.0f, 0.0f), 1.0f, &contact));
        TEST_CHECK_NEAR(contact.depth, 0.5f, 1e-5f);
        TEST_CHECK_NEAR(contact.normal.x, 1.0f, 1e-5f);
        TEST_CHECK(!app_collider_sphere_contact_(&sphere, test_vec3_(2.5f, 0.0f, 0.0f), 1.0f, &contact));
        // Concentric degenerate pushes up.
        TEST_CHECK(app_collider_sphere_contact_(&sphere, test_vec3_(0.0f, 0.0f, 0.0f), 1.0f, &contact));
        TEST_CHECK_NEAR(contact.normal.y, 1.0f, 1e-6f);
        TEST_CHECK_NEAR(contact.depth, 2.0f, 1e-5f);
    }

    // ── Resolve: walking into a wall stops exactly at face + radius and
    // never penetrates past it; the velocity into the wall dies.
    {
        // A wall long in z, so the diagonal slide below cannot round its
        // corner inside the scripted window.
        test_collision_set_.count = 1u;
        test_collision_set_.dropped = 0u;
        test_collision_set_.colliders[0] = test_collision_box_(0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 50.0f);

        AppPlayerState player = {};
        player.position = test_vec3_(-3.0f, APP_GAME_GROUND_Y + APP_GAME_PLAYER_RADIUS, 0.0f);
        player.grounded = 1;
        AppGameActions intoWall = {};
        intoWall.moveX = 1.0f;
        B32 neverPast = 1;
        for (U32 tick = 0u; tick < 180u; ++tick) {
            test_collision_tick_(&player, &intoWall, tick);
            neverPast = neverPast && (player.position.x <= -2.0f + 1e-3f);
        }
        TEST_CHECK(neverPast);
        TEST_CHECK_NEAR(player.position.x, -2.0f, 1e-3f);
        TEST_CHECK(player.grounded);
        TEST_CHECK(player.velocity.x < 1e-3f);

        // Diagonal input slides: x stays pinned at the wall, z runs free.
        player.position = test_vec3_(-3.0f, APP_GAME_GROUND_Y + APP_GAME_PLAYER_RADIUS, 0.0f);
        player.velocity = test_vec3_(0.0f, 0.0f, 0.0f);
        AppGameActions diagonal = {};
        diagonal.moveX = 1.0f;
        diagonal.moveZ = 1.0f;
        for (U32 tick = 0u; tick < 180u; ++tick) {
            test_collision_tick_(&player, &diagonal, tick);
        }
        TEST_CHECK_NEAR(player.position.x, -2.0f, 1e-3f);
        TEST_CHECK(player.position.z > 10.0f);
    }

    // ── Resolve: dropping onto the top face lands grounded at top +
    // radius and rests there stably (the discrete resting contact).
    {
        AppPlayerState player = {};
        player.position = test_vec3_(0.0f, 4.0f, 0.0f);
        AppGameActions idle = {};
        for (U32 tick = 0u; tick < 120u; ++tick) {
            test_collision_tick_(&player, &idle, tick);
        }
        TEST_CHECK(player.grounded);
        TEST_CHECK_NEAR(player.position.y, 3.0f, 1e-3f);
        TEST_CHECK_NEAR(player.velocity.y, 0.0f, 1e-5f);
        B32 stayedPut = 1;
        for (U32 tick = 0u; tick < 120u; ++tick) {
            test_collision_tick_(&player, &idle, tick);
            stayedPut = stayedPut && player.grounded &&
                        (test_abs_(player.position.y - 3.0f) <= 1e-3f);
        }
        TEST_CHECK(stayedPut);

        // Walking off the edge goes airborne, then lands on the plane.
        player.position = test_vec3_(0.5f, 3.0f, 0.0f);
        player.velocity = test_vec3_(0.0f, 0.0f, 0.0f);
        player.grounded = 1;
        AppGameActions walk = {};
        walk.moveX = 1.0f;
        B32 sawAir = 0;
        for (U32 tick = 0u; tick < 240u; ++tick) {
            test_collision_tick_(&player, &walk, tick);
            sawAir = sawAir || !player.grounded;
        }
        TEST_CHECK(sawAir);
        TEST_CHECK(player.grounded);
        TEST_CHECK_NEAR(player.position.y, APP_GAME_GROUND_Y + APP_GAME_PLAYER_RADIUS, 1e-3f);
        TEST_CHECK(player.position.x > 2.0f);
    }

    // ── Speed cap: free fall pins at the no-tunneling bound.
    {
        test_collision_set_.count = 0u;
        AppPlayerState player = {};
        player.position = test_vec3_(0.0f, 300.0f, 0.0f);
        AppGameActions idle = {};
        for (U32 tick = 0u; tick < 150u; ++tick) {
            test_collision_tick_(&player, &idle, tick);
        }
        TEST_CHECK(!player.grounded);
        TEST_CHECK_NEAR(player.velocity.y, -APP_GAME_MAX_SPEED, 1e-3f);
    }

    // ── The grid world: ghost cells pass the player through, solid cells
    // stop it. Walking +z down the x = -3.3 column of the side-4 world
    // crosses ghost (0,0) at z -3.3 and stops at solid (0,1)'s face
    // (z -1.6) minus the radius: z = -2.6. A solid ghost cell would have
    // stopped the walk a full two units earlier.
    {
        app_scene_build_colliders_(4u, &test_collision_set_);
        AppPlayerState player = {};
        player.position = test_vec3_(-3.3f, APP_GAME_GROUND_Y + APP_GAME_PLAYER_RADIUS, -6.0f);
        player.grounded = 1;
        AppGameActions forward = {};
        forward.moveZ = 1.0f;
        for (U32 tick = 0u; tick < 240u; ++tick) {
            test_collision_tick_(&player, &forward, tick);
        }
        TEST_CHECK(player.position.z > -3.0f); // passed straight through the ghost
        TEST_CHECK_NEAR(player.position.z, -2.6f, 1e-3f);
        TEST_CHECK(player.grounded);
    }

    // ── Quat helpers: the kernel rotation must be the same rotation
    // quat_to_mat4 expresses (row-vector v·M, basis in storage rows), and
    // unrotate must invert rotate.
    {
        // Normalized at construction: the polynomial sin/cos leave the raw
        // axis-angle quat ~1e-4 off unit, and the helper contract is unit.
        QuatF32 quats[2];
        quats[0] = quat_normalize(quat_from_axis_angle(test_vec3_(0.0f, 0.0f, 1.0f), -(PI_F32 / 6.0f)));
        quats[1] = quat_normalize(quat_from_axis_angle(test_vec3_(0.0f, 1.0f, 0.0f), PI_F32 / 4.0f));
        Vec3F32 probes[2];
        probes[0] = test_vec3_(1.0f, 0.0f, 0.0f);
        probes[1] = test_vec3_(0.3f, -0.7f, 1.1f);
        for (U32 quatIndex = 0u; quatIndex < 2u; ++quatIndex) {
            Mat4x4F32 m = quat_to_mat4(quats[quatIndex]);
            for (U32 probeIndex = 0u; probeIndex < 2u; ++probeIndex) {
                Vec3F32 v = probes[probeIndex];
                Vec3F32 rotated = app_collider_quat_rotate_(quats[quatIndex], v);
                F32 mx = v.x * m.v[0][0] + v.y * m.v[1][0] + v.z * m.v[2][0];
                F32 my = v.x * m.v[0][1] + v.y * m.v[1][1] + v.z * m.v[2][1];
                F32 mz = v.x * m.v[0][2] + v.y * m.v[1][2] + v.z * m.v[2][2];
                TEST_CHECK_NEAR(rotated.x, mx, 1e-5f);
                TEST_CHECK_NEAR(rotated.y, my, 1e-5f);
                TEST_CHECK_NEAR(rotated.z, mz, 1e-5f);
                Vec3F32 roundTrip = app_collider_quat_unrotate_(quats[quatIndex], rotated);
                TEST_CHECK_NEAR(roundTrip.x, v.x, 1e-5f);
                TEST_CHECK_NEAR(roundTrip.y, v.y, 1e-5f);
                TEST_CHECK_NEAR(roundTrip.z, v.z, 1e-5f);
            }
        }
    }

    // ── Oriented box contact: a unit box yawed 45 degrees presents its
    // edge toward +x; a sphere on the x axis penetrates depth sqrt(2)-1
    // with a world normal of exactly +x (symmetry).
    {
        AppCollider obb = test_collision_box_(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
        obb.kind = AppCollider_OrientedBox;
        obb.orientation = quat_normalize(quat_from_axis_angle(test_vec3_(0.0f, 1.0f, 0.0f), PI_F32 / 4.0f));
        AppContact contact = {};
        TEST_CHECK(app_collider_sphere_contact_(&obb, test_vec3_(2.0f, 0.0f, 0.0f), 1.0f, &contact));
        TEST_CHECK_NEAR(contact.depth, 0.41421f, 1e-4f);
        TEST_CHECK_NEAR(contact.normal.x, 1.0f, 1e-4f);
        TEST_CHECK_NEAR(contact.normal.y, 0.0f, 1e-5f);
        TEST_CHECK(!app_collider_sphere_contact_(&obb, test_vec3_(2.5f, 0.0f, 0.0f), 1.0f, &contact));
    }

    // ── Plaza geometry: fixed table order, unit orientations, the ramp's
    // surface normal inside the grounded cone, stair top meeting the
    // platform top exactly.
    {
        AppScenePlayground playground;
        app_scene_build_playground_(0.0f, 0.0f, &playground);
        TEST_CHECK(playground.count == 15u);
        for (U32 at = 0u; at < playground.count; ++at) {
            const QuatF32* q = &playground.colliders[at].orientation;
            F32 lengthSq = q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w;
            TEST_CHECK_NEAR(lengthSq, 1.0f, 1e-5f);
        }
        const AppCollider* ramp = playground.colliders + 4u;
        TEST_CHECK(ramp->kind == AppCollider_OrientedBox);
        // A probe just above the slope midpoint surfaces the ramp normal.
        AppContact contact = {};
        Vec3F32 surfaceMid = test_vec3_(-6.0f + 3.0f * 0.8660254f, 3.0f - 3.0f * 0.5f, -10.0f);
        Vec3F32 probe = test_vec3_(surfaceMid.x + 0.5f * 0.9f, surfaceMid.y + 0.8660254f * 0.9f,
                                   surfaceMid.z);
        TEST_CHECK(app_collider_sphere_contact_(ramp, probe, 1.0f, &contact));
        TEST_CHECK_NEAR(contact.normal.x, 0.5f, 1e-3f);
        TEST_CHECK_NEAR(contact.normal.y, 0.8660254f, 1e-3f);
        TEST_CHECK(contact.normal.y > APP_GAME_GROUND_NORMAL_Y);
        // Stairs [5..9]: 0.6 risers, the top tread flush with the platform
        // top (3.0), every tread top below the sphere center relative to
        // the previous tread (the curb-climb precondition).
        const AppCollider* platform = playground.colliders + 3u;
        TEST_CHECK_NEAR(platform->center.y + platform->halfExtents.y, 3.0f, 1e-5f);
        const AppCollider* topStep = playground.colliders + 9u;
        TEST_CHECK_NEAR(topStep->center.y + topStep->halfExtents.y, 3.0f, 1e-5f);
        for (U32 step = 0u; step < 5u; ++step) {
            const AppCollider* tread = playground.colliders + 5u + step;
            TEST_CHECK_NEAR(tread->center.y + tread->halfExtents.y, 0.6f * (F32)(step + 1u), 1e-5f);
        }
    }

    // ── Stair climb (the seam stress, on purpose): driving into the
    // staircase ratchets up the 0.6 risers, across the adjacent-tread
    // seams and the coplanar stair/platform junction, and ends standing
    // on the platform.
    {
        AppScenePlayground playground;
        app_scene_build_playground_(0.0f, 0.0f, &playground);
        test_collision_set_.count = playground.count;
        test_collision_set_.dropped = 0u;
        MEMCPY(test_collision_set_.colliders, playground.colliders,
               playground.count * sizeof(AppCollider));

        AppPlayerState player = {};
        player.position = test_vec3_(-10.0f, APP_GAME_GROUND_Y + APP_GAME_PLAYER_RADIUS, 5.0f);
        player.grounded = 1;
        for (U32 tick = 0u; tick < 600u; ++tick) {
            AppGameActions actions = {};
            actions.moveZ = (player.position.z > -10.0f) ? -1.0f : 0.0f;
            test_collision_tick_(&player, &actions, tick);
        }
        TEST_CHECK(player.grounded);
        TEST_CHECK_NEAR(player.position.y, 4.0f, 1e-2f);
        TEST_CHECK(test_abs_(player.position.z + 10.0f) < 3.0f);

        // Ramp climb: approach from +x, ride the 30-degree slope onto the
        // platform; grounded holds for the majority of the ascent (the
        // cone property under motion).
        player.position = test_vec3_(2.0f, APP_GAME_GROUND_Y + APP_GAME_PLAYER_RADIUS, -10.0f);
        player.velocity = test_vec3_(0.0f, 0.0f, 0.0f);
        player.grounded = 1;
        U32 rampTicks = 0u;
        U32 rampGroundedTicks = 0u;
        for (U32 tick = 0u; tick < 600u; ++tick) {
            AppGameActions actions = {};
            actions.moveX = (player.position.x > -9.0f) ? -1.0f : 0.0f;
            test_collision_tick_(&player, &actions, tick);
            if (player.position.x <= -0.8f && player.position.x >= -6.0f) {
                rampTicks += 1u;
                rampGroundedTicks += player.grounded ? 1u : 0u;
            }
        }
        TEST_CHECK(player.grounded);
        TEST_CHECK_NEAR(player.position.y, 4.0f, 1e-2f);
        TEST_CHECK(player.position.x < -6.0f);
        TEST_CHECK(rampTicks > 0u);
        TEST_CHECK(rampGroundedTicks * 2u > rampTicks);
    }

    // ── Bit-determinism through a contact-heavy script: two runs over the
    // side-4 world (walls, proxy grazes, jumps onto cells) must match
    // bytewise at every checkpoint — the property the replay leans on.
    {
        AppPlayerState first = {};
        AppPlayerState second = {};
        U64 firstTrail[12];
        U64 secondTrail[12];
        first.position = test_vec3_(-3.3f, APP_GAME_GROUND_Y + APP_GAME_PLAYER_RADIUS, -6.0f);
        first.grounded = 1;
        second = first;
        for (U32 tick = 0u; tick < 600u; ++tick) {
            AppGameActions actions = test_collision_script_(tick);
            test_collision_tick_(&first, &actions, tick);
            if ((tick % 50u) == 49u) {
                firstTrail[tick / 50u] = app_game_state_checksum_(&first);
            }
        }
        for (U32 tick = 0u; tick < 600u; ++tick) {
            AppGameActions actions = test_collision_script_(tick);
            test_collision_tick_(&second, &actions, tick);
            if ((tick % 50u) == 49u) {
                secondTrail[tick / 50u] = app_game_state_checksum_(&second);
            }
        }
        TEST_CHECK(MEMCMP(&first, &second, sizeof(first)) == 0);
        TEST_CHECK(MEMCMP(firstTrail, secondTrail, sizeof(firstTrail)) == 0);
    }
}
