#pragma once

#include "../Utils/utils_helpers.h"
#include "../Utils/utils_memory_functions.h"

namespace utils {
    template <typename T>
    class vector {
    private:
        T* _data;
        unsigned long long _size;
        unsigned long long _capacity;

        UTILS_ALWAYS_INLINE void reallocate(unsigned long long c) {
            c = utils::align_to_page(c * sizeof(T)) / sizeof(T);
            T* n = static_cast<T*>(utils::allocator::allocate(c * sizeof(T)));
            utils::page_touch(n, c * sizeof(T));
            utils::memcpy(n, _data, _size * sizeof(T));
            utils::allocator::deallocate(_data);
            _data = n;
            _capacity = c;
        }

    public:
        UTILS_ALWAYS_INLINE vector() : _data(nullptr), _size(0), _capacity(0) {}

        UTILS_ALWAYS_INLINE vector(const vector<T>& other) : _data(nullptr), _size(0), _capacity(0) {
	        if (other.size() > 0) {
                reallocate(other.size());
                utils::memcpy(_data, other._data, other.size() * sizeof(T));
                _size = other.size();
	        }
        }

        UTILS_ALWAYS_INLINE vector(const vector<T>&& other) noexcept : _data(nullptr), _size(0), _capacity(0) {
	        if (this != &other) {
                clear_full();
                _data = other._data;
                _size = other.size();
                _capacity = other.capacity();

                other._data = nullptr;
                other._size = 0;
                other._capacity = 0;
	        }
        }

        UTILS_ALWAYS_INLINE ~vector() {
            if (_data) {
                utils::destruct_range(_data, _size);
                utils::allocator::deallocate(_data);
            }
        }

        UTILS_ALWAYS_INLINE vector& operator=(const vector& other) {
            if (this != &other) {
                clear_soft();
                if (other._size > _capacity) {
                    reallocate(other._size);
                }
                utils::memcpy(_data, other._data, other._size * sizeof(T));
                _size = other._size;
            }
            return *this;
        }

        UTILS_ALWAYS_INLINE vector& operator=(vector&& other) noexcept {
            if (this != &other) {
                clear_full();
                _data = other._data;
                _size = other._size;
                _capacity = other._capacity;

                other._data = nullptr;
                other._size = 0;
                other._capacity = 0;
            }
            return *this;
        }

        UTILS_ALWAYS_INLINE unsigned long long size() const { return _size; }
        UTILS_ALWAYS_INLINE unsigned long long capacity() const { return _capacity; }

        UTILS_ALWAYS_INLINE void reserve(unsigned long long c) {
            UTILS_DEBUG_ASSERT((_data == nullptr && _size == 0) || (_data != nullptr));
            if (c <= _capacity) return;
            reallocate(c);
        }

        UTILS_ALWAYS_INLINE void push_back(const T& v) {
            if (_size == _capacity) [[unlikely]] {
                reserve((_capacity == 0) ? 1ULL : (_capacity * 2ULL));
            }
            new (reinterpret_cast<void*>(_data + _size)) T(v);
            ++_size;
        }

        UTILS_ALWAYS_INLINE void push_back(T&& v) {
            if (_size == _capacity) [[unlikely]] {
                reserve((_capacity == 0) ? 1ULL : (_capacity * 2ULL));
            }
            new (reinterpret_cast<void*>(_data + _size)) T(utils::move(v));
            ++_size;
        }

        template <typename... Args>
        UTILS_ALWAYS_INLINE void emplace_back(Args&&... args) {
            if (_size == _capacity) [[unlikely]] {
                reserve((_capacity == 0) ? 1ULL : (_capacity * 2ULL));
            }
            new (reinterpret_cast<void*>(_data + _size)) T(utils::forward<Args>(args)...);
            ++_size;
        }

        UTILS_ALWAYS_INLINE void push_back_multiple(const T* arr, unsigned long long count) {
            UTILS_DEBUG_ASSERT(arr || (count == 0ULL));
            if (_size == _capacity) [[unlikely]] {
                unsigned long long n = (_capacity == 0ULL) ? count : _capacity;
                while (n < _size + count) {
	                n *= 2ULL;
                }
                reserve(n);
            }
            utils::memcpy(_data + _size, arr, count);
            _size += count;
        }

        UTILS_ALWAYS_INLINE void push_back_multiple(vector<T>& other) {
            if (_size + other._size > _capacity) [[unlikely]] {
                unsigned long long n = (_capacity == 0ULL) ? other._size : _capacity;
                while (n < _size + other._size) {
	                n *= 2ULL;
                }
                reserve(n);
            }
            utils::memcpy(_data + _size, other._data, other._size);
            _size += other._size;
            other._size = 0;
        }

        UTILS_ALWAYS_INLINE T& operator[](unsigned long long i) {
            UTILS_DEBUG_ASSERT(i < _size);
            return _data[i];
        }

        UTILS_ALWAYS_INLINE const T& operator[](unsigned long long i) const {
            UTILS_DEBUG_ASSERT(i < _size);
            return _data[i];
        }

        UTILS_ALWAYS_INLINE void clear_soft() {
            utils::destruct_range(_data, _size);
            _size = 0;
        }

        UTILS_ALWAYS_INLINE void clear_full() {
            utils::destruct_range(_data, _size);
            utils::allocator::deallocate(_data);
            _data = nullptr;
            _size = 0;
            _capacity = 0;
        }

        UTILS_ALWAYS_INLINE void resize(unsigned long long newSize) {
            if (newSize > _capacity) {
                reallocate(newSize);
            }
            if (newSize > _size) {
                utils::default_construct_range(_data + _size, newSize - _size);
            } else {
                utils::destruct_range(_data + newSize, _size - newSize);
            }
            _size = newSize;
        }

        class iterator {
        public:
            UTILS_ALWAYS_INLINE iterator(T* p) : _ptr(p) {}
            UTILS_ALWAYS_INLINE T& operator*() const { return *_ptr; }
            UTILS_ALWAYS_INLINE iterator& operator++() { ++_ptr; return *this; }
            UTILS_ALWAYS_INLINE iterator operator++(int) { iterator tmp = *this; ++_ptr; return tmp; }
            UTILS_ALWAYS_INLINE bool operator==(const iterator& other) const { return _ptr == other._ptr; }
            UTILS_ALWAYS_INLINE bool operator!=(const iterator& other) const { return _ptr != other._ptr; }
        private:
            T* _ptr;
        };

        class const_iterator {
        public:
            UTILS_ALWAYS_INLINE const_iterator(const T* p) : _ptr(p) {}
            UTILS_ALWAYS_INLINE const T& operator*() const { return *_ptr; }
            UTILS_ALWAYS_INLINE const_iterator& operator++() { ++_ptr; return *this; }
            UTILS_ALWAYS_INLINE const_iterator operator++(int) { const_iterator tmp = *this; ++_ptr; return tmp; }
            UTILS_ALWAYS_INLINE bool operator==(const const_iterator& other) const { return _ptr == other._ptr; }
            UTILS_ALWAYS_INLINE bool operator!=(const const_iterator& other) const { return _ptr != other._ptr; }
        private:
            const T* _ptr;
        };

        UTILS_ALWAYS_INLINE iterator begin() {
            return iterator(_data);
        }

        UTILS_ALWAYS_INLINE iterator end() {
            return iterator(_data + _size);
        }

        const_iterator begin() const {
            return const_iterator(_data);
        }

        const_iterator end() const {
            return const_iterator(_data + _size);
        }
    };
}