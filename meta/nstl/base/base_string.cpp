//
// Created by André Leite on 15/10/2025.
//

StringU8 STR8_NIL = {(U8*)0, 0};
StringU8 STR8_EMPTY = {(U8*)"", 0};

// ////////////////////////
// String

StringU8 str8(U8* source, U64 size) {
    return StringU8{source, size};
}

StringU8 str8(const char* source, U64 size) {
    return StringU8{(U8*)source, size};
}

StringU8 str8(const char* source) {
    if (source == 0) {
        return STR8_EMPTY;
    }
    return StringU8{(U8*)source, C_STR_LEN(source)};
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

StringU8 str8_cpy(Arena* arena, const char* source) {
    return str8_cpy(arena, str8(source));
}

StringU8 str8_concat_n(Arena* arena, const StringU8* pieces, U64 count) {
    U64 total = 0;
    for (U64 i = 0; i < count; ++i) {
        total += pieces[i].size;
    }

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
    return (B8)(s.data == 0);
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
    U8 buffer[65];
    U64 i = 0;
    if (value == 0) {
        buffer[i++] = (U8)'0';
    } else {
        while (value != 0) {
            U64 digit = value % base;
            buffer[i++] = (U8)(digit < 10 ? ('0' + digit) : ('a' + (digit - 10)));
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
    char buffer[64];
    int len = snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    ASSERT_DEBUG(len >= 0);
    return str8_cpy(arena, str8((U8*)buffer, (U64)len));
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
            result.data[i] = (U8)(c - 'a' + 'A');
        } else {
            result.data[i] = c;
        }
    }
    result.data[s.size] = '\0';
    return result;
}

StringU8 str8_to_lowercase(Arena* arena, StringU8 s) {
    StringU8 result;
    result.size = s.size;
    result.data = ARENA_PUSH_ARRAY(arena, U8, s.size + 1);
    for (U64 i = 0; i < s.size; ++i) {
        U8 c = s.data[i];
        if (c >= 'A' && c <= 'Z') {
            result.data[i] = (U8)(c - 'A' + 'a');
        } else {
            result.data[i] = c;
        }
    }
    result.data[s.size] = '\0';
    return result;
}

StringU8 str8_to_snake_case(Arena* arena, StringU8 s) {
    if (s.size == 0) {
        return STR8_EMPTY;
    }
    
    U64 resultSize = 0;
    for (U64 i = 0; i < s.size; ++i) {
        U8 c = s.data[i];
        B8 isUpper = (c >= 'A' && c <= 'Z');
        if (isUpper && i > 0) {
            resultSize++;
        }
        resultSize++;
    }
    
    U8* out = ARENA_PUSH_ARRAY(arena, U8, resultSize + 1);
    U64 j = 0;
    for (U64 i = 0; i < s.size; ++i) {
        U8 c = s.data[i];
        B8 isUpper = (c >= 'A' && c <= 'Z');
        if (isUpper && i > 0) {
            out[j++] = '_';
        }
        out[j++] = (U8)(isUpper ? (c - 'A' + 'a') : c);
    }
    out[resultSize] = '\0';
    return str8(out, resultSize);
}

B8 str8_starts_with(StringU8 s, StringU8 prefix) {
    if (prefix.size > s.size) {
        return 0;
    }
    return (MEMCMP(s.data, prefix.data, prefix.size) == 0);
}

B8 str8_ends_with(StringU8 s, StringU8 suffix) {
    if (suffix.size > s.size) {
        return 0;
    }
    return (MEMCMP(s.data + s.size - suffix.size, suffix.data, suffix.size) == 0);
}

StringU8 str8_substr(StringU8 s, U64 start, U64 len) {
    if (start >= s.size) {
        return STR8_EMPTY;
    }
    U64 maxLen = s.size - start;
    if (len > maxLen) {
        len = maxLen;
    }
    return str8(s.data + start, len);
}

StringU8 str8_trim_whitespace(StringU8 s) {
    U64 start = 0;
    U64 end = s.size;
    
    while (start < end) {
        U8 c = s.data[start];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            start++;
        } else {
            break;
        }
    }
    
    while (end > start) {
        U8 c = s.data[end - 1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            end--;
        } else {
            break;
        }
    }
    
    return str8(s.data + start, end - start);
}

S64 str8_find_char(StringU8 s, U8 c) {
    for (U64 i = 0; i < s.size; ++i) {
        if (s.data[i] == c) {
            return (S64)i;
        }
    }
    return -1;
}

S64 str8_find_char_last(StringU8 s, U8 c) {
    for (S64 i = (S64)s.size - 1; i >= 0; --i) {
        if (s.data[i] == c) {
            return i;
        }
    }
    return -1;
}


// ////////////////////////
// Path Utilities

StringU8 str8_path_extension(StringU8 path) {
    S64 lastSlash = str8_find_char_last(path, '/');
    S64 lastDot = str8_find_char_last(path, '.');
    
    if (lastDot < 0 || lastDot <= lastSlash) {
        return STR8_EMPTY;
    }
    
    return str8_substr(path, (U64)lastDot, path.size - (U64)lastDot);
}

StringU8 str8_path_basename(StringU8 path) {
    S64 lastSlash = str8_find_char_last(path, '/');
    if (lastSlash < 0) {
        return path;
    }
    return str8_substr(path, (U64)(lastSlash + 1), path.size - (U64)(lastSlash + 1));
}

StringU8 str8_path_stem(StringU8 path) {
    StringU8 basename = str8_path_basename(path);
    S64 lastDot = str8_find_char_last(basename, '.');
    
    if (lastDot <= 0) {
        return basename;
    }
    
    return str8_substr(basename, 0, (U64)lastDot);
}

StringU8 str8_path_directory(StringU8 path) {
    S64 lastSlash = str8_find_char_last(path, '/');
    if (lastSlash < 0) {
        return str8(".");
    }
    if (lastSlash == 0) {
        return str8("/");
    }
    return str8_substr(path, 0, (U64)lastSlash);
}

StringU8 str8_strip_suffix(StringU8 s, StringU8 suffix) {
    if (!str8_ends_with(s, suffix)) {
        return s;
    }
    return str8_substr(s, 0, s.size - suffix.size);
}

StringU8 str8_path_join(Arena* arena, StringU8 dir, StringU8 filename) {
    if (dir.size == 0) {
        return str8_cpy(arena, filename);
    }
    if (filename.size == 0) {
        return str8_cpy(arena, dir);
    }
    
    B8 dirHasSlash = (dir.data[dir.size - 1] == '/');
    B8 fileHasSlash = (filename.data[0] == '/');
    
    if (dirHasSlash && fileHasSlash) {
        return str8_concat(arena, dir, str8_substr(filename, 1, filename.size - 1));
    } else if (dirHasSlash || fileHasSlash) {
        return str8_concat(arena, dir, filename);
    } else {
        return str8_concat(arena, dir, str8("/"), filename);
    }
}

StringU8 str8_path_normalize(StringU8 path) {
    if (path.size == 0) {
        return path;
    }
    
    U64 end = path.size;
    while (end > 1 && path.data[end - 1] == '/') {
        end--;
    }
    
    return str8_substr(path, 0, end);
}


// ////////////////////////
// String List

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
    l->items[l->count++] = s;
}

StringU8 str8list_join(Str8List* l, StringU8 separator) {
    if (l->count == 0) {
        return STR8_EMPTY;
    }
    
    U64 total = 0;
    for (U64 i = 0; i < l->count; ++i) {
        total += l->items[i].size;
        if (i > 0) {
            total += separator.size;
        }
    }
    
    U8* out = ARENA_PUSH_ARRAY(l->arena, U8, total + 1);
    U64 off = 0;
    for (U64 i = 0; i < l->count; ++i) {
        if (i > 0 && separator.size > 0) {
            MEMMOVE(out + off, separator.data, separator.size);
            off += separator.size;
        }
        if (l->items[i].size > 0) {
            MEMMOVE(out + off, l->items[i].data, l->items[i].size);
            off += l->items[i].size;
        }
    }
    out[total] = '\0';
    return str8(out, total);
}


// ////////////////////////
// String Builder

void str8builder_init(Str8Builder* sb, Arena* arena, U64 initialCap) {
    sb->arena = arena;
    sb->cap = initialCap ? initialCap : 256;
    sb->data = ARENA_PUSH_ARRAY(arena, U8, sb->cap);
    sb->size = 0;
}

static void str8builder_ensure_capacity(Str8Builder* sb, U64 additionalSize) {
    U64 needed = sb->size + additionalSize;
    if (needed > sb->cap) {
        U64 newCap = sb->cap * 2;
        while (newCap < needed) {
            newCap *= 2;
        }
        U8* newData = ARENA_PUSH_ARRAY(sb->arena, U8, newCap);
        MEMMOVE(newData, sb->data, sb->size);
        sb->data = newData;
        sb->cap = newCap;
    }
}

void str8builder_append(Str8Builder* sb, StringU8 s) {
    if (s.size == 0) {
        return;
    }
    str8builder_ensure_capacity(sb, s.size);
    MEMMOVE(sb->data + sb->size, s.data, s.size);
    sb->size += s.size;
}

void str8builder_append_char(Str8Builder* sb, U8 c) {
    str8builder_ensure_capacity(sb, 1);
    sb->data[sb->size++] = c;
}

void str8builder_appendf(Str8Builder* sb, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    
    if (needed <= 0) {
        va_end(args);
        return;
    }
    
    str8builder_ensure_capacity(sb, (U64)needed);
    vsnprintf((char*)(sb->data + sb->size), (size_t)(sb->cap - sb->size), fmt, args);
    sb->size += (U64)needed;
    
    va_end(args);
}

StringU8 str8builder_to_string(Str8Builder* sb) {
    str8builder_ensure_capacity(sb, 1);
    sb->data[sb->size] = '\0';
    return str8(sb->data, sb->size);
}



