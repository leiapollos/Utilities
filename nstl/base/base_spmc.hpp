//
// Created by André Leite on 02/09/2025.
//

// Work-stealing deque (single-owner bottom, multi-thief top)
// Bounded, power-of-two capacity. Non-blocking try-ops.

#pragma once

struct alignas(CACHE_LINE_SIZE) WSDeque {
    U64 capacity;                           // power of two
    U64 mask;                               // capacity - 1
    U64 elementSize;                        // size in bytes of each slot
    alignas(CACHE_LINE_SIZE) U8* buffer;    // ring buffer of raw bytes
    alignas(CACHE_LINE_SIZE) U64 bottom;    // owner index (only owner mutates)
    alignas(CACHE_LINE_SIZE) U64 top;       // thieves CAS on this
};

static WSDeque* wsdq_create(Arena* arena, U64 capacity, U64 elementSize);
static B32 wsdq_push(WSDeque* dq, const void* value);
static B32 wsdq_pop(WSDeque* dq, void* out_value);
static B32 wsdq_steal(WSDeque* dq, void* out_value);
static S64 wsdq_count_approx(const WSDeque* dq);

