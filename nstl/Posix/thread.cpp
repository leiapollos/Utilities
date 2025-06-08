//
// Created by André Leite on 01/06/2025.
// Modified on 08/06/2025.
//

#include "../thread.h"
#include "../platform.h"

#if defined(PLATFORM_OS_MACOS) || defined(PLATFORM_OS_LINUX)

#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#if defined(PLATFORM_ARCH_X64) &&                                             \
    (defined(COMPILER_GCC) || defined(COMPILER_CLANG))
#include <immintrin.h>
#elif defined(PLATFORM_ARCH_ARM64) &&                                          \
    (defined(COMPILER_GCC) || defined(COMPILER_CLANG))
#include <arm_acle.h>
#endif

namespace nstl {
    struct ThreadImpl {
        static void* entry_point(void* param) {
            Thread* threadInstance = static_cast<Thread*>(param);
            if (threadInstance) {
                threadInstance->_threadId = Thread::get_current_thread_id();
                if (threadInstance->_task) {
                    threadInstance->_task();
                }
                threadInstance->_state = Thread::State::FINISHED;
            }
            return nullptr;
        }
    };

    bool Thread::start_internal() {
        pthread_t handle;
        const i32 result =
            pthread_create(&handle, nullptr, ThreadImpl::entry_point, this);

        if (result != 0) {
            _task = Function();
            return false;
        }

        _handle = reinterpret_cast<uintptr>(handle);
        _state = RUNNING;
        _joinable = true;
        return true;
    }

    bool Thread::join() {
        if (!_joinable || _state == NOT_STARTED) {
            return false;
        }

        const pthread_t handle = reinterpret_cast<pthread_t>(_handle);
        const i32 result = pthread_join(handle, nullptr);
        _joinable = false;
        return (result == 0);
    }

    bool Thread::detach() {
        if (!_joinable || _state == NOT_STARTED) {
            return false;
        }

        const pthread_t handle = reinterpret_cast<pthread_t>(_handle);
        const i32 result = pthread_detach(handle);
        if (result == 0) {
            _joinable = false;
            return true;
        }
        return false;
    }

    Thread::thread_id Thread::get_current_thread_id() {
        return static_cast<thread_id>(
            reinterpret_cast<uintptr>(pthread_self()));
    }

    void Thread::sleep(const ui32 milliseconds) { usleep(milliseconds * 1000); }

    void Thread::yield() { sched_yield(); }

    void Thread::pause() {
#if defined(PLATFORM_ARCH_X64) &&                                              \
    (defined(COMPILER_GCC) || defined(COMPILER_CLANG))
        __builtin_ia32_pause();
#elif defined(PLATFORM_ARCH_ARM64) &&                                          \
    (defined(COMPILER_GCC) || defined(COMPILER_CLANG))
        __yield();
#else
        yield();
#endif
    }

    ui32 Thread::get_hardware_concurrency() {
        const i64 numCores = sysconf(_SC_NPROCESSORS_ONLN);
        return (numCores > 0) ? static_cast<ui32>(numCores) : 1;
    }
}

#endif