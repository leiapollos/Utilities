#pragma once

#include "../Utils/utils_macros.h"
#include "../Utils/typedefs.hpp"
#include "../Utils/utils_memory_functions.h"

namespace utils {
    class string {
    public:
        enum class StringError {
            None,
            MemoryAllocationFailed,
            OutOfRange,
            NullPointer,
            InvalidArgument,
        };

    private:
        static constexpr size_t k_SSOThreshold = 15;

        bool _isSSO;
        size_t _size;
        size_t _capacity;
        union {
            char _ssoBuffer[k_SSOThreshold + 1];
            char* _heapBuffer;
        };

        StringError expand_capacity(const size_t newCapacity) {
            if (_isSSO) {
                char* buffer = new char[newCapacity + 1];
                if (buffer == nullptr) {
                    return StringError::MemoryAllocationFailed;
                }
                utils::memcpy(buffer, _ssoBuffer, _size);
                buffer[_size] = '\0';
                _heapBuffer = buffer;
                _isSSO = false;
            } else {
                char* buffer = new char[newCapacity + 1];
                if (buffer == nullptr) {
                    return StringError::MemoryAllocationFailed;
                }
                utils::memcpy(buffer, _heapBuffer, _size);
                buffer[_size] = '\0';
                delete[] _heapBuffer;
                _heapBuffer = buffer;
            }
            _capacity = newCapacity;
            return StringError::None;
        }

        StringError ensure_capacity(const size_t minCapacity) {
            if (_isSSO) {
                if (minCapacity <= k_SSOThreshold) {
                    return StringError::None;
                }
                size_t newCap = k_SSOThreshold * 2;
                while (newCap < minCapacity) {
                    newCap *= 2;
                }
                return expand_capacity(newCap);
            } else {
                if (minCapacity <= _capacity) {
                    return StringError::None;
                }
                size_t newCap = _capacity * 2;
                while (newCap < minCapacity) {
                    newCap *= 2;
                }
                return expand_capacity(newCap);
            }
        }

        void set_empty_string() {
            _isSSO = true;
            _size = 0;
            _capacity = k_SSOThreshold;
            _ssoBuffer[0] = '\0';
        }

    public:
        string() : _isSSO(true), _size(0), _capacity(k_SSOThreshold) {
            _ssoBuffer[0] = '\0';
        }

        string(const char* cstr) : _isSSO(true), _size(0), _capacity(k_SSOThreshold) {
            if (cstr == nullptr) {
                _ssoBuffer[0] = '\0';
                return;
            }

            while (cstr[_size] != '\0') {
                _size++;
            }

            if (_size > k_SSOThreshold) {
                _isSSO = false;
                _capacity = _size;
                _heapBuffer = new char[_capacity + 1];
                if (_heapBuffer == nullptr) {
                    UTILS_DEBUG_ASSERT(false);
                    set_empty_string();
                    return;
                }
                utils::memcpy(_heapBuffer, cstr, _size);
                _heapBuffer[_size] = '\0';
            } else {
                utils::memcpy(_ssoBuffer, cstr, _size);
                _ssoBuffer[_size] = '\0';
            }
        }

        string(const string& other) : _isSSO(other._isSSO), _size(other._size), _capacity(other._capacity) {
            if (_isSSO) {
                utils::memcpy(_ssoBuffer, other._ssoBuffer, _size);
                _ssoBuffer[_size] = '\0';
            } else {
                _heapBuffer = new char[_capacity + 1];
                if (_heapBuffer == nullptr) {
                    UTILS_DEBUG_ASSERT(false);
                    set_empty_string();
                    return;
                }
                utils::memcpy(_heapBuffer, other._heapBuffer, _size);
                _heapBuffer[_size] = '\0';
            }
        }

        string(string&& other) noexcept : _isSSO(other._isSSO), _size(other._size), _capacity(other._capacity) {
            if (_isSSO) {
                utils::memcpy(_ssoBuffer, other._ssoBuffer, _size);
                _ssoBuffer[_size] = '\0';
            } else {
                _heapBuffer = other._heapBuffer;
                other._heapBuffer = nullptr;
                other._size = 0;
                other._capacity = k_SSOThreshold;
                other._isSSO = true;
                other._ssoBuffer[0] = '\0';
            }
        }

        ~string() {
            if (!_isSSO && _heapBuffer) {
                delete[] _heapBuffer;
            }
        }

        string& operator=(const string& other) {
            if (this == &other) {
                return *this;
            }

            if (other._isSSO) {
                if (!_isSSO) {
                    delete[] _heapBuffer;
                    _isSSO = true;
                }
                if (other._size > k_SSOThreshold) {
                    return *this;
                }
                utils::memcpy(_ssoBuffer, other._ssoBuffer, other._size);
                _size = other._size;
                _ssoBuffer[_size] = '\0';
            } else {
                StringError err = ensure_capacity(other._size);
                if (err != StringError::None) {
                    UTILS_DEBUG_ASSERT(false);
                    return *this;
                }
                utils::memcpy(_heapBuffer, other._heapBuffer, other._size);
                _size = other._size;
                _heapBuffer[_size] = '\0';
                if (_isSSO) {
                    _isSSO = false;
                }
            }
            return *this;
        }

        /*
        // Move Assignment Operator
        string& operator=(string&& other) noexcept {
            move_assign(std::move(other));
            return *this;
        }
        */

        string operator+=(const string& other) {
            const size_t newSize = _size + other._size;
            if (newSize > (_isSSO ? k_SSOThreshold : _capacity)) {
                StringError err = ensure_capacity(newSize);
                if (err != StringError::None) {
                    UTILS_DEBUG_ASSERT(false);
                    set_empty_string();
                    return *this;
                }
            }

            if (_isSSO) {
                utils::memcpy(_ssoBuffer + _size, other._isSSO ? other._ssoBuffer : other._heapBuffer, other._size);
                _size = newSize;
                _ssoBuffer[_size] = '\0';
            } else {
                utils::memcpy(_heapBuffer + _size, other._isSSO ? other._ssoBuffer : other._heapBuffer, other._size);
                _size = newSize;
                _heapBuffer[_size] = '\0';
            }
            return *this;
        }

        string operator+=(const char& other) {
            const size_t newSize = _size + 1;
            if (newSize > (_isSSO ? k_SSOThreshold : _capacity)) {
                StringError err = ensure_capacity(newSize);
                if (err != StringError::None) {
                    UTILS_DEBUG_ASSERT(false);
                    set_empty_string();
                    return *this;
                }
            }

            if (_isSSO) {
                _ssoBuffer[_size] = other;
                _size = newSize;
                _ssoBuffer[_size] = '\0';
            } else {
                _heapBuffer[_size] = other;
                _size = newSize;
                _heapBuffer[_size] = '\0';
            }
            return *this;
        }

        string operator+(const string& other) const {
            string result;
            const size_t newSize = _size + other._size;
            if (newSize > k_SSOThreshold) {
                result._isSSO = false;
                result._capacity = newSize;
                result._heapBuffer = new char[newSize + 1];
                if (result._heapBuffer == nullptr) {
                    UTILS_DEBUG_ASSERT(false);
                    result.set_empty_string();
                    return result;
                }
                utils::memcpy(result._heapBuffer, _isSSO ? _ssoBuffer : _heapBuffer, _size);
                utils::memcpy(result._heapBuffer + _size, other._isSSO ? other._ssoBuffer : other._heapBuffer, other._size);
                result._heapBuffer[newSize] = '\0';
                result._size = newSize;
            } else {
                result._isSSO = true;
                result._capacity = k_SSOThreshold;
                utils::memcpy(result._ssoBuffer, _isSSO ? _ssoBuffer : _heapBuffer, _size);
                utils::memcpy(result._ssoBuffer + _size, other._isSSO ? other._ssoBuffer : other._heapBuffer, other._size);
                result._size = newSize;
                result._ssoBuffer[newSize] = '\0';
            }
            return result;
        }

        char& operator[](const size_t index) {
            return _isSSO ? _ssoBuffer[index] : _heapBuffer[index];
        }

        const char& operator[](const size_t index) const {
            return _isSSO ? _ssoBuffer[index] : _heapBuffer[index];
        }

        StringError at(const size_t index, char& outChar) const {
            if (index >= _size) {
                return StringError::OutOfRange;
            }
            outChar = _isSSO ? _ssoBuffer[index] : _heapBuffer[index];
            return StringError::None;
        }

        bool operator==(const string& other) const {
            if (_size != other._size) {
                return false;
            }
            return memcmp(_isSSO ? _ssoBuffer : _heapBuffer, other._isSSO ? other._ssoBuffer : other._heapBuffer, _size) == 0;
        }

        bool operator!=(const string& other) const {
            return !(*this == other);
        }

        const char* c_str() const {
            return _isSSO ? _ssoBuffer : _heapBuffer;
        }

        size_t size() const {
            return _size;
        }

        size_t capacity() const {
            return _isSSO ? k_SSOThreshold : _capacity;
        }

        void clear() {
            _size = 0;
            if (_isSSO) {
                _ssoBuffer[0] = '\0';
            } else {
                _heapBuffer[0] = '\0';
            }
        }

        bool empty() const {
            return _size == 0;
        }

    public:
        class iterator {
        public:
            explicit iterator(char* p) : _ptr(p) {}
            char& operator*() const { return *_ptr; }
            iterator& operator++() { ++_ptr; return *this; }
            iterator operator++(int32_t) {
                const iterator tmp = *this;
                ++_ptr;
                return tmp;
            }
            bool operator==(const iterator& other) const { return _ptr == other._ptr; }
            bool operator!=(const iterator& other) const { return _ptr != other._ptr; }
        private:
            char* _ptr;
        };

        class const_iterator {
        public:
            explicit const_iterator(const char* p) : _ptr(p) {}
            const char& operator*() const { return *_ptr; }
            const_iterator& operator++() { ++_ptr; return *this; }
            const_iterator operator++(int32_t) {
                const const_iterator tmp = *this;
                ++_ptr;
                return tmp;
            }
            bool operator==(const const_iterator& other) const { return _ptr == other._ptr; }
            bool operator!=(const const_iterator& other) const { return _ptr != other._ptr; }
        private:
            const char* _ptr;
        };

        iterator begin() {
            return iterator((_isSSO)? _ssoBuffer : _heapBuffer);
        }

        iterator end() {
            return iterator((_isSSO) ? (_ssoBuffer + _size) : (_heapBuffer + _size));
        }

        const_iterator begin() const {
            return const_iterator((_isSSO) ? _ssoBuffer : _heapBuffer);
        }

        const_iterator end() const {
            return const_iterator((_isSSO) ? (_ssoBuffer + _size) : (_heapBuffer + _size));
        }
    };
}
