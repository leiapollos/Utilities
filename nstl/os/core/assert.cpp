//
// Created by André Leite on 08/06/2025.
//

#include "assert.h"

#if NSTL_DEBUG
#include <cstdarg>
#include <cstdio>

void debugPrint(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

#endif