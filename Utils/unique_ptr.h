#pragma once

#include "utils_helpers.h"
#include "utils_ptr.h"

namespace utils {
    template <typename T, typename deleter = default_deleter<T> >
    class unique_ptr {
    private:
        T* _ptr;
        deleter _deleter;

    public:
        constexpr unique_ptr() noexcept : _ptr(nullptr), _deleter(deleter()) {}

        explicit unique_ptr(T* p) noexcept : _ptr(p), _deleter(deleter()) {}

        unique_ptr(T* p, deleter d) noexcept : _ptr(p), _deleter(d) {}

        unique_ptr(unique_ptr&& other) noexcept
            : _ptr(other._ptr), _deleter(other._deleter) {
            other._ptr = nullptr;
        }

        template <typename U, typename D>
        unique_ptr(unique_ptr<U, D>&& other) noexcept
            : _ptr(other._ptr), _deleter(other._deleter) {
            other._ptr = nullptr;
        }

        ~unique_ptr() {
            reset();
        }

        unique_ptr& operator=(unique_ptr&& other) noexcept {
            if (this != &other) {
                reset();
                _ptr = other._ptr;
                _deleter = other._deleter;
                other._ptr = nullptr;
            }
            return *this;
        }

        template <typename U, typename D>
        unique_ptr& operator=(unique_ptr<U, D>&& other) noexcept {
            reset();
            _ptr = other._ptr;
            _deleter = other._deleter;
            other._ptr = nullptr;
            return *this;
        }

        unique_ptr(const unique_ptr&) = delete;
        unique_ptr& operator=(const unique_ptr&) = delete;

        constexpr T* get() const noexcept { return _ptr; }

        deleter& get_deleter() noexcept { return _deleter; }
        const deleter& get_deleter() const noexcept { return _deleter; }

        explicit operator bool() const noexcept { return _ptr != nullptr; }

        T& operator*() const noexcept { return *_ptr; }

        T* operator->() const noexcept { return _ptr; }

        T* release() noexcept {
            T* temp = _ptr;
            _ptr = nullptr;
            return temp;
        }

        void reset(T* p = nullptr) noexcept {
            if (_ptr) {
                _deleter(_ptr);
            }
            _ptr = p;
        }

        void swap(unique_ptr& other) noexcept {
            T* temp = _ptr;
            _ptr = other._ptr;
            other._ptr = temp;

            deleter tempDeleter = _deleter;
            _deleter = other._deleter;
            other._deleter = tempDeleter;
        }
    };

    template <typename T, typename... Args>
    unique_ptr<T> make_unique(Args&&... args) {
        return unique_ptr<T>(new T(utils::forward<Args>(args)...));
    }

    template <typename T, typename deleter, typename... Args>
    unique_ptr<T, deleter> make_unique_with_deleter(deleter d, Args&&... args) {
        return unique_ptr<T, deleter>(new T(utils::forward<Args>(args)...), d);
    }
}