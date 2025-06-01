//
// Created by André Leite on 01/06/2025.
//

#pragma once

#include "platform.h"

#ifdef PLATFORM_OS_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #if defined(PLATFORM_ARCH_X64) && defined(COMPILER_MSVC)
        #include <immintrin.h>
    #endif
#else
    #include <pthread.h>
    #include <unistd.h>
    #include <sched.h>
    #if defined(PLATFORM_ARCH_X64) && (defined(COMPILER_GCC) || defined(COMPILER_CLANG))
        #include <immintrin.h>
    #endif
#endif

namespace nstl {
    class Thread {
    public:
        typedef void (*ThreadFunction)(void* userData);

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
        static DWORD WINAPI threadEntryPoint(LPVOID param);
#else
        static void* threadEntryPoint(void* param);
#endif

    public:
        Thread();
        ~Thread();

        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        bool start(ThreadFunction function, void* userData = nullptr);
        bool join();
        bool detach();
        [[nodiscard]] bool isJoinable() const;
        [[nodiscard]] State getState() const;

        static unsigned int getCurrentThreadId();
        static void sleep(unsigned int milliseconds);
        static void yield();
        static void pause();
        static unsigned int getHardwareConcurrency();
    };

    inline Thread::Thread()
        : _function(nullptr)
        , _userData(nullptr)
        , _state(NOT_STARTED)
        , _joinable(false)
    {
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
            threadEntryPoint,
            &_threadData,
            0,
            &_threadId
        );

        if (_handle == nullptr) {
            return false;
        }
#else
        int result = pthread_create(&_handle, nullptr, threadEntryPoint, &_threadData);
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
        int result = pthread_join(_handle, nullptr);
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
        int result = pthread_detach(_handle);
        if (result == 0) {
            _joinable = false;
            return true;
        }
#endif

        return false;
    }

    inline bool Thread::isJoinable() const {
        return _joinable;
    }

    inline Thread::State Thread::getState() const {
        return _state;
    }

#ifdef PLATFORM_OS_WINDOWS
    inline DWORD WINAPI Thread::threadEntryPoint(LPVOID param) {
        ThreadData* data = static_cast<ThreadData*>(param);
        if (data && data->function) {
            data->function(data->userData);
            data->threadInstance->_state = FINISHED;
        }
        return 0;
    }
#else
    inline void* Thread::threadEntryPoint(void* param) {
        ThreadData* data = static_cast<ThreadData*>(param);
        if (data && data->function) {
            data->function(data->userData);
            data->threadInstance->_state = FINISHED;
        }
        return nullptr;
    }
#endif

    inline unsigned int Thread::getCurrentThreadId() {
#ifdef PLATFORM_OS_WINDOWS
        return static_cast<unsigned int>(::GetCurrentThreadId());
#else
        return static_cast<unsigned int>(reinterpret_cast<uintptr_t>(pthread_self()));
#endif
    }

    inline void Thread::sleep(unsigned int milliseconds) {
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
        __asm__ __volatile__("yield" ::: "memory");
#else
        yield();
#endif
#else
        yield();
#endif
    }

    inline unsigned int Thread::getHardwareConcurrency() {
#ifdef PLATFORM_OS_WINDOWS
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        return static_cast<unsigned int>(sysInfo.dwNumberOfProcessors);
#else
        long numCores = sysconf(_SC_NPROCESSORS_ONLN);
        return (numCores > 0) ? static_cast<unsigned int>(numCores) : 1;
#endif
    }
}