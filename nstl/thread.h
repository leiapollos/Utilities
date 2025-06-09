//
// Created by André Leite on 01/06/2025.
//

#pragma once

#include "typedefs.h"
#include "function.h"

namespace nstl {
    class Thread {
    public:
        typedef void (*ThreadFunction)(void* userData);
        typedef u32 thread_id;

        enum State {
            NOT_STARTED,
            RUNNING,
            FINISHED,
        };

    private:
        uintptr _handle;
        u32 _threadId;
        State _state;
        bool _joinable;

        Function _task;

        bool start_internal();

    public:
        Thread()
            : _handle(0),
              _threadId(0),
              _state(NOT_STARTED),
              _joinable(false),
              _task() {
        }
        ~Thread() = default;

        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;


        bool start(ThreadFunction function, void* userData) {
            if (_state != NOT_STARTED || !function) {
                return false;
            }
            _task = [function, userData]() { function(userData); };
            return start_internal();
        }

        template <typename T>
        bool start(T* object, void (T::*method)()) {
            if (_state != NOT_STARTED || !object || !method) {
                return false;
            }
            _task = [object, method]() { (object->*method)(); };
            return start_internal();
        }

        bool join();
        bool detach();

        [[nodiscard]] bool is_joinable() const { return _joinable; }
        [[nodiscard]] State get_state() const { return _state; }
        [[nodiscard]] thread_id get_id() const { return _threadId; }

        static thread_id get_current_thread_id();
        static void sleep(const u32 milliseconds);
        static void yield();
        static void pause();
        static u32 get_hardware_concurrency();

        friend struct ThreadImpl;
    };
}