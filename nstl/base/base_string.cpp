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

static StringU8 str8_concat_(Arena* arena, StringU8 first, ...) {
    va_list args;

    va_start(args, first);
    U64 totalLength = first.length;
    StringU8 str;

    while (true) {
        str = va_arg(args, StringU8);
        if (str.data == nullptr) break;
        totalLength += str.length;
    }
    va_end(args);

    StringU8 dst;
    dst.length = totalLength;
    dst.data = ARENA_PUSH_ARRAY(arena, U8, totalLength + 1);

    va_start(args, first);
    U64 offset = 0;

    MEMMOVE(dst.data + offset, first.data, first.length);
    offset += first.length;

    while (1) {
        str = va_arg(args, StringU8);
        if (str.data == nullptr) break;
        MEMMOVE(dst.data + offset, str.data, str.length);
        offset += str.length;
    }
    va_end(args);

    dst.data[totalLength] = '\0';
    return dst;
}