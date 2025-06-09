//
// Created by André Leite on 09/06/2025.
//

#pragma once

#include "../os_include.h"
#include "../../math/math.h"
#include "../../memory.h"
#include "assert.h"

namespace nstl {
    class Arena {
    public:
        explicit Arena(const u64 reserveSize) {
            _pageSize = vmem::get_page_size();
            _reserved = nstl::align_up(reserveSize, _pageSize);
            _memory = static_cast<u8*>(vmem::reserve(_reserved));

            if (_memory == nullptr) {
                _reserved = 0;
            }
        }

        ~Arena() {
            if (_memory) {
                vmem::release(_memory, _reserved);
            }
        }

        Arena(const Arena&) = delete;
        Arena& operator=(const Arena&) = delete;
        Arena(Arena&&) = delete;
        Arena& operator=(Arena&&) = delete;

        void* alloc(const u64 size) {
            if (_memory == nullptr || size == 0) {
                return nullptr;
            }

            u64 newPos = _pos + size;

            if (newPos > _reserved) {
                return nullptr;
            }

            if (newPos > _committed) {
                u64 newCommitTarget = nstl::align_up(newPos, _pageSize);
                newCommitTarget = nstl::min(newCommitTarget, _reserved);

                u64 sizeToCommit = newCommitTarget - _committed;
                void* commitStartAddr = _memory + _committed;

                if (!vmem::commit(
                    commitStartAddr, sizeToCommit,
                    vmem::Protection::Read | vmem::Protection::Write
                )) {
                    return nullptr;
                }
                _committed = newCommitTarget;
            }

            void* result = _memory + _pos;
            _pos = newPos;
            return result;
        }

        void set_pos(const u64 newPos) {
            NSTL_ASSERT(newPos <= _pos && "Cannot set position forward.");
            _pos = newPos;
        }

        u64 get_pos() const {
            return _pos;
        }

        u64 get_committed() const {
            return _committed;
        }

        u64 get_reserved() const {
            return _reserved;
        }

        u64 get_page_size() const {
            return _pageSize;
        }

    private:
        u8* _memory = nullptr;
        u64 _pageSize = 0;
        u64 _reserved = 0;
        u64 _committed = 0;
        u64 _pos = 0;
    };

    class ScopedArena {
    public:
        explicit ScopedArena(Arena& arenaRef)
            : _arena(arenaRef), _posAtScopeStart(arenaRef.get_pos()) {
        }

        ~ScopedArena() {
            _arena.set_pos(_posAtScopeStart);
        }

        ScopedArena(const ScopedArena&) = delete;
        ScopedArena& operator=(const ScopedArena&) = delete;
        ScopedArena(ScopedArena&&) = delete;
        ScopedArena& operator=(ScopedArena&&) = delete;

    private:
        Arena& _arena;
        u64 _posAtScopeStart;
    };
}
