#pragma once

typedef signed char int8_t;
typedef unsigned char uint8_t;

typedef short int16_t;
typedef unsigned short uint16_t;

typedef int int32_t;
typedef unsigned int uint32_t;

typedef long long int64_t;
typedef unsigned long long uint64_t;

typedef uint64_t size_t;
typedef int64_t ptrdiff_t;
typedef uint64_t uintptr_t;

#ifdef _WIN32
#define PLATFORM_WINDOWS
#else
#error "Unsupported platform"
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_X64
#else
#error "Unsupported architecture"
#endif