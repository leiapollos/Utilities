//
// Created by André Leite on 15/10/2025.
//

// ////////////////////////
// String

static StringU8 str8(U8* source, U64 size) {
    return StringU8{source, size};
}

static StringU8 str8(const char* source, U64 size) {
    return StringU8{(U8*)source, size};
}

static StringU8 str8(const char* source) {
    if (source == 0) {
        return STR8_EMPTY;
    }
    return StringU8{(U8*)source, C_STR_LEN(source)};
}

static StringU8 str8_cpy(Arena* arena, StringU8 src) {
    StringU8 dst;
    dst.size = src.size;
    dst.data = ARENA_PUSH_ARRAY(arena, U8, dst.size + 1);
    if (dst.size) {
        MEMMOVE(dst.data, src.data, dst.size);
    }
    dst.data[dst.size] = '\0';
    return dst;
}

static StringU8 str8_cstring_cpy(Arena* arena, const char* source) {
    return str8_cpy(arena, str8(source));
}

static StringU8 str8_concat_(Arena* arena, StringU8 first, ...) {
    va_list args;

    va_start(args, first);
    U64 totalSize = first.size;
    StringU8 str;

    while (true) {
        str = va_arg(args, StringU8);
        if (str.data == nullptr)
            break;
        totalSize += str.size;
    }
    va_end(args);

    StringU8 dst;
    dst.size = totalSize;
    dst.data = ARENA_PUSH_ARRAY(arena, U8, totalSize + 1);

    va_start(args, first);
    U64 offset = 0;

    MEMMOVE(dst.data + offset, first.data, first.size);
    offset += first.size;

    while (1) {
        str = va_arg(args, StringU8);
        if (str.data == nullptr)
            break;
        MEMMOVE(dst.data + offset, str.data, str.size);
        offset += str.size;
    }
    va_end(args);

    dst.data[totalSize] = '\0';
    return dst;
}

static StringU8 str8_concat_n(Arena* arena,
                              const StringU8* pieces,
                              U64 count) {
    U64 total = 0;
    for (U64 i = 0; i < count; ++i)
        total += pieces[i].size;

    U8* out = ARENA_PUSH_ARRAY(arena, U8, total + 1);
    U64 off = 0;
    for (U64 i = 0; i < count; ++i) {
        if (pieces[i].size) {
            MEMMOVE(out + off, pieces[i].data, pieces[i].size);
            off += pieces[i].size;
        }
    }
    out[total] = '\0';
    return str8(out, total);
}

static B1 str8_is_nil(StringU8 s) {
    return (B1)(s.data == 0);
}

static B1 str8_is_empty(StringU8 s) {
    return (B1)(s.size == 0);
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
    return value ? str8("true") : str8("false");
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