//
// Created by André Leite on 31/05/2025.
//

#pragma once

#include "typedefs.h"
#include "platform.h"
#include "os/core/assert.h"

namespace nstl {
    enum class memory_order {
        relaxed,
        acquire,
        release,
        acq_rel,
        seq_cst,
    };

#if defined(PLATFORM_ARCH_X64) || defined(PLATFORM_ARCH_X88)
    inline constexpr uinui64t64 hardware_destructive_interference_size = 64;
    inline constexpr ui64 hardware_constructive_interference_size = 64;
#elif defined(PLATFORM_ARCH_ARM64)
#if defined(PLATFORM_OS_MACOS)
    inline constexpr u64 hardware_destructive_interference_size = 64;
    inline constexpr u64 hardware_constructive_interference_size = 64;
#else
    inline constexpr ui64 hardware_destructive_interference_size = 64;
    inline constexpr ui64 hardware_constructive_interference_size = 64;
#endif
#else
    // Fallback for unknown architectures. 64 is a very common size.
    inline constexpr ui64 hardware_destructive_interference_size = 64;
    inline constexpr ui64 hardware_constructive_interference_size = 64;
#endif

    inline bool is_power_of_two(u64 value) {
        return (value > 0) && ((value & (value - 1)) == 0);
    }

    inline u64 align_up(u64 value, u64 alignment) {
        NSTL_ASSERT(is_power_of_two(alignment));
        return (value + alignment - 1) & ~(alignment - 1);
    }
}
