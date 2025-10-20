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
    str.data = (U8*) cstr;
    return str;
}

static StringU8 str8(U8* data, U64 length) {
    StringU8 result;
    result.data = data;
    result.length = length;
    return result;
}

static StringU8 str8_concat_(Arena* arena, StringU8 first, ...) {
    va_list args;

    va_start(args, first);
    U64 totalLength = first.length;
    StringU8 str;

    while (true) {
        str = va_arg(args, StringU8);
        if (str.data == nullptr)
            break;
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
        if (str.data == nullptr)
            break;
        MEMMOVE(dst.data + offset, str.data, str.length);
        offset += str.length;
    }
    va_end(args);

    dst.data[totalLength] = '\0';
    return dst;
}

static StringU8 str8_concat_n(Arena* arena,
                              const StringU8* pieces,
                              U64 count) {
    U64 total = 0;
    for (U64 i = 0; i < count; ++i)
        total += pieces[i].length;

    U8* out = ARENA_PUSH_ARRAY(arena, U8, total + 1);
    U64 off = 0;
    for (U64 i = 0; i < count; ++i) {
        if (pieces[i].length) {
            MEMMOVE(out + off, pieces[i].data, pieces[i].length);
            off += pieces[i].length;
        }
    }
    out[total] = '\0';
    return str8(out, total);
}

static StringU8 str8_from_U64(Arena* arena, U64 value, U64 base) {
    char buffer[64];
    int len = snprintf(buffer, sizeof(buffer), "%llu", value);
    return str8_cpy(arena, str8((U8*) buffer, len));
}

static StringU8 str8_from_S64(Arena* arena, S64 value) {
    char buffer[64];
    int len = snprintf(buffer, sizeof(buffer), "%lld", value);
    return str8_cpy(arena, str8((U8*) buffer, len));
}

static StringU8 str8_from_F64(Arena* arena, F64 value, int precision) {
    char buffer[64];
    int len = snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
    return str8_cpy(arena, str8((U8*) buffer, len));
}

static StringU8 str8_from_bool(Arena* arena, B1 value) {
    return value ? str8_cstring("true") : str8_cstring("false");
}

static StringU8 str8_from_ptr(Arena* arena, const void* ptr) {
    char buffer[32];
    int len = snprintf(buffer, sizeof(buffer), "%p", ptr);
    return str8_cpy(arena, str8((U8*) buffer, len));
}

static StringU8 str8_from_char(Arena* arena, char c) {
    U8* data = ARENA_PUSH_ARRAY(arena, U8, 2);
    data[0] = c;
    data[1] = '\0';
    return str8(data, 1);
}

static void str8list_init(Str8List* l, Arena* arena, U64 initialCap) {
    l->arena = arena;
    l->count = 0;
    l->cap = initialCap ? initialCap : 8;
    l->items = ARENA_PUSH_ARRAY(arena, StringU8, l->cap);
}

static void str8list_push(Str8List* l, StringU8 s) {
    if (l->count >= l->cap) {
        U64 newCap = l->cap * 2;
        StringU8* n = ARENA_PUSH_ARRAY(l->arena, StringU8, newCap);
        MEMMOVE(n, l->items, l->count * (U64)sizeof(StringU8));
        l->items = n;
        l->cap = newCap;
    }
    l->items[l->count++] = s;
}