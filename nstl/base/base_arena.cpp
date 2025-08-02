//
// Created by André Leite on 26/07/2025.
//

// ////////////////////////
// Arena

Arena* arena_alloc(const ArenaParameters& parameters) {
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

    return arena;
}

void arena_release(Arena* arena) {
    for (Arena* n = arena->current,* prev = 0; n != 0; n = prev) {
        prev = n->prev;
        OS_release(n, n->reserved);
    }
}

void* arena_push(Arena* arena, U64 size, U64 alignment) {
    Arena* current = arena;
    ASSERT_DEBUG(is_power_of_two(alignment) && "Alignment must be a power of two");

    U64 alignedPos = align_pow2(current->pos, alignment);
    U64 newPos = alignedPos + size;

    if (newPos > current->reserved) {
        if (current->flags.has(DoChain)) {
            Arena* nextArena = arena_alloc({
                .arenaSize = MAX(current->reserved, size),
                .committedSize = size,
                .flags = current->flags,
            });
            nextArena->startPos = current->startPos + current->reserved;
            nextArena->prev = current;
            current->current = nextArena;
            current = nextArena;
        } else {
            ASSERT_DEBUG((newPos <= current->reserved) && "Arena is out of bounds");
            OS_abort(1);
        }
    }

    if (newPos > current->committed) {
        auto sysInfo = OS_get_system_info();
        U64 pageSize = sysInfo->pageSize;

        U64 newCommitTarget = align_pow2(newPos, pageSize);
        newCommitTarget = MIN(newCommitTarget, current->reserved);

        U64 sizeToCommit = newCommitTarget - current->committed;
        void* commitStartAddr = (U8*) current + current->committed;

        OS_commit(commitStartAddr, sizeToCommit);
        current->committed = newCommitTarget;
    }

    void* result = (U8*) current + alignedPos;
    current->pos = newPos;

    if (UNLIKELY(result == nullptr)) {
        ASSERT_DEBUG(false && "Allocation Failure");
        OS_abort(1);
    }

    return result;
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

    U64 relative_pos = absolutePos - current->startPos;
    current->pos = CLAMP_BOT(ARENA_HEADER_SIZE, relative_pos);
}

U64 arena_get_pos(Arena* arena) {
    return (arena->current->startPos + arena->current->pos) - ARENA_HEADER_SIZE;
}

U64 arena_get_committed(Arena* arena) {
    return arena->current->committed - ARENA_HEADER_SIZE;
}

U64 arena_get_reserved(Arena* arena) {
    return arena->current->reserved - ARENA_HEADER_SIZE;
}
