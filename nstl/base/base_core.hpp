//
// Created by André Leite on 26/07/2025.
//

#pragma once

// ////////////////////////
// Assert

inline void debug_break() {
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
#if _MSC_VER >= 1928 && defined(__cplusplus) && __cplusplus >= 202002L
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

#define FUNCTION static
#define EXTERN_C extern "C"


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

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))


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

namespace nstl {
    template<bool B, typename T = void>
    struct enable_if {
    };

    template<typename T>
    struct enable_if<true, T> {
        using type = T;
    };
} // namespace nstl

#define Bit(n) (1ULL << (n))
#define BitMask(start, end) ((Bit((end) - (start) + 1) - 1) << (start))

template<typename E>
struct bitmask_enum_traits {
    static constexpr bool enabled = false;
    using underlying_type = void;
};

#define ENABLE_BITMASK_OPERATORS(E)                     \
    template <>                                         \
        struct bitmask_enum_traits<E> {                 \
        static constexpr bool enabled = true;           \
        using underlying_type = __underlying_type(E);   \
    };

template<typename E>
struct Flags;

template<typename E>
typename nstl::enable_if<bitmask_enum_traits<E>::enabled, Flags<E>>::type
operator|(E lhs, E rhs) {
    return Flags<E>(lhs) | rhs;
}

template<typename E>
typename nstl::enable_if<bitmask_enum_traits<E>::enabled, Flags<E>>::type
operator&(E lhs, E rhs) {
    return Flags<E>(lhs) & rhs;
}

template<typename E>
typename nstl::enable_if<bitmask_enum_traits<E>::enabled, Flags<E>>::type
operator^(E lhs, E rhs) {
    return Flags<E>(lhs) ^ rhs;
}

template<typename E>
typename nstl::enable_if<bitmask_enum_traits<E>::enabled, Flags<E>>::type
operator~(E val) {
    return ~Flags<E>(val);
}

template<typename E>
struct Flags {
    using underlying = typename bitmask_enum_traits<E>::underlying_type;
    underlying value;

    constexpr Flags() : value(0) {
    }

    constexpr Flags(E flag) : value(static_cast<underlying>(flag)) {
    }

    constexpr explicit Flags(underlying v) : value(v) {
    }

    constexpr Flags<E>& set(Flags<E> flags, bool condition) {
        if (condition) {
            value |= flags.value;
        } else {
            value &= ~flags.value;
        }
        return *this;
    }

    constexpr Flags<E>& set(Flags<E> flags) {
        value |= flags.value;
        return *this;
    }

    constexpr Flags<E>& clear(Flags<E> flags) {
        value &= ~flags.value;
        return *this;
    }

    constexpr Flags<E>& toggle(Flags<E> flags) {
        value ^= flags.value;
        return *this;
    }

    constexpr bool has(Flags<E> flags) const {
        return (value & flags.value) == flags.value;
    }

    constexpr bool any(Flags<E> flags) const {
        return (value & flags.value) != 0;
    }

    constexpr Flags<E>& operator|=(Flags<E> other) {
        value |= other.value;
        return *this;
    }

    constexpr Flags<E>& operator&=(Flags<E> other) {
        value &= other.value;
        return *this;
    }

    constexpr Flags<E>& operator^=(Flags<E> other) {
        value ^= other.value;
        return *this;
    }

    constexpr Flags<E> operator|(Flags<E> other) const {
        return Flags<E>(value | other.value);
    }

    constexpr Flags<E> operator&(Flags<E> other) const {
        return Flags<E>(value & other.value);
    }

    constexpr Flags<E> operator^(Flags<E> other) const {
        return Flags<E>(value ^ other.value);
    }

    constexpr Flags<E> operator~() const {
        return Flags<E>(~value);
    }

    constexpr bool operator==(Flags<E> other) const {
        return value == other.value;
    }

    constexpr bool operator!=(Flags<E> other) const {
        return value != other.value;
    }

    constexpr explicit operator bool() const {
        return value != 0;
    }

    constexpr explicit operator underlying() const {
        return value;
    }
};
