#define CONTENT_DEFAULT_BLOB_CAPACITY 64u
#define CONTENT_DEFAULT_KEY_CAPACITY 64u
#define CONTENT_KEY_HASH_HISTORY_COUNT 64u
#define CONTENT_KEY_HASH_STRONG_REF_COUNT 2u
#define CONTENT_TABLE_MAX_LOAD_PERCENT 70u

enum ContentTableEntryState {
    ContentTableEntryState_Empty = 0,
    ContentTableEntryState_Occupied,
    ContentTableEntryState_Tombstone,
};

struct ContentBlobNode {
    ContentHash hash;
    U8* data;
    U64 size;
    U64 reservedSize;
    U64 committedSize;
    U64 lastTouchFrame;
    U64 keyRefCount;
    U64 downstreamRefCount;
    StringU8 debugName;
};

struct ContentKeyNode {
    ContentKey key;
    ContentHash history[CONTENT_KEY_HASH_HISTORY_COUNT];
    U64 historyGen;
    B32 alive;
};

struct ContentBlobEntry {
    ContentHash hash;
    U32 slot;
    U8 state;
};

struct ContentKeyEntry {
    ContentKey key;
    U32 slot;
    U8 state;
};

struct ContentStore {
    Arena* arena;
    OS_Handle mutex;
    SlotMap blobs;
    SlotMap keys;
    ContentBlobEntry* blobTable;
    U32 blobTableCapacity;
    U32 blobTableCount;
    U32 blobTableTombstones;
    ContentKeyEntry* keyTable;
    U32 keyTableCapacity;
    U32 keyTableCount;
    U32 keyTableTombstones;
    U64 rootIdGen;
    U64 payloadBytes;
    U64 committedBytes;
    U32 evictCount;
    U32 hitCount;
    U32 missCount;
};

static U32 content_table_capacity_from_count_(U32 requested) {
    U32 result = MAX(requested, 64u);
    return is_power_of_two(result) ? result : u32_next_power_of_two(result);
}

static U64 content_hash_u64_pair_(U64 a, U64 b, U64 seed) {
    U64 values[2] = {a, b};
    return hash_fnv1a(values, sizeof(values), seed);
}

static U64 content_hash_hash_(ContentHash hash) {
    return content_hash_u64_pair_(hash.hash[0], hash.hash[1], 1469598103934665603ull);
}

static U64 content_hash_key_(ContentKey key) {
    U64 values[3] = {key.root.id, key.id.u64[0], key.id.u64[1]};
    return hash_fnv1a(values, sizeof(values), 1099511628211ull ^ 0x517CC1B727220A95ull);
}

B32 content_hash_equal(ContentHash a, ContentHash b) {
    return (a.hash[0] == b.hash[0] && a.hash[1] == b.hash[1]) ? 1 : 0;
}

B32 content_hash_is_zero(ContentHash hash) {
    return content_hash_equal(hash, CONTENT_HASH_ZERO);
}

ContentHash content_hash_from_bytes(const void* data, U64 size) {
    ContentHash result = CONTENT_HASH_ZERO;
    result.hash[0] = hash_fnv1a(data, size, 1469598103934665603ull);
    result.hash[1] = hash_fnv1a(data, size, 1099511628211ull ^ 0x9E3779B97F4A7C15ull);
    return result;
}

ContentId content_id_from_bytes(const void* data, U64 size) {
    ContentHash hash = content_hash_from_bytes(data, size);
    ContentId result = {{hash.hash[0], hash.hash[1]}};
    return result;
}

ContentId content_id_from_u64(U64 value) {
    return content_id_from_bytes(&value, sizeof(value));
}

B32 content_key_is_zero(ContentKey key) {
    return (key.root.id == 0u && key.id.u64[0] == 0u && key.id.u64[1] == 0u) ? 1 : 0;
}

B32 content_key_equal(ContentKey a, ContentKey b) {
    return (a.root.id == b.root.id &&
            a.id.u64[0] == b.id.u64[0] &&
            a.id.u64[1] == b.id.u64[1]) ? 1 : 0;
}

ContentKey content_key_make(ContentRoot root, ContentId id) {
    ContentKey result = {};
    result.root = root;
    result.id = id;
    return result;
}

static U64 content_page_aligned_size_(U64 size) {
    OS_SystemInfo* info = OS_get_system_info();
    U64 pageSize = (info && info->pageSize) ? info->pageSize : KB(4);
    U64 result = align_pow2(size, pageSize);
    if (result == 0u) {
        result = pageSize;
    }
    return result;
}

static void content_lock_(ContentStore* store) {
    if (store && store->mutex.handle) {
        OS_mutex_lock(store->mutex);
    }
}

static void content_unlock_(ContentStore* store) {
    if (store && store->mutex.handle) {
        OS_mutex_unlock(store->mutex);
    }
}

static B32 content_blob_table_find_(ContentStore* store, ContentHash hash, U32* outIndex, B32* outFound) {
    if (!store || !store->blobTable || store->blobTableCapacity == 0u || content_hash_is_zero(hash)) {
        return 0;
    }

    U32 mask = store->blobTableCapacity - 1u;
    U32 start = (U32)(content_hash_hash_(hash) & (U64)mask);
    U32 firstTombstone = SLOT_MAP_INVALID_INDEX;
    for (U32 probe = 0u; probe < store->blobTableCapacity; ++probe) {
        U32 index = (start + probe) & mask;
        ContentBlobEntry* entry = store->blobTable + index;
        if (entry->state == ContentTableEntryState_Empty) {
            if (outIndex) {
                *outIndex = (firstTombstone != SLOT_MAP_INVALID_INDEX) ? firstTombstone : index;
            }
            if (outFound) {
                *outFound = 0;
            }
            return 1;
        }
        if (entry->state == ContentTableEntryState_Tombstone) {
            if (firstTombstone == SLOT_MAP_INVALID_INDEX) {
                firstTombstone = index;
            }
            continue;
        }
        if (content_hash_equal(entry->hash, hash)) {
            if (outIndex) {
                *outIndex = index;
            }
            if (outFound) {
                *outFound = 1;
            }
            return 1;
        }
    }

    if (firstTombstone != SLOT_MAP_INVALID_INDEX) {
        if (outIndex) {
            *outIndex = firstTombstone;
        }
        if (outFound) {
            *outFound = 0;
        }
        return 1;
    }
    return 0;
}

static B32 content_key_table_find_(ContentStore* store, ContentKey key, U32* outIndex, B32* outFound) {
    if (!store || !store->keyTable || store->keyTableCapacity == 0u || content_key_is_zero(key)) {
        return 0;
    }

    U32 mask = store->keyTableCapacity - 1u;
    U32 start = (U32)(content_hash_key_(key) & (U64)mask);
    U32 firstTombstone = SLOT_MAP_INVALID_INDEX;
    for (U32 probe = 0u; probe < store->keyTableCapacity; ++probe) {
        U32 index = (start + probe) & mask;
        ContentKeyEntry* entry = store->keyTable + index;
        if (entry->state == ContentTableEntryState_Empty) {
            if (outIndex) {
                *outIndex = (firstTombstone != SLOT_MAP_INVALID_INDEX) ? firstTombstone : index;
            }
            if (outFound) {
                *outFound = 0;
            }
            return 1;
        }
        if (entry->state == ContentTableEntryState_Tombstone) {
            if (firstTombstone == SLOT_MAP_INVALID_INDEX) {
                firstTombstone = index;
            }
            continue;
        }
        if (content_key_equal(entry->key, key)) {
            if (outIndex) {
                *outIndex = index;
            }
            if (outFound) {
                *outFound = 1;
            }
            return 1;
        }
    }

    if (firstTombstone != SLOT_MAP_INVALID_INDEX) {
        if (outIndex) {
            *outIndex = firstTombstone;
        }
        if (outFound) {
            *outFound = 0;
        }
        return 1;
    }
    return 0;
}

static B32 content_blob_table_rebuild_(ContentStore* store, U32 requestedCapacity) {
    U32 newCapacity = content_table_capacity_from_count_(requestedCapacity);
    ContentBlobEntry* newTable = ARENA_PUSH_ARRAY(store->arena, ContentBlobEntry, newCapacity);
    if (!newTable) {
        return 0;
    }
    MEMSET(newTable, 0, sizeof(ContentBlobEntry) * newCapacity);

    ContentBlobEntry* oldTable = store->blobTable;
    U32 oldCapacity = store->blobTableCapacity;
    store->blobTable = newTable;
    store->blobTableCapacity = newCapacity;
    store->blobTableCount = 0u;
    store->blobTableTombstones = 0u;

    for (U32 oldIndex = 0u; oldIndex < oldCapacity; ++oldIndex) {
        ContentBlobEntry* oldEntry = oldTable + oldIndex;
        if (oldEntry->state != ContentTableEntryState_Occupied) {
            continue;
        }

        U32 index = 0u;
        B32 found = 0;
        if (!content_blob_table_find_(store, oldEntry->hash, &index, &found)) {
            return 0;
        }
        ContentBlobEntry* entry = store->blobTable + index;
        entry->hash = oldEntry->hash;
        entry->slot = oldEntry->slot;
        entry->state = ContentTableEntryState_Occupied;
        store->blobTableCount += 1u;
    }
    return 1;
}

static B32 content_key_table_rebuild_(ContentStore* store, U32 requestedCapacity) {
    U32 newCapacity = content_table_capacity_from_count_(requestedCapacity);
    ContentKeyEntry* newTable = ARENA_PUSH_ARRAY(store->arena, ContentKeyEntry, newCapacity);
    if (!newTable) {
        return 0;
    }
    MEMSET(newTable, 0, sizeof(ContentKeyEntry) * newCapacity);

    ContentKeyEntry* oldTable = store->keyTable;
    U32 oldCapacity = store->keyTableCapacity;
    store->keyTable = newTable;
    store->keyTableCapacity = newCapacity;
    store->keyTableCount = 0u;
    store->keyTableTombstones = 0u;

    for (U32 oldIndex = 0u; oldIndex < oldCapacity; ++oldIndex) {
        ContentKeyEntry* oldEntry = oldTable + oldIndex;
        if (oldEntry->state != ContentTableEntryState_Occupied) {
            continue;
        }

        U32 index = 0u;
        B32 found = 0;
        if (!content_key_table_find_(store, oldEntry->key, &index, &found)) {
            return 0;
        }
        ContentKeyEntry* entry = store->keyTable + index;
        entry->key = oldEntry->key;
        entry->slot = oldEntry->slot;
        entry->state = ContentTableEntryState_Occupied;
        store->keyTableCount += 1u;
    }
    return 1;
}

static B32 content_blob_table_ensure_(ContentStore* store, U32 addCount) {
    U32 used = store->blobTableCount + store->blobTableTombstones + addCount;
    if (store->blobTableCapacity == 0u ||
        used * 100u >= store->blobTableCapacity * CONTENT_TABLE_MAX_LOAD_PERCENT) {
        U32 requested = store->blobTableCapacity ? store->blobTableCapacity * 2u : CONTENT_DEFAULT_BLOB_CAPACITY;
        while (used * 100u >= requested * CONTENT_TABLE_MAX_LOAD_PERCENT) {
            requested *= 2u;
        }
        return content_blob_table_rebuild_(store, requested);
    }
    return 1;
}

static B32 content_key_table_ensure_(ContentStore* store, U32 addCount) {
    U32 used = store->keyTableCount + store->keyTableTombstones + addCount;
    if (store->keyTableCapacity == 0u ||
        used * 100u >= store->keyTableCapacity * CONTENT_TABLE_MAX_LOAD_PERCENT) {
        U32 requested = store->keyTableCapacity ? store->keyTableCapacity * 2u : CONTENT_DEFAULT_KEY_CAPACITY;
        while (used * 100u >= requested * CONTENT_TABLE_MAX_LOAD_PERCENT) {
            requested *= 2u;
        }
        return content_key_table_rebuild_(store, requested);
    }
    return 1;
}

static ContentBlobNode* content_blob_from_hash_locked_(ContentStore* store, ContentHash hash, U32* outTableIndex) {
    U32 index = 0u;
    B32 found = 0;
    if (!content_blob_table_find_(store, hash, &index, &found) || !found) {
        return 0;
    }
    if (outTableIndex) {
        *outTableIndex = index;
    }
    ContentBlobEntry* entry = store->blobTable + index;
    return (ContentBlobNode*)slot_map_item_at(&store->blobs, entry->slot);
}

static void content_blob_remove_locked_(ContentStore* store, ContentHash hash) {
    U32 index = 0u;
    B32 found = 0;
    if (!content_blob_table_find_(store, hash, &index, &found) || !found) {
        return;
    }
    ContentBlobEntry* entry = store->blobTable + index;
    entry->state = ContentTableEntryState_Tombstone;
    entry->hash = CONTENT_HASH_ZERO;
    entry->slot = 0u;
    if (store->blobTableCount > 0u) {
        store->blobTableCount -= 1u;
    }
    store->blobTableTombstones += 1u;
}

static ContentKeyNode* content_key_from_key_locked_(ContentStore* store, ContentKey key, U32* outTableIndex) {
    U32 index = 0u;
    B32 found = 0;
    if (!content_key_table_find_(store, key, &index, &found) || !found) {
        return 0;
    }
    if (outTableIndex) {
        *outTableIndex = index;
    }
    ContentKeyEntry* entry = store->keyTable + index;
    return (ContentKeyNode*)slot_map_item_at(&store->keys, entry->slot);
}

static void content_blob_ref_key_locked_(ContentStore* store, ContentHash hash, S64 delta) {
    ContentBlobNode* blob = content_blob_from_hash_locked_(store, hash, 0);
    if (!blob) {
        return;
    }
    if (delta >= 0) {
        blob->keyRefCount += (U64)delta;
    } else {
        U64 amount = (U64)(-delta);
        blob->keyRefCount = (blob->keyRefCount >= amount) ? (blob->keyRefCount - amount) : 0u;
    }
}

static B32 content_key_push_hash_locked_(ContentStore* store, ContentKey key, ContentHash hash) {
    if (content_key_is_zero(key) || content_hash_is_zero(hash)) {
        return 1;
    }
    if (!content_key_table_ensure_(store, 1u)) {
        return 0;
    }

    U32 tableIndex = 0u;
    B32 found = 0;
    if (!content_key_table_find_(store, key, &tableIndex, &found)) {
        return 0;
    }

    ContentKeyNode* node = 0;
    if (found) {
        ContentKeyEntry* entry = store->keyTable + tableIndex;
        node = (ContentKeyNode*)slot_map_item_at(&store->keys, entry->slot);
    } else {
        void* slotItem = 0;
        U32 slotIndex = 0u;
        U32 slotGeneration = 0u;
        if (!slot_map_alloc(&store->keys, &slotItem, &slotIndex, &slotGeneration)) {
            return 0;
        }
        (void)slotGeneration;
        node = (ContentKeyNode*)slotItem;
        node->key = key;
        node->alive = 1;

        ContentKeyEntry* entry = store->keyTable + tableIndex;
        if (entry->state == ContentTableEntryState_Tombstone && store->keyTableTombstones > 0u) {
            store->keyTableTombstones -= 1u;
        }
        entry->key = key;
        entry->slot = slotIndex;
        entry->state = ContentTableEntryState_Occupied;
        store->keyTableCount += 1u;
    }

    if (node->historyGen != 0u) {
        ContentHash latest = node->history[(node->historyGen - 1u) % CONTENT_KEY_HASH_HISTORY_COUNT];
        if (content_hash_equal(latest, hash)) {
            return 1;
        }
    }

    if (node->historyGen >= CONTENT_KEY_HASH_STRONG_REF_COUNT) {
        ContentHash expired = node->history[(node->historyGen - CONTENT_KEY_HASH_STRONG_REF_COUNT) %
                                            CONTENT_KEY_HASH_HISTORY_COUNT];
        content_blob_ref_key_locked_(store, expired, -1);
    }
    node->history[node->historyGen % CONTENT_KEY_HASH_HISTORY_COUNT] = hash;
    node->historyGen += 1u;
    content_blob_ref_key_locked_(store, hash, 1);
    return 1;
}

static void content_key_close_locked_(ContentStore* store, ContentKeyNode* node) {
    if (!store || !node || !node->alive) {
        return;
    }

    U64 strongCount = MIN(node->historyGen, CONTENT_KEY_HASH_STRONG_REF_COUNT);
    for (U64 index = 0u; index < strongCount; ++index) {
        ContentHash hash = node->history[(node->historyGen - 1u - index) % CONTENT_KEY_HASH_HISTORY_COUNT];
        content_blob_ref_key_locked_(store, hash, -1);
    }
    node->alive = 0;
}

static void content_node_release_blob_(ContentStore* store, ContentBlobNode* node) {
    if (!store || !node || !node->data) {
        return;
    }

    OS_release(node->data, node->reservedSize);
    if (store->payloadBytes >= node->size) {
        store->payloadBytes -= node->size;
    } else {
        store->payloadBytes = 0u;
    }
    if (store->committedBytes >= node->committedSize) {
        store->committedBytes -= node->committedSize;
    } else {
        store->committedBytes = 0u;
    }
    node->data = 0;
    node->size = 0u;
    node->reservedSize = 0u;
    node->committedSize = 0u;
}

B32 content_store_create(const ContentStoreDesc* desc, ContentStore* outStore) {
    if (!desc || !desc->arena || !outStore) {
        return 0;
    }

    MEMSET(outStore, 0, sizeof(*outStore));
    outStore->arena = desc->arena;
    outStore->mutex = OS_mutex_create();
    if (!outStore->mutex.handle) {
        MEMSET(outStore, 0, sizeof(*outStore));
        return 0;
    }

    U32 blobCapacity = desc->initialBlobCapacity ? desc->initialBlobCapacity : CONTENT_DEFAULT_BLOB_CAPACITY;
    U32 keyCapacity = desc->initialKeyCapacity ? desc->initialKeyCapacity : CONTENT_DEFAULT_KEY_CAPACITY;
    if (!slot_map_init(&outStore->blobs, outStore->arena, sizeof(ContentBlobNode), blobCapacity) ||
        !slot_map_init(&outStore->keys, outStore->arena, sizeof(ContentKeyNode), keyCapacity) ||
        !content_blob_table_rebuild_(outStore, blobCapacity * 2u) ||
        !content_key_table_rebuild_(outStore, keyCapacity * 2u)) {
        OS_mutex_destroy(outStore->mutex);
        MEMSET(outStore, 0, sizeof(*outStore));
        return 0;
    }

    return 1;
}

ContentStore* content_store_alloc(const ContentStoreDesc* desc) {
    if (!desc || !desc->arena) {
        return 0;
    }

    ContentStore* result = ARENA_PUSH_STRUCT(desc->arena, ContentStore);
    if (!result || !content_store_create(desc, result)) {
        return 0;
    }
    return result;
}

void content_store_destroy(ContentStore* store) {
    if (!store) {
        return;
    }

    content_lock_(store);
    for (U32 slot = 0u; slot < store->blobs.capacity; ++slot) {
        if (!slot_map_is_occupied(&store->blobs, slot)) {
            continue;
        }
        ContentBlobNode* node = (ContentBlobNode*)slot_map_item_at(&store->blobs, slot);
        content_node_release_blob_(store, node);
    }
    content_unlock_(store);

    if (store->mutex.handle) {
        OS_mutex_destroy(store->mutex);
    }
    MEMSET(store, 0, sizeof(*store));
}

ContentRoot content_root_alloc(ContentStore* store) {
    ContentRoot result = CONTENT_ROOT_ZERO;
    if (!store) {
        return result;
    }

    content_lock_(store);
    store->rootIdGen += 1u;
    if (store->rootIdGen == 0u) {
        store->rootIdGen = 1u;
    }
    result.id = store->rootIdGen;
    content_unlock_(store);
    return result;
}

void content_root_release(ContentStore* store, ContentRoot root) {
    if (!store || root.id == 0u) {
        return;
    }

    content_lock_(store);
    for (U32 slot = 0u; slot < store->keys.capacity; ++slot) {
        if (!slot_map_is_occupied(&store->keys, slot)) {
            continue;
        }
        ContentKeyNode* node = (ContentKeyNode*)slot_map_item_at(&store->keys, slot);
        if (!node || node->key.root.id != root.id) {
            continue;
        }

        content_key_close_locked_(store, node);
        U32 tableIndex = 0u;
        if (content_key_from_key_locked_(store, node->key, &tableIndex)) {
            ContentKeyEntry* entry = store->keyTable + tableIndex;
            entry->state = ContentTableEntryState_Tombstone;
            entry->key = CONTENT_KEY_ZERO;
            entry->slot = 0u;
            if (store->keyTableCount > 0u) {
                store->keyTableCount -= 1u;
            }
            store->keyTableTombstones += 1u;
        }
        void* released = 0;
        slot_map_release(&store->keys, slot, store->keys.generations[slot], &released);
    }
    content_unlock_(store);
}

ContentHash content_submit_bytes(ContentStore* store, ContentKey key, const void* data, U64 size, StringU8 debugName) {
    if (!store || (!data && size != 0u)) {
        return CONTENT_HASH_ZERO;
    }

    ContentHash hash = content_hash_from_bytes(data, size);
    U64 committedSize = content_page_aligned_size_(size + 1u);
    U8* bytes = (U8*)OS_reserve(committedSize);
    if (!bytes) {
        return CONTENT_HASH_ZERO;
    }
    if (!OS_commit(bytes, committedSize)) {
        OS_release(bytes, committedSize);
        return CONTENT_HASH_ZERO;
    }
    if (size != 0u) {
        MEMCPY(bytes, data, size);
    }
    bytes[size] = 0;

    content_lock_(store);
    ContentBlobNode* existing = content_blob_from_hash_locked_(store, hash, 0);
    if (existing) {
#if !defined(NDEBUG)
        ASSERT_DEBUG(existing->size == size);
        if (existing->size == size && size != 0u) {
            ASSERT_DEBUG(MEMCMP(existing->data, data, size) == 0);
        }
#endif
        OS_release(bytes, committedSize);
        if (!content_key_is_zero(key)) {
            content_key_push_hash_locked_(store, key, hash);
        }
        content_unlock_(store);
        return hash;
    }

    if (!content_blob_table_ensure_(store, 1u)) {
        content_unlock_(store);
        OS_release(bytes, committedSize);
        return CONTENT_HASH_ZERO;
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 slotGeneration = 0u;
    if (!slot_map_alloc(&store->blobs, &slotItem, &slotIndex, &slotGeneration)) {
        content_unlock_(store);
        OS_release(bytes, committedSize);
        return CONTENT_HASH_ZERO;
    }
    (void)slotGeneration;

    ContentBlobNode* node = (ContentBlobNode*)slotItem;
    node->hash = hash;
    node->data = bytes;
    node->size = size;
    node->reservedSize = committedSize;
    node->committedSize = committedSize;
    node->lastTouchFrame = 0u;
    node->debugName = str8_cpy(store->arena, debugName);

    U32 tableIndex = 0u;
    B32 found = 0;
    if (!content_blob_table_find_(store, hash, &tableIndex, &found) || found) {
        slot_map_release(&store->blobs, slotIndex, store->blobs.generations[slotIndex], 0);
        content_unlock_(store);
        OS_release(bytes, committedSize);
        return CONTENT_HASH_ZERO;
    }
    ContentBlobEntry* entry = store->blobTable + tableIndex;
    if (entry->state == ContentTableEntryState_Tombstone && store->blobTableTombstones > 0u) {
        store->blobTableTombstones -= 1u;
    }
    entry->hash = hash;
    entry->slot = slotIndex;
    entry->state = ContentTableEntryState_Occupied;
    store->blobTableCount += 1u;
    store->payloadBytes += size;
    store->committedBytes += committedSize;

    if (!content_key_is_zero(key) && !content_key_push_hash_locked_(store, key, hash)) {
        content_blob_remove_locked_(store, hash);
        content_node_release_blob_(store, node);
        slot_map_release(&store->blobs, slotIndex, store->blobs.generations[slotIndex], 0);
        content_unlock_(store);
        return CONTENT_HASH_ZERO;
    }

    content_unlock_(store);
    return hash;
}

ContentHash content_hash_from_key(ContentStore* store, ContentKey key, U64 rewindCount) {
    ContentHash result = CONTENT_HASH_ZERO;
    if (!store || content_key_is_zero(key)) {
        return result;
    }

    content_lock_(store);
    ContentKeyNode* node = content_key_from_key_locked_(store, key, 0);
    if (node && node->alive && node->historyGen != 0u && node->historyGen > rewindCount) {
        result = node->history[(node->historyGen - 1u - rewindCount) % CONTENT_KEY_HASH_HISTORY_COUNT];
    }
    content_unlock_(store);
    return result;
}

ContentView content_view_hash(ContentStore* store, ContentHash hash) {
    ContentView result = {};
    if (!store || content_hash_is_zero(hash)) {
        return result;
    }

    content_lock_(store);
    ContentBlobNode* node = content_blob_from_hash_locked_(store, hash, 0);
    if (node && node->data) {
        result.data = node->data;
        result.size = node->size;
        result.hash = node->hash;
        result.valid = 1;
        store->hitCount += 1u;
    } else {
        store->missCount += 1u;
    }
    content_unlock_(store);
    return result;
}

B32 content_retain_hash(ContentStore* store, ContentHash hash) {
    B32 result = 0;
    if (!store || content_hash_is_zero(hash)) {
        return result;
    }

    content_lock_(store);
    ContentBlobNode* node = content_blob_from_hash_locked_(store, hash, 0);
    if (node) {
        node->downstreamRefCount += 1u;
        result = 1;
    }
    content_unlock_(store);
    return result;
}

void content_release_hash(ContentStore* store, ContentHash hash) {
    if (!store || content_hash_is_zero(hash)) {
        return;
    }

    content_lock_(store);
    ContentBlobNode* node = content_blob_from_hash_locked_(store, hash, 0);
    if (node && node->downstreamRefCount > 0u) {
        node->downstreamRefCount -= 1u;
    }
    content_unlock_(store);
}

void content_touch_hash(ContentStore* store, ContentHash hash, U64 frameIndex) {
    if (!store || content_hash_is_zero(hash)) {
        return;
    }

    content_lock_(store);
    ContentBlobNode* node = content_blob_from_hash_locked_(store, hash, 0);
    if (node) {
        node->lastTouchFrame = frameIndex;
    }
    content_unlock_(store);
}

void content_tick_gc(ContentStore* store, U64 frameIndex, U64 targetBytes) {
    if (!store) {
        return;
    }

    content_lock_(store);
    while (store->committedBytes > targetBytes) {
        U32 bestSlot = SLOT_MAP_INVALID_INDEX;
        U64 bestFrame = UINT64_MAX;
        for (U32 slot = 0u; slot < store->blobs.capacity; ++slot) {
            if (!slot_map_is_occupied(&store->blobs, slot)) {
                continue;
            }
            ContentBlobNode* node = (ContentBlobNode*)slot_map_item_at(&store->blobs, slot);
            if (!node ||
                node->lastTouchFrame == frameIndex ||
                node->keyRefCount != 0u ||
                node->downstreamRefCount != 0u) {
                continue;
            }
            if (node->lastTouchFrame < bestFrame) {
                bestFrame = node->lastTouchFrame;
                bestSlot = slot;
            }
        }
        if (bestSlot == SLOT_MAP_INVALID_INDEX) {
            break;
        }

        ContentBlobNode* node = (ContentBlobNode*)slot_map_item_at(&store->blobs, bestSlot);
        ContentHash hash = node->hash;
        content_blob_remove_locked_(store, hash);
        content_node_release_blob_(store, node);
        void* released = 0;
        slot_map_release(&store->blobs, bestSlot, store->blobs.generations[bestSlot], &released);
        store->evictCount += 1u;
    }
    content_unlock_(store);
}

ContentStats content_stats(ContentStore* store) {
    ContentStats result = {};
    if (!store) {
        return result;
    }

    content_lock_(store);
    result.payloadBytes = store->payloadBytes;
    result.committedBytes = store->committedBytes;
    result.blobCount = store->blobs.count;
    result.keyCount = store->keys.count;
    result.evictCount = store->evictCount;
    result.hitCount = store->hitCount;
    result.missCount = store->missCount;
    content_unlock_(store);
    return result;
}
