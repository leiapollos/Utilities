//
// Created by André Leite on 27/10/2025.
//

#pragma once

// ////////////////////////
// Barrier

struct Barrier {
    U64* barrier;
};

static Barrier barrier_alloc(U32 count);
static void barrier_release(Barrier barrier);
static void barrier_wait(Barrier barrier);


// ////////////////////////
// SPMD (Single Program Multiple Data)

struct SPMDGroup {
    U64 laneCount;
    Barrier barrier;

    void* data;
    U64 dataSize;
    
    U64 nextLaneId;
};

struct SPMDMembership {
    U64 laneId;
    SPMDGroup* group;
};

struct SPMDGroupParameters {
    U64 broadcastScratchSize = KB(1);
};

static SPMDGroup* spmd_create_group_(Arena* arena, U32 laneCount, const SPMDGroupParameters& params);
#define spmd_group_create(arena, laneCount, ...) spmd_create_group_(arena, laneCount, {__VA_ARGS__})

static void spmd_destroy_group(SPMDGroup* group);

static void spmd_join_group(SPMDGroup* group, U64 lane);
static U64 spmd_join_group_auto(SPMDGroup* group);
static void spmd_group_leave();

static SPMDGroup* spmd_current_group();
static U64 spmd_lane_id();
static U64 spmd_lane_count();

static RangeU64 spmd_split_range_(U64 totalCount, U64 laneId, U64 laneCount);
#define SPMD_SPLIT_RANGE(totalCount) spmd_split_range_((totalCount), spmd_lane_id(), spmd_lane_count())

static void spmd_broadcast(SPMDGroup* group, void* dst, void* src, U64 size, U64 rootLane);
static void spmd_sync(SPMDGroup* group);
static B32 spmd_is_root(SPMDGroup* group, U64 lane);

#define SPMD_SYNC() spmd_sync(spmd_current_group())
#define SPMD_IS_ROOT(lane) (spmd_is_root(spmd_current_group(), lane))
#define SPMD_BROADCAST(dst, src, rootLane) spmd_broadcast(spmd_current_group(), dst, src, sizeof(*(dst)), rootLane)

// ////////////////////////
// SPMD Dispatch via Job System

typedef void SPMDKernel(void* kernelParameters);

struct SPMDDispatchParameters {
    SPMDGroup* group;
    U64 laneId;
    SPMDKernel* kernel;
    void* kernelParameters;
};

struct SPMDDispatchOptions {
    U32 laneCount;
    SPMDKernel* kernel;
    void* kernelParameters;
    SPMDGroupParameters groupParams;
};

static SPMDGroup* spmd_dispatch_(JobSystem* jobSystem, Arena* arena, const SPMDDispatchOptions& options);
#define spmd_dispatch(jobSystem, arena, ...) spmd_dispatch_(jobSystem, arena, {__VA_ARGS__})


// ////////////////////////
// Thread Context

struct ThreadContext {
    ScratchArenas* arenas;
    SPMDMembership* membership;
};

ThreadContext* thread_context_alloc();
void thread_context_release();
ThreadContext* thread_context();