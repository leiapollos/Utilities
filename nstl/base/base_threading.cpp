//
// Created by André Leite on 27/10/2025.
//

// ////////////////////////
// Barrier

static Barrier barrier_alloc(U32 count) {
    OS_Handle handle = OS_barrier_create(count);
    return {handle.handle};
}

static void barrier_release(Barrier barrier) {
    OS_Handle handle{barrier.barrier};
    OS_barrier_destroy(handle);
}

static void barrier_wait(Barrier barrier) {
    OS_Handle handle{barrier.barrier};
    OS_barrier_wait(handle);
}


// ////////////////////////
// Thread Context

thread_local ThreadContext* g_threadContext = nullptr;

ThreadContext* thread_context_alloc() {
    Arena* arena = arena_alloc();
    g_threadContext = ARENA_PUSH_STRUCT(arena, ThreadContext);
    {
        ScratchArenas* scratch = ARENA_PUSH_STRUCT(arena, ScratchArenas);
        g_threadContext->arenas = scratch;

        scratch->slots[0] = arena;
        for (U32 i = 1; i < SCRATCH_TLS_ARENA_COUNT; ++i) {
            scratch->slots[i] = arena_alloc();
        }
        scratch->nextIndex = 0;
        scratch->initialized = 1;
        scratch->initialized = true;
    }
    {
        SPMDMembership* membership = ARENA_PUSH_STRUCT(arena, SPMDMembership);
        g_threadContext->membership = membership;
    }
    return g_threadContext;
}

void thread_context_release() {
    if (g_threadContext == nullptr) {
        ASSERT_DEBUG(false && "Trying to destroy null g_threadContext");
        return;
    }
    ScratchArenas* scratch = g_threadContext->arenas;
    for (S32 i = SCRATCH_TLS_ARENA_COUNT - 1; i >= 0 ; i--) { // Inverse order since the first arena holds the actual struct
        if (scratch->slots[i]) {
            arena_release(scratch->slots[i]);
        }
    }
}

ThreadContext* thread_context() {
    ASSERT_DEBUG(g_threadContext != nullptr && "g_threadContext not initialized!");
    return g_threadContext;
}


// ////////////////////////
// SPMD (Single Program Multiple Data)

static READ_ONLY SPMDGroup g_nilGroup {.laneCount = 0};
static READ_ONLY SPMDMembership g_nilMembership {.group = &g_nilGroup};

SPMDGroup* spmd_create_group(Arena* arena, U32 laneCount) {
    ASSERT_DEBUG(laneCount != 0 && "laneCount must be non-zero");
    SPMDGroup* group = ARENA_PUSH_STRUCT(arena, SPMDGroup);
    memset(group, 0, sizeof(SPMDGroup));
    group->laneCount = laneCount;
    group->barrier = barrier_alloc(laneCount);
    return group;
}

void spmd_destroy_group(SPMDGroup* group) {
    ASSERT_DEBUG(group != nullptr && "group must be valid");
    // TODO: do we really need this? Maybe not since we can just roll back the arena
}

SPMDMembership* spmd_membership() {
    if (!g_threadContext) {
        ASSERT_DEBUG(g_threadContext != nullptr && "g_threadContext not initialized");
        return &g_nilMembership;
    }
    SPMDMembership* membership = g_threadContext->membership;
    if (!membership) {
        ASSERT_DEBUG(membership != nullptr && "membership must be valid");
        return &g_nilMembership;
    }
    return membership;
}

void spmd_join_group(SPMDGroup* group, U64 lane) {
    ASSERT_DEBUG(g_threadContext != nullptr && "g_threadContext not initialized");
    ASSERT_DEBUG(group != nullptr && "group must be valid");
    SPMDMembership* membership = g_threadContext->membership;
    membership->group = group;
    membership->laneId = lane;
}

void spmd_group_leave() {
    ASSERT_DEBUG(g_threadContext != nullptr && "g_threadContext not initialized");
    SPMDMembership* membership = g_threadContext->membership;
    if (!membership) {
        ASSERT_DEBUG(membership != nullptr && "membership must be valid");
        return;
    }
    membership->group = nullptr;
    membership->laneId = 0;
}

SPMDGroup* spmd_current_group() {
    SPMDMembership* membership = spmd_membership();
    return membership->group;
}

U64 spmd_lane_id() {
    SPMDMembership* membership = spmd_membership();
    return membership->laneId;
}

U64 spmd_lane_count() {
    SPMDMembership* membership = spmd_membership();
    SPMDGroup* group = membership->group;
    return group->laneCount;
}

void spmd_broadcast(SPMDGroup* group, void* dst, void* src, U64 size, U64 rootLane) {
    ASSERT_DEBUG(group != nullptr && "group must be valid");
    ASSERT_DEBUG(dst != nullptr && "dst must be valid");
    ASSERT_DEBUG(src != nullptr && "src must be valid");
    ASSERT_DEBUG(size != 0 && "size must be valid");
    ASSERT_DEBUG(rootLane < group->laneCount && "rootLane out of range");
    ASSERT_DEBUG(size <= group->dataSize && "Broadcast size exceeds preallocated buffer; increase SPMDGroup::dataSize");

    barrier_wait(group->barrier);

    if (spmd_lane_id() == rootLane) {
        memcpy(group->data, src, size);
    }

    barrier_wait(group->barrier);

    memcpy(dst, group->data, size);

    barrier_wait(group->barrier);
}