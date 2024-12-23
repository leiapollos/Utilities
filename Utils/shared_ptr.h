#pragma once

#include "utils_helpers.h"
#include "utils_ptr.h"
#include "mutex.h"

namespace utils {
    template <typename T>
    struct control_block {
        T* ptr;
        atomic_int shared_count;
        atomic_int weak_count;

        control_block(T* p) : ptr(p), shared_count(1), weak_count(0) {}
        ~control_block() {}
    };

    template <typename T>
    class weak_ptr;

    template <typename T>
    class shared_ptr {
    private:
        control_block<T>* _control;
        T* _ptr;

        void release() {
            if (_control) {
                const int new_shared = _control->shared_count.atomic_decrement();
                if (new_shared == 0) {
                    delete _ptr;
                    _ptr = nullptr;
                    if (_control->weak_count.load() == 0) {
                        delete _control;
                    }
                }
                _control = nullptr;
            }
        }

    public:
        shared_ptr() : _control(nullptr), _ptr(nullptr) {}

        explicit shared_ptr(T* p) : _control(nullptr), _ptr(p) {
            if (p) {
                _control = new control_block<T>(p);
            }
        }

        shared_ptr(const shared_ptr& other) : _control(other._control), _ptr(other._ptr) {
            if (_control) {
                _control->shared_count.atomic_increment();
            }
        }

        shared_ptr(shared_ptr&& other) noexcept : _control(other._control), _ptr(other._ptr) {
            other._control = nullptr;
            other._ptr = nullptr;
        }

        ~shared_ptr() {
            release();
        }

        shared_ptr& operator=(const shared_ptr& other) {
            if (this != &other) {
                release();
                _control = other._control;
                _ptr = other._ptr;
                if (_control) {
                    _control->shared_count.atomic_increment();
                }
            }
            return *this;
        }

        shared_ptr& operator=(shared_ptr&& other) noexcept {
            if (this != &other) {
                release();
                _control = other._control;
                _ptr = other._ptr;
                other._control = nullptr;
                other._ptr = nullptr;
            }
            return *this;
        }

        T* get() const { return _ptr; }
        T& operator*() const { return *_ptr; }
        T* operator->() const { return _ptr; }
        int use_count() const { return _control ? _control->shared_count.load() : 0; }
        explicit operator bool() const { return _ptr != nullptr; }

        friend class weak_ptr<T>;
    };

    template <typename T>
    class weak_ptr {
    private:
        control_block<T>* control;

        void release() {
            if (control) {
                int new_weak = control->weak_count.atomic_decrement();
                if (new_weak == 0 && control->shared_count.load() == 0) {
                    delete control;
                }
                control = nullptr;
            }
        }

    public:
        weak_ptr() : control(nullptr) {}

        weak_ptr(const shared_ptr<T>& sp) : control(sp._control) {
            if (control) {
                control->weak_count.atomic_increment();
            }
        }

        weak_ptr(const weak_ptr& other) : control(other.control) {
            if (control) {
                control->weak_countatomic_increment();
            }
        }

        weak_ptr(weak_ptr&& other) noexcept : control(other.control) {
            other.control = nullptr;
        }

        ~weak_ptr() {
            release();
        }

        weak_ptr& operator=(const weak_ptr& other) {
            if (this != &other) {
                release();
                control = other.control;
                if (control) {
                    control->weak_countatomic_increment();
                }
            }
            return *this;
        }

        weak_ptr& operator=(weak_ptr&& other) noexcept {
            if (this != &other) {
                release();
                control = other.control;
                other.control = nullptr;
            }
            return *this;
        }

        weak_ptr& operator=(const shared_ptr<T>& sp) {
            release();
            control = sp.control;
            if (control) {
                control->weak_countatomic_increment();
            }
            return *this;
        }

        int use_count() const {
	        return control ? control->shared_count.load() : 0;
        }

        bool expired() const {
	        return use_count() == 0;
        }

        shared_ptr<T> lock() const {
            shared_ptr<T> sp;
            if (control) {
                while (true) {
                    int current_shared = control->shared_count.load();
                    if (current_shared == 0) {
                        break;
                    }
                    int expected = current_shared;
                    if (control->shared_count.compare_exchange(expected, current_shared + 1)) {
                        sp._control = control;
                        sp._ptr = control->ptr;
                        break;
                    }
                }
            }
            return sp;
        }
    };

    template <typename T, typename... Args>
    shared_ptr<T> make_shared(Args&&... args) {
        T* obj = new T(utils::forward<Args>(args)...);
        return shared_ptr<T>(obj);
    }
}