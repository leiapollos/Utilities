//
// Builder winding — right-handed cross of the emitted index order must
// agree with the authored vertex normals (the sphere-builder incident:
// one builder wound inward and single-sided transparents vanished).
//

static U32 test_winding_bad_tris_(const AppWorldMeshBuilder* b, U32 firstIndex, U32 indexCount) {
    U32 bad = 0u;
    for (U32 at = firstIndex; at + 2u < firstIndex + indexCount; at += 3u) {
        const ShdWorldVertexRecord* v0 = b->vertices + b->indices[at + 0u];
        const ShdWorldVertexRecord* v1 = b->vertices + b->indices[at + 1u];
        const ShdWorldVertexRecord* v2 = b->vertices + b->indices[at + 2u];
        Vec3F32 e1 = test_vec3_(v1->position[0] - v0->position[0],
                                v1->position[1] - v0->position[1],
                                v1->position[2] - v0->position[2]);
        Vec3F32 e2 = test_vec3_(v2->position[0] - v0->position[0],
                                v2->position[1] - v0->position[1],
                                v2->position[2] - v0->position[2]);
        Vec3F32 n = vec3_cross(e1, e2);
        Vec3F32 authored = test_vec3_(v0->normal[0] + v1->normal[0] + v2->normal[0],
                                      v0->normal[1] + v1->normal[1] + v2->normal[1],
                                      v0->normal[2] + v1->normal[2] + v2->normal[2]);
        // Degenerate cap triangles (zero cross) are legal; skip them.
        if (vec3_length(n) <= 1e-8f) {
            continue;
        }
        if (vec3_dot(n, authored) <= 0.0f) {
            bad += 1u;
        }
    }
    return bad;
}

static void test_winding_(void) {
    enum { TEST_CAP_V = 2048u, TEST_CAP_I = 8192u };
    static ShdWorldVertexRecord verts[TEST_CAP_V];
    static U32 indices[TEST_CAP_I];
    AppWorldMeshBuilder b = {};
    b.vertices = verts;
    b.indices = indices;
    b.vertexCapacity = TEST_CAP_V;
    b.indexCapacity = TEST_CAP_I;

    U32 cubeFirst = b.indexCount;
    app_world_build_cube_(&b);
    U32 cubeCount = b.indexCount - cubeFirst;

    U32 sphereFirst = b.indexCount;
    app_world_build_sphere_(&b, 12u, 18u);
    U32 sphereCount = b.indexCount - sphereFirst;

    U32 planeFirst = b.indexCount;
    app_world_build_plane_(&b);
    U32 planeCount = b.indexCount - planeFirst;

    TEST_CHECK(cubeCount == 36u);
    TEST_CHECK(sphereCount == 12u * 18u * 6u);
    TEST_CHECK(planeCount == 6u);
    TEST_CHECK(b.vertexCount == 24u + 13u * 19u + 4u);

    TEST_CHECK(test_winding_bad_tris_(&b, cubeFirst, cubeCount) == 0u);
    TEST_CHECK(test_winding_bad_tris_(&b, sphereFirst, sphereCount) == 0u);
    TEST_CHECK(test_winding_bad_tris_(&b, planeFirst, planeCount) == 0u);
}
