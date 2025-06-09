#pragma once

#include "utils_macros.h"
#include "typedefs.hpp"

#if defined(_MSC_VER)
extern "C" long long InterlockedOr64(volatile long long* ptr,
                                     long long value);
extern "C" long long InterlockedExchange64(volatile long long* ptr,
                                           long long value);
extern "C" long long InterlockedCompareExchange64(volatile long long* ptr,
                                                  long long exchange,
                                                  long long comparand);
#include <intrin.h>
#else
    #error "unsupported compiler"
#endif

namespace utils {
    enum memory_order {
        memory_order_relaxed,
        memory_order_acquire,
        memory_order_release,
        memory_order_seq_cst
    };

    class alignas(64) atomic_int {
        volatile int64_t _val;

    public:
        UTILS_ALWAYS_INLINE explicit atomic_int(int init = 0)
            : _val(init) {
        }

        UTILS_ALWAYS_INLINE int load(memory_order order = memory_order_seq_cst) {
            long long tmp = 0;
#if defined(_MSC_VER) && defined(_M_X64)
            switch (order) {
            case memory_order_relaxed:
                tmp = _val;
                break;
            case memory_order_acquire:
                tmp = _val;
                _ReadBarrier();
                break;
            case memory_order_seq_cst:
                tmp = InterlockedOr64(&_val, 0);
                break;
            default:
                UTILS_UNREACHABLE();
            }
#endif
            return static_cast<int>(tmp);
        }

        UTILS_ALWAYS_INLINE void store(int x,
                                       memory_order order = memory_order_seq_cst) {
            const long long val = x;
#if defined(_MSC_VER) && defined(_M_X64)
            switch (order) {
            case memory_order_relaxed:
                _val = val;
                break;
            case memory_order_release:
                _WriteBarrier();
                _val = val;
                break;
            case memory_order_seq_cst:
                InterlockedExchange64(&_val, val);
                break;
            default:
                UTILS_UNREACHABLE();
            }
#endif
        }

        UTILS_ALWAYS_INLINE bool compare_exchange(int& expected, int desired,
                                                  memory_order order = memory_order_seq_cst) {
            long long old_val = expected;
            const long long new_val = desired;
            const long long prev = InterlockedCompareExchange64(&_val, new_val, old_val);
            bool success = (prev == old_val);
            if (!success)
                expected = static_cast<int>(prev);
            return success;
        }

        UTILS_ALWAYS_INLINE int atomic_increment() {
            int old_val = load(memory_order_relaxed);
            while (!compare_exchange(old_val, old_val + 1)) {
            }
            return old_val + 1;
        }

        UTILS_ALWAYS_INLINE int atomic_decrement() {
            int old_val = load(memory_order_relaxed);
            while (!compare_exchange(old_val, old_val - 1)) {
            }
            return old_val - 1;
        }
    };
}
