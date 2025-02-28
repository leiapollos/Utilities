#pragma once

#if defined(_MSC_VER)
  // MSVC: Check if <new> was included
  #ifndef _NEW_
    inline void* operator new(size_t /*size*/, void* ptr) noexcept {
        return ptr;
    }
#endif
#endif

#include "utils_macros.h"
#include "typedefs.hpp"

#pragma region Move Semantics
namespace utils {
    template <typename T> struct remove_reference { typedef T type; };
    template <typename T> struct remove_reference<T&> { typedef T type; };
    template <typename T> struct remove_reference<T&&> { typedef T type; };

    template <typename T> struct is_lvalue_reference { static const bool value = false; };
    template <typename T> struct is_lvalue_reference<T&> { static const bool value = true; };

    template <typename T>
    constexpr typename remove_reference<T>::type&& move(T&& t) noexcept {
        return static_cast<typename remove_reference<T>::type&&>(t);
    }

    template <typename T>
    constexpr T&& forward(typename remove_reference<T>::type& t) noexcept {
        return static_cast<T&&>(t);
    }

    template <typename T>
    constexpr T&& forward(typename remove_reference<T>::type&& t) noexcept {
        static_assert(!is_lvalue_reference<T>::value, "");
        return static_cast<T&&>(t);
    }
}

#pragma endregion


#pragma region Allocator

namespace utils {
    struct allocator {
        UTILS_ALWAYS_INLINE static void* allocate(uint64_t s) {
            return ::operator new(s);
        }
        UTILS_ALWAYS_INLINE static void deallocate(void* p) {
            ::operator delete(p);
        }
    };

    size_t get_page_size_helper();

    UTILS_ALWAYS_INLINE size_t get_page_size() {
        static const size_t pageSize = get_page_size_helper();
        return pageSize;
    }

    UTILS_ALWAYS_INLINE uint64_t align_to_page(const uint64_t n) {
        static const size_t p = get_page_size();
        return (n + p - 1ULL) & ~(p - 1ULL);
    }

    UTILS_ALWAYS_INLINE void page_touch(void* p, const uint64_t bytes) {
        static const size_t step = get_page_size();
        for (uint64_t offset = 0; offset < bytes; offset += step) {
            static_cast<volatile char*>(p)[offset] = 0;
        }
    }

    UTILS_ALWAYS_INLINE void* align_forward(void* p, size_t alignment) {
        UTILS_DEBUG_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0 && "Alignment must be a power of two");

        uintptr_t pi = reinterpret_cast<uintptr_t>(p);
        uintptr_t aligned = (pi + alignment - 1) & ~(alignment - 1);
        return (void*)aligned;
    }

    template <typename T>
    static void destruct_range(T* ptr, uint64_t count) {
        for (uint64_t i = 0; i < count; ++i) {
            ptr[i].~T();
        }
    }

    template <typename T>
    static void default_construct_range(T* ptr, uint64_t count) {
        for (uint64_t i = 0; i < count; ++i) {
            new (reinterpret_cast<void*>(ptr + i)) T();
        }
    }
}

#pragma endregion


#pragma region Math

namespace utils {
    template<typename T>
    T min_value();

    template<>
    UTILS_ALWAYS_INLINE int32_t min_value<int32_t>() { return -2147483647 - 1; }

    template<>
    UTILS_ALWAYS_INLINE long min_value<long>() { return -2147483647L - 1; }

    template<>
    UTILS_ALWAYS_INLINE int64_t min_value<int64_t>() { return -9223372036854775807LL - 1; }

    template<typename T>
    T max_value();

    template<>
    UTILS_ALWAYS_INLINE int32_t max_value<int32_t>() { return 2147483647; }

    template<>
    UTILS_ALWAYS_INLINE long max_value<long>() { return 2147483647L; }

    template<>
    UTILS_ALWAYS_INLINE int64_t max_value<int64_t>() { return 9223372036854775807LL; }


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