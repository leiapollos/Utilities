//
// Created by André Leite on 16/10/2025.
//

// ////////////////////////
// Log

static thread_local Arena* g_log_arena;
static LogLevel g_log_level = LogLevel::LogLevel_Debug;


static void log_init(Arena* logArena) {
    g_log_arena = logArena;
}

static void set_log_level(LogLevel level) {
    g_log_level = level;
}

static StringU8 get_log_level_str(LogLevel level) {
    switch (level) {
        case LogLevel_Debug: {
            return str8_cstring("DEBUG");
        }
        case LogLevel_Info: {
            return str8_cstring("INFO");
        }
        case LogLevel_Warning: {
            return str8_cstring("WARNING");
        }
        case LogLevel_Error: {
            return str8_cstring("ERROR");
        }
        case LogLevel_Fatal: {
            return str8_cstring("FATAL");
        }
    }
}

static void log(LogLevel level, StringU8 str) {
    if (level < g_log_level) {
        return;
    }
    Temp tmp = get_scratch(0, 0);
    Arena* arena = tmp.arena;
    {
        StringU8 levelStr = get_log_level_str(level);

        StringU8 res = str8_concat(arena, str8_cstring("["), levelStr, str8_cstring("]:\t"), str);
        OS_file_write(OS_get_log_handle(), res.length, res.data);
    }
    temp_end(&tmp);
}
