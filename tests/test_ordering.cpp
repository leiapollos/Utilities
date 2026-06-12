//
// Transparent ordering. The nearest-point key (view depth − radius) is
// what stopped the nested-shell popping: an engulfing shell sorts nearer
// than its contents, so the back-to-front walk draws contents first.
// The radix must be stable and handle negative keys.
//

static void test_ordering_(void) {
    F32 eye[3] = {0.0f, 0.0f, 0.0f};
    Vec3F32 forward = test_vec3_(0.0f, 0.0f, 1.0f);

    F32 sharedCenter[3] = {0.0f, 0.0f, 10.0f};
    F32 contentKey = eng_world_transparent_depth_(sharedCenter, 1.0f, eye, forward);
    F32 shellKey = eng_world_transparent_depth_(sharedCenter, 5.0f, eye, forward);
    TEST_CHECK_NEAR(contentKey, 9.0f, 1e-6f);
    TEST_CHECK_NEAR(shellKey, 5.0f, 1e-6f);
    // Shell sorts nearer → later in the back-to-front (descending) walk.
    TEST_CHECK(shellKey < contentKey);

    // The full pipeline shape: ascending radix + reversed walk == strictly
    // descending depths (back-to-front).
    {
        F32 depths[2];
        depths[0] = contentKey;
        depths[1] = shellKey;
        U32 order[2];
        U32 scratch[2];
        eng_world_order_ascending_(depths, order, scratch, 2u);
        TEST_CHECK(order[0] == 1u && order[1] == 0u);
        // Reversed copy-out: content (deeper) first, shell after.
        TEST_CHECK(order[2u - 1u - 0u] == 0u);
    }

    // Radix correctness: negatives, zeros, duplicates.
    {
        F32 depths[9] = {3.5f, -1.25f, 0.0f, 3.5f, 7.25f, -8.5f, 0.0f, 0.001f, -0.001f};
        U32 order[9];
        U32 scratch[9];
        eng_world_order_ascending_(depths, order, scratch, 9u);
        B32 ascending = 1;
        for (U32 at = 0u; at + 1u < 9u; ++at) {
            if (depths[order[at]] > depths[order[at + 1u]]) {
                ascending = 0;
            }
        }
        TEST_CHECK(ascending);
        TEST_CHECK(order[0] == 5u);
        TEST_CHECK(order[8] == 4u);
        // Stability: equal keys keep submission order.
        U32 position[9];
        for (U32 at = 0u; at < 9u; ++at) {
            position[order[at]] = at;
        }
        TEST_CHECK(position[2] < position[6]); // the two 0.0f keys
        TEST_CHECK(position[0] < position[3]); // the two 3.5f keys
    }

    // A bigger deterministic shuffle stays a valid ascending permutation.
    {
        enum { TEST_SORT_N = 1024u };
        static F32 depths[TEST_SORT_N];
        static U32 order[TEST_SORT_N];
        static U32 scratch[TEST_SORT_N];
        U32 lcg = 0x12345678u;
        for (U32 at = 0u; at < TEST_SORT_N; ++at) {
            lcg = lcg * 1664525u + 1013904223u;
            F32 magnitude = (F32)(lcg >> 8u) / 1048576.0f;
            depths[at] = ((lcg & 1u) != 0u) ? -magnitude : magnitude;
        }
        eng_world_order_ascending_(depths, order, scratch, TEST_SORT_N);
        B32 ascending = 1;
        U64 indexSum = 0ull;
        for (U32 at = 0u; at < TEST_SORT_N; ++at) {
            indexSum += order[at];
            if (at + 1u < TEST_SORT_N && depths[order[at]] > depths[order[at + 1u]]) {
                ascending = 0;
            }
        }
        TEST_CHECK(ascending);
        TEST_CHECK(indexSum == ((U64)TEST_SORT_N * (TEST_SORT_N - 1u)) / 2ull);
    }
}
