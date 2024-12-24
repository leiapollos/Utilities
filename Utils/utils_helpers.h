#pragma once

#pragma region Traits

#if defined(__GNUC__) || defined(__clang__)
#define UTILS_IS_TRIVIALLY_COPYABLE(T) __is_trivially_copyable(T)
#elif defined(_MSC_VER)
#define UTILS_IS_TRIVIALLY_COPYABLE(T) __is_trivially_copyable(T)
#else
#define UTILS_IS_TRIVIALLY_COPYABLE(T) false
#endif

namespace utils {
    template <typename T>
    struct is_trivially_copyable {
        static const bool value = UTILS_IS_TRIVIALLY_COPYABLE(T);
    };

    template <typename T>
    struct static_asserts {
        static_assert(sizeof(T) <= 1024, "");
    };
}

#pragma endregion


#pragma region Inline

#if defined(__GNUC__) || defined(__clang__)
#define UTILS_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define UTILS_ALWAYS_INLINE __forceinline
#else
#define UTILS_ALWAYS_INLINE inline
#endif

#pragma endregion


#pragma region Move Semantics
namespace utils {
    template <typename T> struct remove_reference { typedef T type; };
    template <typename T> struct remove_reference<T&> { typedef T type; };
    template <typename T> struct remove_reference<T&&> { typedef T type; };

    template <typename T> struct is_lvalue_reference { static const bool value = false; };
    template <typename T> struct is_lvalue_reference<T&> { static const bool value = true; };

    template <typename T>
    constexpr typename remove_reference<T>::type&& utils_move(T&& t) noexcept {
        return static_cast<typename remove_reference<T>::type&&>(t);
    }

    template <typename T>
    constexpr T&& utils_forward(typename remove_reference<T>::type& t) noexcept {
        return static_cast<T&&>(t);
    }

    template <typename T>
    constexpr T&& utils_forward(typename remove_reference<T>::type&& t) noexcept {
        static_assert(!is_lvalue_reference<T>::value, "");
        return static_cast<T&&>(t);
    }
}

#pragma endregion


#pragma region Allocator

namespace utils {
    struct allocator {
        UTILS_ALWAYS_INLINE static void* allocate(unsigned long long s) {
            return ::operator new(s);
        }
        UTILS_ALWAYS_INLINE static void deallocate(void* p) {
            ::operator delete(p);
        }
    };

    UTILS_ALWAYS_INLINE unsigned long long align_to_page(unsigned long long n) {
        const unsigned long long p = 4096ULL;
        return (n + p - 1ULL) & ~(p - 1ULL);
    }
}

#pragma endregion


#pragma region Assert

#define UTILS_DEBUG_ASSERT(expr) \
    do {                         \
        if (!(expr)) {          \
            *(volatile int*)0 = 0; /* Crash immediately */ \
        }                       \
    } while (0)

#pragma endregion


#pragma region Math

namespace utils {
    template<typename T>
    T min_value();

    template<>
    UTILS_ALWAYS_INLINE int min_value<int>() { return -2147483647 - 1; }

    template<>
    UTILS_ALWAYS_INLINE long min_value<long>() { return -2147483647L - 1; }

    template<>
    UTILS_ALWAYS_INLINE long long min_value<long long>() { return -9223372036854775807LL - 1; }

    template<typename T>
    T max_value();

    template<>
    UTILS_ALWAYS_INLINE int max_value<int>() { return 2147483647; }

    template<>
    UTILS_ALWAYS_INLINE long max_value<long>() { return 2147483647L; }

    template<>
    UTILS_ALWAYS_INLINE long long max_value<long long>() { return 9223372036854775807LL; }


    template<typename T>
    UTILS_ALWAYS_INLINE T min(T a, T b) {
        return (a < b) ? a : b;
    }

    template<typename T>
    UTILS_ALWAYS_INLINE T max(T a, T b) {
        return (a > b) ? a : b;
    }
}

#pragma endregion