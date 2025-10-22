//
// Created by André Leite on 16/10/2025.
//

// ////////////////////////
// Log

static thread_local Arena* g_log_arena;
static LogLevel g_log_level = LogLevel::LogLevel_Debug;
static bool g_use_color = false;
static const StringU8 g_default_terminal_color = str8("\033[0m");

static const LogLevelInfo g_log_level_info[] = {
    [LogLevel_Debug] = {
        .name = str8("DEBUG"),
        .colorCode = str8("\033[36m"), // Cyan
    },
    [LogLevel_Info] = {
        .name = str8("INFO"),
        .colorCode = str8("\033[32m"), // Green
    },
    [LogLevel_Warning] = {
        .name = str8("WARNING"),
        .colorCode = str8("\033[33m"), // Yellow
    },
    [LogLevel_Error] = {
        .name = str8("ERROR"),
        .colorCode = str8("\033[31m"), // Red
    },
};

static const LogLevelInfo* log_get_level_info(LogLevel level) {
    return &g_log_level_info[level];
}

static void log_init(Arena* logArena) {
    g_log_arena = logArena;
    g_use_color = OS_terminal_supports_color();
}

static void set_log_level(LogLevel level) {
    g_log_level = level;
}

static void log(LogLevel level, StringU8 str) {
    if (level < g_log_level) {
        return;
    }
    Temp tmp = get_scratch(0, 0);
    Arena* arena = tmp.arena; {
        const LogLevelInfo* info = log_get_level_info(level);
        StringU8 color = (g_use_color) ? info->colorCode : STR8_EMPTY;
        StringU8 defaultColor = (g_use_color) ? g_default_terminal_color : STR8_EMPTY;
        StringU8 res;
        str8_concat(res,
                    arena,
                    color,
                    str8("["),
                    info->name,
                    str8("]:\t"),
                    str,
                    defaultColor
        );

        OS_file_write(OS_get_log_handle(), res.size, res.data);
    }
    temp_end(&tmp);
}

static StringU8 arg_to_string(Arena* arena, const LogFmtArg& arg) {
    switch (arg.kind) {
        case LogFmtKind::S64:
            return str8_from_S64(arena, arg.S64Val);
        case LogFmtKind::U64:
            return str8_from_U64(arena, arg.U64Val, 10);
        case LogFmtKind::F64:
            return str8_from_F64(arena, arg.F64Val, 5);
        case LogFmtKind::CSTR:
            return str8(arg.cstrVal);
        case LogFmtKind::CHAR:
            return str8_from_char(arena, arg.chVal);
        case LogFmtKind::PTR:
            return str8_from_ptr(arena, arg.ptrVal);
        case LogFmtKind::BOOL:
            return str8_from_bool(arena, arg.boolVal);
        case LogFmtKind::STRINGU8:
            return arg.stringU8Val;
    }
    return STR8_EMPTY;
}

static void log_fmt_(LogLevel level,
                     StringU8 fmt,
                     const LogFmtArg* args,
                     U64 argCount) {
    if (level < g_log_level) {
        return;
    }

    Temp tmp = temp_begin(g_log_arena);
    Arena* arena = tmp.arena; {
        Str8List pieces;
        str8list_init(&pieces, arena, 16);

        const U8* data = fmt.data;
        U64 len = fmt.size;
        U64 i = 0;
        U64 last = 0;
        const LogFmtArg* it = args;
        const LogFmtArg* end = args ? args + argCount : 0;

        while (i < len) {
            if (data[i] == '{') {
                if (i + 1 < len && data[i + 1] == '{') {
                    if (i > last) {
                        str8list_push(&pieces, str8((U8*) (data + last), i - last));
                    }
                    str8list_push(&pieces, str8((U8*) "{", 1));
                    i += 2;
                    last = i;
                } else if (i + 1 < len && data[i + 1] == '}') {
                    if (i > last) {
                        str8list_push(&pieces, str8((U8*) (data + last), i - last));
                    }
                    if (it && it < end) {
                        str8list_push(&pieces, arg_to_string(arena, *it));
                        ++it;
                    } else {
                        str8list_push(&pieces, str8("<MISSING>"));
                    }
                    i += 2;
                    last = i;
                } else {
                    ++i;
                }
            } else if (data[i] == '}') {
                if (i + 1 < len && data[i + 1] == '}') {
                    if (i > last) {
                        str8list_push(&pieces, str8((U8*) (data + last), i - last));
                    }
                    str8list_push(&pieces, str8((U8*) "}", 1));
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
            str8list_push(&pieces, str8((U8*) (data + last), len - last));
        }

        StringU8 formatted = str8_concat_n(arena, pieces.items, pieces.count);
        log(level, formatted);
    }
    temp_end(&tmp);
}
