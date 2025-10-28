//
// Created by André Leite on 26/07/2025.
//


static void base_entry_point(int argc, char** argv) {
    log_init();
    profiler_initialize();

    entry_point();

    profiler_print_report();
    profiler_shutdown();
}

static void thread_entry_point(void (*func)(void* params), void* args) {
    thread_context_alloc();
    log_init();

    func(args);

    thread_context_release();
}
