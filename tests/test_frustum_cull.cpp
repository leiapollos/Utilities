//
// Frustum extraction + sphere culling — the exact formulas the CPU
// transparent path and world_cull.slang share.
//

static void test_frustum_cull_(void) {
    Vec3F32 eye = test_vec3_(0.0f, 0.0f, -10.0f);
    Vec3F32 target = test_vec3_(0.0f, 0.0f, 0.0f);
    Vec3F32 up = test_vec3_(0.0f, 1.0f, 0.0f);
    Mat4x4F32 vp = eng_world_camera_view_proj_(eye, target, up, 1.0472f, 1.0f, 0.1f, 100.0f);

    F32 planes[24];
    eng_world_frustum_planes_(&vp, planes);

    // Plane normals come out normalized.
    for (U32 plane = 0u; plane < 6u; ++plane) {
        F32 lengthSq = planes[plane * 4u + 0u] * planes[plane * 4u + 0u] +
                       planes[plane * 4u + 1u] * planes[plane * 4u + 1u] +
                       planes[plane * 4u + 2u] * planes[plane * 4u + 2u];
        TEST_CHECK_NEAR(lengthSq, 1.0f, 1e-3f);
    }

    F32 origin[3] = {0.0f, 0.0f, 0.0f};
    TEST_CHECK(eng_world_sphere_visible_(planes, origin, 1.0f));

    F32 behind[3] = {0.0f, 0.0f, -20.0f};
    TEST_CHECK(!eng_world_sphere_visible_(planes, behind, 1.0f));

    F32 farRight[3] = {100.0f, 0.0f, 0.0f};
    TEST_CHECK(!eng_world_sphere_visible_(planes, farRight, 1.0f));

    // Conservative: a sphere whose center is out but whose radius reaches
    // back across the plane stays visible.
    TEST_CHECK(eng_world_sphere_visible_(planes, farRight, 200.0f));

    // Near plane (camera at z=-10 looking +z, near 0.1): a point between
    // the eye and the near plane is culled; past the near plane it is not.
    F32 beforeNear[3] = {0.0f, 0.0f, -9.95f};
    TEST_CHECK(!eng_world_sphere_visible_(planes, beforeNear, 0.01f));
    F32 pastNear[3] = {0.0f, 0.0f, -9.5f};
    TEST_CHECK(eng_world_sphere_visible_(planes, pastNear, 0.01f));

    // Far plane at view depth 100 → world z = 90.
    F32 beyondFar[3] = {0.0f, 0.0f, 95.0f};
    TEST_CHECK(!eng_world_sphere_visible_(planes, beyondFar, 0.01f));
    F32 withinFar[3] = {0.0f, 0.0f, 80.0f};
    TEST_CHECK(eng_world_sphere_visible_(planes, withinFar, 0.01f));
}
