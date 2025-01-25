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
        union {
            struct {
                uint8_t sso_size;  // Bit 7: SSO flag (0), Bits 0-6: size
                char sso_buffer[23]; // 22 chars + null
            };
            struct {
                size_t heap_size;  // Bit 63: Heap flag (1), Bits 0-62: size
                size_t heap_capacity;
                char* heap_buffer;
            };
        };

        static constexpr size_t k_SSOThreshold = 22;
        static constexpr uint8_t SSO_FLAG_MASK = 0x80;
        static constexpr uint8_t SSO_SIZE_MASK = ~SSO_FLAG_MASK;
        static constexpr size_t HEAP_FLAG_MASK = 1ULL << 63;
        static constexpr size_t HEAP_SIZE_MASK = ~HEAP_FLAG_MASK;

        UTILS_ALWAYS_INLINE bool is_SSO() const { 
            return (sso_size & SSO_FLAG_MASK) == 0;
        }

        UTILS_ALWAYS_INLINE size_t get_size() const {
            return is_SSO() ? (sso_size & SSO_SIZE_MASK)
                           : (heap_size & HEAP_SIZE_MASK);
        }

        UTILS_ALWAYS_INLINE void set_size_SSO(size_t s) {
            sso_size = static_cast<uint8_t>(s & SSO_SIZE_MASK);
        }

        UTILS_ALWAYS_INLINE void set_size_heap(size_t s) {
            heap_size = (s & HEAP_SIZE_MASK) | HEAP_FLAG_MASK;
        }

        StringError expand_capacity(size_t newCapacity) {
            if (is_SSO()) {
                const size_t current_size = get_size();
                char* buffer = new char[newCapacity + 1];
                if (!buffer) return StringError::MemoryAllocationFailed;

                utils::memcpy(buffer, sso_buffer, current_size);
                buffer[current_size] = '\0';
                
                set_size_heap(current_size);
                heap_capacity = newCapacity;
                heap_buffer = buffer;
            } else {
                const size_t current_size = get_size();
                char* buffer = new char[newCapacity + 1];
                if (!buffer) return StringError::MemoryAllocationFailed;

                utils::memcpy(buffer, heap_buffer, current_size);
                buffer[current_size] = '\0';
                
                delete[] heap_buffer;
                heap_buffer = buffer;
                heap_capacity = newCapacity;
            }
            return StringError::None;
        }

        StringError ensure_capacity(size_t minCapacity) {
            if (is_SSO()) {
                if (minCapacity <= k_SSOThreshold) return StringError::None;
                size_t newCap = k_SSOThreshold * 2;
                while (newCap < minCapacity) newCap *= 2;
                return expand_capacity(newCap);
            } else {
                if (minCapacity <= heap_capacity) return StringError::None;
                size_t newCap = heap_capacity * 2;
                while (newCap < minCapacity) newCap *= 2;
                return expand_capacity(newCap);
            }
        }

        void convert_to_SSO_if_possible(size_t new_size) {
            if (!is_SSO() && new_size <= k_SSOThreshold) {
                char* old_buffer = heap_buffer;
                utils::memcpy(sso_buffer, old_buffer, new_size);
                sso_buffer[new_size] = '\0';
                delete[] old_buffer;
                set_size_SSO(new_size);
            }
        }

        void set_empty_string() {
            if (!is_SSO()) delete[] heap_buffer;
            set_size_SSO(0);
            sso_buffer[0] = '\0';
        }

    public:
        string() {
            set_size_SSO(0);
            sso_buffer[0] = '\0';
        }

        string(const char* cstr) {
            if (!cstr) {
                set_empty_string();
                return;
            }

            size_t len = 0;
            while (cstr[len] != '\0') ++len;

            if (len > k_SSOThreshold) {
                char* buffer = new char[len + 1];
                if (!buffer) {
                    UTILS_DEBUG_ASSERT(false);
                    set_empty_string();
                    return;
                }
                utils::memcpy(buffer, cstr, len);
                buffer[len] = '\0';
                set_size_heap(len);
                heap_capacity = len;
                heap_buffer = buffer;
            } else {
                set_size_SSO(len);
                utils::memcpy(sso_buffer, cstr, len);
                sso_buffer[len] = '\0';
            }
        }

        string(const string& other) {
            if (other.is_SSO()) {
                sso_size = other.sso_size;
                utils::memcpy(sso_buffer, other.sso_buffer, k_SSOThreshold + 1);
            } else {
                const size_t other_size = other.get_size();
                char* buffer = new char[other.heap_capacity + 1];
                if (!buffer) {
                    UTILS_DEBUG_ASSERT(false);
                    set_empty_string();
                    return;
                }
                utils::memcpy(buffer, other.heap_buffer, other_size + 1);
                set_size_heap(other_size);
                heap_capacity = other.heap_capacity;
                heap_buffer = buffer;
            }
        }

        string(string&& other) noexcept {
            if (other.is_SSO()) {
                sso_size = other.sso_size;
                utils::memcpy(sso_buffer, other.sso_buffer, k_SSOThreshold + 1);
            } else {
                heap_size = other.heap_size;
                heap_capacity = other.heap_capacity;
                heap_buffer = other.heap_buffer;
                other.set_size_SSO(0);
                other.sso_buffer[0] = '\0';
            }
        }

        ~string() {
            if (!is_SSO()) {
                delete[] heap_buffer;
            }
        }

        string& operator=(const string& other) {
            if (this == &other) return *this;

            if (other.is_SSO()) {
                if (!is_SSO()) delete[] heap_buffer;
                sso_size = other.sso_size;
                utils::memcpy(sso_buffer, other.sso_buffer, k_SSOThreshold + 1);
            } else {
                const size_t other_size = other.get_size();
                StringError err = ensure_capacity(other_size);
                if (err != StringError::None) {
                    UTILS_DEBUG_ASSERT(false);
                    return *this;
                }
                utils::memcpy(get_buffer(), other.get_buffer(), other_size);
                if (is_SSO()) set_size_heap(other_size);
                else set_size_heap(other_size);
                get_buffer()[other_size] = '\0';
                convert_to_SSO_if_possible(other_size);
            }
            return *this;
        }

        string& operator=(string&& other) noexcept {
            if (this == &other) return *this;
            
            if (!is_SSO()) delete[] heap_buffer;
            
            if (other.is_SSO()) {
                sso_size = other.sso_size;
                utils::memcpy(sso_buffer, other.sso_buffer, k_SSOThreshold + 1);
            } else {
                heap_size = other.heap_size;
                heap_capacity = other.heap_capacity;
                heap_buffer = other.heap_buffer;
            }
            
            other.set_size_SSO(0);
            other.sso_buffer[0] = '\0';
            return *this;
        }

        string operator+=(const string& other) {
            const size_t other_size = other.get_size();
            const size_t new_size = get_size() + other_size;

            if (new_size > capacity()) {
                StringError err = ensure_capacity(new_size);
                if (err != StringError::None) {
                    UTILS_DEBUG_ASSERT(false);
                    set_empty_string();
                    return *this;
                }
            }

            char* buf = get_buffer();
            utils::memcpy(buf + get_size(), other.c_str(), other_size);
            is_SSO()? set_size_SSO(new_size) : set_size_heap(new_size);
            buf[new_size] = '\0';
            convert_to_SSO_if_possible(new_size);
            return *this;
        }

        string operator+=(const char* other) {
            string temp = string(other);
            return *this += temp;
        }

        string operator+=(const char& other) {
            const size_t new_size = get_size() + 1;

            if (new_size > capacity()) {
                StringError err = ensure_capacity(new_size);
                if (err != StringError::None) {
                    UTILS_DEBUG_ASSERT(false);
                    set_empty_string();
                    return *this;
                }
            }

            char* buf = get_buffer();
            buf[get_size()] = other;
            is_SSO()? set_size_SSO(new_size) : set_size_heap(new_size);
            buf[new_size] = '\0';
            convert_to_SSO_if_possible(new_size);
            return *this;
        }
        
        string operator+(const string& other) {
            const size_t other_size = other.get_size();
            const size_t new_size = get_size() + other_size;

            if (new_size > capacity()) {
                StringError err = ensure_capacity(new_size);
                if (err != StringError::None) {
                    UTILS_DEBUG_ASSERT(false);
                    set_empty_string();
                    return *this;
                }
            }

            char* buf = get_buffer();
            utils::memcpy(buf + get_size(), other.c_str(), other_size);
            is_SSO()? set_size_SSO(new_size) : set_size_heap(new_size);
            buf[new_size] = '\0';
            convert_to_SSO_if_possible(new_size);
            return *this;
        }

        string operator+(const string& other) const {
            string result;
            const size_t other_size = other.get_size();
            const size_t new_size = result.get_size() + other_size;

            if (new_size > result.capacity()) {
                StringError err = result.ensure_capacity(new_size);
                if (err != StringError::None) {
                    UTILS_DEBUG_ASSERT(false);
                    result.set_empty_string();
                    return result;
                }
            }

            char* buf = result.get_buffer();
            utils::memcpy(buf + result.get_size(), other.c_str(), other_size);
            result.is_SSO()? result.set_size_SSO(new_size) : result.set_size_heap(new_size);
            buf[new_size] = '\0';
            result.convert_to_SSO_if_possible(new_size);
            return result;
        }

        char& operator[](size_t index) { 
            return is_SSO()? sso_buffer[index] : heap_buffer[index];
        }

        const char& operator[](size_t index) const { 
            return is_SSO()? sso_buffer[index] : heap_buffer[index];
        }

        StringError at(size_t index, char& outChar) const {
            if (index >= get_size()) return StringError::OutOfRange;
            outChar = (*this)[index];
            return StringError::None;
        }

        const char* c_str() const { 
            return is_SSO()? sso_buffer : heap_buffer;
        }

        size_t size() const { return get_size(); }
        size_t capacity() const { return is_SSO()? k_SSOThreshold : heap_capacity; }

        void clear() {
            if (is_SSO()) {
                set_size_SSO(0);
                sso_buffer[0] = '\0';
            } else {
                set_size_heap(0);
                heap_buffer[0] = '\0';
                convert_to_SSO_if_possible(0);
            }
        }

        UTILS_ALWAYS_INLINE bool empty() const { return get_size() == 0; }

        class iterator {
            char* ptr;
        public:
            explicit iterator(char* p) : ptr(p) {}
            char& operator*() { return *ptr; }
            iterator& operator++() { ++ptr; return *this; }
            iterator operator++(int) { iterator tmp = *this; ++ptr; return tmp; }
            bool operator==(const iterator& other) const { return ptr == other.ptr; }
            bool operator!=(const iterator& other) const { return ptr != other.ptr; }
        };

        class const_iterator {
            const char* ptr;
        public:
            explicit const_iterator(const char* p) : ptr(p) {}
            const char& operator*() { return *ptr; }
            const_iterator& operator++() { ++ptr; return *this; }
            const_iterator operator++(int) { const_iterator tmp = *this; ++ptr; return tmp; }
            bool operator==(const const_iterator& other) const { return ptr == other.ptr; }
            bool operator!=(const const_iterator& other) const { return ptr != other.ptr; }
        };

        iterator begin() { return iterator(is_SSO()? sso_buffer : heap_buffer); }
        iterator end() { return iterator((is_SSO()? sso_buffer : heap_buffer) + get_size()); }
        const_iterator begin() const { return const_iterator(c_str()); }
        const_iterator end() const { return const_iterator(c_str() + get_size()); }

    private:
        char* get_buffer() { return is_SSO()? sso_buffer : heap_buffer; }
        const char* get_buffer() const { return is_SSO()? sso_buffer : heap_buffer; }
    };
} // namespace utils