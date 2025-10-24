//
// Created by André Leite on 15/10/2025.
//

// ////////////////////////
// String

static StringU8 str8(U8* source, U64 size) {
    return StringU8{source, size};
}

static StringU8 str8(const char* source, U64 size) {
    return StringU8{(U8*) source, size};
}

static StringU8 str8(const char* source) {
    if (source == 0) {
        return STR8_EMPTY;
    }
    return StringU8{(U8*) source, C_STR_LEN(source)};
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

static B8 str8_is_nil(StringU8 s) {
    return (B8) (s.data == 0);
}

static B8 str8_is_empty(StringU8 s) {
    return (s.size == 0 && s.data != 0);
}

static StringU8 str8_from_U64(Arena* arena, U64 value, U64 base) {
    if (base < 2 || base > 16) {
        base = 10;
    }
    U8 buffer[65]; // Enough for base-2 of 64-bit value plus null.
    U64 i = 0;
    if (value == 0) {
        buffer[i++] = (U8) '0';
    } else {
        while (value != 0) {
            U64 digit = value % base;
            buffer[i++] = (U8) (digit < 10 ? ('0' + digit) : ('a' + (digit - 10)));
            value /= base;
        }
    }
    for (U64 l = 0, r = (i ? i - 1 : 0); l < r; ++l, --r) {
        U8 tmp = buffer[l];
        buffer[l] = buffer[r];
        buffer[r] = tmp;
    }
    StringU8 out;
    out.size = i;
    out.data = ARENA_PUSH_ARRAY(arena, U8, i + 1);
    if (i) {
        MEMMOVE(out.data, buffer, i);
    }
    out.data[i] = '\0';
    return out;
}

static StringU8 str8_from_S64(Arena* arena, S64 value) {
    char buffer[64];
    int len = snprintf(buffer, sizeof(buffer), "%lld", value);
    ASSERT_DEBUG(len >= 0);
    return str8_cpy(arena, str8((U8*) buffer, (U64) len));
}

static StringU8 str8_from_F64(Arena* arena, F64 value, int precision) {
    char buffer[64];
    int len = snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
    ASSERT_DEBUG(len >= 0);
    return str8_cpy(arena, str8((U8*) buffer, (U64) len));
}

static StringU8 str8_from_ptr(Arena* arena, const void* ptr) {
    char buffer[32];
    int len = snprintf(buffer, sizeof(buffer), "%p", ptr);
    ASSERT_DEBUG(len >= 0);
    return str8_cpy(arena, str8((U8*) buffer, (U64) len));
}

static StringU8 str8_from_char(Arena* arena, U8 c) {
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
    StringU8 copy = str8_cpy(l->arena, s);
    l->items[l->count++] = copy;
}
