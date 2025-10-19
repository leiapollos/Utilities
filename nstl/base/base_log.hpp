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

static void log_init(Arena* logArena);
static void set_log_level(LogLevel level);
static StringU8 get_log_level_str(LogLevel level);

static void log(LogLevel level, StringU8 str);