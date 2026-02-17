//
// Created by André Leite on 17/02/2026.
//

static B32 slot_map_grow_(SlotMap* map) {
    ASSERT_ALWAYS(map != 0);
    ASSERT_ALWAYS(map->arena != 0);
    ASSERT_ALWAYS(map->itemSize > 0u);

    U32 oldCapacity = map->capacity;
    U32 newCapacity = (oldCapacity == 0u) ? 64u : (oldCapacity * 2u);
    if (newCapacity < 64u) {
        newCapacity = 64u;
    }

    U64 itemBytes = (U64)newCapacity * (U64)map->itemSize;
    U8* newItems = (U8*)arena_push(map->arena, itemBytes, 8);
    U32* newGenerations = ARENA_PUSH_ARRAY(map->arena, U32, newCapacity);
    U32* newFreeNext = ARENA_PUSH_ARRAY(map->arena, U32, newCapacity);
    U8* newOccupied = ARENA_PUSH_ARRAY(map->arena, U8, newCapacity);

    if (!newItems || !newGenerations || !newFreeNext || !newOccupied) {
        return 0;
    }

    MEMSET(newItems, 0, itemBytes);
    MEMSET(newGenerations, 0, sizeof(U32) * newCapacity);
    MEMSET(newFreeNext, 0, sizeof(U32) * newCapacity);
    MEMSET(newOccupied, 0, sizeof(U8) * newCapacity);

    if (oldCapacity > 0u) {
        MEMCPY(newItems, map->items, (U64)oldCapacity * (U64)map->itemSize);
        MEMCPY(newGenerations, map->generations, sizeof(U32) * oldCapacity);
        MEMCPY(newFreeNext, map->freeNext, sizeof(U32) * oldCapacity);
        MEMCPY(newOccupied, map->occupied, sizeof(U8) * oldCapacity);
    }

    U32 oldFreeHead = map->freeHead;
    U32 firstNewIndex = oldCapacity;
    U32 lastNewIndex = (newCapacity > 0u) ? (newCapacity - 1u) : SLOT_MAP_INVALID_INDEX;

    for (U32 i = oldCapacity; i < newCapacity; ++i) {
        U32 next = (i + 1u < newCapacity) ? (i + 1u) : oldFreeHead;
        newFreeNext[i] = next;
    }

    map->items = newItems;
    map->generations = newGenerations;
    map->freeNext = newFreeNext;
    map->occupied = newOccupied;
    map->capacity = newCapacity;
    map->freeHead = (firstNewIndex <= lastNewIndex) ? firstNewIndex : oldFreeHead;

    return 1;
}

B32 slot_map_init(SlotMap* map, Arena* arena, U32 itemSize, U32 initialCapacity) {
    if (!map || !arena || itemSize == 0u) {
        return 0;
    }

    MEMSET(map, 0, sizeof(*map));
    map->arena = arena;
    map->itemSize = itemSize;
    map->freeHead = SLOT_MAP_INVALID_INDEX;

    if (initialCapacity == 0u) {
        initialCapacity = 64u;
    }

    map->capacity = initialCapacity;

    U64 itemBytes = (U64)initialCapacity * (U64)itemSize;
    map->items = (U8*)arena_push(arena, itemBytes, 8);
    map->generations = ARENA_PUSH_ARRAY(arena, U32, initialCapacity);
    map->freeNext = ARENA_PUSH_ARRAY(arena, U32, initialCapacity);
    map->occupied = ARENA_PUSH_ARRAY(arena, U8, initialCapacity);

    if (!map->items || !map->generations || !map->freeNext || !map->occupied) {
        MEMSET(map, 0, sizeof(*map));
        return 0;
    }

    MEMSET(map->items, 0, itemBytes);
    MEMSET(map->generations, 0, sizeof(U32) * initialCapacity);
    MEMSET(map->freeNext, 0, sizeof(U32) * initialCapacity);
    MEMSET(map->occupied, 0, sizeof(U8) * initialCapacity);

    for (U32 i = 0; i < initialCapacity; ++i) {
        map->freeNext[i] = (i + 1u < initialCapacity) ? (i + 1u) : SLOT_MAP_INVALID_INDEX;
    }
    map->freeHead = 0u;

    return 1;
}

B32 slot_map_alloc(SlotMap* map, void** outItem, U32* outSlotIndex, U32* outGeneration) {
    if (!map || !outItem || !outSlotIndex || !outGeneration) {
        return 0;
    }

    if (map->freeHead == SLOT_MAP_INVALID_INDEX) {
        if (!slot_map_grow_(map)) {
            return 0;
        }
    }

    U32 slotIndex = map->freeHead;
    ASSERT_ALWAYS(slotIndex < map->capacity);
    map->freeHead = map->freeNext[slotIndex];

    map->occupied[slotIndex] = 1u;
    if (map->generations[slotIndex] == 0u) {
        map->generations[slotIndex] = 1u;
    }

    map->count += 1u;

    U8* item = map->items + ((U64)slotIndex * (U64)map->itemSize);
    MEMSET(item, 0, map->itemSize);

    *outItem = item;
    *outSlotIndex = slotIndex;
    *outGeneration = map->generations[slotIndex];
    return 1;
}

B32 slot_map_release(SlotMap* map, U32 slotIndex, U32 generation, void** outItem) {
    if (!map || slotIndex >= map->capacity) {
        return 0;
    }
    if (!map->occupied[slotIndex]) {
        return 0;
    }
    if (map->generations[slotIndex] != generation || generation == 0u) {
        return 0;
    }

    U8* item = map->items + ((U64)slotIndex * (U64)map->itemSize);
    if (outItem) {
        *outItem = item;
    }

    map->occupied[slotIndex] = 0u;
    map->freeNext[slotIndex] = map->freeHead;
    map->freeHead = slotIndex;

    U32 nextGeneration = map->generations[slotIndex] + 1u;
    if (nextGeneration == 0u) {
        nextGeneration = 1u;
    }
    map->generations[slotIndex] = nextGeneration;

    if (map->count > 0u) {
        map->count -= 1u;
    }

    return 1;
}

void* slot_map_get(SlotMap* map, U32 slotIndex, U32 generation) {
    if (!map || generation == 0u) {
        return 0;
    }
    if (slotIndex >= map->capacity) {
        return 0;
    }
    if (!map->occupied[slotIndex]) {
        return 0;
    }
    if (map->generations[slotIndex] != generation) {
        return 0;
    }

    return map->items + ((U64)slotIndex * (U64)map->itemSize);
}

void slot_map_clear(SlotMap* map) {
    if (!map || map->capacity == 0u) {
        return;
    }

    MEMSET(map->items, 0, (U64)map->capacity * (U64)map->itemSize);
    MEMSET(map->occupied, 0, sizeof(U8) * map->capacity);

    for (U32 i = 0; i < map->capacity; ++i) {
        U32 nextGeneration = map->generations[i] + 1u;
        if (nextGeneration == 0u) {
            nextGeneration = 1u;
        }
        map->generations[i] = nextGeneration;
        map->freeNext[i] = (i + 1u < map->capacity) ? (i + 1u) : SLOT_MAP_INVALID_INDEX;
    }

    map->count = 0u;
    map->freeHead = 0u;
}

B32 slot_map_is_occupied(const SlotMap* map, U32 slotIndex) {
    if (!map || slotIndex >= map->capacity) {
        return 0;
    }
    return map->occupied[slotIndex] ? 1 : 0;
}

void* slot_map_item_at(SlotMap* map, U32 slotIndex) {
    if (!map || slotIndex >= map->capacity) {
        return 0;
    }
    return map->items + ((U64)slotIndex * (U64)map->itemSize);
}

const void* slot_map_item_at_const(const SlotMap* map, U32 slotIndex) {
    if (!map || slotIndex >= map->capacity) {
        return 0;
    }
    return map->items + ((U64)slotIndex * (U64)map->itemSize);
}
