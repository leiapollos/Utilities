//
// Created by André Leite on 10/06/2025.
//

#pragma once

#include "arenaAllocator.h"
#include "memset.h"
#include "assert.h"

namespace nstl {
    template<typename T>
    class PoolAllocator {
    private:
        // Memory gets reinterpret_cast to this to store the next pointer in the free list
        // This saves memory so we don't have to store an extra pointer in each struct
        // The only requirement is that an object we store in the pool needs to have sizeof(T) >= sizeof(FreeNode*)
        struct FreeNode {
            FreeNode* next;
        };

    public:
        explicit PoolAllocator(Arena& arena) : _arena(arena), _freeListHead(nullptr) {
            static_assert(
                sizeof(T) >= sizeof(FreeNode*),
                "Type T must be at least the size of a pointer."
            );
        }

        PoolAllocator(const PoolAllocator&) = delete;
        PoolAllocator& operator=(const PoolAllocator&) = delete;
        PoolAllocator(PoolAllocator&&) = delete;
        PoolAllocator& operator=(PoolAllocator&&) = delete;

        T* alloc() {
            T* result = nullptr;

            if (_freeListHead != nullptr) {
                result = reinterpret_cast<T*>(_freeListHead);
                _freeListHead = _freeListHead->next;
            } else {
                result = static_cast<T*>(_arena.alloc(sizeof(T)));
            }

            if (result != nullptr) {
                nstl::memset(result, 0, sizeof(T));
            } else {
                NSTL_ASSERT(result != nullptr && "result should not be nullptr here, allocation failed!");
            }

            return result;
        }

        void release(T* obj) {
            if (obj == nullptr) {
                NSTL_ASSERT(obj != nullptr && "don't try to release a nullptr!");
                return;
            }

            FreeNode* node = reinterpret_cast<FreeNode*>(obj);
            node->next = _freeListHead;
            _freeListHead = node;
        }

    private:
        Arena& _arena;
        FreeNode* _freeListHead;
    };
}
