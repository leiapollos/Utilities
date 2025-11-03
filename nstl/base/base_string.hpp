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

StringU8 str8(U8* source, U64 size);
StringU8 str8(const char* source, U64 size);
StringU8 str8(const char* source);
StringU8 str8_cpy(Arena* arena, const char* source, U64 size);
StringU8 str8_cpy(Arena* arena, const char* source);
StringU8 str8_cpy(Arena* arena, StringU8 src);

StringU8 str8_concat_n(Arena* arena, const StringU8* pieces, U64 count);
#ifdef CPP_LANG

/* C++ (requires C++11) */
#define str8_concat(arena, ...)                                             \
    ([&](Arena* _a) -> StringU8 {                                           \
        StringU8 _pieces[] = { __VA_ARGS__ };                               \
        return str8_concat_n(_a,                                            \
                             _pieces,                                       \
                             (U64)(sizeof(_pieces) / sizeof(_pieces[0])));  \
    }((arena)))

#else

/* C (requires C99 compound literals) */
#define str8_concat(arena, ...)                                                \
    (str8_concat_n((arena),                                                    \
                   (StringU8[]){ __VA_ARGS__ },                                     \
                   (U64)(sizeof((StringU8[]){ __VA_ARGS__ }) / sizeof(StringU8))))

#endif

B8 str8_is_nil(StringU8 s);
B8 str8_is_empty(StringU8 s);
B8 str8_equal(StringU8 a, StringU8 b);

StringU8 str8_from_U64(Arena* arena, U64 value, U64 base);
StringU8 str8_from_S64(Arena* arena, S64 value);
StringU8 str8_from_F64(Arena* arena, F64 value, int precision);
StringU8 str8_from_ptr(Arena* arena, const void* ptr);
StringU8 str8_from_char(Arena* arena, U8 c);
StringU8 str8_to_uppercase(Arena* arena, StringU8 s);

struct Str8List {
    StringU8* items;
    U64 count;
    U64 cap;
    Arena* arena;
};

void str8list_init(Str8List* l, Arena* arena, U64 initialCap);
void str8list_push(Str8List* l, StringU8 s);
