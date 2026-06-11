//
// Base seams: arena temp scoping, SlotMap generation invalidation,
// spmd_split_range partition exactness.
//

static void test_base_(void) {
    Arena* arena = arena_alloc();
    TEST_CHECK(arena != 0);

    // Linear arena: pushes do not overlap; Temp restores the position.
    U8* first = ARENA_PUSH_ARRAY(arena, U8, 100u);
    U8* second = ARENA_PUSH_ARRAY(arena, U8, 100u);
    TEST_CHECK(first != 0 && second != 0);
    TEST_CHECK(second >= first + 100u);

    Temp temp = temp_begin(arena);
    U8* inside = ARENA_PUSH_ARRAY(arena, U8, 64u);
    temp_end(&temp);
    U8* after = ARENA_PUSH_ARRAY(arena, U8, 64u);
    TEST_CHECK(after == inside);

    // SlotMap: stale generations resolve to null; released slots are
    // reused with a bumped generation.
    SlotMap map = {};
    TEST_CHECK(slot_map_init(&map, arena, sizeof(U64), 4u));
    void* item = 0;
    U32 slot = 0u;
    U32 generation = 0u;
    TEST_CHECK(slot_map_alloc(&map, &item, &slot, &generation));
    TEST_CHECK(slot_map_get(&map, slot, generation) == item);

    void* released = 0;
    TEST_CHECK(slot_map_release(&map, slot, generation, &released));
    TEST_CHECK(slot_map_get(&map, slot, generation) == 0);

    void* item2 = 0;
    U32 slot2 = 0u;
    U32 generation2 = 0u;
    TEST_CHECK(slot_map_alloc(&map, &item2, &slot2, &generation2));
    if (slot2 == slot) {
        TEST_CHECK(generation2 != generation);
        TEST_CHECK(slot_map_get(&map, slot, generation) == 0);
        TEST_CHECK(slot_map_get(&map, slot2, generation2) == item2);
    }

    // split_range: lanes cover [0, total) exactly once, contiguously,
    // with lane sizes differing by at most one.
    U64 totals[6] = {0ull, 1ull, 7ull, 16ull, 100ull, 1000ull};
    U64 laneCounts[4] = {1ull, 3ull, 7ull, 16ull};
    B32 partitionsExact = 1;
    for (U32 totalAt = 0u; totalAt < 6u; ++totalAt) {
        for (U32 laneAt = 0u; laneAt < 4u; ++laneAt) {
            U64 total = totals[totalAt];
            U64 laneCount = laneCounts[laneAt];
            U64 expectedStart = 0ull;
            U64 minSize = 0xFFFFFFFFFFFFFFFFull;
            U64 maxSize = 0ull;
            for (U64 lane = 0ull; lane < laneCount; ++lane) {
                RangeU64 range = spmd_split_range_(total, lane, laneCount);
                if (range.min != expectedStart || range.max < range.min || range.max > total) {
                    partitionsExact = 0;
                }
                U64 size = range.max - range.min;
                minSize = MIN(minSize, size);
                maxSize = MAX(maxSize, size);
                expectedStart = range.max;
            }
            if (expectedStart != total) {
                partitionsExact = 0;
            }
            if (laneCount <= total && maxSize - minSize > 1ull) {
                partitionsExact = 0;
            }
        }
    }
    TEST_CHECK(partitionsExact);
}
