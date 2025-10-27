//
// Created by André Leite on 26/07/2025.
//

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// ////////////////////////
// Assert

void debug_break() {
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__clang__) || defined(__GNUC__)
    __builtin_trap();
#else
    std::abort();
#endif
}

#define ASSERT_ALWAYS(condition) \
do { \
if (!(condition)) { \
debug_break(); \
} \
} while (false)

#ifndef NDEBUG
#define ASSERT_DEBUG(condition) ASSERT_ALWAYS(condition)
#else
#define ASSERT_DEBUG(condition) do {} while (false)
#endif

#define ASSERT_NOT_IMPLEMENTED() ASSERT_ALWAYS(false && "Not implemented")


// ////////////////////////
// Branch Prediction

#if defined(COMPILER_MSVC)
#if _MSC_VER >= 1928 && defined(CPP_LANG) && _MSVC_LANG >= 202002L
// MSVC with C++20 support for [[likely]] and [[unlikely]]
#define LIKELY(condition) [[likely]] (condition)
#define UNLIKELY(condition) [[unlikely]] (condition)
#else
// Fallback for older MSVC or non-C++20
#define LIKELY(condition) (condition)
#define UNLIKELY(condition) (condition)
#endif
#elif defined(COMPILER_CLANG) || defined(COMPILER_GCC)
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
// Keywords

#define EXTERN_C extern "C"

#if defined(COMPILER_MSVC) || (defined(COMPILER_CLANG) && defined(OS_WINDOWS))
# pragma section(".rdata$", read)
# define READ_ONLY __declspec(allocate(".rdata$"))
#elif defined(COMPILER_CLANG) && defined(OS_LINUX)
# define READ_ONLY __attribute__((section(".rodata")))
#else
// GCC read only attributes introduce some issues
# define READ_ONLY
#endif

// ////////////////////////
// Address Sanitizer

#if ADDRESS_SANITIZER

EXTERN_C void __asan_poison_memory_region(void const volatile* addr, U32 size);
EXTERN_C void __asan_unpoison_memory_region(void const volatile* addr, U32 size);
# define ASAN_POISON_MEMORY_REGION(addr, size)   __asan_poison_memory_region((addr), (size))
# define ASAN_UNPOISON_MEMORY_REGION(addr, size) __asan_unpoison_memory_region((addr), (size))

#else

# define ASAN_POISON_MEMORY_REGION(addr, size)
# define ASAN_UNPOISON_MEMORY_REGION(addr, size)

#endif


// //////////////////////////////
// Misc

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

// //////////////////////////////
// Ranges

struct RangeU64 {
    U64 min;
    U64 max;
};

// //////////////////////////////
// Cache line size

#ifndef CACHE_LINE_SIZE
#  if defined(PLATFORM_ARCH_ARM64)
#    define CACHE_LINE_SIZE 256
#  elif defined(PLATFORM_ARCH_X64)
#    define CACHE_LINE_SIZE 64
#  else
#    define CACHE_LINE_SIZE 64
#  endif
#endif


// //////////////////////////////
// Atomics

#if defined(COMPILER_CLANG) || defined(COMPILER_GCC)
#define MEMORY_ORDER_RELAXED __ATOMIC_RELAXED
#define MEMORY_ORDER_ACQUIRE __ATOMIC_ACQUIRE
#define MEMORY_ORDER_RELEASE __ATOMIC_RELEASE
#define MEMORY_ORDER_ACQ_REL __ATOMIC_ACQ_REL
#define MEMORY_ORDER_SEQ_CST __ATOMIC_SEQ_CST

#define ATOMIC_LOAD(p, mo)          __atomic_load_n((p), (mo))
#define ATOMIC_STORE(p, v, mo)      __atomic_store_n((p), (v), (mo))
#define ATOMIC_EXCHANGE(p, v, mo)   __atomic_exchange_n((p), (v), (mo))
#define ATOMIC_COMPARE_EXCHANGE(p, expected_ptr, desired, weak, s, f)       \
                                    __atomic_compare_exchange_n((p), (expected_ptr), (desired), (weak), (s), (f))

// Fetch ops
#define ATOMIC_FETCH_ADD(p, v, mo) __atomic_fetch_add((p), (v), (mo))
#define ATOMIC_FETCH_SUB(p, v, mo) __atomic_fetch_sub((p), (v), (mo))
#define ATOMIC_FETCH_AND(p, v, mo) __atomic_fetch_and((p), (v), (mo))
#define ATOMIC_FETCH_OR(p, v, mo)  __atomic_fetch_or((p), (v), (mo))
#define ATOMIC_FETCH_XOR(p, v, mo) __atomic_fetch_xor((p), (v), (mo))

// Fences
#define ATOMIC_THREAD_FENCE(mo) __atomic_thread_fence((mo))
#define ATOMIC_SIGNAL_FENCE(mo) __atomic_signal_fence((mo))
#else
#error "Atomics not implemented for this compiler"
#endif


// ////////////////////////
// Bit masking

#define FLAGS_HAS(value, bits)    (((U64)(value) & (U64)(bits)) == (U64)(bits))
#define FLAGS_ANY(value, bits)    (((U64)(value) & (U64)(bits)) != 0)
#define FLAGS_IS_ZERO(value)      ((U64)(value) == 0)
#define FLAGS_SET(ptr, bits)      ((*(ptr)) |=  (U64)(bits))
#define FLAGS_CLEAR(ptr, bits)    ((*(ptr)) &= ~(U64)(bits))
#define FLAGS_TOGGLE(ptr, bits)   ((*(ptr)) ^=  (U64)(bits))


// ////////////////////////
// Defer

typedef void (*defer_fn)(void*);

struct DeferGuard {
    defer_fn fn;
    void* ctx;
    bool active;

    DeferGuard(defer_fn f, void* c) : fn(f), ctx(c), active(true) {}
    ~DeferGuard() {
        if (active && fn) {
            fn(ctx);
        }
    }
    void dismiss() {
        active = false;
    }
};

#define DEFER_UNIQ(prefix) NAME_CONCAT(prefix, __COUNTER__)

#define DEFER_IMPL(ID, CAP, CODE)                                         \
    auto NAME_CONCAT(ID, _lam) = CAP() { \
        CODE;  \
    };                        \
    defer_fn NAME_CONCAT(ID, _fn) = [](void* _ctx) {                     \
        typedef decltype(NAME_CONCAT(ID, _lam))                          \
        NAME_CONCAT(ID, _lam_t);                                      \
        (*static_cast<NAME_CONCAT(ID, _lam_t)*>(_ctx))();                \
    };                                                                     \
    DeferGuard NAME_CONCAT(ID, _g)(NAME_CONCAT(ID, _fn), &NAME_CONCAT(ID, _lam))

#define DEFER(CODE) DEFER_IMPL(DEFER_UNIQ(__defer_), [=], CODE)
#define DEFER_REF(CODE) DEFER_IMPL(DEFER_UNIQ(__defer_), [&], CODE)