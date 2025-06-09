//
// Created by André Leite on 08/06/2025.
//

#pragma once

#if NSTL_DEBUG
void debugPrint(const char* format, ...);
#define NSTL_ASSERT(expression) \
do { \
if (!(expression)) { \
debugPrint("Assertion failed: %s, file %s, line %d, function %s\n", \
#expression, __FILE__, __LINE__, __func__); \
\
volatile int *invalid_ptr = nullptr; \
*invalid_ptr = 42; \
while(1) {} \
} \
} while (0)
#else
#define NSTL_ASSERT(expression)
#endif