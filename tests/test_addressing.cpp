//
// Index addressing conventions. Builtins: pool-absolute indices, mesh
// records carry baseVertex 0. Cooked meshes are the opposite (relative
// indices + baseVertex; engine/engine_assets.hpp). Applying the base
// twice was U5's invisible-sphere bug — a relative emitter here would
// restart each mesh's indices at 0, which this suite would catch.
//

static void test_addressing_(void) {
    enum { TEST_CAP_V = 2048u, TEST_CAP_I = 8192u };
    static ShdWorldVertexRecord verts[TEST_CAP_V];
    static U32 indices[TEST_CAP_I];
    EngWorldMeshBuilder b = {};
    b.vertices = verts;
    b.indices = indices;
    b.vertexCapacity = TEST_CAP_V;
    b.indexCapacity = TEST_CAP_I;

    U32 cubeFirst = b.indexCount;
    eng_world_build_cube_(&b);
    U32 cubeIndexEnd = b.indexCount;
    U32 cubeVertexCount = b.vertexCount;

    U32 sphereFirst = b.indexCount;
    eng_world_build_sphere_(&b, 12u, 18u);
    U32 sphereIndexEnd = b.indexCount;
    U32 sphereVertexEnd = b.vertexCount;

    eng_world_build_plane_(&b);

    U32 cubeMin = 0xFFFFFFFFu;
    U32 cubeMax = 0u;
    for (U32 at = cubeFirst; at < cubeIndexEnd; ++at) {
        cubeMin = MIN(cubeMin, b.indices[at]);
        cubeMax = MAX(cubeMax, b.indices[at]);
    }
    TEST_CHECK(cubeMin == 0u);
    TEST_CHECK(cubeMax == cubeVertexCount - 1u);

    U32 sphereMin = 0xFFFFFFFFu;
    U32 sphereMax = 0u;
    for (U32 at = sphereFirst; at < sphereIndexEnd; ++at) {
        sphereMin = MIN(sphereMin, b.indices[at]);
        sphereMax = MAX(sphereMax, b.indices[at]);
    }
    // Pool-absolute: the sphere's indices start where the cube's vertices
    // ended, not at zero.
    TEST_CHECK(sphereMin == cubeVertexCount);
    TEST_CHECK(sphereMax == sphereVertexEnd - 1u);

    B32 allInRange = 1;
    for (U32 at = 0u; at < b.indexCount; ++at) {
        if (b.indices[at] >= b.vertexCount) {
            allInRange = 0;
        }
    }
    TEST_CHECK(allInRange);
}
