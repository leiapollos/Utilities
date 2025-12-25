//
// Meta Pre-compiler - SPMD (Single Program Multiple Data)
// Simplified SPMD utilities for parallel processing
//

#pragma once

// ////////////////////////
// Barrier

struct Barrier {
    U64* barrier;
};

Barrier barrier_alloc(U32 count);
void barrier_release(Barrier barrier);
void barrier_wait(Barrier barrier);


// ////////////////////////
// SPMD Group

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

SPMDGroup* spmd_group_create_(Arena* arena, U32 laneCount, const SPMDGroupParameters& params);
#define spmd_group_create(arena, laneCount, ...) spmd_group_create_(arena, laneCount, {__VA_ARGS__})

void spmd_group_destroy(SPMDGroup* group);

void spmd_join_group(SPMDGroup* group, U64 lane);
U64 spmd_join_group_auto(SPMDGroup* group);
void spmd_group_leave();

SPMDGroup* spmd_current_group();
U64 spmd_lane_id();
U64 spmd_lane_count();

RangeU64 spmd_split_range_(U64 totalCount, U64 laneId, U64 laneCount);
#define SPMD_SPLIT_RANGE(totalCount) spmd_split_range_((totalCount), spmd_lane_id(), spmd_lane_count())

void spmd_broadcast(SPMDGroup* group, void* dst, void* src, U64 size, U64 rootLane);
void spmd_sync(SPMDGroup* group);
B32 spmd_is_root(SPMDGroup* group, U64 lane);

#define SPMD_SYNC() spmd_sync(spmd_current_group())
#define SPMD_IS_ROOT(lane) (spmd_is_root(spmd_current_group(), lane))
#define SPMD_BROADCAST(dst, src, rootLane) spmd_broadcast(spmd_current_group(), dst, src, sizeof(*(dst)), rootLane)


// ////////////////////////
// SPMD Run (simplified dispatch without job system)

typedef void SPMDKernel(void* kernelParameters);

struct SPMDRunOptions {
    U32 laneCount;
    SPMDKernel* kernel;
    void* kernelParameters;
    SPMDGroupParameters groupParams;
};

SPMDGroup* spmd_run_(Arena* arena, const SPMDRunOptions& options);
#define spmd_run(arena, ...) spmd_run_(arena, {__VA_ARGS__})