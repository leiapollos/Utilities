//
// Created by André Leite on 16/10/2025.
//

// ////////////////////////
// Log

struct LogDomainEntry {
    LogDomainEntry* next;
    StringU8 domain;
    LogLevel level;
};

static LogLevel g_logLevel = LogLevel_Debug;
static B32 g_useColor = false;
static const StringU8 g_defaultTerminalColor = str8("\033[0m");
static LogDomainEntry* g_logDomainList = nullptr;
static Arena* g_logDomainArena = nullptr;
static OS_Handle g_logDomainMutex = {};

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

static LogLevel log_get_domain_level(StringU8 domain) {
    if (g_logDomainMutex.handle == 0) {
        log_init();
    }
    OS_mutex_lock(g_logDomainMutex);
    DEFER_REF(OS_mutex_unlock(g_logDomainMutex));
    
    LogLevel result = g_logLevel;
    for (LogDomainEntry* entry = g_logDomainList; entry != nullptr; entry = entry->next) {
        if (str8_equal(entry->domain, domain)) {
            result = entry->level;
            break;
        }
    }
    return result;
}

void log_init() {
    g_useColor = OS_terminal_supports_color();
    if (g_logDomainMutex.handle == 0) {
        g_logDomainMutex = OS_mutex_create();
    }
    if (g_logDomainArena == nullptr) {
        g_logDomainArena = arena_alloc();
    }
}

void set_log_level(LogLevel level) {
    g_logLevel = level;
}

void set_log_domain_level(StringU8 domain, LogLevel level) {
    if (g_logDomainMutex.handle == 0) {
        log_init();
    }
    OS_mutex_lock(g_logDomainMutex);
    DEFER_REF(OS_mutex_unlock(g_logDomainMutex));
    
    for (LogDomainEntry* entry = g_logDomainList; entry != nullptr; entry = entry->next) {
        if (str8_equal(entry->domain, domain)) {
            entry->level = level;
            return;
        }
    }
    
    LogDomainEntry* newEntry = ARENA_PUSH_STRUCT(g_logDomainArena, LogDomainEntry);
    newEntry->domain = str8_cpy(g_logDomainArena, domain);
    newEntry->level = level;
    newEntry->next = g_logDomainList;
    g_logDomainList = newEntry;
}

void log(LogLevel level, StringU8 domain, StringU8 str) {
    LogLevel domainLevel = log_get_domain_level(domain);
    if (level < domainLevel) {
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
                               str8("]["),
                               domain,
                               str8("]:\t"),
                               str,
                               defaultColor);

    OS_file_write(OS_get_log_handle(), res.size, res.data);
}

void log_fmt_(LogLevel level,
              StringU8 domain,
              B32 addNewline,
              StringU8 fmt,
              const Str8FmtArg* args,
              U64 argCount) {
    LogLevel domainLevel = log_get_domain_level(domain);
    if (level < domainLevel) {
        return;
    }

    Temp tmp = get_scratch(0, 0);
    DEFER_REF(temp_end(&tmp));
    Arena* arena = tmp.arena;

    StringU8 formatted = str8_fmt_(arena, fmt, args, argCount);
    if (addNewline) {
        formatted = str8_concat(arena, formatted, str8("\n"));
    }

    log(level, domain, formatted);
}
