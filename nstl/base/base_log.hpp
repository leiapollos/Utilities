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

void log_fmt_(LogLevel level,
              StringU8 domain,
              B32 addNewline,
              StringU8 fmt,
              const Str8FmtArg* args,
              U64 argCount);

static inline StringU8 log_domain_wrap(const char* domain) {
    return str8(domain);
}
static inline StringU8 log_domain_wrap(StringU8 domain) {
    return domain;
}

#define log_fmt(level, domain, addNewline, fmt, ...)                                         \
    ([&](){                                                                                  \
        /* Dummy first element keeps zero-arg calls valid;                                   \
           pointer skips it and count subtracts it. */                                       \
        const Str8FmtArg _log_args_local[] = { Str8FmtArg(), __VA_ARGS__ };                  \
        const U64 _log_args_count = (U64)((sizeof(_log_args_local) / sizeof(Str8FmtArg)) - 1); \
        const Str8FmtArg* _log_args_ptr = (_log_args_count > 0) ? (_log_args_local + 1) : nullptr; \
        log_fmt_(level, log_domain_wrap(domain), addNewline ? 1 : 0, str8(fmt), _log_args_ptr, _log_args_count); \
    }())

#define LOG_DEBUG(domain, fmt, ...)     log_fmt(LogLevel_Debug, domain, 1, fmt, __VA_ARGS__)
#define LOG_INFO(domain, fmt, ...)      log_fmt(LogLevel_Info, domain, 1, fmt, __VA_ARGS__)
#define LOG_WARNING(domain, fmt, ...)   log_fmt(LogLevel_Warning, domain, 1, fmt, __VA_ARGS__)
#define LOG_ERROR(domain, fmt, ...)     log_fmt(LogLevel_Error, domain, 1, fmt, __VA_ARGS__)
