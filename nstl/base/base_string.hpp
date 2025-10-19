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
static StringU8 str8(const char* data, U64 length);
static StringU8 str8_concat_(Arena* arena, StringU8 first, ...);
#define str8_concat(arena, ...) str8_concat_(arena, __VA_ARGS__, (StringU8){0})

static StringU8 str8_from_U64(Arena* arena, U64 value, U64 base = 10);
static StringU8 str8_from_S64(Arena* arena, S64 value);
static StringU8 str8_from_F64(Arena* arena, F64 value, int precision = 5);
static StringU8 str8_from_bool(Arena* arena, B1 value);
static StringU8 str8_from_ptr(Arena* arena, const void* ptr);
static StringU8 str8_from_char(Arena* arena, char c);

#define STR8_EMPTY() (str8_cstring(""))
