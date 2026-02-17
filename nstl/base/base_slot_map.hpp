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

B32 slot_map_init(SlotMap* map, Arena* arena, U32 itemSize, U32 initialCapacity);
B32 slot_map_alloc(SlotMap* map, void** outItem, U32* outSlotIndex, U32* outGeneration);
B32 slot_map_release(SlotMap* map, U32 slotIndex, U32 generation, void** outItem);
void* slot_map_get(SlotMap* map, U32 slotIndex, U32 generation);
void slot_map_clear(SlotMap* map);
B32 slot_map_is_occupied(const SlotMap* map, U32 slotIndex);
void* slot_map_item_at(SlotMap* map, U32 slotIndex);
const void* slot_map_item_at_const(const SlotMap* map, U32 slotIndex);
