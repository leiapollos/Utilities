#include "Allocator.hpp"

LinearAllocator& LinearAllocator::operator=(const LinearAllocator& other) {
    if (this != &other) {
        delete[] _memory;
        _memory = new char[other._size];
        _size = other._size;
        _used = 0;
    }
    return *this;
}

LinearAllocator::~LinearAllocator() {
    delete[] _memory;
}

void LinearAllocator::rewind(void* const mark) noexcept {
    _used = reinterpret_cast<std::uintptr_t>(mark);
}

void LinearAllocator::reset() noexcept {
    _used = 0;
}