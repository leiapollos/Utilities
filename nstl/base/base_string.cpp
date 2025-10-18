//
// Created by André Leite on 15/10/2025.
//

// ////////////////////////
// String

static StringU8 str8_cpy(Arena* arena, StringU8 src) {
    StringU8 dst;
    U64 length = src.length;
    dst.length = length;
    dst.data = ARENA_PUSH_ARRAY(arena, U8, length + 1);
    MEMMOVE(dst.data, src.data, length);
    dst.data[length] = '\0';
    return dst;
}

static StringU8 str8_cstring_cpy(Arena* arena, const char* source) {
    StringU8 dst;
    U64 length = C_STR_LEN(source);
    dst.length = length;
    dst.data = ARENA_PUSH_ARRAY(arena, U8, length + 1);
    MEMMOVE(dst.data, source, length);
    dst.data[length] = '\0';
    return dst;
}

static StringU8 str8_cstring(const char* cstr) {
    StringU8 str;
    U64 length = C_STR_LEN(cstr);
    str.length = length;
    str.data = (U8*)cstr;
    return str;
}