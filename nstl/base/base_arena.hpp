//
// Created by André Leite on 26/07/2025.
//

#pragma once

// ////////////////////////
// Arena

enum ArenaFlags : U64 {
    None = 0,
    DoChain = (1 << 0),
};

struct ArenaParameters {
    U64 arenaSize = MB(4);
    U64 committedSize = KB(32);
    U64 flags = DoChain;
};

struct Arena {
    U64 reserved;
    U64 committed;
    U64 pos;
    U64 startPos; // This position is relative to the total arena size, including all blocks
    U64 flags;
    Arena* prev;
    Arena* current;
};

#define ARENA_HEADER_SIZE sizeof(Arena)

static Arena* arena_alloc_(const ArenaParameters& parameters);
#define arena_alloc(...) arena_alloc_({__VA_ARGS__})
static void arena_release(Arena* arena);

static void* arena_push(Arena* arena, U64 size, U64 alignment = sizeof(void*));
static void arena_pop_to(Arena* arena, U64 pos);
static U64 arena_get_pos(Arena* arena);

#define ARENA_PUSH_ARRAY_ALIGNED(arena, T, count, alignment) (T*)arena_push(arena, sizeof(T)*count, alignment)
#define ARENA_PUSH_ARRAY(arena, T, count) ARENA_PUSH_ARRAY_ALIGNED(arena, T, count, alignof(T))


// ////////////////////////
// Temp

struct Temp {
    Arena* arena;
    U64 pos;          // absolute position from arena_get_pos
    B32 is_temporary; // if true, release arena on temp_end
};

static Temp temp_begin(Arena* arena);
static void temp_end(Temp* t);


// ////////////////////////
// Scratch

#define SCRATCH_TLS_ARENA_COUNT 2

struct Scratch_TLS {
    Arena* slots[SCRATCH_TLS_ARENA_COUNT];
    U32 next_index; // alternate 0 -> SCRATCH_TLS_ARENA_COUNT-1
    B32 initialized;
};

static void scratch_thread_init_with_params(const ArenaParameters& params);
static void scratch_thread_init();
static void scratch_thread_shutdown();
static Temp get_scratch(Arena* const* excludes, U32 count);