//
// Created by André Leite on 16/10/2025.
//

// ////////////////////////
// Log

static LogLevel g_logLevel = LogLevel_Debug;
static bool g_useColor = false;
static const StringU8 g_defaultTerminalColor = str8("\033[0m");

static const LogLevelInfo g_logLevelInfo[] = {
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
    return &g_logLevelInfo[level];
}

static void log_init() {
    g_useColor = OS_terminal_supports_color();
}

static void set_log_level(LogLevel level) {
    g_logLevel = level;
}

static void log(LogLevel level, StringU8 str) {
    if (level < g_logLevel) {
        return;
    }

    Temp tmp = get_scratch(0, 0);
    DEFER_REF(temp_end(&tmp));
    Arena* arena = tmp.arena;

    const LogLevelInfo* info = log_get_level_info(level);
    StringU8 color = (g_useColor) ? info->colorCode : STR8_EMPTY;
    StringU8 defaultColor = (g_useColor) ? g_defaultTerminalColor : STR8_EMPTY;
    StringU8 res = str8_concat(arena,
                               color,
                               str8("["),
                               info->name,
                               str8("]:\t"),
                               str,
                               defaultColor);

    OS_file_write(OS_get_log_handle(), res.size, res.data);
}

static LogFmtSpec log_fmt_parse_spec(StringU8 specStr) {
    LogFmtSpec spec;
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
            if (i < len && data[i] == 'f') {
                ++i;
            }
        } else if (i < len) {
            U8 fmtChar = data[i];
            if (fmtChar == 'd') {
                spec.intBase = 10;
                ++i;
            } else if (fmtChar == 'x') {
                spec.intBase = 16;
                spec.uppercaseHex = 0;
                ++i;
            } else if (fmtChar == 'X') {
                spec.intBase = 16;
                spec.uppercaseHex = 1;
                ++i;
            } else if (fmtChar == 'b') {
                spec.intBase = 2;
                ++i;
            } else if (fmtChar == 'o') {
                spec.intBase = 8;
                ++i;
            }
        }
    }

    return spec;
}

static StringU8 arg_to_string(Arena* arena, const LogFmtArg& arg, LogFmtSpec spec) {
    switch (arg.kind) {
        case LogFmtKind_S64: {
            if (spec.hasSpec && spec.intBase != 10) {
                U64 absVal = (U64) (arg.S64Val < 0 ? -arg.S64Val : arg.S64Val);
                StringU8 result = str8_from_U64(arena, absVal, spec.intBase);
                if (spec.uppercaseHex) {
                    result = str8_to_uppercase(arena, result);
                }
                if (arg.S64Val < 0) {
                    return str8_concat(arena, str8("-"), result);
                }
                return result;
            }
            return str8_from_S64(arena, arg.S64Val);
        }
        case LogFmtKind_U64: {
            if (spec.hasSpec) {
                StringU8 result = str8_from_U64(arena, arg.U64Val, spec.intBase);
                if (spec.uppercaseHex) {
                    result = str8_to_uppercase(arena, result);
                }
                return result;
            }
            return str8_from_U64(arena, arg.U64Val, 10);
        }
        case LogFmtKind_F64: {
            int precision = spec.hasSpec ? spec.floatPrecision : 5;
            return str8_from_F64(arena, arg.F64Val, precision);
        }
        case LogFmtKind_CSTR:
            return str8(arg.cstrVal);
        case LogFmtKind_CHAR:
            return str8_from_char(arena, arg.charVal);
        case LogFmtKind_PTR:
            return str8_from_ptr(arena, arg.ptrVal);
        case LogFmtKind_STRINGU8:
            return arg.stringU8Val;
    }
    return STR8_EMPTY;
}

static void log_fmt_(LogLevel level,
                     B32 addNewline,
                     StringU8 fmt,
                     const LogFmtArg* args,
                     U64 argCount) {
    if (level < g_logLevel) {
        return;
    }

    Temp tmp = get_scratch(0, 0);
    DEFER_REF(temp_end(&tmp));
    Arena* arena = tmp.arena;
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
            } else {
                U64 specStart = i + 1;
                U64 specEnd = specStart;
                while (specEnd < len && data[specEnd] != '}') {
                    ++specEnd;
                }
                if (specEnd < len) {
                    if (i > last) {
                        str8list_push(&pieces, str8((U8*) (data + last), i - last));
                    }
                    StringU8 specStr = str8((U8*) (data + specStart), specEnd - specStart);
                    LogFmtSpec spec = log_fmt_parse_spec(specStr);
                    if (it && it < end) {
                        str8list_push(&pieces, arg_to_string(arena, *it, spec));
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

    if (addNewline) {
        str8list_push(&pieces, str8("\n"));
    }

    StringU8 formatted = str8_concat_n(arena, pieces.items, pieces.count);
    log(level, formatted);
}
