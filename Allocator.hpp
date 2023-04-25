#pragma once
#include <cstddef>
#include <memory>

class LinearAllocator {
public:
    LinearAllocator(std::size_t size) : _memory(new char[size]), _size(size), _used(0) {}
    LinearAllocator(const LinearAllocator& other) : _memory(new char[other._size]), _size(other._size), _used(0) {}
    LinearAllocator& operator=(const LinearAllocator& other);
    ~LinearAllocator();

    template <
        typename T, 
        typename... Args, 
        std::enable_if_t<std::is_invocable_v<T(Args...), Args...>, bool> = true
    >
    [[nodiscard]] inline T* allocate(Args&&... args) {
        return new (allocateHelper<T>()) T(std::forward<Args>(args)...);
    }

    template <typename T>
    void deallocate(T* p, std::size_t n) { /* do nothing */ }

    void rewind(void* const mark) noexcept;

    void reset() noexcept;

private:
    template <typename T>
    [[nodiscard]] inline T* allocateHelper() {
        std::size_t space = sizeof(T);
        std::size_t alignment = alignof(T);
        std::size_t adjustment = (alignment - (reinterpret_cast<std::uintptr_t>(_memory + _used) % alignment)) % alignment;
        if (_used + adjustment + space > _size) {
            throw std::bad_alloc();
        }
        T* result = reinterpret_cast<T*>(_memory + _used + adjustment);
        _used += adjustment + space;
        return result;
    }

    char* _memory;
    std::size_t _size;
    std::size_t _used;
};