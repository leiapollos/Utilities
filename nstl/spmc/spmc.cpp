//
// Created by André Leite on 02/09/2025.
//

// Bounded work-stealing deque implementation (Chase-Lev).
// References: Chase & Lev (SPAA'05).

static
WSDeque* wsdq_create(Arena* arena, S64 capacity) {
    ASSERT_DEBUG(capacity != 0 && is_power_of_two(capacity));

    WSDeque* dq = (WSDeque*)arena_push(arena, sizeof(WSDeque), CACHE_LINE_SIZE);
    ASSERT_DEBUG(dq);

    dq->capacity = capacity;
    dq->mask = capacity - 1u;
    dq->buffer = (void**)arena_push(arena, capacity * (U64)sizeof(void*), CACHE_LINE_SIZE);
    ASSERT_DEBUG(dq->buffer);

    for (S64 i = 0; i < capacity; ++i) {
        dq->buffer[i] = nullptr;
    }

    ATOMIC_STORE(&dq->bottom, 0, MEMORY_ORDER_RELAXED);
    ATOMIC_STORE(&dq->top, 0, MEMORY_ORDER_RELAXED);

    return dq;
}

static
B32 wsdq_push(WSDeque* dq, void* value) {
    ASSERT_DEBUG(dq);
    ASSERT_DEBUG(value != nullptr);

    S64 b = ATOMIC_LOAD(&dq->bottom, MEMORY_ORDER_RELAXED);
    S64 t = ATOMIC_LOAD(&dq->top, MEMORY_ORDER_ACQUIRE);
    void** buffer = ATOMIC_LOAD(&dq->buffer, MEMORY_ORDER_RELAXED);
    if (b - t > dq->capacity - 1) {
        return 0;
    }
    buffer[b & dq->mask] = value;
    ATOMIC_THREAD_FENCE(MEMORY_ORDER_RELEASE);
    ATOMIC_STORE(&dq->bottom, b + 1, MEMORY_ORDER_RELAXED);
    return 1;
}

static
B32 wsdq_pop(WSDeque* dq, void** out_value) {
    ASSERT_DEBUG(dq);
    ASSERT_DEBUG(out_value);

    S64 b = ATOMIC_LOAD(&dq->bottom, MEMORY_ORDER_RELAXED) - 1;
    void** buffer = ATOMIC_LOAD(&dq->buffer, MEMORY_ORDER_RELAXED);
    
    ATOMIC_STORE(&dq->bottom, b, MEMORY_ORDER_RELAXED);
    
    ATOMIC_THREAD_FENCE(MEMORY_ORDER_SEQ_CST);
    S64 t = ATOMIC_LOAD(&dq->top, MEMORY_ORDER_RELAXED);
    
    if (t <= b) {
        if (t == b) {
            S64 expected = t;
            if (!ATOMIC_COMPARE_EXCHANGE(&dq->top, &expected, t + 1, false, MEMORY_ORDER_SEQ_CST, MEMORY_ORDER_RELAXED)) {
                *out_value = nullptr;
                ATOMIC_STORE(&dq->bottom, b + 1, MEMORY_ORDER_RELAXED);
                return 0;
            }
            ATOMIC_STORE(&dq->bottom, b + 1, MEMORY_ORDER_RELAXED);
        }
        
        *out_value = buffer[b & dq->mask];
        return 1;
    } else {
        ATOMIC_STORE(&dq->bottom, b + 1, MEMORY_ORDER_RELAXED);
        *out_value = nullptr;
        return 0;
    }
}

static
B32 wsdq_steal(WSDeque* dq, void** out_value) {
    ASSERT_DEBUG(dq);
    ASSERT_DEBUG(out_value);

    S64 t = ATOMIC_LOAD(&dq->top, MEMORY_ORDER_ACQUIRE);
    ATOMIC_THREAD_FENCE(MEMORY_ORDER_SEQ_CST);
    S64 b = ATOMIC_LOAD(&dq->bottom, MEMORY_ORDER_ACQUIRE);
    if (t < b) {
        *out_value = dq->buffer[t & dq->mask];
        S64 expected = t;
        if (ATOMIC_COMPARE_EXCHANGE(&dq->top, &expected, t + 1, false, MEMORY_ORDER_SEQ_CST, MEMORY_ORDER_RELAXED)) {
            return 1;
        }
    }
    *out_value = nullptr;
    return 0;
}

static
S64 wsdq_count_approx(const WSDeque* dq) {
    S64 b = ATOMIC_LOAD(&dq->bottom, MEMORY_ORDER_ACQUIRE);
    S64 t = ATOMIC_LOAD(&dq->top, MEMORY_ORDER_ACQUIRE);
    return (b >= t) ? (b - t) : 0;
}
