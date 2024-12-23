#pragma once

#include "utils_simd_copy.h"
#include "utils_helpers.h"

template <typename T, bool IsTrivial = utils::is_trivially_copyable<T>::value>
struct type_ops_trivial {
    static void move_range(T* src, T* dst, unsigned long long count) {
        bulk_copy(src, dst, count);
    }
    static void copy_range(const T* src, T* dst, unsigned long long count) {
        bulk_copy(src, dst, count);
    }
    static void destruct_range(T* ptr, unsigned long long count) {
        for (unsigned long long i = 0; i < count; ++i) {
            ptr[i].~T();
        }
    }
    static void default_construct_range(T* ptr, unsigned long long count) {
        for (unsigned long long i = 0; i < count; ++i) {
            new (reinterpret_cast<void*>(ptr + i)) T();
        }
    }
};

template <typename T>
struct type_ops_trivial<T, false> {
    static void move_range(T* src, T* dst, unsigned long long count) {
        for (unsigned long long i = 0; i < count; ++i) {
            new (reinterpret_cast<void*>(dst + i)) T(utils::utils_move(src[i]));
            src[i].~T();
        }
    }
    static void copy_range(const T* src, T* dst, unsigned long long count) {
        for (unsigned long long i = 0; i < count; ++i) {
            new (reinterpret_cast<void*>(dst + i)) T(src[i]);
        }
    }
    static void destruct_range(T* ptr, unsigned long long count) {
        for (unsigned long long i = 0; i < count; ++i) {
            ptr[i].~T();
        }
    }
    static void default_construct_range(T* ptr, unsigned long long count) {
        for (unsigned long long i = 0; i < count; ++i) {
            new (reinterpret_cast<void*>(ptr + i)) T();
        }
    }
};

template <typename T>
struct type_ops {
    using ops = type_ops_trivial<T, utils::is_trivially_copyable<T>::value>;
};