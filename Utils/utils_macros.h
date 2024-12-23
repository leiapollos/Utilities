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


#pragma region Assert

#define UTILS_DEBUG_ASSERT(expr) \
    do {                         \
        if (!(expr)) {          \
            *(volatile int*)0 = 0; /* Crash immediately */ \
        }                       \
    } while (0)

#pragma endregion