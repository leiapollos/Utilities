//
// Created by André Leite on 01/06/2025.
//

#pragma once

#include "typedefs.h"

namespace nstl {
    struct true_type { static constexpr bool value = true; };
    struct false_type { static constexpr bool value = false; };

    template<typename T, typename U>
    struct is_same : false_type {};
    template<typename T>
    struct is_same<T, T> : true_type {};

    template<typename T> struct is_integral_helper : false_type {};
    template<> struct is_integral_helper<bool> : true_type {};
    template<> struct is_integral_helper<char> : true_type {};
    template<> struct is_integral_helper<i8> : true_type {};
    template<> struct is_integral_helper<ui8> : true_type {};
    template<> struct is_integral_helper<i16> : true_type {};
    template<> struct is_integral_helper<ui16> : true_type {};
    template<> struct is_integral_helper<i32> : true_type {};
    template<> struct is_integral_helper<ui32> : true_type {};
    template<> struct is_integral_helper<i64> : true_type {};
    template<> struct is_integral_helper<ui64> : true_type {};

    template<typename T> struct is_integral : is_integral_helper<T> {};

}