//
// Created by André Leite on 26/07/2025.
//

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// ////////////////////////
// Debug Break

static inline void debug_break() {
#if defined(COMPILER_MSVC)
    __debugbreak();
#elif defined(COMPILER_CLANG) || defined(COMPILER_GCC)
    __builtin_trap();
#else
    abort();
#endif
}

// ////////////////////////
// Assert

#define ASSERT_ALWAYS(condition) \
do { \
    if (!(condition)) { \
        debug_break(); \
    } \
} while (false)

#if defined(BUILD_DEBUG)
#define ASSERT_DEBUG(condition) ASSERT_ALWAYS(condition)
#else
#define ASSERT_DEBUG(condition) do {} while (false)
#endif

#define ASSERT_NOT_IMPLEMENTED() ASSERT_ALWAYS(false && "Not implemented")


// ////////////////////////
// Branch Prediction

#if defined(COMPILER_CLANG) || defined(COMPILER_GCC)
#define LIKELY(condition) __builtin_expect((condition), 1)
#define UNLIKELY(condition) __builtin_expect((condition), 0)
#else
#define LIKELY(condition) (condition)
#define UNLIKELY(condition) (condition)
#endif


// ////////////////////////
// Force Inline

#if defined(COMPILER_CLANG) || defined(COMPILER_GCC)
#define FORCE_INLINE inline __attribute__((always_inline))
#elif defined(COMPILER_MSVC)
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline
#endif


// ////////////////////////
// Misc Macros

#define MACRO_STR_2(s)      #s
#define MACRO_STR(s)        MACRO_STR_2(s)
#define NAME_CONCAT_2(A, B) A##B
#define NAME_CONCAT(A, B)   NAME_CONCAT_2(A, B)

#define ARRAY_COUNT(arr)        (sizeof(arr) / sizeof(arr[0]))

#define MEMCPY(dst, src, len)   memcpy(dst, src, len)
#define MEMMOVE(dst, src, len)  memmove(dst, src, len)
#define MEMCMP(a, b, len)       memcmp(a, b, len)
#define MEMSET(dst, v, len)     memset(dst, v, len)

#define C_STR_LEN(ptr)          strlen(ptr)


// ////////////////////////
// Alignment

static constexpr U64 align_pow2(U64 x, U64 a) {
    return (x + a - 1) & (~(a - 1));
}

static constexpr B32 is_power_of_two(U64 x) {
    return (x > 0) && ((x & (x - 1)) == 0);
}


// ////////////////////////
// Min/Max/Clamp

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP_BOT(x, bottom)    MAX(x, bottom)
#define CLAMP_TOP(x, top)       MIN(x, top)
#define CLAMP(x, bottom, top)   MIN(MAX(x, bottom), top)


// ////////////////////////
// Memory Units

static constexpr U64 KB(U64 n) { return n << 10; }
static constexpr U64 MB(U64 n) { return n << 20; }
static constexpr U64 GB(U64 n) { return n << 30; }


// ////////////////////////
// Ranges

struct RangeU64 {
    U64 min;
    U64 max;
};


// ////////////////////////
// Atomics (GCC/Clang)

#if defined(COMPILER_CLANG) || defined(COMPILER_GCC)
#define MEMORY_ORDER_RELAXED __ATOMIC_RELAXED
#define MEMORY_ORDER_ACQUIRE __ATOMIC_ACQUIRE
#define MEMORY_ORDER_RELEASE __ATOMIC_RELEASE
#define MEMORY_ORDER_ACQ_REL __ATOMIC_ACQ_REL
#define MEMORY_ORDER_SEQ_CST __ATOMIC_SEQ_CST

#define ATOMIC_LOAD(p, mo)          __atomic_load_n((p), (mo))
#define ATOMIC_STORE(p, v, mo)      __atomic_store_n((p), (v), (mo))
#define ATOMIC_FETCH_ADD(p, v, mo)  __atomic_fetch_add((p), (v), (mo))
#define ATOMIC_FETCH_SUB(p, v, mo)  __atomic_fetch_sub((p), (v), (mo))
#else
#error "Atomics not implemented for this compiler"
#endif


// ////////////////////////
// Bit Flags

#define FLAGS_HAS(value, bits)    (((U64)(value) & (U64)(bits)) == (U64)(bits))
#define FLAGS_ANY(value, bits)    (((U64)(value) & (U64)(bits)) != 0)


// ////////////////////////
// Defer (C++ RAII helper)

typedef void (*defer_fn)(void*);

struct DeferGuard {
    defer_fn fn;
    void* ctx;

    DeferGuard(defer_fn f, void* c) : fn(f), ctx(c) {}
    ~DeferGuard() { fn(ctx); }
};

#define DEFER_UNIQ(prefix) NAME_CONCAT(prefix, __COUNTER__)

#define DEFER_IMPL(ID, CAP, ...) \
    auto NAME_CONCAT(ID, _lam) = CAP() { __VA_ARGS__; }; \
    struct NAME_CONCAT(ID, _Thunk) { \
        static void call(void* ctx) { \
            (*static_cast<decltype(NAME_CONCAT(ID, _lam))*>(ctx))(); \
        } \
    }; \
    DeferGuard NAME_CONCAT(ID, _g)(&NAME_CONCAT(ID, _Thunk)::call, &NAME_CONCAT(ID, _lam))

#define DEFER(...) DEFER_IMPL(DEFER_UNIQ(__defer_), [=], __VA_ARGS__)
#define DEFER_REF(...) DEFER_IMPL(DEFER_UNIQ(__defer_), [&], __VA_ARGS__)



