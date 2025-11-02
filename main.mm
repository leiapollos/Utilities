//
// Created by André Leite on 31/10/2025.
//

#include "nstl/base/base_include.hpp"
#include "nstl/os/os_include.hpp"
#include "nstl/base/base_include.cpp"
#include "nstl/os/os_include.cpp"
#include "host_runtime.cpp"

void entry_point(void) {
    extern int host_main_loop(int argc, char** argv);
    host_main_loop(0, 0);
}

