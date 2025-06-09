//
// Created by André Leite on 09/06/2025.
//

#pragma once

#include "../core.h"

#include <sys/mman.h>
#include <unistd.h>
#include <cassert>

namespace nstl {
    namespace vmem {
        static int to_posix_protection(Protection prot) {
            int posixProt = PROT_NONE;
            if (static_cast<uint32_t>(prot) & static_cast<uint32_t>(Protection::Read)) {
                posixProt |= PROT_READ;
            }
            if (static_cast<uint32_t>(prot) & static_cast<uint32_t>(Protection::Write)) {
                posixProt |= PROT_WRITE;
            }
            if (static_cast<uint32_t>(prot) &
                static_cast<uint32_t>(Protection::Execute)) {
                posixProt |= PROT_EXEC;
                }
            return posixProt;
        }

        inline u64 get_page_size() {
            return static_cast<u64>(sysconf(_SC_PAGESIZE));
        }

        inline void* reserve(const u64 size) {
            void* addr = mmap(
                nullptr,
                size,
                PROT_NONE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1,
                0
            );

            if (addr == MAP_FAILED) {
                return nullptr;
            }

            return addr;
        }

        inline bool commit(void* addr, const u64 size, const Protection prot) {
            int posixProt = to_posix_protection(prot);
#if defined(PLATFORM_OS_MACOS)
            assert(
                (posixProt & PROT_EXEC) == 0 &&
                "On macOS, PROT_EXEC requires MAP_JIT on reservation."
            );
#endif
            return mprotect(addr, size, posixProt) == 0;
        }

        inline bool decommit(void* addr, const u64 size) {
            if (mprotect(addr, size, PROT_NONE) != 0) {
                return false;
            }
#if defined(PLATFORM_OS_LINUX)
            (void)madvise(addr, size, MADV_DONTNEED);
#endif

            return true;
        }

        inline bool release(void* addr, const u64 size) {
            return munmap(addr, size) == 0;
        }
    }
}