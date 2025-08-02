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
ENABLE_BITMASK_OPERATORS(ArenaFlags)

struct ArenaParameters {
    U64 arenaSize = MB(4);
    U64 committedSize = KB(32);
    Flags<ArenaFlags> flags = DoChain;
};

struct Arena {
    U64 reserved;
    U64 committed;
    U64 pos;
    U64 startPos; // This position is relative to the total arena size, including all blocks
    Flags<ArenaFlags> flags;
    Arena* prev;
    Arena* current;
};

#define ARENA_HEADER_SIZE sizeof(Arena)

static Arena* arena_alloc(const ArenaParameters& parameters);
static void arena_release(Arena* arena);

static void* arena_push(Arena* arena, U64 size, U64 alignment = sizeof(void*));
static void arena_pop_to(Arena* arena, U64 pos);
static U64 arena_get_pos(Arena* arena);
static U64 arena_get_committed(Arena* arena);
static U64 arena_get_reserved(Arena* arena);
