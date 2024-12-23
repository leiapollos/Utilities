#pragma once

#include "../Utils/utils_helpers.h"
#include "../Utils/utils_type_ops.h"

template <typename T>
class vector {
    utils::static_asserts<T> _compileTimeChecks;

    using ops = typename type_ops<T>::ops;

private:
    T* _data;
    unsigned long long _size;
    unsigned long long _capacity;

    UTILS_ALWAYS_INLINE void page_touch(void* p, unsigned long long bytes) {
        unsigned long long step = 4096ULL;
        for (unsigned long long offset = 0; offset < bytes; offset += step) {
            reinterpret_cast<volatile char*>(p)[offset] = 0;
        }
    }

    UTILS_ALWAYS_INLINE void reallocate(unsigned long long c) {
        c = utils::align_to_page(c * sizeof(T)) / sizeof(T);
        T* n = static_cast<T*>(utils::allocator::allocate(c * sizeof(T)));
        page_touch(n, c * sizeof(T));
        ops::move_range(_data, n, _size);
        utils::allocator::deallocate(_data);
        _data = n;
        _capacity = c;
    }

public:
    UTILS_ALWAYS_INLINE vector() : _data(nullptr), _size(0), _capacity(0) {}
    UTILS_ALWAYS_INLINE ~vector() {
        if (_data) {
            ops::destruct_range(_data, _size);
            utils::allocator::deallocate(_data);
        }
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
        new (reinterpret_cast<void*>(_data + _size)) T(utils::utils_move(v));
        ++_size;
    }

    template <typename... Args>
    UTILS_ALWAYS_INLINE void emplace_back(Args&&... args) {
        if (_size == _capacity) [[unlikely]] {
            reserve((_capacity == 0) ? 1ULL : (_capacity * 2ULL));
        }
        new (reinterpret_cast<void*>(_data + _size)) T(utils::utils_forward<Args>(args)...);
        ++_size;
    }

    UTILS_ALWAYS_INLINE void push_back_multiple(const T* arr, unsigned long long count) {
        UTILS_DEBUG_ASSERT(arr || (count == 0ULL));
        if (_size == _capacity) [[unlikely]] {
            unsigned long long n = (_capacity == 0ULL) ? count : _capacity;
            while (n < _size + count) { n *= 2ULL; }
            reserve(n);
        }
        ops::copy_range(arr, _data + _size, count);
        _size += count;
    }

    UTILS_ALWAYS_INLINE void push_back_multiple(vector<T>& other) {
        if (UTILS_UNLIKELY(_size + other._size > _capacity)) {
            unsigned long long n = (_capacity == 0ULL) ? other._size : _capacity;
            while (n < _size + other._size) { n *= 2ULL; }
            reserve(n);
        }
        ops::move_range(other._data, _data + _size, other._size);
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
        ops::destruct_range(_data, _size);
        _size = 0;
    }

    UTILS_ALWAYS_INLINE void clear_full() {
        ops::destruct_range(_data, _size);
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
            ops::default_construct_range(_data + _size, newSize - _size);
        }
        else {
            ops::destruct_range(_data + newSize, _size - newSize);
        }
        _size = newSize;
    }
};
