#pragma once

#include "utils_helpers.h"

#if defined(_MSC_VER)
#include <immintrin.h>
#endif


// Fallback
template <typename T>
UTILS_ALWAYS_INLINE void simd_copy_fallback(const T* src, T* dst, unsigned long long count) {
    for (unsigned long long i = 0; i < count; ++i) {
        new (reinterpret_cast<void*>(dst + i)) T(src[i]);
    }
}

#if defined(__GNUC__) || defined(__clang__)
#if defined(__AVX2__) || defined(__SSE2__) || defined(__ARM_NEON__)
template <typename T>
UTILS_ALWAYS_INLINE void simd_copy(const T* src, T* dst, unsigned long long count) {
    unsigned long long bytes = count * sizeof(T);
    unsigned char* s = reinterpret_cast<unsigned char*>(const_cast<T*>(src));
    unsigned char* d = reinterpret_cast<unsigned char*>(dst);
#if defined(__AVX2__)
    while (bytes >= 32ULL) {
        __asm__ __volatile__(
            "vmovdqu (%0), %%ymm0\n\t"
            "vmovdqu %%ymm0, (%1)\n\t"
            :
        : "r"(s), "r"(d)
            : "memory", "%ymm0"
            );
        s += 32ULL; d += 32ULL; bytes -= 32ULL;
    }
#elif defined(__SSE2__)
    while (bytes >= 16ULL) {
        __asm__ __volatile__(
            "movups (%0), %%xmm0\n\t"
            "movups %%xmm0, (%1)\n\t"
            :
        : "r"(s), "r"(d)
            : "memory", "%xmm0"
            );
        s += 16ULL; d += 16ULL; bytes -= 16ULL;
    }
#elif defined(__ARM_NEON__)
    // TODO: ARM
#endif
    while (bytes > 0ULL) { *d++ = *s++; --bytes; }
}
#else
template <typename T>
UTILS_ALWAYS_INLINE void simd_copy(const T* src, T* dst, unsigned long long count) {
    simd_copy_fallback(src, dst, count);
}
#endif
#elif defined(_MSC_VER)
#if defined(_M_X64) || (defined(_M_IX86_FP) && (_M_IX86_FP >= 2))
#if defined(__AVX2__) || defined(_M_AVX2)
extern "C" __m256i __cdecl _mm256_loadu_si256(const __m256i * p);
extern "C" void __cdecl _mm256_storeu_si256(__m256i * p, __m256i a);
template <typename T>
UTILS_ALWAYS_INLINE void simd_copy(const T* src, T* dst, unsigned long long count) {
    unsigned long long bytes = count * sizeof(T);
    unsigned char* s = reinterpret_cast<unsigned char*>(const_cast<T*>(src));
    unsigned char* d = reinterpret_cast<unsigned char*>(dst);
    while (bytes >= 32ULL) {
        __m256i reg = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), reg);
        s += 32ULL; d += 32ULL; bytes -= 32ULL;
    }
    while (bytes > 0ULL) { *d++ = *s++; --bytes; }
}
#else
extern "C" __m128i __cdecl _mm_loadu_si128(const __m128i * p);
extern "C" void __cdecl _mm_storeu_si128(__m128i * p, __m128i a);
template <typename T>
UTILS_ALWAYS_INLINE void simd_copy(const T* src, T* dst, unsigned long long count) {
    unsigned long long bytes = count * sizeof(T);
    unsigned char* s = reinterpret_cast<unsigned char*>(const_cast<T*>(src));
    unsigned char* d = reinterpret_cast<unsigned char*>(dst);
    while (bytes >= 16ULL) {
        __m128i reg = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), reg);
        s += 16ULL; d += 16ULL; bytes -= 16ULL;
    }
    while (bytes > 0ULL) { *d++ = *s++; --bytes; }
}
#endif
#else
template <typename T>
UTILS_ALWAYS_INLINE void simd_copy(const T* src, T* dst, unsigned long long count) {
    simd_copy_fallback(src, dst, count);
}
#endif
#else
template <typename T>
UTILS_ALWAYS_INLINE void simd_copy(const T* src, T* dst, unsigned long long count) {
    simd_copy_fallback(src, dst, count);
}
#endif

template <typename T>
UTILS_ALWAYS_INLINE void bulk_copy(const T* src, T* dst, unsigned long long count) {
    simd_copy(src, dst, count);
}
