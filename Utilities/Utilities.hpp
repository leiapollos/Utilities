#pragma once
#include <cstdint>
#include <random>
#include <ostream>

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

namespace Utilities
{
    class UUID {
    public:
        UUID() {}

        inline friend std::ostream& operator<<(std::ostream& os, UUID uuid);

    private:
        inline static thread_local std::mt19937 _rng = std::mt19937(std::random_device{}());
        static constexpr bool _includeDashes = false;

        std::string generate() {
            std::string uuid;
            for (int i = 0; i < 4; ++i) {
                const uint32_t random_value = _rng();

                char buffer[9];
                const int ret = snprintf(buffer, sizeof(buffer), "%08x", random_value);
                if (ret < 0 || ret >= sizeof(buffer)) {
                    throw std::runtime_error("snprintf error");
                }
                uuid += buffer;

                if (_includeDashes && i != 3) uuid += '-';
            }
            return uuid;
        }
    };

    std::ostream& operator<<(std::ostream& os, UUID uuid) {
        return os << uuid.generate();
    }
}