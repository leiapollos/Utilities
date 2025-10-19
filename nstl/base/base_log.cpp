//
// Created by André Leite on 16/10/2025.
//

// ////////////////////////
// Log

static thread_local Arena* g_log_arena;
static LogLevel g_log_level = LogLevel::LogLevel_Debug;
static bool g_use_color = false;
static const StringU8 g_default_terminal_color = str8_cstring("\033[0m");

static const LogLevelInfo g_log_level_info[] = {
    [LogLevel_Debug] = {
        .name = str8_cstring("DEBUG"),
        .colorCode = str8_cstring("\033[36m"), // Cyan
    },
    [LogLevel_Info] = {
        .name = str8_cstring("INFO"),
        .colorCode = str8_cstring("\033[32m"), // Green
    },
    [LogLevel_Warning] = {
        .name = str8_cstring("WARNING"),
        .colorCode = str8_cstring("\033[33m"), // Yellow
    },
    [LogLevel_Error] = {
        .name = str8_cstring("ERROR"),
        .colorCode = str8_cstring("\033[31m"), // Red
    },
    [LogLevel_Fatal] = {
        .name = str8_cstring("FATAL"),
        .colorCode = str8_cstring("\033[35m"), // Magenta
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
        StringU8 color = (g_use_color) ? info->colorCode : STR8_EMPTY();
        StringU8 defaultColor = (g_use_color) ? g_default_terminal_color : STR8_EMPTY();
        StringU8 res = str8_concat(arena,
            color,
            str8_cstring("["),
            info->name,
            str8_cstring("]:\t"),
            str,
            defaultColor
        );

        OS_file_write(OS_get_log_handle(), res.length, res.data);
    }
    temp_end(&tmp);
}
