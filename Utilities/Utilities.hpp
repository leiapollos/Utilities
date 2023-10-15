#pragma once
#include <cstdint>

constexpr unsigned long long operator "" _hash(const char* s, size_t count) {
    constexpr unsigned long long fnvOffsetBasis = 14695981039346656037ULL;
    constexpr unsigned long long fnvPrime = 1099511628211ULL;

    unsigned long long hash = fnvOffsetBasis;
    for (unsigned long long i = 0; i < count; ++i) {
        hash ^= static_cast<unsigned long long>(s[i]);
        hash *= fnvPrime;
    }
    return hash;
}
