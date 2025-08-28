//
// Created by André Leite on 26/07/2025.
//


static void base_entry_point(int argc, char **argv) {
    entry_point();
}

static void thread_entry_point(void (*func)(void* params), void* args) {
    scratch_thread_init();
    
    func(args);
    
    scratch_thread_shutdown();
}
