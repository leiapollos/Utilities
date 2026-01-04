//
// Created by André Leite on 26/07/2025.
//

#pragma once

#include <stdarg.h>

// ////////////////////////
// Assert

void debug_break() {
#if defined(COMPILER_MSVC)
    __debugbreak();
#elif defined(COMPILER_CLANG) || defined(COMPILER_GCC)
    __builtin_trap();
#else
#error "Unsupported compiler"
#endif
}

#define ASSERT_ALWAYS(condition) \
do { \
if (!(condition)) { \
debug_break(); \
} \
} while (false)

#if defined(DEBUG)
#define ASSERT_DEBUG(condition) ASSERT_ALWAYS(condition)
#else
#define ASSERT_DEBUG(condition) do {} while (false)
#endif

#define ASSERT_NOT_IMPLEMENTED() ASSERT_ALWAYS(false && "Not implemented")

#define INIT_SUCCESS(initStatement)  \
do { \
if (!(initStatement)) { \
debug_break(); \
} \
} while (false)

// ////////////////////////
// Safe Casts

#if defined(DEBUG)
static inline S32 safe_cast_u32_s32_impl(U32 value) {
    ASSERT_DEBUG(value <= (U32)INT32_MAX);
    return (S32)value;
}
#define SafeCast_U32_S32(value) safe_cast_u32_s32_impl(value)
#else
#define SafeCast_U32_S32(value) ((S32)(value))
#endif


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

#if defined(COMPILER_MSVC) || (defined(COMPILER_CLANG) && defined(PLATFORM_OS_WINDOWS))
// # pragma section(".rdata$", read)
// # define READ_ONLY __declspec(allocate(".rdata$"))
#error check if the above is working correctly
#elif defined(COMPILER_CLANG) && defined(PLATFORM_OS_MACOS)
#define READ_ONLY __attribute__((section("__DATA,__const"))) \
__attribute__((used))
#elif defined(COMPILER_CLANG) && defined(PLATFORM_OS_LINUX)
// # define READ_ONLY __attribute__((section(".rodata")))
#error check if the above is working correctly
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
// Memory Operations

FORCE_INLINE void* mem_copy(void* dst, const void* src, U64 size) {
    U8* d = (U8*)dst;
    const U8* s = (const U8*)src;

    if (((uintptr)d & 7) == 0 && ((uintptr)s & 7) == 0) {
        while (size >= 8) {
            *(U64*)d = *(const U64*)s;
            d += 8; s += 8; size -= 8;
        }
    }
    while (size > 0) {
        *d++ = *s++;
        size--;
    }
    return dst;
}

FORCE_INLINE void* mem_move(void* dst, const void* src, U64 size) {
    U8* d = (U8*)dst;
    const U8* s = (const U8*)src;

    if (d == s || size == 0) {
        return dst;
    }

    if (d < s || d >= s + size) {
        return mem_copy(dst, src, size);
    }

    d += size;
    s += size;

    if (((uintptr)d & 7) == 0 && ((uintptr)s & 7) == 0) {
        while (size >= 8) {
            d -= 8; s -= 8;
            *(U64*)d = *(const U64*)s;
            size -= 8;
        }
    }
    while (size > 0) {
        *--d = *--s;
        size--;
    }
    return dst;
}

FORCE_INLINE void* mem_set(void* dst, U8 value, U64 size) {
    U8* d = (U8*)dst;
    U64 fill8 = value;
    fill8 |= fill8 << 8;
    fill8 |= fill8 << 16;
    fill8 |= fill8 << 32;

    if (((uintptr)d & 7) == 0) {
        while (size >= 8) {
            *(U64*)d = fill8;
            d += 8; size -= 8;
        }
    }
    while (size > 0) {
        *d++ = value;
        size--;
    }
    return dst;
}

FORCE_INLINE S32 mem_cmp(const void* a, const void* b, U64 size) {
    const U8* pa = (const U8*)a;
    const U8* pb = (const U8*)b;

    if (((uintptr)pa & 7) == 0 && ((uintptr)pb & 7) == 0) {
        while (size >= 8) {
            U64 va = *(const U64*)pa;
            U64 vb = *(const U64*)pb;
            if (va != vb) {
                for (U64 i = 0; i < 8; ++i) {
                    U8 ca = pa[i];
                    U8 cb = pb[i];
                    if (ca != cb) {
                        return (ca < cb) ? -1 : 1;
                    }
                }
            }
            pa += 8; pb += 8; size -= 8;
        }
    }
    while (size > 0) {
        if (*pa != *pb) {
            return (*pa < *pb) ? -1 : 1;
        }
        ++pa; ++pb; --size;
    }
    return 0;
}


// //////////////////////////////
// String Operations

FORCE_INLINE U64 c_str_len(const char* str) {
    if (!str) { return 0; }
    const char* s = str;
    while (*s) { ++s; }
    return (U64)(s - str);
}

FORCE_INLINE S32 c_str_cmp(const char* a, const char* b) {
    if (!a && !b) { return 0; }
    if (!a) { return -1; }
    if (!b) { return 1; }
    while (*a && *b && *a == *b) {
        ++a; ++b;
    }
    return (S32)(*(const U8*)a) - (S32)(*(const U8*)b);
}


// //////////////////////////////
// Misc

#define MACRO_STR_2(s)      #s
#define MACRO_STR(s)        MACRO_STR_2(s)
#define NAME_CONCAT_2(A, B) A##B
#define NAME_CONCAT(A, B)   NAME_CONCAT_2(A, B)

#define ARRAY_COUNT(arr)        (sizeof(arr) / sizeof(arr[0]))

#define MEMCPY(dst, src, len)   mem_copy(dst, src, len)
#define MEMMOVE(dst, src, len)  mem_move(dst, src, len)
#define MEMCMP(a, b, len)       mem_cmp(a, b, len)
#define MEMSET(dst, v, len)     mem_set(dst, (U8)(v), len)

#define C_STR_LEN(ptr)          c_str_len(ptr)


// //////////////////////////////
// Free List

#define FREELIST_POP(head, outNode, nextField) \
do { \
    (outNode) = (head); \
    if (outNode) { \
        (head) = (outNode)->nextField; \
    } \
} while (false)

#define FREELIST_PUSH(head, node, nextField) \
do { \
    (node)->nextField = (head); \
    (head) = (node); \
} while (false)

#define FREELIST_IS_EMPTY(head) ((head) == 0)

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

    DeferGuard(defer_fn f, void* c) : fn(f), ctx(c) {}
    ~DeferGuard() {
        fn(ctx);
    }
};

#define DEFER_UNIQ(prefix) NAME_CONCAT(prefix, __COUNTER__)

#define DEFER_STRUCT_IMPL(ID, CAP, ...) \
    auto NAME_CONCAT(ID, _lam) = CAP() { __VA_ARGS__; }; \
    struct NAME_CONCAT(ID, _Thunk) { \
        static void call(void* ctx) { \
            (*static_cast<decltype(NAME_CONCAT(ID, _lam))*>(ctx))(); \
        } \
    }; \
    DeferGuard NAME_CONCAT(ID, _g)(&NAME_CONCAT(ID, _Thunk)::call, &NAME_CONCAT(ID, _lam))

#define DEFER(...) DEFER_STRUCT_IMPL(DEFER_UNIQ(__defer_), [=], __VA_ARGS__)
#define DEFER_REF(...) DEFER_STRUCT_IMPL(DEFER_UNIQ(__defer_), [&], __VA_ARGS__)