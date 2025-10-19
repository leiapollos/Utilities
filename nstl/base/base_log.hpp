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
    LogLevel_Fatal,
};

struct LogLevelInfo {
    StringU8 name;
    StringU8 colorCode;    // ANSI color escape
};

static void log_init(Arena* logArena);
static void set_log_level(LogLevel level);

static void log(LogLevel level, StringU8 str);