//
// Created by André Leite on 15/10/2025.
//

#pragma once

// ////////////////////////
// String

struct StringU8 {
    U8* data;
    U64 length;
};

static StringU8 str8(Arena* arena, StringU8 source);
static StringU8 str8_cstring_cpy(Arena* arena, const char* source);
static StringU8 str8_cstring(const char* str);
static StringU8 str8_concat_(Arena* arena, StringU8 a, StringU8 b);

#define str8_concat(arena, ...) str8_concat_(arena, __VA_ARGS__, (StringU8){0})