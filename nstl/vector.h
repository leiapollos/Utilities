//
// Created by André Leite on 08/06/2025.
//

#pragma once

#include "typedefs.h"
#include "typetraits.h"
#include "os/core/memcpy.h"
#include "os/core/arenaAllocator.h"

namespace nstl {
    template<typename T, u64 ArenaSize = 4 * 1024>
    class Vector {
    public:
        static constexpr u64 capacity_v = (ArenaSize > 0 && ArenaSize >= sizeof(T)) ? ArenaSize / sizeof(T) : 0;

        Vector() : _arena(ArenaSize), _data(nullptr), _size(0) {
            if constexpr (capacity_v > 0) {
                _data = static_cast<T *>(_arena.alloc(capacity_v * sizeof(T)));
                NSTL_ASSERT(
                    _data != nullptr &&
                    "Failed to initialize Vector's Arena."
                );
            }
        }

        ~Vector() {
            clear();
            // _arena clears itself
        }

        void push_back(const T &value) {
            NSTL_ASSERT(_size < capacity_v && "Vector capacity exceeded.");
            new (&_data[_size]) T(value);
            _size++;
        }

        void push_back(T &&value) {
            NSTL_ASSERT(_size < capacity_v && "Vector capacity exceeded.");
            new (&_data[_size]) T(nstl::move(value));
            _size++;
        }

        void pop_back() {
            NSTL_ASSERT(_size > 0 && "pop_back() called on empty Vector.");
            _size--;
            if constexpr (!is_trivially_destructible_v<T>) {
                _data[_size].~T();
            }
        }

        template <typename... Args>
        void emplace_back(Args &&...args) {
            NSTL_ASSERT(_size < capacity_v && "Vector capacity exceeded.");
            new (&_data[_size]) T(nstl::forward<Args>(args)...);
            _size++;
        }

        void resize(u64 newSize) {
            NSTL_ASSERT(newSize <= capacity_v && "Resize exceeds capacity.");
            if (newSize > _size) {
                for (u64 i = _size; i < newSize; ++i) {
                    new (&_data[i]) T();
                }
            } else if (newSize < _size) {
                if constexpr (!nstl::is_trivially_destructible_v<T>) {
                    for (u64 i = newSize; i < _size; ++i) {
                        _data[i].~T();
                    }
                }
            }
            _size = newSize;
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
            return capacity_v;
        }

        T &operator[](u64 index) {
            NSTL_ASSERT(index < _size && "Vector index out of bounds.");
            return _data[index];
        }

        const T &operator[](u64 index) const {
            NSTL_ASSERT(index < _size && "Vector index out of bounds.");
            return _data[index];
        }

        T* data() {
            return _data;
        }

        const T* data() const {
            return _data;
        }

    private:
        Arena _arena;
        T* _data;
        u64 _size;

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
