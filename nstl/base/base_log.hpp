//
// Created by André Leite on 16/10/2025.
//

#pragma once

// ////////////////////////
// Log

enum LogLevel {
    LogLevel_Debug,
    LogLevel_Info,
    LogLevel_Warning,
    LogLevel_Error,
};

struct LogLevelInfo {
    StringU8 name;
    StringU8 colorCode; // ANSI color escape
};

static void log_init(Arena* logArena);
static void set_log_level(LogLevel level);

static void log(LogLevel level, StringU8 str);


// ////////////////////////
// Log Formatting

enum class LogFmtKind {
    S64,
    U64,
    F64,
    CSTR,
    CHAR,
    STRINGU8,
    PTR,
    BOOL,
    END,
};

struct LogFmtArg {
    LogFmtKind kind;

    union {
        S64 S64Val;
        U64 U64Val;
        F64 F64Val;
        const char* cstrVal;
        char chVal;
        const void* ptrVal;
        B1 boolVal;
        StringU8 stringU8Val;
    };

    LogFmtArg() {
        kind = LogFmtKind::CSTR;
        cstrVal = "(null)";
    }

    LogFmtArg(char v) {
        kind = LogFmtKind::CHAR;
        chVal = v;
    }

    LogFmtArg(const char* v) {
        kind = LogFmtKind::CSTR;
        cstrVal = v ? v : "(null)";
    }

    LogFmtArg(StringU8 v) {
        kind = LogFmtKind::STRINGU8;
        stringU8Val = v;
    }

    LogFmtArg(B1 v) {
        kind = LogFmtKind::BOOL;
        boolVal = v;
    }

    LogFmtArg(void* v) {
        kind = LogFmtKind::PTR;
        ptrVal = v;
    }

    LogFmtArg(const void* v) {
        kind = LogFmtKind::PTR;
        ptrVal = v;
    }

    LogFmtArg(F32 v) {
        kind = LogFmtKind::F64;
        F64Val = (F64) v;
    }

    LogFmtArg(F64 v) {
        kind = LogFmtKind::F64;
        F64Val = v;
    }

    LogFmtArg(S8 v) {
        kind = LogFmtKind::S64;
        S64Val = (S64) v;
    }

    LogFmtArg(S16 v) {
        kind = LogFmtKind::S64;
        S64Val = (S64) v;
    }

    LogFmtArg(S32 v) {
        kind = LogFmtKind::S64;
        S64Val = (S64) v;
    }

    LogFmtArg(S64 v) {
        kind = LogFmtKind::S64;
        S64Val = v;
    }

    LogFmtArg(U8 v) {
        kind = LogFmtKind::U64;
        U64Val = (U64) v;
    }

    LogFmtArg(U16 v) {
        kind = LogFmtKind::U64;
        U64Val = (U64) v;
    }

    LogFmtArg(U32 v) {
        kind = LogFmtKind::U64;
        U64Val = (U64) v;
    }

    LogFmtArg(U64 v) {
        kind = LogFmtKind::U64;
        U64Val = v;
    }
};

static inline LogFmtArg LOG_ARG_END() {
    LogFmtArg arg = {};
    arg.kind = LogFmtKind::END;
    return arg;
}

static void log_fmt_(LogLevel level,
                     StringU8 fmt,
                     const LogFmtArg* args);

#define log_fmt(level, fmt, ...)                                                    \
    do {                                                                            \
        const LogFmtArg _log_args[] = { __VA_OPT__(__VA_ARGS__,) LOG_ARG_END() };  \
        log_fmt_(level, str8_cstring(fmt), _log_args);                              \
    } while (0)

#define LOG_DEBUG(fmt, ...)     log_fmt(LogLevel_Debug, fmt, __VA_ARGS__)
#define LOG_INFO(fmt, ...)      log_fmt(LogLevel_Info, fmt, __VA_ARGS__)
#define LOG_WARNING(fmt, ...)   log_fmt(LogLevel_Warning, fmt, __VA_ARGS__)
#define LOG_ERROR(fmt, ...)     log_fmt(LogLevel_Error, fmt, __VA_ARGS__)
