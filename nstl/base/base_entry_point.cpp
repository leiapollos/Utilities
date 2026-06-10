//
// Created by André Leite on 26/07/2025.
//

UTILITIES_SHARED_API void prof_init();
UTILITIES_SHARED_API void prof_shutdown();

void base_entry_point(int argc, char** argv) {
    thread_context_alloc();
    log_init();
    prof_init();

    entry_point();

    prof_shutdown();
    thread_context_release();
}

void thread_entry_point(void (*func)(void* params), void* args) {
    thread_context_alloc();
    log_init();

    func(args);

    thread_context_release();
}
