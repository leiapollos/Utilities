//
// Created by André Leite on 26/07/2025.
//

#pragma once

// ////////////////////////
// Arena

enum ArenaFlags {
    ArenaFlags_None = 0,
    ArenaFlags_DoChain = (1 << 0),
};

struct ArenaParameters {
    U64 arenaSize = MB(4);
    U64 committedSize = KB(32);
    U64 flags = ArenaFlags_DoChain;
    const char* debugName = 0; // static string; non-null registers the arena for debug snapshots
};

struct Arena {
    U64 reserved;
    U64 committed;
    U64 pos;
    U64 startPos; // This position is relative to the total arena size, including all blocks
    U64 highWater; // max arena_get_pos ever reached, survives pops
    U64 flags;
    Arena* prev;
    Arena* current;
};

#define ARENA_HEADER_SIZE sizeof(Arena)

UTILITIES_SHARED_API Arena* arena_alloc_(const ArenaParameters& parameters);
#define arena_alloc(...) arena_alloc_({__VA_ARGS__})
UTILITIES_SHARED_API void arena_release(Arena* arena);

UTILITIES_SHARED_API void* arena_push(Arena* arena, U64 size, U64 alignment = sizeof(void*));
UTILITIES_SHARED_API void arena_pop_to(Arena* arena, U64 pos);
UTILITIES_SHARED_API U64 arena_get_pos(Arena* arena);

// Debug registry: arenas allocated with a debugName appear in snapshots.
// Values are read without synchronizing against the owning thread — display
// quality only.
#define ARENA_DEBUG_MAX 128u

struct ArenaDebugInfo {
    const char* name;
    U64 reserved;
    U64 committed;
    U64 pos;
    U64 highWater;
    U32 blockCount;
};

UTILITIES_SHARED_API U32 arena_debug_snapshot(ArenaDebugInfo* out, U32 capacity);

#define ARENA_PUSH_ARRAY_ALIGNED(arena, T, count, alignment) (T*)arena_push((arena), sizeof(T) * (count), (alignment))
#define ARENA_PUSH_ARRAY(arena, T, count) ARENA_PUSH_ARRAY_ALIGNED(arena, T, count, alignof(T))
#define ARENA_PUSH_STRUCT(arena, T) (T*)arena_push(arena, sizeof(T), alignof(T))


// ////////////////////////
// Temp

struct Temp {
    Arena* arena;
    U64 pos;          // absolute position from arena_get_pos
    B32 isTemporary; // if true, release arena on temp_end
};

UTILITIES_SHARED_API Temp temp_begin(Arena* arena);
UTILITIES_SHARED_API void temp_end(Temp* t);


// ////////////////////////
// Scratch

#define SCRATCH_TLS_ARENA_COUNT 2

struct ScratchArenas {
    Arena* slots[SCRATCH_TLS_ARENA_COUNT];
    U32 nextIndex;
    B32 initialized;
};
static_assert(is_power_of_two(SCRATCH_TLS_ARENA_COUNT), "SCRATCH_TLS_ARENA_COUNT must be a power of two");

UTILITIES_SHARED_API Temp get_scratch(Arena* const* excludes, U32 count);
