//
// Created by André Leite on 08/06/2025.
//

#pragma once

#include "typedefs.h"
#include "typetraits.h"
#include "os/core/memcpy.h"

namespace nstl {
    template<typename T>
    class Vector {
    public:
        Vector() : _data(nullptr), _size(0), _capacity(0) {
        }

        ~Vector() {
            delete_data(_data, _size);
        }

        void push_back(const T& value) {
            maybe_grow();
            _data[_size++] = value;
        }

        void push_back(T&& value) {
            maybe_grow();
            _data[_size++] = nstl::move(value);
        }

        void pop_back() {
            _size--;
            if constexpr (!is_trivially_destructible_v<T>) {
                _data[_size].~T();
            }
        }

        template<typename... Args>
        void emplace_back(Args&&... args) {
            maybe_grow();
            new(&_data[_size]) T(nstl::forward<Args>(args)...);
            _size++;
        }

        void reserve(u64 newCapacity) {
            reallocate(newCapacity);
        }

        void clear() {
            if constexpr (!nstl::is_trivially_destructible_v<T>) {
                for (u64 i = 0; i < _size; ++i) {
                    _data[i].~T();
                }
            }
            _size = 0;
        }

        u64 size() const {
            return _size;
        }

        u64 capacity() const {
            return _capacity;
        }

        T& operator[](u64 index) {
            return _data[index];
        }

        const T& operator[](u64 index) const {
            return _data[index];
        }

        T* data() {
            return _data;
        }

        const T* data() const {
            return _data;
        }

    private:
        T* _data;
        u64 _size;
        u64 _capacity;

        void maybe_grow(u64 numberOfNewElements = 1) {
            if (_capacity - _size < numberOfNewElements) [[unlikely]] {
                u64 newCapacity = (_capacity >= 1) ? _capacity << 1 : 2;
                reallocate(newCapacity);
            }
        }

        void reallocate(u64 newCapacity) {
            if (newCapacity <= _capacity) [[unlikely]] {
                return;
            }
            T* oldData = _data;
            T* newData = static_cast<T*>(::operator new(newCapacity * sizeof(T)));
            if constexpr (nstl::is_trivially_destructible_v<T>) {
                nstl::memcpy(newData, oldData, _size * sizeof(T));
            } else {
                for (u64 i = 0; i < _size; ++i) {
                    new(&newData[i]) T(nstl::move(_data[i]));
                }
            }
            _data = newData;
            _capacity = newCapacity;
            delete_data(oldData, _size);
        }

        void delete_data(T* data, u64 size) {
            if (data == nullptr) [[unlikely]] {
                return;
            }
            if constexpr (!nstl::is_trivially_destructible_v<T>) {
                for (u64 i = 0; i < size; ++i) {
                    data[i].~T();
                }
            }
            ::operator delete(data);
        }

    public:
        using iterator = T*;
        using const_iterator = const T*;

        iterator begin() {
            return _data;
        }

        iterator end() {
            return _data + _size;
        }

        const_iterator begin() const {
            return _data;
        }

        const_iterator end() const {
            return _data + _size;
        }

        const_iterator cbegin() const {
            return _data;
        }

        const_iterator cend() const {
            return _data + _size;
        }
    };
}
