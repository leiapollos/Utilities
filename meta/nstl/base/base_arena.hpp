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
};

struct Arena {
    U64 reserved;
    U64 committed;
    U64 pos;
    U64 startPos;
    U64 flags;
    Arena* prev;
    Arena* current;
};

#define ARENA_HEADER_SIZE sizeof(Arena)

Arena* arena_alloc_(const ArenaParameters& parameters);
#define arena_alloc(...) arena_alloc_({__VA_ARGS__})

void arena_release(Arena* arena);

void* arena_push(Arena* arena, U64 size, U64 alignment = sizeof(void*));
void arena_pop_to(Arena* arena, U64 pos);
U64 arena_get_pos(Arena* arena);

#define ARENA_PUSH_ARRAY_ALIGNED(arena, T, count, alignment) (T*)arena_push(arena, sizeof(T)*(count), alignment)
#define ARENA_PUSH_ARRAY(arena, T, count) ARENA_PUSH_ARRAY_ALIGNED(arena, T, count, alignof(T))
#define ARENA_PUSH_STRUCT(arena, T) (T*)arena_push(arena, sizeof(T), alignof(T))


// ////////////////////////
// Temp

struct Temp {
    Arena* arena;
    U64 pos;
    B32 isTemporary;
};

Temp temp_begin(Arena* arena);
void temp_end(Temp* t);

#define SCRATCH_BEGIN() Temp _scratch = get_scratch(0, 0)
#define SCRATCH_END() temp_end(&_scratch)


// ////////////////////////
// Thread-local Scratch Arenas

#define SCRATCH_ARENA_COUNT 2

struct ScratchArenas {
    Arena* slots[SCRATCH_ARENA_COUNT];
    U32 nextIndex;
    B32 initialized;
};

void thread_context_init();
void thread_context_release();
Temp get_scratch(Arena* const* excludes, U32 count);



