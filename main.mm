//
// Created by André Leite on 31/10/2025.
//

#include "host_runtime.cpp"

void entry_point(void) {
    extern int host_main_loop(int argc, char** argv);
    host_apply_environment_defaults();
    host_main_loop(0, 0);
}

