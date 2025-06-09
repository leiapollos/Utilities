//
// Created by André Leite on 08/06/2025.
//

#pragma once

#include "../../typedefs.h"

namespace nstl {
    inline bool is_aligned(const void* ptr, const u32 alignment) {
        return reinterpret_cast<uintptr>(ptr) % alignment == 0;
    }

    inline void* memcpy(void* dest, const void* src, u32 count) {
        char* destChar = static_cast<char*>(dest);
        const char* srcChar = static_cast<const char*>(src);

        if (count < 32) {
            for (u32 i = 0; i < count; ++i) {
                destChar[i] = srcChar[i];
            }
            return dest;
        }

        u32 i = 0;
        while (count > 0 && !is_aligned(destChar + i, sizeof(uintptr))) {
            destChar[i] = srcChar[i];
            ++i;
            --count;
        }

        uintptr* destWord = reinterpret_cast<uintptr*>(destChar + i);
        const uintptr* srcWord =
                reinterpret_cast<const uintptr*>(srcChar + i);
        u64 wordCount = count / sizeof(uintptr);

        for (u64 j = 0; j < wordCount; ++j) {
            destWord[j] = srcWord[j];
        }

        i += wordCount * sizeof(uintptr);
        u64 remaining_bytes = count % sizeof(uintptr);
        for (u64 j = 0; j < remaining_bytes; ++j) {
            destChar[i + j] = srcChar[i + j];
        }

        return dest;
    }
}
