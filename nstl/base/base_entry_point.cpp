//
// Created by André Leite on 26/07/2025.
//


static void base_entry_point(int argc, char** argv) {
    log_init();

    entry_point();
}

static void thread_entry_point(void (*func)(void* params), void* args) {
    thread_context_alloc();
    log_init();

    func(args);

    thread_context_destroy();
}
