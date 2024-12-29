#pragma once

#include "utils_macros.h"
#include "atomic.h"
#if defined(_MSC_VER)
#if defined(_M_X64)
extern "C" void _mm_pause();
#endif
#endif

namespace utils {
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
}
