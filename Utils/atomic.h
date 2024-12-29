#pragma once

#include "utils_macros.h"

#if defined(_MSC_VER)
	extern "C" long InterlockedOr64(volatile long* ptr, long value);
	extern "C" long InterlockedExchange64(volatile long* ptr, long value);
	extern "C" long InterlockedCompareExchange64(volatile long* ptr, long exchange, long comparand);
#elif defined(__GNUC__)
// GCC
#elif defined(__clang__)
// Clang
#endif

namespace utils {
	class alignas(64) atomic_int {
	private:
		volatile long _val;

	public:
		UTILS_ALWAYS_INLINE explicit atomic_int(int init = 0) : _val(init) {
		}

		UTILS_ALWAYS_INLINE int load() {
			int tmp;
#if defined(_MSC_VER)
#if defined(_M_X64)
			tmp = static_cast<int>(
				InterlockedOr64(
					reinterpret_cast<volatile long*>(&_val),
					0
				)
				);
#else
			// TODO: ARM
#endif
#elif defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__)
			__asm__ __volatile__(
				"mov %1, %0"
				: "=r"(tmp)
				: "m"(_val)
				: "memory"
			);
#else
			// TODO: ARM
#endif
#endif
			return tmp;
		}

		UTILS_ALWAYS_INLINE void store(int x) {
#if defined(_MSC_VER)
#if defined(_M_X64)
			InterlockedExchange64(
				reinterpret_cast<volatile long*>(&_val),
				static_cast<long>(x)
			);
#else
			// TODO: ARM
#endif
#elif defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__)
			__asm__ __volatile__(
				"mov %0, %1"
				:
			: "m"(_val), "r"(x)
				: "memory"
				);
#else
			// TODO: ARM
#endif
#endif
		}

		UTILS_ALWAYS_INLINE bool compare_exchange(int& expected, int desired) {
			bool success;
			int old = expected;
#if defined(_MSC_VER)
#if defined(_M_X64)
			long prev = InterlockedCompareExchange64(
				reinterpret_cast<volatile long*>(&_val),
				static_cast<long>(desired),
				static_cast<long>(old)
			);
			success = (prev == static_cast<long>(old));
			if (!success) {
				expected = static_cast<int>(prev);
			}
#else
			// TODO: ARM
#endif
#elif defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__)
			__asm__ __volatile__(
				"lock cmpxchg %3, %1\n"
				: "=a"(old), "=m"(_val)
				: "a"(old), "r"(desired)
				: "memory"
			);
			success = (old == expected);
			if (!success) {
				expected = old;
			}
#else
			// TODO: ARM
#endif
#endif
			return success;
		}

		UTILS_ALWAYS_INLINE int atomic_increment() {
			int old_val;
			int new_val;
			do {
				old_val = load();
				new_val = old_val + 1;
			} while (!compare_exchange(old_val, new_val));
			return new_val;
		}

		UTILS_ALWAYS_INLINE int atomic_decrement() {
			int old_val;
			int new_val;
			do {
				old_val = load();
				new_val = old_val - 1;
			} while (!compare_exchange(old_val, new_val));
			return new_val;
		}
	};
}