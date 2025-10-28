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
};

struct SPMDMembership {
    U64 laneId;
    SPMDGroup* group;
};

static SPMDGroup* spmd_create_group(Arena* arena, U64 laneCount);
static void spmd_destroy_group(SPMDGroup* group);

static void spmd_join_group(SPMDGroup* group, U64 lane);
static void spmd_group_leave();

static SPMDGroup* spmd_current_group();
static U64 spmd_lane_id();
static U64 spmd_lane_count();

static void spmd_broadcast(SPMDGroup* group, void* dst, void* src, U64 size, U64 rootLane);


// ////////////////////////
// Thread Context

struct ThreadContext {
    ScratchArenas* arenas;
    SPMDMembership* membership;
};

static ThreadContext* thread_context_alloc();
static void thread_context_release();
static ThreadContext* thread_context();