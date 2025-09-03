//
// Created by André Leite on 02/09/2025.
//

// Bounded work-stealing deque implementation (Chase-Lev).
// References: Chase & Lev (SPAA'05).

static
WSDeque* wsdq_create(Arena* arena, U64 capacity) {
    ASSERT_DEBUG(capacity != 0 && is_power_of_two(capacity));

    WSDeque* dq = (WSDeque*)arena_push(arena, sizeof(WSDeque), CACHE_LINE_SIZE);
    ASSERT_DEBUG(dq);

    dq->capacity = capacity;
    dq->mask = capacity - 1u;
    dq->buffer = (void**)arena_push(arena, capacity * sizeof(uintptr), CACHE_LINE_SIZE);
    ASSERT_DEBUG(dq->buffer);

    for (U64 i = 0; i < capacity; ++i) {
        dq->buffer[i] = nullptr;
    }

    ATOMIC_STORE(&dq->bottom, 0u, MEMORY_ORDER_RELAXED);
    ATOMIC_STORE(&dq->top, 0u, MEMORY_ORDER_RELAXED);

    return dq;
}

static
B32 wsdq_push(WSDeque* dq, void* value) {
    ASSERT_DEBUG(dq);
    ASSERT_DEBUG(value != nullptr);

    U64 b = ATOMIC_LOAD(&dq->bottom, MEMORY_ORDER_RELAXED);
    U64 t = ATOMIC_LOAD(&dq->top, MEMORY_ORDER_ACQUIRE);
    void** buffer = dq->buffer;

    if ((b - t) >= dq->capacity) {
        return 0;
    }

    buffer[b & dq->mask] = value;
    ATOMIC_STORE(&dq->bottom, b + 1u, MEMORY_ORDER_RELEASE);
    return 1;
}

static
B32 wsdq_pop(WSDeque* dq, void** out_value) {
    ASSERT_DEBUG(dq);
    ASSERT_DEBUG(out_value);

    void** buffer = dq->buffer;

    U64 b_cur = ATOMIC_LOAD(&dq->bottom, MEMORY_ORDER_RELAXED);
    U64 t = ATOMIC_LOAD(&dq->top, MEMORY_ORDER_ACQUIRE);
    if (t >= b_cur) {
        *out_value = nullptr;
        return 0;
    }

    U64 b = b_cur - 1u;
    ATOMIC_STORE(&dq->bottom, b, MEMORY_ORDER_RELAXED);
    ATOMIC_THREAD_FENCE(MEMORY_ORDER_SEQ_CST);
    t = ATOMIC_LOAD(&dq->top, MEMORY_ORDER_RELAXED);

    if (t <= b) {
        void* x = buffer[b & dq->mask];

        if (t == b) {
            U64 expected = t;
            if (!ATOMIC_COMPARE_EXCHANGE(&dq->top,
                                         &expected,
                                         t + 1u,
                                         false,
                                         MEMORY_ORDER_SEQ_CST,
                                         MEMORY_ORDER_RELAXED)) {
                *out_value = nullptr;
                ATOMIC_STORE(&dq->bottom, b + 1u, MEMORY_ORDER_RELAXED);
                return 0;
            }
            ATOMIC_STORE(&dq->bottom, b + 1u, MEMORY_ORDER_RELAXED);
        }

        *out_value = x;
        return 1;
    }

    ATOMIC_STORE(&dq->bottom, b + 1u, MEMORY_ORDER_RELAXED);
    *out_value = nullptr;
    return 0;
}

static
B32 wsdq_steal(WSDeque* dq, void** out_value) {
    ASSERT_DEBUG(dq);
    ASSERT_DEBUG(out_value);

    U64 t = ATOMIC_LOAD(&dq->top, MEMORY_ORDER_ACQUIRE);
    U64 b = ATOMIC_LOAD(&dq->bottom, MEMORY_ORDER_ACQUIRE);

    if (t < b) {
        void* x = dq->buffer[t & dq->mask];
        U64 expected = t;
        if (ATOMIC_COMPARE_EXCHANGE(&dq->top,
                                    &expected,
                                    t + 1u,
                                    false,
                                    MEMORY_ORDER_SEQ_CST,
                                    MEMORY_ORDER_RELAXED)) {
            *out_value = x;
            return 1;
        }
    }

    *out_value = nullptr;
    return 0;
}

static
S64 wsdq_count_approx(const WSDeque* dq) {
    U64 b = ATOMIC_LOAD(&dq->bottom, MEMORY_ORDER_RELAXED);
    U64 t = ATOMIC_LOAD(&dq->top, MEMORY_ORDER_RELAXED);
    U64 cnt = (b >= t) ? (b - t) : 0u;
    return (S64)cnt;
}