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

void log_init();
void set_log_level(LogLevel level);
void set_log_domain_level(StringU8 domain, LogLevel level);

void log(LogLevel level, StringU8 domain, StringU8 str);


// ////////////////////////
// Log Formatting

enum LogFmtKind {
    LogFmtKind_S64,
    LogFmtKind_U64,
    LogFmtKind_F64,
    LogFmtKind_CSTR,
    LogFmtKind_CHAR,
    LogFmtKind_STRINGU8,
    LogFmtKind_PTR,
};

struct LogFmtSpec {
    B32 hasSpec;
    int floatPrecision;
    U64 intBase;
    B32 uppercaseHex;
};

struct LogFmtArg {
    LogFmtKind kind;

    union {
        S64 S64Val;
        U64 U64Val;
        F64 F64Val;
        const char* cstrVal;
        U8 charVal;
        const void* ptrVal;
        StringU8 stringU8Val;
    };

    LogFmtArg() {
        kind = LogFmtKind_CSTR;
        cstrVal = "(null)";
    }

    LogFmtArg(U8 v) {
        kind = LogFmtKind_CHAR;
        charVal = v;
    }

    LogFmtArg(const char* v) {
        kind = LogFmtKind_CSTR;
        cstrVal = v ? v : "(null)";
    }

    LogFmtArg(StringU8 v) {
        kind = LogFmtKind_STRINGU8;
        stringU8Val = v;
    }

    LogFmtArg(void* v) {
        kind = LogFmtKind_PTR;
        ptrVal = v;
    }

    LogFmtArg(const void* v) {
        kind = LogFmtKind_PTR;
        ptrVal = v;
    }

    LogFmtArg(F32 v) {
        kind = LogFmtKind_F64;
        F64Val = (F64) v;
    }

    LogFmtArg(F64 v) {
        kind = LogFmtKind_F64;
        F64Val = v;
    }

    LogFmtArg(S8 v) {
        kind = LogFmtKind_S64;
        S64Val = (S64) v;
    }

    LogFmtArg(S16 v) {
        kind = LogFmtKind_S64;
        S64Val = (S64) v;
    }

    LogFmtArg(S32 v) {
        kind = LogFmtKind_S64;
        S64Val = (S64) v;
    }

    LogFmtArg(S64 v) {
        kind = LogFmtKind_S64;
        S64Val = v;
    }

    LogFmtArg(U16 v) {
        kind = LogFmtKind_U64;
        U64Val = (U64) v;
    }

    LogFmtArg(U32 v) {
        kind = LogFmtKind_U64;
        U64Val = (U64) v;
    }

    LogFmtArg(U64 v) {
        kind = LogFmtKind_U64;
        U64Val = v;
    }
};

LogFmtSpec log_fmt_parse_spec(StringU8 specStr);
StringU8 arg_to_string(Arena* arena, const LogFmtArg& arg, LogFmtSpec spec);
void log_fmt_(LogLevel level,
              StringU8 domain,
              B32 addNewline,
              StringU8 fmt,
              const LogFmtArg* args,
              U64 argCount);

static inline StringU8 log_domain_wrap(const char* domain) {
    return str8(domain);
}
static inline StringU8 log_domain_wrap(StringU8 domain) {
    return domain;
}

#define log_fmt(level, domain, addNewline, fmt, ...)                                                 \
    ([&](){                                                                                         \
        /* Dummy first element keeps zero-arg calls valid;                                          \
        pointer skips it and count subtracts it. */                                                 \
        const LogFmtArg _log_args_local[] = { LogFmtArg(), __VA_ARGS__ };                           \
        const U64 _log_args_count = (U64)((sizeof(_log_args_local) / sizeof(LogFmtArg)) - 1);       \
        const LogFmtArg* _log_args_ptr = (_log_args_count > 0)? (_log_args_local + 1) : nullptr;    \
        log_fmt_(level, log_domain_wrap(domain), addNewline ? 1 : 0, str8(fmt), _log_args_ptr, _log_args_count); \
    }())

#define LOG_DEBUG(domain, fmt, ...)     log_fmt(LogLevel_Debug, domain, 1, fmt, __VA_ARGS__)
#define LOG_INFO(domain, fmt, ...)      log_fmt(LogLevel_Info, domain, 1, fmt, __VA_ARGS__)
#define LOG_WARNING(domain, fmt, ...)   log_fmt(LogLevel_Warning, domain, 1, fmt, __VA_ARGS__)
#define LOG_ERROR(domain, fmt, ...)     log_fmt(LogLevel_Error, domain, 1, fmt, __VA_ARGS__)
