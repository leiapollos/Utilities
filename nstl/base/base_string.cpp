//
// Created by André Leite on 15/10/2025.
//

READ_ONLY StringU8 STR8_NIL = {(U8*)0, 0};
READ_ONLY StringU8 STR8_EMPTY = {(U8*)"", 0};

// ////////////////////////
// String

StringU8 str8(U8* source, U64 size) {
    return StringU8{source, size};
}

StringU8 str8(const char* source, U64 size) {
    return StringU8{(U8*) source, size};
}

StringU8 str8(const char* source) {
    if (source == 0) {
        return STR8_EMPTY;
    }
    return StringU8{(U8*) source, C_STR_LEN(source)};
}

StringU8 str8_cpy(Arena* arena, StringU8 src) {
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


StringU8 str8_concat_n(Arena* arena,
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

B8 str8_is_nil(StringU8 s) {
    return (B8) (s.data == 0);
}

B8 str8_is_empty(StringU8 s) {
    return (s.size == 0 && s.data != 0);
}

B8 str8_equal(StringU8 a, StringU8 b) {
    if (a.size != b.size) {
        return 0;
    }
    if (a.size == 0) {
        return 1;
    }
    return (MEMCMP(a.data, b.data, a.size) == 0);
}

StringU8 str8_from_U64(Arena* arena, U64 value, U64 base) {
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

StringU8 str8_from_S64(Arena* arena, S64 value) {
    U8 buffer[21];
    U64 i = 0;
    B32 negative = (value < 0);
    U64 absVal = negative ? (U64)(-value) : (U64)value;

    if (absVal == 0) {
        buffer[i++] = '0';
    } else {
        while (absVal > 0) {
            buffer[i++] = (U8)('0' + (absVal % 10));
            absVal /= 10;
        }
    }
    if (negative) {
        buffer[i++] = '-';
    }
    for (U64 l = 0, r = i - 1; l < r; ++l, --r) {
        U8 tmp = buffer[l];
        buffer[l] = buffer[r];
        buffer[r] = tmp;
    }

    StringU8 out;
    out.size = i;
    out.data = ARENA_PUSH_ARRAY(arena, U8, i + 1);
    if (i) { MEMMOVE(out.data, buffer, i); }
    out.data[i] = '\0';
    return out;
}

StringU8 str8_from_F64(Arena* arena, F64 value, int precision) {
    U8 buffer[64];
    U64 i = 0;

    if (value < 0.0) {
        buffer[i++] = '-';
        value = -value;
    }

    U64 intPart = (U64)value;
    F64 fracPart = value - (F64)intPart;

    if (intPart == 0) {
        buffer[i++] = '0';
    } else {
        U8 intBuf[20];
        U64 j = 0;
        while (intPart > 0) {
            intBuf[j++] = (U8)('0' + (intPart % 10));
            intPart /= 10;
        }
        while (j > 0) {
            buffer[i++] = intBuf[--j];
        }
    }

    if (precision > 0) {
        buffer[i++] = '.';
        for (int p = 0; p < precision; ++p) {
            fracPart *= 10.0;
            int digit = (int)fracPart;
            buffer[i++] = (U8)('0' + digit);
            fracPart -= digit;
        }
    }

    StringU8 out;
    out.size = i;
    out.data = ARENA_PUSH_ARRAY(arena, U8, i + 1);
    if (i) { MEMMOVE(out.data, buffer, i); }
    out.data[i] = '\0';
    return out;
}

StringU8 str8_from_ptr(Arena* arena, const void* ptr) {
    U8 buffer[20];
    uintptr val = (uintptr)ptr;

    buffer[0] = '0';
    buffer[1] = 'x';
    U64 i = 2;

    for (int shift = (sizeof(uintptr) * 8 - 4); shift >= 0; shift -= 4) {
        int digit = (val >> shift) & 0xF;
        buffer[i++] = (U8)(digit < 10 ? '0' + digit : 'a' + (digit - 10));
    }

    StringU8 out;
    out.size = i;
    out.data = ARENA_PUSH_ARRAY(arena, U8, i + 1);
    if (i) { MEMMOVE(out.data, buffer, i); }
    out.data[i] = '\0';
    return out;
}

StringU8 str8_from_char(Arena* arena, U8 c) {
    U8* data = ARENA_PUSH_ARRAY(arena, U8, 2);
    data[0] = c;
    data[1] = '\0';
    return str8(data, 1);
}

StringU8 str8_to_uppercase(Arena* arena, StringU8 s) {
    StringU8 result;
    result.size = s.size;
    result.data = ARENA_PUSH_ARRAY(arena, U8, s.size + 1);
    for (U64 i = 0; i < s.size; ++i) {
        U8 c = s.data[i];
        if (c >= 'a' && c <= 'z') {
            result.data[i] = (U8) (c - 'a' + 'A');
        } else {
            result.data[i] = c;
        }
    }
    result.data[s.size] = '\0';
    return result;
}

void str8list_init(Str8List* l, Arena* arena, U64 initialCap) {
    l->arena = arena;
    l->count = 0;
    l->cap = initialCap ? initialCap : 8;
    l->items = ARENA_PUSH_ARRAY(arena, StringU8, l->cap);
}

void str8list_push(Str8List* l, StringU8 s) {
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


// ////////////////////////
// String Formatting

Str8FmtSpec str8_fmt_parse_spec(StringU8 specStr) {
    Str8FmtSpec spec;
    spec.hasSpec = 0;
    spec.floatPrecision = 5;
    spec.intBase = 10;
    spec.uppercaseHex = 0;

    if (specStr.size == 0) {
        return spec;
    }

    const U8* data = specStr.data;
    U64 i = 0;
    U64 len = specStr.size;

    if (i < len && data[i] == ':') {
        ++i;
        spec.hasSpec = 1;

        if (i < len && data[i] == '.') {
            ++i;
            int precision = 0;
            U64 precisionStart = i;
            while (i < len && data[i] >= '0' && data[i] <= '9') {
                precision = precision * 10 + (data[i] - '0');
                ++i;
            }
            if (i > precisionStart) {
                spec.floatPrecision = precision;
            }
        }

        if (i < len) {
            U8 c = data[i];
            if (c == 'x') {
                spec.intBase = 16;
            } else if (c == 'X') {
                spec.intBase = 16;
                spec.uppercaseHex = 1;
            } else if (c == 'b') {
                spec.intBase = 2;
            } else if (c == 'o') {
                spec.intBase = 8;
            }
        }
    }
    return spec;
}

StringU8 str8_fmt_arg_to_string(Arena* arena, const Str8FmtArg& arg, Str8FmtSpec spec) {
    switch (arg.kind) {
        case Str8FmtKind_S64: {
            if (spec.hasSpec && spec.intBase != 10) {
                U64 unsigned_val = (arg.s64Val < 0) ? (U64)(-arg.s64Val) : (U64)arg.s64Val;
                StringU8 result = str8_from_U64(arena, unsigned_val, spec.intBase);
                if (spec.uppercaseHex) {
                    result = str8_to_uppercase(arena, result);
                }
                if (arg.s64Val < 0) {
                    return str8_concat(arena, str8("-"), result);
                }
                return result;
            }
            return str8_from_S64(arena, arg.s64Val);
        }
        case Str8FmtKind_U64: {
            if (spec.hasSpec) {
                StringU8 result = str8_from_U64(arena, arg.u64Val, spec.intBase);
                if (spec.uppercaseHex) {
                    result = str8_to_uppercase(arena, result);
                }
                return result;
            }
            return str8_from_U64(arena, arg.u64Val, 10);
        }
        case Str8FmtKind_F64: {
            int precision = spec.hasSpec ? spec.floatPrecision : 5;
            return str8_from_F64(arena, arg.f64Val, precision);
        }
        case Str8FmtKind_CSTR:
            return str8(arg.cstrVal);
        case Str8FmtKind_CHAR:
            return str8_from_char(arena, arg.charVal);
        case Str8FmtKind_PTR:
            return str8_from_ptr(arena, arg.ptrVal);
        case Str8FmtKind_STRINGU8:
            return arg.stringU8Val;
    }
    return STR8_EMPTY;
}

StringU8 str8_fmt_(Arena* arena, StringU8 fmt, const Str8FmtArg* args, U64 argCount) {
    Str8List pieces;
    str8list_init(&pieces, arena, 16);

    const U8* data = fmt.data;
    U64 len = fmt.size;
    U64 i = 0;
    U64 last = 0;
    const Str8FmtArg* it = args;
    const Str8FmtArg* end = args ? args + argCount : 0;

    while (i < len) {
        if (data[i] == '{') {
            if (i + 1 < len && data[i + 1] == '{') {
                if (i > last) {
                    str8list_push(&pieces, str8((U8*)(data + last), i - last));
                }
                str8list_push(&pieces, str8("{"));
                i += 2;
                last = i;
            } else {
                U64 specStart = i + 1;
                U64 specEnd = specStart;
                while (specEnd < len && data[specEnd] != '}') {
                    ++specEnd;
                }
                if (specEnd < len) {
                    if (i > last) {
                        str8list_push(&pieces, str8((U8*)(data + last), i - last));
                    }
                    StringU8 specStr = str8((U8*)(data + specStart), specEnd - specStart);
                    Str8FmtSpec spec = str8_fmt_parse_spec(specStr);
                    if (it && it < end) {
                        str8list_push(&pieces, str8_fmt_arg_to_string(arena, *it, spec));
                        ++it;
                    } else {
                        str8list_push(&pieces, str8("<MISSING>"));
                    }
                    i = specEnd + 1;
                    last = i;
                } else {
                    ++i;
                }
            }
        } else if (data[i] == '}') {
            if (i + 1 < len && data[i + 1] == '}') {
                if (i > last) {
                    str8list_push(&pieces, str8((U8*)(data + last), i - last));
                }
                str8list_push(&pieces, str8("}"));
                i += 2;
                last = i;
            } else {
                ++i;
            }
        } else {
            ++i;
        }
    }
    if (last < len) {
        str8list_push(&pieces, str8((U8*)(data + last), len - last));
    }

    return str8_concat_n(arena, pieces.items, pieces.count);
}
