//
// Created by André Leite on 31/05/2025.
//

#pragma once

#include "memory.h"
#include "typetraits.h"

namespace nstl {
    template <typename T>
    class Atomic;

    #if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
        inline int to_compiler_memory_order(const memory_order order) {
            switch (order) {
                case memory_order::relaxed: return __ATOMIC_RELAXED;
                case memory_order::acquire: return __ATOMIC_ACQUIRE;
                case memory_order::release: return __ATOMIC_RELEASE;
                case memory_order::acq_rel: return __ATOMIC_ACQ_REL;
                case memory_order::seq_cst: return __ATOMIC_SEQ_CST;
            }
            return __ATOMIC_SEQ_CST; // Should not be reached, defensive
        }
    #endif // COMPILER_GCC || COMPILER_CLANG

    template<typename T> struct is_atomic_supported_helper : false_type {};
    template<> struct is_atomic_supported_helper<i64> : true_type {};
    template<> struct is_atomic_supported_helper<ui64> : true_type {};
    template<typename T> struct is_atomic_supported : is_atomic_supported_helper<T> {};

    template <typename T>
    class Atomic {
        static_assert(is_atomic_supported<T>::value && sizeof(T) == 8,
                      "Atomic<T> only supports 64-bit integral types. ");
    private:
        alignas(sizeof(T)) volatile T _value;

    public:
        using value_type = T;
        using difference_type = T; // For fetch_add/sub, argument is of type T

        // Constructors
        Atomic() noexcept : _value(static_cast<T>(0)) {}
        explicit Atomic(T val) noexcept : _value(val) {}

        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) volatile = delete;

        void store(T desired, memory_order order = memory_order::seq_cst) noexcept {
    #if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
            __atomic_store_n(&_value, desired, to_compiler_memory_order(order));
    #elif defined(COMPILER_MSVC)
            msvc_store_helper(desired, order);
    #endif
        }

        T load(memory_order order = memory_order::seq_cst) const noexcept {
    #if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
            return __atomic_load_n(const_cast<T*>(&_value), to_compiler_memory_order(order));
    #elif defined(COMPILER_MSVC)
            return msvc_load_helper(order);
    #endif
        }

        bool compare_exchange_strong(T& expected, T desired,
                                     memory_order success_order,
                                     memory_order failure_order) noexcept {
    #if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
            return __atomic_compare_exchange_n(&_value, &expected, desired,
                                               false, // strong
                                               to_compiler_memory_order(success_order),
                                               to_compiler_memory_order(failure_order));
    #elif defined(COMPILER_MSVC)
            return msvc_compare_exchange_strong_helper(expected, desired, success_order, failure_order);
    #endif
        }

        bool compare_exchange_weak(T& expected, T desired,
                                   memory_order success_order,
                                   memory_order failure_order) noexcept {
    #if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
            return __atomic_compare_exchange_n(&_value, &expected, desired,
                                               true, // weak
                                               to_compiler_memory_order(success_order),
                                               to_compiler_memory_order(failure_order));
    #elif defined(COMPILER_MSVC)
            // MSVC _InterlockedCompareExchange is strong. Emulate weak as strong.
            return msvc_compare_exchange_strong_helper(expected, desired, success_order, failure_order);
    #endif
        }

        T fetch_add(difference_type arg, memory_order order = memory_order::seq_cst) noexcept {
    #if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
            return __atomic_fetch_add(&_value, arg, to_compiler_memory_order(order));
    #elif defined(COMPILER_MSVC)
            return msvc_fetch_add_helper(arg, order);
    #endif
        }

        T fetch_sub(difference_type arg, memory_order order = memory_order::seq_cst) noexcept {
    #if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
            return __atomic_fetch_sub(&_value, arg, to_compiler_memory_order(order));
    #elif defined(COMPILER_MSVC)
            // For unsigned types, adding the two's complement of 'arg'
            if constexpr (static_cast<T>(-1) > static_cast<T>(0)) { // Unsigned T
                 return msvc_fetch_add_helper(static_cast<T>(~arg + 1), order);
            } else { // Signed T
                 return msvc_fetch_add_helper(static_cast<T>(-arg), order);
            }
    #endif
        }

        T increment(memory_order order = memory_order::seq_cst) noexcept {
            return fetch_add(static_cast<difference_type>(1), order);
        }

        T decrement(memory_order order = memory_order::seq_cst) noexcept {
            return fetch_sub(static_cast<difference_type>(1), order);
        }

    private:
    #if defined(COMPILER_MSVC)
        void msvc_store_helper(T desired, memory_order order) noexcept {
            volatile __int64* ptr = reinterpret_cast<volatile __int64*>(&_value);
            __int64 val_desired = static_cast<__int64>(desired);

            if (order == memory_order::relaxed) {
                __iso_volatile_store64(ptr, val_desired);
            } else if (order == memory_order::release) {
                _InterlockedExchange64_rel(ptr, val_desired);
            } else {
                _InterlockedExchange64(ptr, val_desired);
            }
        }

        T msvc_load_helper(memory_order order) const noexcept {
            volatile __int64* ptr = reinterpret_cast<volatile __int64*>(const_cast<T*>(&_value));

            if (order == memory_order::relaxed) {
                return static_cast<T>(__iso_volatile_load64(ptr));
            } else if (order == memory_order::acquire) {
                __int64 value = __iso_volatile_load64(ptr);
                _ReadBarrier();
                return static_cast<T>(value);
            } else {
                return static_cast<T>(_InterlockedOr64(ptr, 0));
            }
        }

        bool msvc_compare_exchange_strong_helper(T& expected, T desired,
                                                 memory_order success_order,
                                                 memory_order /* failure_order */) noexcept {
            volatile __int64* ptr = reinterpret_cast<volatile __int64*>(&_value);
            __int64 current_expected_raw = static_cast<__int64>(expected);
            __int64 desired_raw = static_cast<__int64>(desired);
            __int64 previous_val_raw;

            switch (success_order) {
                case memory_order::relaxed:
                    previous_val_raw = _InterlockedCompareExchange64(ptr, desired_raw, current_expected_raw);
                    break;
                case memory_order::acquire:
                    previous_val_raw = _InterlockedCompareExchange64_acq(ptr, desired_raw, current_expected_raw);
                    break;
                case memory_order::release:
                    previous_val_raw = _InterlockedCompareExchange64_rel(ptr, desired_raw, current_expected_raw);
                    break;
                case memory_order::acq_rel:
                case memory_order::seq_cst:
                default:
                    previous_val_raw = _InterlockedCompareExchange64(ptr, desired_raw, current_expected_raw);
                    break;
            }

            if (previous_val_raw == current_expected_raw) {
                return true;
            } else {
                expected = static_cast<T>(previous_val_raw);
                return false;
            }
        }

        memory_order msvc_cas_failure_order_for_rmw_loop(memory_order success_order) const noexcept {
            if (success_order == memory_order::relaxed) return memory_order::relaxed;
            return memory_order::acquire;
        }

        T msvc_fetch_add_helper(T arg, memory_order order) noexcept {
            volatile __int64* ptr = reinterpret_cast<volatile __int64*>(&_value);
            __int64 val_arg = static_cast<__int64>(arg);

            switch (order) {
                case memory_order::relaxed:
                    {
                        T current_val_snapshot = load(memory_order::relaxed);
                        T desired_val;
                        do {
                            desired_val = static_cast<T>(current_val_snapshot + arg);
                        } while (!compare_exchange_weak(current_val_snapshot, desired_val,
                                                       memory_order::relaxed,
                                                       memory_order::relaxed
                                                       ));
                        return current_val_snapshot;
                    }
                case memory_order::acquire:
                    return static_cast<T>(_InterlockedExchangeAdd64_acq(ptr, val_arg));
                case memory_order::release:
                    return static_cast<T>(_InterlockedExchangeAdd64_rel(ptr, val_arg));
                case memory_order::acq_rel:
                case memory_order::seq_cst:
                default:
                    return static_cast<T>(_InterlockedExchangeAdd64(ptr, val_arg));
            }
        }
    #endif // COMPILER_MSVC
    };
}
