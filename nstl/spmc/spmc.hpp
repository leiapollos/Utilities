//
// Created by André Leite on 02/09/2025.
//

// Work-stealing deque (single-owner bottom, multi-thief top)
// Bounded, power-of-two capacity. Non-blocking try-ops.

#pragma once

struct alignas(CACHE_LINE_SIZE) WSDeque {
    S64 capacity;                           // power of two
    S64 mask;                               // capacity - 1
    alignas(CACHE_LINE_SIZE) void** buffer; // ring buffer of values
    alignas(CACHE_LINE_SIZE) S64 bottom;    // owner index (only owner mutates)
    alignas(CACHE_LINE_SIZE) S64 top;       // thieves CAS on this
};

static WSDeque* wsdq_create(Arena* arena, S64 capacity);
static B32 wsdq_push(WSDeque* dq, void* value);
static B32 wsdq_pop(WSDeque* dq, void** out_value);
static B32 wsdq_steal(WSDeque* dq, void** out_value);
static S64 wsdq_count_approx(const WSDeque* dq);
