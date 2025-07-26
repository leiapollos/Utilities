//
// Created by André Leite on 26/07/2025.
//

#pragma once

// ////////////////////////
// Arena

struct Arena {
    U64 reserved;
    U64 committed;
    U64 pos;
};

#define ARENA_HEADER_SIZE sizeof(Arena)

Arena* arena_alloc(U64 arenaSize = MB(4));
void arena_release(Arena* arena);

void* arena_push(Arena* arena, U64 size, U64 alignment = sizeof(void*));
void arena_set_pos(Arena* arena, U64 newPos);
U64 arena_get_pos(Arena* arena);
U64 arena_get_committed(Arena* arena);
U64 arena_get_reserved(Arena* arena);
