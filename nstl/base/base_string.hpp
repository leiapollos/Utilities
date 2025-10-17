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
static StringU8 str8_cstring_cpy(char* source);
static StringU8 str8_cstring(char* str);