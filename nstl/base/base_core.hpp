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
// Keywords

#define FUNCTION static
#define EXTERN_C extern "C"


// ////////////////////////
// Address Sanitizer

EXTERN_C void __asan_poison_memory_region(void const volatile *addr, size_t size);
EXTERN_C void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
# define ASAN_POISON_MEMORY_REGION(addr, size)   __asan_poison_memory_region((addr), (size))
# define ASAN_UNPOISON_MEMORY_REGION(addr, size) __asan_unpoison_memory_region((addr), (size))


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
