//
// Created by André Leite on 10/06/2025.
//

#pragma once

#include "../../typedefs.h"

namespace nstl {
    inline void* memset(void* dest, u8 value, u64 size) {
        u8* dest_ptr = static_cast<u8*>(dest);
        u64 offset = 0;

        while (size > 0 &&
               (reinterpret_cast<uintptr>(dest_ptr + offset) % sizeof(uintptr) != 0)) {
            dest_ptr[offset] = value;
            ++offset;
            --size;
        }

        uintptr word = value;
        for (u64 i = 1; i < sizeof(uintptr); i <<= 1) {
            word |= word << (i * 8);
        }

        uintptr* word_ptr = reinterpret_cast<uintptr*>(dest_ptr + offset);
        u64 word_count = size / sizeof(uintptr);
        for (u64 i = 0; i < word_count; ++i) {
            word_ptr[i] = word;
        }

        offset += word_count * sizeof(uintptr);
        size %= sizeof(uintptr);

        while (size > 0) {
            dest_ptr[offset] = value;
            ++offset;
            --size;
        }

        return dest;
    }
}
