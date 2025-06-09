//
// Created by André Leite on 01/06/2025.
//

#pragma once

#include "platform.h"
#include "typedefs.h"

namespace nstl {
    struct true_type {
        static constexpr bool value = true;
    };

    struct false_type {
        static constexpr bool value = false;
    };

    template<typename T, typename U>
    struct is_same : false_type {
    };

    template<typename T>
    struct is_same<T, T> : true_type {
    };

    template<typename T>
    struct is_integral_helper : false_type {
    };

    template<>
    struct is_integral_helper<bool> : true_type {
    };

    template<>
    struct is_integral_helper<char> : true_type {
    };

    template<>
    struct is_integral_helper<i8> : true_type {
    };

    template<>
    struct is_integral_helper<u8> : true_type {
    };

    template<>
    struct is_integral_helper<i16> : true_type {
    };

    template<>
    struct is_integral_helper<u16> : true_type {
    };

    template<>
    struct is_integral_helper<i32> : true_type {
    };

    template<>
    struct is_integral_helper<u32> : true_type {
    };

    template<>
    struct is_integral_helper<i64> : true_type {
    };

    template<>
    struct is_integral_helper<u64> : true_type {
    };

    template<typename T>
    struct is_integral : is_integral_helper<T> {
    };


    template<typename T>
    struct remove_reference {
        using type = T;
    };

    template<typename T>
    struct remove_reference<T&> {
        using type = T;
    };

    template<typename T>
    struct remove_reference<T&&> {
        using type = T;
    };

    template<typename T>
    using remove_reference_t = typename remove_reference<T>::type;

    template<typename T>
    constexpr remove_reference_t<T>&& move(T&& arg) noexcept {
        return static_cast<remove_reference_t<T>&&>(arg);
    }


    template<typename T>
    struct is_trivially_destructible {
#if defined(COMPILER_CLANG) || defined(COMPILER_GCC)
        static constexpr bool value = __is_trivially_destructible(T);
#elif defined(COMPILER_MSVC)
        static constexpr bool value = __is_trivially_destructible(T);
#endif
    };
    template<typename T>
    inline constexpr bool is_trivially_destructible_v = is_trivially_destructible<T>::value;


    template<typename T>
    struct is_trivially_copyable {
#if defined(__GNUC__) || defined(__clang__)
        static constexpr bool value = __is_trivially_copyable(T);
#elif defined(_MSC_VER)
        static constexpr bool value = __is_trivially_copyable(T);
#endif
    };
    template<typename T>
    inline constexpr bool is_trivially_copyable_v = is_trivially_copyable<T>::value;


    template<typename T>
    constexpr T&& forward(nstl::remove_reference_t<T>& arg) noexcept {
        return static_cast<T&&>(arg);
    }
}
