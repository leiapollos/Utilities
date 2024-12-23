#pragma once

namespace utils {
    void* memcpy(void* dest, const void* src, unsigned long long size) {
    #if defined(_MSC_VER)
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
    #else
        char* d = (char*)dest;
        const char* s = (const char*)src;
        while (size >= 32) {
            __asm__ __volatile__ (
                "movdqu  (%0), %%xmm0\n\t"
                "movdqu 16(%0), %%xmm1\n\t"
                "movdqu  %%xmm0, (%1)\n\t"
                "movdqu  %%xmm1, 16(%1)\n\t"
                :
                : "r"(s), "r"(d)
                : "memory", "xmm0", "xmm1"
            );
            s += 32;
            d += 32;
            size -= 32;
        }
        while (size--) {
            *d++ = *s++;
        }
    #endif
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
