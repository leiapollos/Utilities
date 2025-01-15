#pragma once

#if !defined(_MSC_VER)
static_assert(false, "Only MSVC is supported");
#endif

#include <intrin.h>
#include <immintrin.h>

namespace utils {
    static bool is_AVX512() {
        int regs[4] = {};
        __cpuid(regs, 0);
        if (regs[0] < 7) return false;
        __cpuidex(regs, 7, 0);
        return (regs[1] & (1 << 16)) != 0; // AVX512F
    }

    static bool is_AVX2() {
        int regs[4] = {};
        __cpuid(regs, 0);
        if (regs[0] < 7) return false;
        __cpuidex(regs, 7, 0);
        return (regs[1] & (1 << 5)) != 0; // AVX2
    }

    void* memcpy(void* dest, const void* src, unsigned long long size) {
        static bool isInited = false;
        static bool useAvx512 = false;
        static bool useAvx2 = false;

        if (!isInited) {
            useAvx512 = is_AVX512();
            useAvx2 = is_AVX2();
            isInited = true;
        }
        
        char* d = (char*)dest;
        const char* s = (const char*)src;
        
        if (useAvx512) {
            while (size >= 64) {
                __m512i chunk = _mm512_loadu_si512((const __m512i*)s);
                _mm512_storeu_si512((__m512i*)d, chunk);
                s += 64; d += 64; size -= 64;
            }
            while (size >= 32) {
                __m256i chunk = _mm256_loadu_si256((const __m256i*)s);
                _mm256_storeu_si256((__m256i*)d, chunk);
                s += 32; d += 32; size -= 32;
            }
            while (size >= 16) {
                __m128i chunk = _mm_loadu_si128((const __m128i*)s);
                _mm_storeu_si128((__m128i*)d, chunk);
                s += 16; d += 16; size -= 16;
            }
            while (size >= 8) {
                *(unsigned long long*)d = *(const unsigned long long*)s;
                d += 8; s += 8; size -= 8;
            }
            while (size--) {
                *d++ = *s++;
            }
        } else if (useAvx2) {
            while (size >= 32) {
                __m256i chunk = _mm256_loadu_si256((const __m256i*)s);
                _mm256_storeu_si256((__m256i*)d, chunk);
                s += 32; d += 32; size -= 32;
            }
            while (size >= 16) {
                __m128i chunk = _mm_loadu_si128((const __m128i*)s);
                _mm_storeu_si128((__m128i*)d, chunk);
                s += 16; d += 16; size -= 16;
            }
            while (size >= 8) {
                *(unsigned long long*)d = *(const unsigned long long*)s;
                d += 8; s += 8; size -= 8;
            }
            while (size--) {
                *d++ = *s++;
            }
        } else {
            unsigned long long* d64 = (unsigned long long*)dest;
            const unsigned long long* s64 = (const unsigned long long*)src;
            while (size >= 8) {
                *d64++ = *s64++;
                size -= 8;
            }
            char* d8 = (char*)d64;
            const char* s8 = (const char*)s64;
            while (size--) {
                *d8++ = *s8++;
            }
        }

        return dest;
    }

    int memcmp(const void* lhs, const void* rhs, unsigned long long size) {
        const unsigned char* l = (const unsigned char*)lhs;
        const unsigned char* r = (const unsigned char*)rhs;
        while (size >= 8) {
            unsigned long long ll = *(const unsigned long long*)l;
            unsigned long long rr = *(const unsigned long long*)r;
            if (ll != rr) {
                for (int i = 0; i < 8; i++) {
                    if (l[i] != r[i]) {
                        return (l[i] < r[i]) ? -1 : 1;
                    }
                }
            }
            l += 8;
            r += 8;
            size -= 8;
        }
        while (size--) {
            if (*l != *r) {
                return (*l < *r) ? -1 : 1;
            }
            l++;
            r++;
        }
        return 0;
    }
}
