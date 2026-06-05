//
// Created by André Leite on 17/02/2026.
//

#pragma once

static const U32 SLOT_MAP_INVALID_INDEX = 0xFFFFFFFFu;

struct SlotMap {
    Arena* arena;
    U8* items;
    U32 itemSize;
    U32 capacity;
    U32 count;
    U32* generations;
    U32* freeNext;
    U8* occupied;
    U32 freeHead;
};

UTILITIES_SHARED_API B32 slot_map_init(SlotMap* map, Arena* arena, U32 itemSize, U32 initialCapacity);
UTILITIES_SHARED_API B32 slot_map_alloc(SlotMap* map, void** outItem, U32* outSlotIndex, U32* outGeneration);
UTILITIES_SHARED_API B32 slot_map_release(SlotMap* map, U32 slotIndex, U32 generation, void** outItem);
UTILITIES_SHARED_API void* slot_map_get(SlotMap* map, U32 slotIndex, U32 generation);
UTILITIES_SHARED_API void slot_map_clear(SlotMap* map);
UTILITIES_SHARED_API B32 slot_map_is_occupied(const SlotMap* map, U32 slotIndex);
UTILITIES_SHARED_API void* slot_map_item_at(SlotMap* map, U32 slotIndex);
UTILITIES_SHARED_API const void* slot_map_item_at_const(const SlotMap* map, U32 slotIndex);
