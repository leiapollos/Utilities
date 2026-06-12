//
// Math conventions. Every case here is a convention that bit (U5: the
// empty world) or the trap that caused it.
//

static void test_math_(void) {
    // Translation lives in storage row 3 and applies through v·M.
    Mat4x4F32 t = mat4_identity();
    t.v[3][0] = 5.0f;
    t.v[3][1] = -2.0f;
    t.v[3][2] = 3.0f;
    Vec4F32 moved = test_mul_point_(&t, test_vec3_(1.0f, 1.0f, 1.0f));
    TEST_CHECK_NEAR(moved.x, 6.0f, 1e-6f);
    TEST_CHECK_NEAR(moved.y, -1.0f, 1e-6f);
    TEST_CHECK_NEAR(moved.z, 4.0f, 1e-6f);
    TEST_CHECK_NEAR(moved.w, 1.0f, 1e-6f);

    // operator*(m, v) is the transpose trap: against this storage it sends
    // the translation into .w instead of .xyz. It is NOT a point transform.
    Vec4F32 homogeneous;
    homogeneous.x = 1.0f;
    homogeneous.y = 1.0f;
    homogeneous.z = 1.0f;
    homogeneous.w = 1.0f;
    Vec4F32 trap = t * homogeneous;
    TEST_CHECK_NEAR(trap.x, 1.0f, 1e-6f);
    TEST_CHECK_NEAR(trap.w, 7.0f, 1e-6f);

    // The camera kernel composes view * projection (row-vector order) and
    // must agree with composing the base_math pieces directly.
    Vec3F32 eye = test_vec3_(0.0f, 5.0f, -10.0f);
    Vec3F32 target = test_vec3_(0.0f, 0.0f, 0.0f);
    Vec3F32 up = test_vec3_(0.0f, 1.0f, 0.0f);
    F32 fovY = 1.0472f;
    F32 aspect = 16.0f / 9.0f;
    F32 zNear = 0.1f;
    F32 zFar = 100.0f;
    Mat4x4F32 vp = eng_world_camera_view_proj_(eye, target, up, fovY, aspect, zNear, zFar);
    Mat4x4F32 view = mat4_look_at(eye, target, up);
    Mat4x4F32 projection = mat4_perspective(fovY, aspect, zNear, zFar);
    Mat4x4F32 composed = view * projection;
    B32 kernelMatches = 1;
    for (U32 row = 0u; row < 4u; ++row) {
        for (U32 col = 0u; col < 4u; ++col) {
            if (vp.v[row][col] != composed.v[row][col]) {
                kernelMatches = 0;
            }
        }
    }
    TEST_CHECK(kernelMatches);

    // Inside / outside / behind through the composite (the U5 cases).
    Vec4F32 inside = test_mul_point_(&vp, test_vec3_(0.0f, 0.0f, 0.0f));
    TEST_CHECK(inside.w > 0.0f);
    TEST_CHECK(test_abs_(inside.x / inside.w) <= 1.0f);
    TEST_CHECK(test_abs_(inside.y / inside.w) <= 1.0f);
    TEST_CHECK(inside.z / inside.w >= 0.0f && inside.z / inside.w <= 1.0f);

    Vec4F32 behind = test_mul_point_(&vp, test_vec3_(0.0f, 10.0f, -20.0f));
    TEST_CHECK(behind.w < 0.0f);

    Vec4F32 outside = test_mul_point_(&vp, test_vec3_(1000.0f, 0.0f, 0.0f));
    TEST_CHECK(outside.w > 0.0f);
    TEST_CHECK(test_abs_(outside.x / outside.w) > 1.0f);

    // The reversed composition is the U5 bug: for the origin it degenerates
    // to clipW == 0 exactly (look_at keeps column 3 identity, so the w
    // column of projection*view is projection's, and its row-3 entry is 0).
    Mat4x4F32 reversed = projection * view;
    Vec4F32 degenerate = test_mul_point_(&reversed, test_vec3_(0.0f, 0.0f, 0.0f));
    TEST_CHECK(degenerate.w == 0.0f);

    // Screen orientation (the upside-down-world bug, found by the human in
    // player mode): Metal NDC is y-up, so a world point ABOVE the view
    // center must land at POSITIVE clip y, and with eye on -Z looking +Z
    // the camera-right axis is -X (right-handed), so world +X lands at
    // NEGATIVE clip x. The original camera commit shipped a Vulkan-style
    // Y-flip in the projection that inverted the whole image.
    Mat4x4F32 level = eng_world_camera_view_proj_(test_vec3_(0.0f, 0.0f, -10.0f),
                                                  test_vec3_(0.0f, 0.0f, 0.0f), up,
                                                  fovY, 1.0f, zNear, zFar);
    Vec4F32 above = test_mul_point_(&level, test_vec3_(0.0f, 1.0f, 0.0f));
    TEST_CHECK(above.w > 0.0f);
    TEST_CHECK(above.y / above.w > 0.0f);
    Vec4F32 worldRight = test_mul_point_(&level, test_vec3_(1.0f, 0.0f, 0.0f));
    TEST_CHECK(worldRight.x / worldRight.w < 0.0f);
}
