//
// Created by André Leite on 01/06/2025.
//

#pragma once

#include "platform.h"
#include "typedefs.h"

#ifdef PLATFORM_OS_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#if defined(PLATFORM_ARCH_X64) && defined(COMPILER_MSVC)
#include <immintrin.h>
#endif
#elif defined(PLATFORM_OS_MACOS) || defined(PLATFORM_OS_LINUX)
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#if defined(PLATFORM_ARCH_X64) && (defined(COMPILER_GCC) || defined(COMPILER_CLANG))
#include <immintrin.h>
#elif defined(PLATFORM_ARCH_ARM64) && (defined(COMPILER_GCC) || defined(COMPILER_CLANG))
#include <arm_acle.h>
#endif
#endif

namespace nstl {
    class Thread {
    public:
        typedef void (*ThreadFunction)(void* userData);
        typedef ui32 thread_id;

        enum State {
            NOT_STARTED,
            RUNNING,
            FINISHED,
        };

    private:
#ifdef PLATFORM_OS_WINDOWS
        HANDLE _handle;
        DWORD _threadId;
#else
        pthread_t _handle;
#endif

        ThreadFunction _function;
        void* _userData;
        State _state;
        bool _joinable;

        struct ThreadData {
            ThreadFunction function;
            void* userData;
            Thread* threadInstance;
        };

        ThreadData _threadData{};

#ifdef PLATFORM_OS_WINDOWS
        static DWORD WINAPI thread_entry_point(LPVOID param);
#else
        static void* thread_entry_point(void* param);
#endif

    public:
        Thread();
        ~Thread();

        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        bool start(ThreadFunction function, void* userData = nullptr);
        bool join();
        bool detach();
        [[nodiscard]] bool is_joinable() const;
        [[nodiscard]] State get_state() const;

        static thread_id get_current_thread_id();
        static void sleep(ui32 milliseconds);
        static void yield();
        static void pause();
        static ui32 get_hardware_concurrency();
    };

    inline Thread::Thread()
        : _function(nullptr)
          , _userData(nullptr)
          , _state(NOT_STARTED)
          , _joinable(false) {
#ifdef PLATFORM_OS_WINDOWS
        _handle = nullptr;
        _threadId = 0;
#else
        _handle = 0;
#endif

        _threadData.function = nullptr;
        _threadData.userData = nullptr;
        _threadData.threadInstance = this;
    }

    inline Thread::~Thread() {
        if (_joinable) {
            join();
        }
    }

    inline bool Thread::start(ThreadFunction function, void* userData) {
        if (_state != NOT_STARTED) {
            return false;
        }

        if (!function) {
            return false;
        }

        _function = function;
        _userData = userData;
        _threadData.function = function;
        _threadData.userData = userData;
        _threadData.threadInstance = this;

#ifdef PLATFORM_OS_WINDOWS
        _handle = CreateThread(
            nullptr,
            0,
            thread_entry_point,
            &_threadData,
            0,
            &_threadId
        );

        if (_handle == nullptr) {
            return false;
        }
#else
        const i32 result = pthread_create(&_handle, nullptr, thread_entry_point, &_threadData);
        if (result != 0) {
            return false;
        }
#endif

        _state = RUNNING;
        _joinable = true;
        return true;
    }

    inline bool Thread::join() {
        if (!_joinable || _state == NOT_STARTED) {
            return false;
        }

#ifdef PLATFORM_OS_WINDOWS
        if (_handle) {
            DWORD result = WaitForSingleObject(_handle, INFINITE);
            CloseHandle(_handle);
            _handle = nullptr;
            _joinable = false;
            return (result == WAIT_OBJECT_0);
        }
#else
        i32 result = pthread_join(_handle, nullptr);
        _joinable = false;
        return (result == 0);
#endif
        return false;
    }

    inline bool Thread::detach() {
        if (!_joinable || _state == NOT_STARTED) {
            return false;
        }

#ifdef PLATFORM_OS_WINDOWS
        if (_handle) {
            CloseHandle(_handle);
            _handle = nullptr;
            _joinable = false;
            return true;
        }
#else
        const i32 result = pthread_detach(_handle);
        if (result == 0) {
            _joinable = false;
            return true;
        }
#endif

        return false;
    }

    inline bool Thread::is_joinable() const {
        return _joinable;
    }

    inline Thread::State Thread::get_state() const {
        return _state;
    }

#ifdef PLATFORM_OS_WINDOWS
    inline DWORD WINAPI Thread::thread_entry_point(LPVOID param) {
        ThreadData* data = static_cast<ThreadData*>(param);
        if (data && data->function) {
            data->function(data->userData);
            data->threadInstance->_state = FINISHED;
        }
        return 0;
    }
#else
    inline void* Thread::thread_entry_point(void* param) {
        ThreadData* data = static_cast<ThreadData*>(param);
        if (data && data->function) {
            data->function(data->userData);
            data->threadInstance->_state = FINISHED;
        }
        return nullptr;
    }
#endif

    inline Thread::thread_id Thread::get_current_thread_id() {
#ifdef PLATFORM_OS_WINDOWS
        return static_cast<ui32>(::GetCurrentThreadId());
#else
        return static_cast<thread_id>(reinterpret_cast<uintptr>(pthread_self()));
#endif
    }

    inline void Thread::sleep(ui32 milliseconds) {
#ifdef PLATFORM_OS_WINDOWS
        ::Sleep(milliseconds);
#else
        usleep(milliseconds * 1000);
#endif
    }

    inline void Thread::yield() {
#ifdef PLATFORM_OS_WINDOWS
        ::SwitchToThread();
#else
        sched_yield();
#endif
    }

    inline void Thread::pause() {
#if defined(PLATFORM_ARCH_X64)
#if defined(COMPILER_MSVC)
        _mm_pause();
#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
        __builtin_ia32_pause();
#else
        yield();
#endif
#elif defined(PLATFORM_ARCH_ARM64)
#if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
        __yield();
#else
        yield();
#endif
#else
        yield();
#endif
    }

    inline ui32 Thread::get_hardware_concurrency() {
#ifdef PLATFORM_OS_WINDOWS
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        return static_cast<ui32>(sysInfo.dwNumberOfProcessors);
#else
        const i64 numCores = sysconf(_SC_NPROCESSORS_ONLN);
        return (numCores > 0) ? static_cast<ui32>(numCores) : 1;
#endif
    }
}
