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

extern StringU8 STR8_NIL;
extern StringU8 STR8_EMPTY;

StringU8 str8(U8* source, U64 size);
StringU8 str8(const char* source, U64 size);
StringU8 str8(const char* source);
StringU8 str8_cpy(Arena* arena, StringU8 src);
StringU8 str8_cpy(Arena* arena, const char* source);

StringU8 str8_concat_n(Arena* arena, const StringU8* pieces, U64 count);

#define str8_concat(arena, ...) \
    ([&](Arena* _a) -> StringU8 { \
        StringU8 _pieces[] = { __VA_ARGS__ }; \
        return str8_concat_n(_a, _pieces, (U64)(sizeof(_pieces) / sizeof(_pieces[0]))); \
    }((arena)))

B8 str8_is_nil(StringU8 s);
B8 str8_is_empty(StringU8 s);
B8 str8_equal(StringU8 a, StringU8 b);

StringU8 str8_from_U64(Arena* arena, U64 value, U64 base);
StringU8 str8_from_S64(Arena* arena, S64 value);
StringU8 str8_from_F64(Arena* arena, F64 value, int precision);
StringU8 str8_from_ptr(Arena* arena, const void* ptr);
StringU8 str8_from_char(Arena* arena, U8 c);
StringU8 str8_to_uppercase(Arena* arena, StringU8 s);
StringU8 str8_to_lowercase(Arena* arena, StringU8 s);
StringU8 str8_to_snake_case(Arena* arena, StringU8 s);

B8 str8_starts_with(StringU8 s, StringU8 prefix);
B8 str8_ends_with(StringU8 s, StringU8 suffix);
StringU8 str8_substr(StringU8 s, U64 start, U64 len);
StringU8 str8_trim_whitespace(StringU8 s);

S64 str8_find_char(StringU8 s, U8 c);
S64 str8_find_char_last(StringU8 s, U8 c);


// ////////////////////////
// Path Utilities

// Get file extension including the dot (e.g., "/foo/bar.txt" -> ".txt")
// Returns empty string if no extension found
StringU8 str8_path_extension(StringU8 path);

// Get filename without directory (e.g., "/foo/bar.txt" -> "bar.txt")
StringU8 str8_path_basename(StringU8 path);

// Get filename without extension (e.g., "/foo/bar.txt" -> "bar", "foo.tar.gz" -> "foo.tar")
StringU8 str8_path_stem(StringU8 path);

// Get directory part (e.g., "/foo/bar.txt" -> "/foo")
// Returns "." if no directory separator found
StringU8 str8_path_directory(StringU8 path);

// Strip a specific suffix from the path (e.g., strip ".metadef" from "foo.metadef" -> "foo")
// Returns original string if suffix not found
StringU8 str8_strip_suffix(StringU8 s, StringU8 suffix);

// Join two path components with a separator, avoiding double slashes
// (e.g., "foo/" + "bar" -> "foo/bar", "foo" + "bar" -> "foo/bar")
StringU8 str8_path_join(Arena* arena, StringU8 dir, StringU8 filename);

// Normalize path by removing trailing slashes (except for root "/")
StringU8 str8_path_normalize(StringU8 path);


// ////////////////////////
// String List

struct Str8List {
    StringU8* items;
    U64 count;
    U64 cap;
    Arena* arena;
};

void str8list_init(Str8List* l, Arena* arena, U64 initialCap);
void str8list_push(Str8List* l, StringU8 s);
StringU8 str8list_join(Str8List* l, StringU8 separator);


// ////////////////////////
// String Builder

struct Str8Builder {
    Arena* arena;
    U8* data;
    U64 size;
    U64 cap;
};

void str8builder_init(Str8Builder* sb, Arena* arena, U64 initialCap);
void str8builder_append(Str8Builder* sb, StringU8 s);
void str8builder_append_char(Str8Builder* sb, U8 c);
void str8builder_appendf(Str8Builder* sb, const char* fmt, ...);
StringU8 str8builder_to_string(Str8Builder* sb);


// ////////////////////////
// String Formatting

enum Str8FmtKind {
    Str8FmtKind_S64,
    Str8FmtKind_U64,
    Str8FmtKind_F64,
    Str8FmtKind_CSTR,
    Str8FmtKind_CHAR,
    Str8FmtKind_STRINGU8,
    Str8FmtKind_PTR,
};

struct Str8FmtSpec {
    B32 hasSpec;
    int floatPrecision;
    U64 intBase;
    B32 uppercaseHex;
};

struct Str8FmtArg {
    Str8FmtKind kind;

    union {
        S64 s64Val;
        U64 u64Val;
        F64 f64Val;
        const char* cstrVal;
        U8 charVal;
        const void* ptrVal;
        StringU8 stringU8Val;
    };

    Str8FmtArg() { kind = Str8FmtKind_CSTR; cstrVal = "(null)"; }
    Str8FmtArg(U8 v) { kind = Str8FmtKind_CHAR; charVal = v; }
    Str8FmtArg(const char* v) { kind = Str8FmtKind_CSTR; cstrVal = v ? v : "(null)"; }
    Str8FmtArg(StringU8 v) { kind = Str8FmtKind_STRINGU8; stringU8Val = v; }
    Str8FmtArg(void* v) { kind = Str8FmtKind_PTR; ptrVal = v; }
    Str8FmtArg(const void* v) { kind = Str8FmtKind_PTR; ptrVal = v; }
    Str8FmtArg(F32 v) { kind = Str8FmtKind_F64; f64Val = (F64)v; }
    Str8FmtArg(F64 v) { kind = Str8FmtKind_F64; f64Val = v; }
    Str8FmtArg(S8 v) { kind = Str8FmtKind_S64; s64Val = (S64)v; }
    Str8FmtArg(S16 v) { kind = Str8FmtKind_S64; s64Val = (S64)v; }
    Str8FmtArg(S32 v) { kind = Str8FmtKind_S64; s64Val = (S64)v; }
    Str8FmtArg(S64 v) { kind = Str8FmtKind_S64; s64Val = v; }
    Str8FmtArg(U16 v) { kind = Str8FmtKind_U64; u64Val = (U64)v; }
    Str8FmtArg(U32 v) { kind = Str8FmtKind_U64; u64Val = (U64)v; }
    Str8FmtArg(U64 v) { kind = Str8FmtKind_U64; u64Val = v; }
};

Str8FmtSpec str8_fmt_parse_spec(StringU8 specStr);
StringU8 str8_fmt_arg_to_string(Arena* arena, const Str8FmtArg& arg, Str8FmtSpec spec);
StringU8 str8_fmt_(Arena* arena, StringU8 fmt, const Str8FmtArg* args, U64 argCount);

#define str8_fmt(arena, fmt, ...)                                                          \
    ([&](){                                                                                \
        /* Dummy first element keeps zero-arg calls valid;                                 \
           pointer skips it and count subtracts it. */                                     \
        const Str8FmtArg _args[] = { Str8FmtArg(), __VA_ARGS__ };                          \
        const U64 _count = (U64)((sizeof(_args) / sizeof(Str8FmtArg)) - 1);                \
        const Str8FmtArg* _ptr = (_count > 0) ? (_args + 1) : nullptr;                     \
        return str8_fmt_((arena), str8(fmt), _ptr, _count);                                \
    }())
