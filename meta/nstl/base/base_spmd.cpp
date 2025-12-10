//
// Meta Pre-compiler - SPMD Implementation
//

// ////////////////////////
// Barrier

Barrier barrier_alloc(U32 count) {
    OS_Handle handle = OS_barrier_create(count);
    return {handle.handle};
}

void barrier_release(Barrier barrier) {
    OS_Handle handle{barrier.barrier};
    OS_barrier_destroy(handle);
}

void barrier_wait(Barrier barrier) {
    OS_Handle handle{barrier.barrier};
    OS_barrier_wait(handle);
}


// ////////////////////////
// SPMD Membership (thread-local)

struct SPMDThreadContext {
    SPMDMembership membership;
    B32 initialized;
};

thread_local SPMDThreadContext g_spmdThreadContext = {};

static SPMDMembership* spmd_membership() {
    return &g_spmdThreadContext.membership;
}


// ////////////////////////
// SPMD Group

SPMDGroup* spmd_group_create_(Arena* arena, U32 laneCount, const SPMDGroupParameters& params) {
    ASSERT_DEBUG(laneCount != 0 && "laneCount must be non-zero");
    ASSERT_DEBUG(params.broadcastScratchSize != 0 && "broadcastScratchSize must be non-zero");
    
    SPMDGroup* group = ARENA_PUSH_STRUCT(arena, SPMDGroup);
    MEMSET(group, 0, sizeof(SPMDGroup));
    group->laneCount = laneCount;
    group->barrier = barrier_alloc(laneCount);
    group->dataSize = params.broadcastScratchSize;
    group->data = arena_push(arena, group->dataSize);
    group->nextLaneId = 0;
    return group;
}

void spmd_group_destroy(SPMDGroup* group) {
    ASSERT_DEBUG(group != nullptr && "group must be valid");
    barrier_release(group->barrier);
}

void spmd_join_group(SPMDGroup* group, U64 lane) {
    ASSERT_DEBUG(group != nullptr && "group must be valid");
    ASSERT_DEBUG(lane < group->laneCount && "lane out of range");
    
    SPMDMembership* membership = spmd_membership();
    membership->group = group;
    membership->laneId = lane;
    g_spmdThreadContext.initialized = 1;
}

U64 spmd_join_group_auto(SPMDGroup* group) {
    ASSERT_DEBUG(group != nullptr && "group must be valid");
    
    U64 lane = ATOMIC_FETCH_ADD(&group->nextLaneId, 1, MEMORY_ORDER_ACQ_REL);
    ASSERT_DEBUG(lane < group->laneCount && "Too many threads trying to join group");
    if (lane >= group->laneCount) {
        return (U64)-1;
    }
    spmd_join_group(group, lane);
    return lane;
}

void spmd_group_leave() {
    SPMDMembership* membership = spmd_membership();
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

RangeU64 spmd_split_range_(U64 totalCount, U64 laneId, U64 laneCount) {
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
    ASSERT_DEBUG(size <= group->dataSize && "Broadcast size exceeds preallocated buffer");

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
// SPMD Run (simplified dispatch without job system)

struct SPMDRunContext {
    SPMDGroup* group;
    SPMDKernel* kernel;
    void* kernelParameters;
};

static void spmd_run_thread_entry(void* arg) {
    SPMDRunContext* ctx = (SPMDRunContext*)arg;
    
    thread_context_init();
    DEFER(thread_context_release());
    
    U64 lane = spmd_join_group_auto(ctx->group);
    if (lane == (U64)-1) {
        return;
    }
    DEFER(spmd_group_leave());
    
    ctx->kernel(ctx->kernelParameters);
}

SPMDGroup* spmd_run_(Arena* arena, const SPMDRunOptions& options) {
    ASSERT_DEBUG(arena != nullptr);
    ASSERT_DEBUG(options.laneCount > 0);
    ASSERT_DEBUG(options.kernel != nullptr);
    
    U32 laneCount = options.laneCount;
    
    SPMDGroup* group = spmd_group_create(arena, laneCount, options.groupParams);
    ASSERT_DEBUG(group != nullptr);
    if (!group) {
        return nullptr;
    }
    
    SPMDRunContext* ctx = ARENA_PUSH_STRUCT(arena, SPMDRunContext);
    ctx->group = group;
    ctx->kernel = options.kernel;
    ctx->kernelParameters = options.kernelParameters;
    
    OS_Handle* threads = ARENA_PUSH_ARRAY(arena, OS_Handle, laneCount);
    
    for (U32 i = 1; i < laneCount; ++i) {
        threads[i] = OS_thread_create(spmd_run_thread_entry, ctx);
    }
    
    {
        U64 lane = spmd_join_group_auto(group);
        ASSERT_DEBUG(lane == 0 && "Main thread should be lane 0");
        DEFER(spmd_group_leave());
        
        options.kernel(options.kernelParameters);
    }
    
    for (U32 i = 1; i < laneCount; ++i) {
        OS_thread_join(threads[i]);
    }
    
    return group;
}



