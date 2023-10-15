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

    class CRC32 {
    public:
        static uint32_t calculate(const std::string& data) {
            uint32_t crc = ~0;
            for (char c : data) {
                crc = (crc >> 8) ^ _table[(crc ^ c) & 0xFF];
            }
            return ~crc;
        }

    private:
        static constexpr uint32_t _polynomial = 0xEDB88320;
        inline static thread_local uint32_t _table[256];

        struct TableInitializer {
            TableInitializer() {
                for (uint32_t i = 0; i < 256; i++) {
                    uint32_t crc = i;
                    for (uint8_t j = 0; j < 8; j++) {
                        crc = (crc >> 1) ^ (-int(crc & 1) & _polynomial);
                    }
                    _table[i] = crc;
                }
            }
        };

        static TableInitializer _initializer;
    };
}