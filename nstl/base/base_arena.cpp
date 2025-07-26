//
// Created by André Leite on 26/07/2025.
//

// ////////////////////////
// Arena

Arena* arena_alloc(U64 arenaSize) {
    auto sysInfo = OS_get_system_info();
    U64 pageSize = sysInfo->pageSize;

    U64 reserveSize = align_pow2(arenaSize + ARENA_HEADER_SIZE, pageSize);
    U64 initialCommitSize = align_pow2(ARENA_HEADER_SIZE, pageSize);

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

    Arena* arena = (Arena*)raw;
    arena->reserved = reserveSize;
    arena->committed = initialCommitSize;
    arena->pos = ARENA_HEADER_SIZE;

    return arena;
}

void arena_release(Arena* arena) {
    U8* raw = (U8*)arena;
    U64 totalReserved = arena->reserved;
    OS_release(raw, totalReserved);
}

void* arena_push(Arena* arena, U64 size, U64 alignment) {
    ASSERT_DEBUG(!(arena == nullptr || size == 0) && "Arena was not initialized?");
    ASSERT_DEBUG(is_power_of_two(alignment) && "Alignment must be a power of two");

    U64 alignedPos = align_pow2(arena->pos, alignment);
    U64 newPos = alignedPos + size;

    ASSERT_DEBUG((newPos <= arena->reserved) && "Arena is out of bounds");

    if (newPos > arena->committed) {
        auto sysInfo = OS_get_system_info();
        U64 pageSize = sysInfo->pageSize;

        U64 newCommitTarget = align_pow2(newPos, pageSize);
        newCommitTarget = MIN(newCommitTarget, arena->reserved);

        U64 sizeToCommit = newCommitTarget - arena->committed;
        void* commitStartAddr = (U8*)arena + arena->committed;

        OS_commit(commitStartAddr, sizeToCommit);
        arena->committed = newCommitTarget;
    }

    void* result = (U8*)arena + alignedPos;
    arena->pos = newPos;

    if (UNLIKELY(result == nullptr)) {
        ASSERT_DEBUG(false && "Allocation Failure");
        OS_abort(1);
    }

    return result;
}

void arena_set_pos(Arena* arena, U64 newUsablePos) {
    U64 newRawPos = newUsablePos + ARENA_HEADER_SIZE;
    if (newRawPos > arena->reserved) {
        newRawPos = arena->reserved;
    }
    newRawPos = MAX(ARENA_HEADER_SIZE, newRawPos);
    arena->pos = newRawPos;
}

U64 arena_get_pos(Arena* arena) {
    return arena->pos - ARENA_HEADER_SIZE;
}

U64 arena_get_committed(Arena* arena) {
    return arena->committed - ARENA_HEADER_SIZE;
}

U64 arena_get_reserved(Arena* arena) {
    return arena->reserved - ARENA_HEADER_SIZE;
}
