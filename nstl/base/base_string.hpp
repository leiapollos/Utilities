//
// Created by André Leite on 15/10/2025.
//

#pragma once

// ////////////////////////
// String

struct StringU8 {
    U8* data;
    U64 size;
};

static const StringU8 STR8_NIL = {(U8*)0, 0};
static const StringU8 STR8_EMPTY = {(U8*)"", 0};

static StringU8 str8(U8* source, U64 size);
static StringU8 str8(const char* source, U64 size);
static StringU8 str8(const char* source);
static StringU8 str8_cpy(Arena* arena, const char* source, U64 size);
static StringU8 str8_cpy(Arena* arena, const char* source);

static StringU8 str8_concat_n(Arena* arena, const StringU8* pieces, U64 count);
#define str8_concat(res, arena, ...) \
    {                                                                               \
        StringU8 _str8_concat_pieces[] = { __VA_ARGS__ };                           \
        res = str8_concat_n((arena), _str8_concat_pieces,                           \
                            (U64)(sizeof(_str8_concat_pieces)/sizeof(StringU8)));   \
    }

static B1 str8_is_nil(StringU8 s);
static B1 str8_is_empty(StringU8 s);

static StringU8 str8_from_U64(Arena* arena, U64 value, U64 base);
static StringU8 str8_from_S64(Arena* arena, S64 value);
static StringU8 str8_from_F64(Arena* arena, F64 value, int precision);
static StringU8 str8_from_bool(Arena* arena, B1 value);
static StringU8 str8_from_ptr(Arena* arena, const void* ptr);
static StringU8 str8_from_char(Arena* arena, U8 c);

struct Str8List {
    StringU8* items;
    U64 count;
    U64 cap;
    Arena* arena;
};

static void str8list_init(Str8List* l, Arena* arena, U64 initialCap);
static void str8list_push(Str8List* l, StringU8 s);
