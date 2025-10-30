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

SPMDGroup* spmd_create_group_(Arena* arena, U32 laneCount, const SPMDGroupParameters& params) {
    ASSERT_DEBUG(laneCount != 0 && "laneCount must be non-zero");
    ASSERT_DEBUG(params.broadcastScratchSize != 0 && "broadcastScratchSize must be non-zero");
    SPMDGroup* group = ARENA_PUSH_STRUCT(arena, SPMDGroup);
    MEMSET(group, 0, sizeof(SPMDGroup));
    group->laneCount = laneCount;
    group->barrier = barrier_alloc((U32)laneCount);
    group->dataSize = params.broadcastScratchSize;
    group->data = arena_push(arena, group->dataSize);
    group->nextLaneId = 0;
    return group;
}

void spmd_destroy_group(SPMDGroup* group) {
    ASSERT_DEBUG(group != nullptr && "group must be valid");
    barrier_release(group->barrier);
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
    if (!g_threadContext) {
        return;
    }
    ASSERT_DEBUG(group != nullptr && "group must be valid");
    ASSERT_DEBUG(lane < group->laneCount && "lane out of range");
    SPMDMembership* membership = g_threadContext->membership;
    if (!membership) {
        return;
    }
    membership->group = group;
    membership->laneId = lane;
}

static B32 spmd_join_group_safe(SPMDGroup* group, U64 lane) {
    if (!g_threadContext || !g_threadContext->membership || !group) {
        return 0;
    }
    if (lane >= group->laneCount) {
        return 0;
    }
    SPMDMembership* membership = g_threadContext->membership;
    membership->group = group;
    membership->laneId = lane;
    return 1;
}

U64 spmd_join_group_auto(SPMDGroup* group) {
    ASSERT_DEBUG(g_threadContext != nullptr && "g_threadContext not initialized");
    ASSERT_DEBUG(group != nullptr && "group must be valid");
    U64 lane = ATOMIC_FETCH_ADD(&group->nextLaneId, 1, MEMORY_ORDER_RELAXED);
    ASSERT_DEBUG(lane < group->laneCount && "Too many threads trying to join group");
    if (lane >= group->laneCount) {
        return (U64)-1;
    }
    spmd_join_group(group, lane);
    return lane;
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
    return (group != nullptr) ? group->laneCount : 0;
}

static RangeU64 spmd_split_range_(U64 totalCount, U64 laneId, U64 laneCount) {
    RangeU64 range;
    range.min = 0;
    range.max = 0;

    if ((laneCount == 0) || (totalCount == 0)) {
        return range;
    }

    ASSERT_DEBUG(laneId < laneCount && "laneId out of range");

    U64 base = totalCount / laneCount;
    U64 remainder = totalCount % laneCount;

    U64 start = laneId * base + MIN(laneId, remainder);
    U64 count = base + ((laneId < remainder) ? 1u : 0u);

    range.min = start;
    range.max = start + count;

    return range;
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
        MEMCPY(group->data, src, size);
    }

    barrier_wait(group->barrier);

    MEMCPY(dst, group->data, size);

    barrier_wait(group->barrier);
}

void spmd_sync(SPMDGroup* group) {
    ASSERT_DEBUG(group != nullptr && "group must be valid");
    barrier_wait(group->barrier);
}

B32 spmd_is_root(SPMDGroup* group, U64 lane) {
    ASSERT_DEBUG(group != nullptr && "group must be valid");
    ASSERT_DEBUG(lane < group->laneCount && "lane out of range");
    return (spmd_lane_id() == lane) ? 1 : 0;
}

// ////////////////////////
// SPMD Dispatch via Job System

static
void spmd_dispatch_lane_job(void* params) {
    SPMDDispatchParameters* dispatchParams = (SPMDDispatchParameters*) params;
    if (!dispatchParams || !dispatchParams->group || !dispatchParams->kernel) {
        return;
    }

    U64 lane = spmd_join_group_auto(dispatchParams->group);
    if (lane == (U64)-1) {
        return;
    }

    DEFER(spmd_group_leave());
    dispatchParams->kernel(dispatchParams->kernelParameters);
}

SPMDGroup* spmd_dispatch_(JobSystem* jobSystem, Arena* arena, const SPMDDispatchOptions& options) {
    ASSERT_DEBUG(jobSystem != nullptr);
    ASSERT_DEBUG(arena != nullptr);
    ASSERT_DEBUG(options.laneCount > 0);
    ASSERT_DEBUG(options.kernel != nullptr);

    U32 laneCount = options.laneCount;
#ifndef NDEBUG
    ASSERT_DEBUG(laneCount <= jobSystem->workerCount && "laneCount exceeds available worker threads");
#else
    if (laneCount > jobSystem->workerCount) {
        LOG_WARNING("spmd_dispatch", "laneCount {} exceeds available worker threads {}. Clamping to {}.", laneCount, jobSystem->workerCount, jobSystem->workerCount);
        laneCount = jobSystem->workerCount;
    }
#endif

    SPMDGroup* group = spmd_group_create(arena, laneCount, options.groupParams);
    ASSERT_DEBUG(group != nullptr);
    if (!group) {
#ifdef NDEBUG
        LOG_ERROR("spmd_dispatch", "Failed to create SPMDGroup.");
        return nullptr;
#else
        ASSERT_DEBUG(group != nullptr && "Failed to create SPMDGroup");
#endif
    }

    SPMDDispatchParameters* dispatchParams = (SPMDDispatchParameters*) arena_push(
        arena, sizeof(SPMDDispatchParameters) * laneCount, alignof(SPMDDispatchParameters));
    ASSERT_DEBUG(dispatchParams != nullptr);
    if (!dispatchParams) {
#ifdef NDEBUG
        LOG_ERROR("spmd_dispatch", "Failed to allocate dispatch parameter array.");
        return nullptr;
#else
        ASSERT_DEBUG(dispatchParams != nullptr && "Failed to allocate dispatch parameter array");
#endif
    }

    Job rootJob = {};
    rootJob.remainingJobs = 0;

    for (U32 i = 0; i < laneCount; ++i) {
        SPMDDispatchParameters* params = &dispatchParams[i];
        params->group = group;
        params->laneId = 0;
        params->kernel = options.kernel;
        params->kernelParameters = options.kernelParameters;

        job_system_submit((.function = spmd_dispatch_lane_job, .parent = &rootJob), *params);
    }

    job_system_wait(jobSystem, &rootJob);

    return group;
}