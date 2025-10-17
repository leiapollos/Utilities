//
// Created by André Leite on 16/10/2025.
//

// ////////////////////////
// Log

static thread_local Arena* g_log_arena;


static void log_init(Arena* logArena) {
    g_log_arena = logArena;
}

static void log(LogLevel level, StringU8 str) {

}