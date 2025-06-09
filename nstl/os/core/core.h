//
// Created by André Leite on 09/06/2025.
//

#pragma once

#include "../../typedefs.h"

namespace nstl {
    namespace vmem {
        enum class Protection : u8 {
            None = 0,
            Read = 1 << 1,
            Write = 1 << 2,
            Execute = 1 << 3,
        };

        inline Protection operator|(Protection a, Protection b) {
            return static_cast<Protection>(
                static_cast<u8>(a) | static_cast<u8>(b)
            );
        }

        u64 get_page_size();
        void* reserve(const u64 size);
        bool commit(void* addr, const u64 size, const Protection prot);
        bool decommit(void* addr, const u64 size);
        bool release(void* addr, const u64 size);
    }
}