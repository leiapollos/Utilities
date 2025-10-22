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
};

struct LogFmtArg {
    LogFmtKind kind;

    union {
        S64 S64Val;
        U64 U64Val;
        F64 F64Val;
        const char* cstrVal;
        U8 chVal;
        const void* ptrVal;
        B1 boolVal;
        StringU8 stringU8Val;
    };

    LogFmtArg() {
        kind = LogFmtKind::CSTR;
        cstrVal = "(null)";
    }

    LogFmtArg(U8 v) {
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

static void log_fmt_(LogLevel level,
                     StringU8 fmt,
                     const LogFmtArg* args,
                     U64 argCount);

#define log_fmt(level, fmt, ...)                                                          \
    do {                                                                                  \
        /* Dummy first element keeps zero-arg calls valid;                                \
        pointer skips it and count subtracts it. */                                       \
        const LogFmtArg _log_args_local[] = { LogFmtArg(), __VA_ARGS__ };                 \
        const U64 _log_args_count =                                                       \
            (U64)((sizeof(_log_args_local) / sizeof(LogFmtArg)) - 1);                     \
        const LogFmtArg* _log_args_ptr = (_log_args_count > 0) ? (_log_args_local + 1) : 0; \
        log_fmt_(level, str8(fmt), _log_args_ptr, _log_args_count);                       \
    } while (0)

#define LOG_DEBUG(fmt, ...)     log_fmt(LogLevel_Debug, fmt, __VA_ARGS__)
#define LOG_INFO(fmt, ...)      log_fmt(LogLevel_Info, fmt, __VA_ARGS__)
#define LOG_WARNING(fmt, ...)   log_fmt(LogLevel_Warning, fmt, __VA_ARGS__)
#define LOG_ERROR(fmt, ...)     log_fmt(LogLevel_Error, fmt, __VA_ARGS__)
