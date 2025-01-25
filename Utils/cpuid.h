#pragma once

#include "typedefs.hpp"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace utils {
    template<unsigned leaf, unsigned subleaf>
    void cpuid(int* data) {
        __cpuidex(data, leaf, subleaf);
    }
    template<> inline void cpuid<0,0>(int* data) { __cpuid(data, 0); }
    
    struct cpu_id {
        struct x86 {
            bool invariantCounter;
            bool avx, avx2, avx512f, sse2, sse4_1, aes;
        };
            
        struct arm {
                
        };
        
        enum class architecture : uint8_t {
            x86_arch,
            arm_arch,
            unknown
        } arch;
        
        union {
            x86 x86_info;
            arm arm_info;
        } isa;

        static cpu_id info() {
            static cpu_id info;
            return info;
        }
        
        cpu_id() {
#if defined(_MSC_VER)
            arch = architecture::x86_arch;
            
            int data[4];

            // Invariant TSC
            cpuid<0x80000007,0>(data);
            isa.x86_info.invariantCounter = (data[3] & (1 << 8)) != 0;

            // Features
            cpuid<1,0>(data);
            isa.x86_info.sse2   = (data[3] & (1 << 26)) != 0;
            isa.x86_info.sse4_1 = (data[2] & (1 << 19)) != 0;
            isa.x86_info.avx    = (data[2] & (1 << 28)) != 0;
            isa.x86_info.aes    = (data[2] & (1 << 25)) != 0;

            cpuid<7,0>(data);
            isa.x86_info.avx2    = (data[1] & (1 << 5)) != 0;
            isa.x86_info.avx512f = (data[1] & (1 << 16)) != 0;
#elif defined(__arm__)
            arch = architecture::arm_arch;
#else
            arch = architecture::unknown;
#endif
        }
    };
}
