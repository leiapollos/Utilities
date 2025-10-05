//
// Created by André Leite on 26/07/2025.
//

// ////////////////////////
// Arena

Arena* arena_alloc_(const ArenaParameters& parameters) {
    ASSERT_DEBUG(parameters.arenaSize >= parameters.committedSize && "Arena size should be bigger than commit size");

    auto sysInfo = OS_get_system_info();
    U64 pageSize = sysInfo->pageSize;

    U64 reserveSize = align_pow2(parameters.arenaSize + ARENA_HEADER_SIZE, pageSize);
    U64 initialCommitSize = align_pow2(ARENA_HEADER_SIZE + parameters.committedSize, pageSize);

    void* raw = OS_reserve(reserveSize);
    if (UNLIKELY(raw == nullptr)) {
        ASSERT_DEBUG(false && "Failed to reserve memory for Arena!");
        OS_abort(1);
    }

    if (UNLIKELY(!OS_commit(raw, initialCommitSize))) {
        OS_release(raw, reserveSize);
        ASSERT_DEBUG(false && "Failed to commit initial memory for Arena!");
        return nullptr;
    }

    Arena* arena = (Arena*) raw;
    arena->reserved = reserveSize;
    arena->committed = initialCommitSize;
    arena->pos = ARENA_HEADER_SIZE;
    arena->flags = parameters.flags;
    arena->current = arena;
    arena->startPos = 0;
    arena->prev = nullptr;

    ASAN_POISON_MEMORY_REGION(raw, initialCommitSize);
    ASAN_UNPOISON_MEMORY_REGION(raw, ARENA_HEADER_SIZE);

    return arena;
}

void arena_release(Arena* arena) {
    for (Arena* n = arena->current,* prev = 0; n != 0; n = prev) {
        prev = n->prev;
        OS_release(n, n->reserved);
    }
}

void* arena_push(Arena* arena, U64 size, U64 alignment) {
    ASSERT_DEBUG(arena && "Arena must not be null");
    ASSERT_DEBUG(is_power_of_two(alignment) && "Alignment must be a power of two");

    Arena* current = arena->current ? arena->current : arena;

    for (;;) {
        U64 alignedPos = align_pow2(current->pos, alignment);
        U64 newPos = alignedPos + size;

        if (newPos <= current->reserved) {
            if (newPos > current->committed) {
                auto sysInfo = OS_get_system_info();
                U64 pageSize = sysInfo->pageSize;

                U64 newCommitTarget = align_pow2(newPos, pageSize);
                newCommitTarget = MIN(newCommitTarget, current->reserved);

                U64 sizeToCommit = newCommitTarget - current->committed;
                void* commitStartAddr = (U8*) current + current->committed;

                OS_commit(commitStartAddr, sizeToCommit);
                current->committed = newCommitTarget;
                ASAN_POISON_MEMORY_REGION(commitStartAddr, sizeToCommit);
            }

            void* result = (U8*) current + alignedPos;
            current->pos = newPos;
            arena->current = current;
            ASAN_UNPOISON_MEMORY_REGION(result, size);

            if (UNLIKELY(result == nullptr)) {
                ASSERT_DEBUG(false && "Allocation Failure");
                OS_abort(1);
            }

            return result;
        }

        if (!flags_has(current->flags, DoChain)) {
            ASSERT_DEBUG((newPos <= current->reserved) && "Arena is out of bounds");
            OS_abort(1);
        }

        U64 nextArenaSize = MAX(current->reserved, size + ARENA_HEADER_SIZE);
        Arena* nextArena = arena_alloc(
            .arenaSize = nextArenaSize,
            .committedSize = nextArenaSize,
            .flags = current->flags,
        );
        nextArena->startPos = current->startPos + current->reserved;
        nextArena->prev = current;
        current->current = nextArena;
        current = nextArena;
        arena->current = current;
        // loop to recompute alignedPos/newPos for the new arena block
    }
}

void arena_pop_to(Arena* arena, U64 pos) {
    U64 absolutePos = pos + ARENA_HEADER_SIZE;
    Arena* current = arena->current;

    while (current->prev != nullptr && absolutePos < current->startPos) {
        Arena* prev = current->prev;
        OS_release(current, current->reserved);
        current = prev;
        arena->current = current;
    }

    U64 relativePos = absolutePos - current->startPos;
    current->pos = CLAMP_BOT(ARENA_HEADER_SIZE, relativePos);
    ASAN_POISON_MEMORY_REGION((U8*)current + current->pos, current->pos - relativePos);
}

U64 arena_get_pos(Arena* arena) {
    return (arena->current->startPos + arena->current->pos) - ARENA_HEADER_SIZE;
}


// ////////////////////////
// Scratch

static thread_local Scratch_TLS g_scratch_tls = {
    .slots = {0, 0},
    .next_index = 0,
    .initialized = 0,
};

static inline B32 scratch_collides_many(Arena* cand,
                                        Arena* const* excludes,
                                        U32 count) {
    if (cand == 0 || excludes == 0) {
        return 0;
    }
    for (U32 i = 0; i < count; ++i) {
        Arena* ex = excludes[i];
        if (ex != 0 && cand == ex)
            return 1;
    }
    return 0;
}

static void scratch_thread_init_with_params(const ArenaParameters& params) {
    if (g_scratch_tls.initialized) {
        return;
    }

    ArenaParameters p = params;
    if (!flags_has(p.flags, DoChain)) {
        flags_set(&p.flags, DoChain);
    }

    for (U32 i = 0; i < SCRATCH_TLS_ARENA_COUNT; ++i) {
        g_scratch_tls.slots[i] = arena_alloc(p);
    }
    g_scratch_tls.next_index = 0;
    g_scratch_tls.initialized = 1;
}

static void scratch_thread_init() {
    ArenaParameters p = {};
    scratch_thread_init_with_params(p);
}

static void scratch_thread_shutdown() {
    if (!g_scratch_tls.initialized) {
        return;
    }

    for (U32 i = 0; i < SCRATCH_TLS_ARENA_COUNT; ++i) {
        if (g_scratch_tls.slots[i]) {
            arena_release(g_scratch_tls.slots[i]);
            g_scratch_tls.slots[i] = 0;
        }
    }
    g_scratch_tls.initialized = 0;
}

static inline Temp temp_begin(Arena* arena) {
    Temp t = {arena, arena_get_pos(arena), 0};
    return t;
}

static void temp_end(Temp* t) {
    if (!t || !t->arena) {
        return;
    }

    arena_pop_to(t->arena, t->pos);
    if (t->is_temporary) {
        arena_release(t->arena);
    }
    t->arena = 0;
    t->pos = 0;
    t->is_temporary = 0;
}

static Temp get_scratch(Arena* const* excludes, U32 count) {
    ASSERT_DEBUG(g_scratch_tls.initialized && "Scratch TLS not initialized");

    static_assert(is_power_of_two(SCRATCH_TLS_ARENA_COUNT), "SCRATCH_TLS_ARENA_COUNT must be a power of two");

    U32 mask = SCRATCH_TLS_ARENA_COUNT - 1u;

    U32 idx = g_scratch_tls.next_index & mask;
    g_scratch_tls.next_index = (g_scratch_tls.next_index + 1u) & mask;

    for (U32 k = 0; k < SCRATCH_TLS_ARENA_COUNT; ++k) {
        U32 i = (idx + k) & mask;
        Arena* cand = g_scratch_tls.slots[i];
        if (!scratch_collides_many(cand, excludes, count)) {
            return temp_begin(cand);
        }
    }

    // All TLS scratch arenas collide -> allocate a temporary arena
    ArenaParameters p = {};
    Arena* temp_arena = arena_alloc(p);
    Temp t = temp_begin(temp_arena);
    t.is_temporary = 1;
    return t;
}
