#pragma once

#include "utils_helpers.h"

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__)
// GCC
#elif defined(__clang__)
// Clang
#endif

UTILS_ALWAYS_INLINE void cpu_relax() {
#if defined(_MSC_VER)
#if defined(_M_X64)
	_mm_pause();
#else
	// TODO: ARM
#endif
#elif defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__)
	__asm__ __volatile__("pause");
#else
	// TODO: ARM
#endif
#endif
}

class alignas(64) atomic_int {
private:
	volatile int _val;

public:
	UTILS_ALWAYS_INLINE explicit atomic_int(int init = 0) : _val(init) {
	}

	UTILS_ALWAYS_INLINE int load() const {
		int tmp;
#if defined(_MSC_VER)
#if defined(_M_X64)
		tmp = static_cast<int>(
			_InterlockedOr64(
				reinterpret_cast<volatile long long*>(&_val),
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
		_InterlockedExchange64(
			reinterpret_cast<volatile long long*>(&_val),
			static_cast<long long>(x)
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
		long long prev = _InterlockedCompareExchange64(
			reinterpret_cast<volatile long long*>(&_val),
			static_cast<long long>(desired),
			static_cast<long long>(old)
		);
		success = (prev == static_cast<long long>(old));
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
};

class alignas(64) mutex {
private:
	atomic_int _lockFlag;

public:
	UTILS_ALWAYS_INLINE mutex() : _lockFlag(0) {
	}

	UTILS_ALWAYS_INLINE void lock() {
		int backoff = 1;
		while (true) {
			if (try_lock()) {
				return;
			}
			int i;
			for (i = 0; i < backoff; i++) {
				cpu_relax();
			}
			if (backoff < 1024) {
				backoff <<= 1;
			}
		}
	}

	UTILS_ALWAYS_INLINE bool try_lock() {
		int expected = 0;
		return _lockFlag.compare_exchange(expected, 1);
	}

	UTILS_ALWAYS_INLINE void unlock() {
		_lockFlag.store(0);
	}
};

class unique_lock {
private:
	mutex& _mutex;

public:
	UTILS_ALWAYS_INLINE explicit unique_lock(mutex& mutex) : _mutex(mutex) {
		_mutex.lock();
	}

	UTILS_ALWAYS_INLINE ~unique_lock() {
		_mutex.unlock();
	}
};
