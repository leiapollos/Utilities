#define ARTIFACT_DEFAULT_SLOT_CAPACITY 64u
#define ARTIFACT_DEFAULT_TABLE_CAPACITY 128u
#define ARTIFACT_DEFAULT_TYPE_CAPACITY 16u
#define ARTIFACT_REQUEST_DATA_MAX 256u
#define ARTIFACT_TABLE_MAX_LOAD_PERCENT 70u

enum ArtifactTableEntryState {
    ArtifactTableEntryState_Empty = 0,
    ArtifactTableEntryState_Occupied,
    ArtifactTableEntryState_Tombstone,
};

struct ArtifactNode {
    ArtifactTypeId typeId;
    ArtifactKey key;
    ArtifactValue value;
    U64 readyGeneration;
    U64 requestedGeneration;
    U64 workingGeneration;
    U64 failedGeneration;
    U64 lastTouchFrame;
    U64 bytes;
    U32 retainCount;
    U32 requestDataSize;
    U8 requestData[ARTIFACT_REQUEST_DATA_MAX];
    U64 cancelFlag;
    ArtifactStatus status;
};

struct ArtifactTableEntry {
    ArtifactTypeId typeId;
    ArtifactKey key;
    U32 slot;
    U8 state;
};

struct ArtifactQueuedJob {
    U32 slot;
    U32 slotGeneration;
    U64 generation;
};

struct ArtifactCompletedJob {
    U32 slot;
    U32 slotGeneration;
    U64 generation;
    ArtifactValue buildValue;
    U64 bytes;
    U64 buildTimeNs;
    B32 succeeded;
    B32 cancelled;
};

struct ArtifactJobParams {
    ArtifactCache* cache;
    U32 slot;
    U32 slotGeneration;
    U64 generation;
};

static_assert(sizeof(ArtifactJobParams) <= JOB_PARAMETER_SPACE, "Artifact job params must fit job inline storage");

struct ArtifactCache {
    Arena* arena;
    JobSystem* jobSystem;
    ContentStore* content;
    OS_Handle mutex;
    SlotMap slots;
    ArtifactTableEntry* table;
    U32 tableCapacity;
    U32 tableCount;
    U32 tableTombstones;
    ArtifactTypeDesc* types;
    U32 typeCount;
    U32 typeCapacity;
    ArtifactQueuedJob* highQueue;
    U32 highQueueCount;
    U32 highQueueCapacity;
    ArtifactQueuedJob* normalQueue;
    U32 normalQueueCount;
    U32 normalQueueCapacity;
    ArtifactCompletedJob* completedQueue;
    U32 completedQueueCount;
    U32 completedQueueCapacity;
    U32 requestDataSize;
    U64 activeJobCount;
    U64 shuttingDown;
    ArtifactStats stats;
};

static void artifact_build_job_(void* params);

static U64 artifact_hash_bytes_(const void* data, U64 size, U64 seed) {
    const U8* bytes = (const U8*)data;
    U64 hash = seed;
    for (U64 i = 0u; i < size; ++i) {
        hash ^= (U64)bytes[i];
        hash *= 1099511628211ull;
    }
    hash ^= size;
    hash *= 1099511628211ull;
    if (hash == 0u) {
        hash = 1u;
    }
    return hash;
}

static U32 artifact_table_capacity_from_count_(U32 requested) {
    U32 result = requested ? requested : ARTIFACT_DEFAULT_TABLE_CAPACITY;
    if (result < ARTIFACT_DEFAULT_TABLE_CAPACITY) {
        result = ARTIFACT_DEFAULT_TABLE_CAPACITY;
    }
    if (!is_power_of_two(result)) {
        U32 next = 1u;
        while (next < result) {
            next <<= 1u;
        }
        result = next;
    }
    return result;
}

static U64 artifact_table_hash_(ArtifactTypeId typeId, ArtifactKey key) {
    U64 values[3] = {(U64)typeId, key.hash[0], key.hash[1]};
    return artifact_hash_bytes_(values, sizeof(values), 1469598103934665603ull ^ 0xF1357AEA2E62A9C5ull);
}

B32 artifact_key_equal(ArtifactKey a, ArtifactKey b) {
    return (a.hash[0] == b.hash[0] && a.hash[1] == b.hash[1]) ? 1 : 0;
}

B32 artifact_key_is_zero(ArtifactKey key) {
    return artifact_key_equal(key, ARTIFACT_KEY_ZERO);
}

ArtifactKey artifact_key_from_bytes(const void* data, U64 size) {
    ArtifactKey result = ARTIFACT_KEY_ZERO;
    result.hash[0] = artifact_hash_bytes_(data, size, 1469598103934665603ull ^ 0xA24BAED4963EE407ull);
    result.hash[1] = artifact_hash_bytes_(data, size, 1099511628211ull ^ 0x9FB21C651E98DF25ull);
    return result;
}

ArtifactKey artifact_key_mix(ArtifactKey a, ArtifactKey b) {
    ArtifactKey result = {};
    result.hash[0] = a.hash[0] ^ (b.hash[0] + 0x9E3779B97F4A7C15ull + (a.hash[0] << 6u) + (a.hash[0] >> 2u));
    result.hash[1] = a.hash[1] ^ (b.hash[1] + 0xC2B2AE3D27D4EB4Full + (a.hash[1] << 6u) + (a.hash[1] >> 2u));
    if (artifact_key_is_zero(result)) {
        result.hash[0] = 1u;
    }
    return result;
}

ArtifactKey artifact_key_mix_u64(ArtifactKey key, U64 value) {
    ArtifactKey valueKey = artifact_key_from_bytes(&value, sizeof(value));
    return artifact_key_mix(key, valueKey);
}

static void artifact_lock_(ArtifactCache* cache) {
    if (cache && cache->mutex.handle) {
        OS_mutex_lock(cache->mutex);
    }
}

static void artifact_unlock_(ArtifactCache* cache) {
    if (cache && cache->mutex.handle) {
        OS_mutex_unlock(cache->mutex);
    }
}

static B32 artifact_table_find_(ArtifactCache* cache,
                                ArtifactTypeId typeId,
                                ArtifactKey key,
                                U32* outIndex,
                                B32* outFound) {
    if (!cache || !cache->table || cache->tableCapacity == 0u ||
        typeId == ARTIFACT_TYPE_ID_ZERO || artifact_key_is_zero(key)) {
        return 0;
    }

    U32 mask = cache->tableCapacity - 1u;
    U32 start = (U32)(artifact_table_hash_(typeId, key) & (U64)mask);
    U32 firstTombstone = SLOT_MAP_INVALID_INDEX;
    for (U32 probe = 0u; probe < cache->tableCapacity; ++probe) {
        U32 index = (start + probe) & mask;
        ArtifactTableEntry* entry = cache->table + index;
        if (entry->state == ArtifactTableEntryState_Empty) {
            if (outIndex) {
                *outIndex = (firstTombstone != SLOT_MAP_INVALID_INDEX) ? firstTombstone : index;
            }
            if (outFound) {
                *outFound = 0;
            }
            return 1;
        }
        if (entry->state == ArtifactTableEntryState_Tombstone) {
            if (firstTombstone == SLOT_MAP_INVALID_INDEX) {
                firstTombstone = index;
            }
            continue;
        }
        if (entry->typeId == typeId && artifact_key_equal(entry->key, key)) {
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

static B32 artifact_table_rebuild_(ArtifactCache* cache, U32 requestedCapacity) {
    U32 newCapacity = artifact_table_capacity_from_count_(requestedCapacity);
    ArtifactTableEntry* oldTable = cache->table;
    U32 oldCapacity = cache->tableCapacity;

    ArtifactTableEntry* newTable = ARENA_PUSH_ARRAY(cache->arena, ArtifactTableEntry, newCapacity);
    if (!newTable) {
        return 0;
    }
    MEMSET(newTable, 0, sizeof(ArtifactTableEntry) * newCapacity);

    cache->table = newTable;
    cache->tableCapacity = newCapacity;
    cache->tableCount = 0u;
    cache->tableTombstones = 0u;

    for (U32 oldIndex = 0u; oldIndex < oldCapacity; ++oldIndex) {
        ArtifactTableEntry* oldEntry = oldTable + oldIndex;
        if (!oldTable || oldEntry->state != ArtifactTableEntryState_Occupied) {
            continue;
        }

        U32 index = 0u;
        B32 found = 0;
        if (!artifact_table_find_(cache, oldEntry->typeId, oldEntry->key, &index, &found)) {
            return 0;
        }

        ArtifactTableEntry* entry = cache->table + index;
        entry->typeId = oldEntry->typeId;
        entry->key = oldEntry->key;
        entry->slot = oldEntry->slot;
        entry->state = ArtifactTableEntryState_Occupied;
        cache->tableCount += 1u;
    }

    return 1;
}

static B32 artifact_table_ensure_(ArtifactCache* cache, U32 addCount) {
    U32 capacity = cache->tableCapacity ? cache->tableCapacity : ARTIFACT_DEFAULT_TABLE_CAPACITY;
    U32 requested = cache->tableCount + cache->tableTombstones + addCount;
    if (requested * 100u >= capacity * ARTIFACT_TABLE_MAX_LOAD_PERCENT) {
        return artifact_table_rebuild_(cache, capacity * 2u);
    }
    return 1;
}

static ArtifactTypeDesc* artifact_type_from_id_locked_(ArtifactCache* cache, ArtifactTypeId typeId) {
    if (!cache || typeId == ARTIFACT_TYPE_ID_ZERO) {
        return 0;
    }

    for (U32 typeIndex = 0u; typeIndex < cache->typeCount; ++typeIndex) {
        ArtifactTypeDesc* type = cache->types + typeIndex;
        if (type->typeId == typeId) {
            return type;
        }
    }
    return 0;
}

static ArtifactNode* artifact_node_from_type_key_locked_(ArtifactCache* cache,
                                                         ArtifactTypeId typeId,
                                                         ArtifactKey key,
                                                         U32* outSlot) {
    U32 tableIndex = 0u;
    B32 found = 0;
    if (!artifact_table_find_(cache, typeId, key, &tableIndex, &found) || !found) {
        return 0;
    }

    ArtifactTableEntry* entry = cache->table + tableIndex;
    if (outSlot) {
        *outSlot = entry->slot;
    }
    return (ArtifactNode*)slot_map_item_at(&cache->slots, entry->slot);
}

static void artifact_table_remove_locked_(ArtifactCache* cache, ArtifactTypeId typeId, ArtifactKey key) {
    U32 tableIndex = 0u;
    B32 found = 0;
    if (!artifact_table_find_(cache, typeId, key, &tableIndex, &found) || !found) {
        return;
    }

    ArtifactTableEntry* entry = cache->table + tableIndex;
    MEMSET(entry, 0, sizeof(*entry));
    entry->state = ArtifactTableEntryState_Tombstone;
    if (cache->tableCount > 0u) {
        cache->tableCount -= 1u;
    }
    cache->tableTombstones += 1u;
}

static ArtifactResult artifact_result_from_node_(ArtifactNode* node, U64 requestedGeneration, U32 flags) {
    ArtifactResult result = {};
    if (!node) {
        return result;
    }

    result.requestedGeneration = requestedGeneration;
    result.generation = node->readyGeneration;
    result.value = node->value;
    result.status = node->status;
    result.flags = flags;

    if (node->readyGeneration != 0u &&
        node->readyGeneration != requestedGeneration &&
        (node->status == ArtifactStatus_Queued ||
         node->status == ArtifactStatus_Building ||
         node->status == ArtifactStatus_Publishing ||
         node->status == ArtifactStatus_Error ||
         node->status == ArtifactStatus_Cancelled)) {
        result.status = ArtifactStatus_Ready;
        result.flags |= ArtifactResultFlags_Stale;
    }

    if (node->readyGeneration == requestedGeneration) {
        result.status = ArtifactStatus_Ready;
        result.flags &= ~ArtifactResultFlags_Stale;
    }
    if (node->failedGeneration == requestedGeneration) {
        result.flags |= ArtifactResultFlags_ErrorCached;
    }

    return result;
}

static B32 artifact_queue_push_(Arena* arena,
                                ArtifactQueuedJob** queue,
                                U32* count,
                                U32* capacity,
                                ArtifactQueuedJob job) {
    if (*count >= *capacity) {
        U32 oldCapacity = *capacity;
        U32 newCapacity = oldCapacity ? oldCapacity * 2u : 64u;
        ArtifactQueuedJob* newQueue = ARENA_PUSH_ARRAY(arena, ArtifactQueuedJob, newCapacity);
        if (!newQueue) {
            return 0;
        }
        MEMSET(newQueue, 0, sizeof(ArtifactQueuedJob) * newCapacity);
        if (*queue && oldCapacity != 0u) {
            MEMCPY(newQueue, *queue, sizeof(ArtifactQueuedJob) * oldCapacity);
        }
        *queue = newQueue;
        *capacity = newCapacity;
    }

    (*queue)[*count] = job;
    *count += 1u;
    return 1;
}

static B32 artifact_completed_push_(ArtifactCache* cache, ArtifactCompletedJob job) {
    if (cache->completedQueueCount >= cache->completedQueueCapacity) {
        U32 oldCapacity = cache->completedQueueCapacity;
        U32 newCapacity = oldCapacity ? oldCapacity * 2u : 64u;
        ArtifactCompletedJob* newQueue = ARENA_PUSH_ARRAY(cache->arena, ArtifactCompletedJob, newCapacity);
        if (!newQueue) {
            return 0;
        }
        MEMSET(newQueue, 0, sizeof(ArtifactCompletedJob) * newCapacity);
        if (cache->completedQueue && oldCapacity != 0u) {
            MEMCPY(newQueue, cache->completedQueue, sizeof(ArtifactCompletedJob) * oldCapacity);
        }
        cache->completedQueue = newQueue;
        cache->completedQueueCapacity = newCapacity;
    }

    cache->completedQueue[cache->completedQueueCount] = job;
    cache->completedQueueCount += 1u;
    return 1;
}

static B32 artifact_queue_node_locked_(ArtifactCache* cache,
                                       ArtifactNode* node,
                                       U32 slot,
                                       U64 generation,
                                       U32 flags) {
    if (!cache || !node || ATOMIC_LOAD(&cache->shuttingDown, MEMORY_ORDER_ACQUIRE)) {
        return 0;
    }
    if (node->workingGeneration == generation &&
        (node->status == ArtifactStatus_Queued ||
         node->status == ArtifactStatus_Building ||
         node->status == ArtifactStatus_Publishing)) {
        return 1;
    }

    ArtifactQueuedJob job = {};
    job.slot = slot;
    job.slotGeneration = cache->slots.generations[slot];
    job.generation = generation;

    B32 ok = 0;
    if (FLAGS_HAS(flags, ArtifactGetFlags_HighPriority)) {
        ok = artifact_queue_push_(cache->arena, &cache->highQueue, &cache->highQueueCount, &cache->highQueueCapacity, job);
    } else {
        ok = artifact_queue_push_(cache->arena, &cache->normalQueue, &cache->normalQueueCount, &cache->normalQueueCapacity, job);
    }
    if (!ok) {
        return 0;
    }

    node->workingGeneration = generation;
    node->requestedGeneration = generation;
    node->cancelFlag = 0u;
    node->status = ArtifactStatus_Queued;
    cache->stats.queued += 1u;
    return 1;
}

static B32 artifact_queue_pop_locked_(ArtifactCache* cache, ArtifactQueuedJob* outJob) {
    if (!cache || !outJob) {
        return 0;
    }

    if (cache->highQueueCount != 0u) {
        cache->highQueueCount -= 1u;
        *outJob = cache->highQueue[cache->highQueueCount];
        return 1;
    }
    if (cache->normalQueueCount != 0u) {
        cache->normalQueueCount -= 1u;
        *outJob = cache->normalQueue[cache->normalQueueCount];
        return 1;
    }
    return 0;
}

static B32 artifact_completed_pop_locked_(ArtifactCache* cache, ArtifactCompletedJob* outJob) {
    if (!cache || !outJob || cache->completedQueueCount == 0u) {
        return 0;
    }

    cache->completedQueueCount -= 1u;
    *outJob = cache->completedQueue[cache->completedQueueCount];
    return 1;
}

static B32 artifact_status_is_working_(ArtifactStatus status) {
    return (status == ArtifactStatus_Queued ||
            status == ArtifactStatus_Building ||
            status == ArtifactStatus_Publishing) ? 1 : 0;
}

static void artifact_destroy_value_(ArtifactTypeDesc* type, ArtifactValue value) {
    if (type && type->destroyProc) {
        type->destroyProc(type->userData, value);
    }
}

B32 artifact_cache_create(const ArtifactCacheDesc* desc, ArtifactCache* outCache) {
    if (!desc || !desc->arena || !outCache) {
        return 0;
    }

    MEMSET(outCache, 0, sizeof(*outCache));
    outCache->arena = desc->arena;
    outCache->jobSystem = desc->jobSystem;
    outCache->content = desc->content;
    outCache->requestDataSize = desc->requestDataSize ? desc->requestDataSize : ARTIFACT_REQUEST_DATA_MAX;
    if (outCache->requestDataSize > ARTIFACT_REQUEST_DATA_MAX) {
        outCache->requestDataSize = ARTIFACT_REQUEST_DATA_MAX;
    }

    U32 slotCapacity = desc->initialSlotCapacity ? desc->initialSlotCapacity : ARTIFACT_DEFAULT_SLOT_CAPACITY;
    U32 typeCapacity = desc->initialTypeCapacity ? desc->initialTypeCapacity : ARTIFACT_DEFAULT_TYPE_CAPACITY;
    if (!slot_map_init(&outCache->slots, outCache->arena, sizeof(ArtifactNode), slotCapacity)) {
        MEMSET(outCache, 0, sizeof(*outCache));
        return 0;
    }

    outCache->types = ARENA_PUSH_ARRAY(outCache->arena, ArtifactTypeDesc, typeCapacity);
    if (!outCache->types) {
        MEMSET(outCache, 0, sizeof(*outCache));
        return 0;
    }
    MEMSET(outCache->types, 0, sizeof(ArtifactTypeDesc) * typeCapacity);
    outCache->typeCapacity = typeCapacity;

    if (!artifact_table_rebuild_(outCache, desc->initialTableCapacity ? desc->initialTableCapacity : ARTIFACT_DEFAULT_TABLE_CAPACITY)) {
        MEMSET(outCache, 0, sizeof(*outCache));
        return 0;
    }

    outCache->mutex = OS_mutex_create();
    if (!outCache->mutex.handle) {
        MEMSET(outCache, 0, sizeof(*outCache));
        return 0;
    }

    return 1;
}

ArtifactCache* artifact_cache_alloc(const ArtifactCacheDesc* desc) {
    if (!desc || !desc->arena) {
        return 0;
    }

    ArtifactCache* result = ARENA_PUSH_STRUCT(desc->arena, ArtifactCache);
    if (!result || !artifact_cache_create(desc, result)) {
        return 0;
    }
    return result;
}

void artifact_cache_destroy(ArtifactCache* cache) {
    if (!cache) {
        return;
    }

    artifact_lock_(cache);
    ATOMIC_STORE(&cache->shuttingDown, 1u, MEMORY_ORDER_RELEASE);
    for (U32 slot = 0u; slot < cache->slots.capacity; ++slot) {
        if (!slot_map_is_occupied(&cache->slots, slot)) {
            continue;
        }
        ArtifactNode* node = (ArtifactNode*)slot_map_item_at(&cache->slots, slot);
        if (node) {
            ATOMIC_STORE(&node->cancelFlag, 1u, MEMORY_ORDER_RELEASE);
        }
    }
    artifact_unlock_(cache);

    for (;;) {
        artifact_lock_(cache);
        U64 activeJobCount = cache->activeJobCount;
        artifact_unlock_(cache);
        if (activeJobCount == 0u) {
            break;
        }
        OS_thread_yield();
    }

    for (U32 slot = 0u; slot < cache->slots.capacity; ++slot) {
        if (!slot_map_is_occupied(&cache->slots, slot)) {
            continue;
        }

        ArtifactNode* node = (ArtifactNode*)slot_map_item_at(&cache->slots, slot);
        ArtifactTypeDesc* type = artifact_type_from_id_locked_(cache, node ? node->typeId : 0u);
        if (node && node->readyGeneration != 0u) {
            artifact_destroy_value_(type, node->value);
        }
    }

    if (cache->mutex.handle) {
        OS_mutex_destroy(cache->mutex);
    }
    MEMSET(cache, 0, sizeof(*cache));
}

B32 artifact_register_type(ArtifactCache* cache, const ArtifactTypeDesc* desc) {
    if (!cache || !desc || desc->typeId == ARTIFACT_TYPE_ID_ZERO || !desc->buildProc) {
        return 0;
    }

    artifact_lock_(cache);
    ArtifactTypeDesc* existing = artifact_type_from_id_locked_(cache, desc->typeId);
    if (existing) {
        *existing = *desc;
        artifact_unlock_(cache);
        return 1;
    }

    if (cache->typeCount >= cache->typeCapacity) {
        U32 oldCapacity = cache->typeCapacity;
        U32 newCapacity = oldCapacity ? oldCapacity * 2u : ARTIFACT_DEFAULT_TYPE_CAPACITY;
        ArtifactTypeDesc* newTypes = ARENA_PUSH_ARRAY(cache->arena, ArtifactTypeDesc, newCapacity);
        if (!newTypes) {
            artifact_unlock_(cache);
            return 0;
        }
        MEMSET(newTypes, 0, sizeof(ArtifactTypeDesc) * newCapacity);
        if (cache->types && oldCapacity != 0u) {
            MEMCPY(newTypes, cache->types, sizeof(ArtifactTypeDesc) * oldCapacity);
        }
        cache->types = newTypes;
        cache->typeCapacity = newCapacity;
    }

    cache->types[cache->typeCount] = *desc;
    cache->typeCount += 1u;
    artifact_unlock_(cache);
    return 1;
}

static ArtifactResult artifact_get_once_(ArtifactCache* cache,
                                         ArtifactTypeId typeId,
                                         ArtifactKey key,
                                         U64 generation,
                                         const void* requestData,
                                         U32 requestDataSize,
                                         U32 flags) {
    ArtifactResult result = {};
    result.requestedGeneration = generation;
    if (!cache || typeId == ARTIFACT_TYPE_ID_ZERO || artifact_key_is_zero(key) ||
        generation == 0u || requestDataSize > cache->requestDataSize) {
        return result;
    }

    artifact_lock_(cache);
    ArtifactTypeDesc* type = artifact_type_from_id_locked_(cache, typeId);
    if (!type || !type->buildProc) {
        artifact_unlock_(cache);
        return result;
    }

    U32 slot = SLOT_MAP_INVALID_INDEX;
    B32 createdNode = 0;
    ArtifactNode* node = artifact_node_from_type_key_locked_(cache, typeId, key, &slot);
    if (!node) {
        if (!artifact_table_ensure_(cache, 1u)) {
            artifact_unlock_(cache);
            return result;
        }

        void* slotItem = 0;
        U32 slotGeneration = 0u;
        if (!slot_map_alloc(&cache->slots, &slotItem, &slot, &slotGeneration)) {
            artifact_unlock_(cache);
            return result;
        }
        (void)slotGeneration;

        U32 tableIndex = 0u;
        B32 found = 0;
        if (!artifact_table_find_(cache, typeId, key, &tableIndex, &found) || found) {
            slot_map_release(&cache->slots, slot, cache->slots.generations[slot], 0);
            artifact_unlock_(cache);
            return result;
        }

        ArtifactTableEntry* entry = cache->table + tableIndex;
        entry->typeId = typeId;
        entry->key = key;
        entry->slot = slot;
        entry->state = ArtifactTableEntryState_Occupied;
        cache->tableCount += 1u;

        node = (ArtifactNode*)slotItem;
        node->typeId = typeId;
        node->key = key;
        createdNode = 1;
        cache->stats.misses += 1u;
    }

    if (node->readyGeneration == generation) {
        cache->stats.hits += 1u;
        result = artifact_result_from_node_(node, generation, ArtifactResultFlags_None);
        artifact_unlock_(cache);
        return result;
    }

    if (node->readyGeneration != 0u) {
        cache->stats.staleHits += 1u;
    } else if (!createdNode && node->status == ArtifactStatus_Null) {
        cache->stats.misses += 1u;
    }

    if (requestData && requestDataSize != 0u) {
        MEMCPY(node->requestData, requestData, requestDataSize);
    }
    node->requestDataSize = requestDataSize;
    node->requestedGeneration = generation;

    if (FLAGS_HAS(flags, ArtifactGetFlags_InvalidateFailed)) {
        node->failedGeneration = 0u;
        if (node->status == ArtifactStatus_Error) {
            node->status = (node->readyGeneration != 0u) ? ArtifactStatus_Ready : ArtifactStatus_Null;
        }
    }

    if (node->failedGeneration == generation) {
        result = artifact_result_from_node_(node, generation, ArtifactResultFlags_ErrorCached);
        artifact_unlock_(cache);
        return result;
    }

    if (!FLAGS_HAS(flags, ArtifactGetFlags_NoQueue)) {
        if (artifact_queue_node_locked_(cache, node, slot, generation, flags)) {
            result = artifact_result_from_node_(node, generation, ArtifactResultFlags_Queued);
        } else {
            node->failedGeneration = generation;
            node->status = (node->readyGeneration != 0u) ? ArtifactStatus_Ready : ArtifactStatus_Error;
            result = artifact_result_from_node_(node, generation, ArtifactResultFlags_None);
        }
    } else {
        result = artifact_result_from_node_(node, generation, ArtifactResultFlags_None);
    }

    artifact_unlock_(cache);
    return result;
}

ArtifactResult artifact_get(ArtifactCache* cache,
                            ArtifactTypeId typeId,
                            ArtifactKey key,
                            U64 generation,
                            const void* requestData,
                            U32 requestDataSize,
                            U32 flags,
                            U64 deadlineNs) {
    ArtifactResult result = artifact_get_once_(cache, typeId, key, generation, requestData, requestDataSize, flags);
    if (!cache || !FLAGS_HAS(flags, ArtifactGetFlags_WaitFresh)) {
        return result;
    }

    while (result.status != ArtifactStatus_Ready ||
           result.generation != generation ||
           FLAGS_HAS(result.flags, ArtifactResultFlags_Stale)) {
        U64 nowNs = OS_get_time_nanoseconds();
        if (deadlineNs != 0u && nowNs >= deadlineNs) {
            result.flags |= ArtifactResultFlags_TimedOut;
            break;
        }

        artifact_cache_tick(cache, 0u, 8u, 8u);
        OS_thread_yield();
        result = artifact_view(cache, typeId, key);
        result.requestedGeneration = generation;
        if (FLAGS_HAS(result.flags, ArtifactResultFlags_ErrorCached)) {
            break;
        }
    }

    return result;
}

ArtifactResult artifact_view(ArtifactCache* cache, ArtifactTypeId typeId, ArtifactKey key) {
    ArtifactResult result = {};
    if (!cache || typeId == ARTIFACT_TYPE_ID_ZERO || artifact_key_is_zero(key)) {
        return result;
    }

    artifact_lock_(cache);
    ArtifactNode* node = artifact_node_from_type_key_locked_(cache, typeId, key, 0);
    if (node) {
        result = artifact_result_from_node_(node, node->requestedGeneration, ArtifactResultFlags_None);
    }
    artifact_unlock_(cache);
    return result;
}

void artifact_touch(ArtifactCache* cache, ArtifactTypeId typeId, ArtifactKey key, U64 frameIndex) {
    if (!cache || typeId == ARTIFACT_TYPE_ID_ZERO || artifact_key_is_zero(key)) {
        return;
    }

    artifact_lock_(cache);
    ArtifactNode* node = artifact_node_from_type_key_locked_(cache, typeId, key, 0);
    if (node) {
        node->lastTouchFrame = frameIndex;
    }
    artifact_unlock_(cache);
}

B32 artifact_retain(ArtifactCache* cache, ArtifactTypeId typeId, ArtifactKey key) {
    B32 result = 0;
    if (!cache || typeId == ARTIFACT_TYPE_ID_ZERO || artifact_key_is_zero(key)) {
        return result;
    }

    artifact_lock_(cache);
    ArtifactNode* node = artifact_node_from_type_key_locked_(cache, typeId, key, 0);
    if (node) {
        node->retainCount += 1u;
        result = 1;
    }
    artifact_unlock_(cache);
    return result;
}

void artifact_release(ArtifactCache* cache, ArtifactTypeId typeId, ArtifactKey key) {
    if (!cache || typeId == ARTIFACT_TYPE_ID_ZERO || artifact_key_is_zero(key)) {
        return;
    }

    artifact_lock_(cache);
    ArtifactNode* node = artifact_node_from_type_key_locked_(cache, typeId, key, 0);
    if (node && node->retainCount != 0u) {
        node->retainCount -= 1u;
    }
    artifact_unlock_(cache);
}

static void artifact_submit_one_(ArtifactCache* cache, ArtifactQueuedJob job) {
    ArtifactJobParams params = {};
    params.cache = cache;
    params.slot = job.slot;
    params.slotGeneration = job.slotGeneration;
    params.generation = job.generation;

    if (cache->jobSystem) {
        job_system_submit((.function = artifact_build_job_), params);
    } else {
        artifact_build_job_(&params);
    }
}

static B32 artifact_prepare_submit_locked_(ArtifactCache* cache, ArtifactQueuedJob* outJob) {
    ArtifactQueuedJob job = {};
    if (!artifact_queue_pop_locked_(cache, &job)) {
        return 0;
    }

    ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots, job.slot, job.slotGeneration);
    if (!node || node->workingGeneration != job.generation ||
        node->status != ArtifactStatus_Queued ||
        ATOMIC_LOAD(&cache->shuttingDown, MEMORY_ORDER_ACQUIRE)) {
        return 0;
    }

    node->status = ArtifactStatus_Building;
    cache->activeJobCount += 1u;
    if (outJob) {
        *outJob = job;
    }
    return 1;
}

static void artifact_publish_completed_(ArtifactCache* cache, ArtifactCompletedJob completed) {
    ArtifactTypeDesc type = {};
    ArtifactKey key = ARTIFACT_KEY_ZERO;
    U8 requestData[ARTIFACT_REQUEST_DATA_MAX] = {};
    U32 requestDataSize = 0u;

    artifact_lock_(cache);
    ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots, completed.slot, completed.slotGeneration);
    if (!node || node->workingGeneration != completed.generation) {
        artifact_unlock_(cache);
        return;
    }

    ArtifactTypeDesc* typePtr = artifact_type_from_id_locked_(cache, node->typeId);
    if (!typePtr) {
        node->failedGeneration = completed.generation;
        node->status = (node->readyGeneration != 0u) ? ArtifactStatus_Ready : ArtifactStatus_Error;
        artifact_unlock_(cache);
        return;
    }

    if (completed.cancelled) {
        node->status = (node->readyGeneration != 0u) ? ArtifactStatus_Ready : ArtifactStatus_Cancelled;
        cache->stats.cancelled += 1u;
        artifact_unlock_(cache);
        return;
    }

    if (!completed.succeeded) {
        node->failedGeneration = completed.generation;
        node->status = (node->readyGeneration != 0u) ? ArtifactStatus_Ready : ArtifactStatus_Error;
        cache->stats.failed += 1u;
        artifact_unlock_(cache);
        return;
    }

    type = *typePtr;
    key = node->key;
    requestDataSize = node->requestDataSize;
    if (requestDataSize != 0u) {
        MEMCPY(requestData, node->requestData, requestDataSize);
    }
    node->status = ArtifactStatus_Publishing;
    artifact_unlock_(cache);

    ArtifactValue finalValue = completed.buildValue;
    U64 finalBytes = completed.bytes;
    B32 publishOk = 1;
    if (type.publishProc) {
        ArtifactPublishContext publishCtx = {};
        publishCtx.cache = cache;
        publishCtx.content = cache->content;
        publishCtx.typeUserData = type.userData;
        publishCtx.typeId = type.typeId;
        publishCtx.key = key;
        publishCtx.generation = completed.generation;
        publishCtx.requestData = requestData;
        publishCtx.requestDataSize = requestDataSize;
        finalValue = {};
        finalBytes = 0u;
        publishOk = type.publishProc(&publishCtx, completed.buildValue, &finalValue, &finalBytes);
    }

    ArtifactValue oldValue = {};
    U64 oldBytes = 0u;
    B32 destroyOld = 0;

    artifact_lock_(cache);
    node = (ArtifactNode*)slot_map_get(&cache->slots, completed.slot, completed.slotGeneration);
    if (!node || node->workingGeneration != completed.generation) {
        artifact_unlock_(cache);
        return;
    }

    if (publishOk) {
        if (node->readyGeneration != 0u) {
            oldValue = node->value;
            oldBytes = node->bytes;
            destroyOld = 1;
        }

        node->value = finalValue;
        node->readyGeneration = completed.generation;
        node->requestedGeneration = completed.generation;
        node->workingGeneration = 0u;
        node->failedGeneration = 0u;
        node->status = ArtifactStatus_Ready;
        if (finalBytes == 0u) {
            finalBytes = completed.bytes;
        }
        node->bytes = finalBytes;
        if (cache->stats.bytesLive >= oldBytes) {
            cache->stats.bytesLive -= oldBytes;
        } else {
            cache->stats.bytesLive = 0u;
        }
        cache->stats.bytesLive += finalBytes;
        cache->stats.built += 1u;
        cache->stats.published += 1u;
        cache->stats.buildTimeNsTotal += completed.buildTimeNs;
    } else {
        node->failedGeneration = completed.generation;
        node->workingGeneration = 0u;
        node->status = (node->readyGeneration != 0u) ? ArtifactStatus_Ready : ArtifactStatus_Error;
        cache->stats.failed += 1u;
    }
    artifact_unlock_(cache);

    if (destroyOld) {
        artifact_destroy_value_(&type, oldValue);
    }
}

void artifact_cache_tick(ArtifactCache* cache, U64 frameIndex, U32 maxSubmits, U32 maxPublishes) {
    if (!cache) {
        return;
    }

    for (U32 submitIndex = 0u; submitIndex < maxSubmits; ++submitIndex) {
        ArtifactQueuedJob job = {};
        artifact_lock_(cache);
        B32 haveJob = artifact_prepare_submit_locked_(cache, &job);
        artifact_unlock_(cache);
        if (!haveJob) {
            break;
        }

        artifact_submit_one_(cache, job);
    }

    for (U32 publishIndex = 0u; publishIndex < maxPublishes; ++publishIndex) {
        ArtifactCompletedJob completed = {};
        artifact_lock_(cache);
        B32 haveCompleted = artifact_completed_pop_locked_(cache, &completed);
        artifact_unlock_(cache);
        if (!haveCompleted) {
            break;
        }

        artifact_publish_completed_(cache, completed);
    }

    if (frameIndex != 0u) {
        ArtifactStats stats = artifact_cache_stats(cache);
        (void)stats;
    }
}

void artifact_cache_evict(ArtifactCache* cache, U64 frameIndex, U32 targetCount) {
    if (!cache) {
        return;
    }

    for (;;) {
        artifact_lock_(cache);
        if (cache->slots.count <= targetCount) {
            artifact_unlock_(cache);
            break;
        }

        U32 bestSlot = SLOT_MAP_INVALID_INDEX;
        U64 bestFrame = UINT64_MAX;
        for (U32 slot = 0u; slot < cache->slots.capacity; ++slot) {
            if (!slot_map_is_occupied(&cache->slots, slot)) {
                continue;
            }

            ArtifactNode* node = (ArtifactNode*)slot_map_item_at(&cache->slots, slot);
            if (!node ||
                node->lastTouchFrame == frameIndex ||
                node->retainCount != 0u ||
                artifact_status_is_working_(node->status)) {
                continue;
            }

            ArtifactTypeDesc* type = artifact_type_from_id_locked_(cache, node->typeId);
            if (type && type->evictionMaxIdleFrames != 0u &&
                node->lastTouchFrame + type->evictionMaxIdleFrames > frameIndex) {
                continue;
            }

            if (node->lastTouchFrame < bestFrame) {
                bestFrame = node->lastTouchFrame;
                bestSlot = slot;
            }
        }

        if (bestSlot == SLOT_MAP_INVALID_INDEX) {
            artifact_unlock_(cache);
            break;
        }

        ArtifactNode* node = (ArtifactNode*)slot_map_item_at(&cache->slots, bestSlot);
        ArtifactTypeDesc type = {};
        ArtifactTypeDesc* typePtr = artifact_type_from_id_locked_(cache, node ? node->typeId : 0u);
        if (typePtr) {
            type = *typePtr;
        }

        ArtifactValue value = {};
        B32 destroyValue = 0;
        if (node && node->readyGeneration != 0u) {
            value = node->value;
            destroyValue = 1;
            if (cache->stats.bytesLive >= node->bytes) {
                cache->stats.bytesLive -= node->bytes;
            } else {
                cache->stats.bytesLive = 0u;
            }
        }

        ArtifactTypeId typeId = node ? node->typeId : 0u;
        ArtifactKey key = node ? node->key : ARTIFACT_KEY_ZERO;
        artifact_table_remove_locked_(cache, typeId, key);
        slot_map_release(&cache->slots, bestSlot, cache->slots.generations[bestSlot], 0);
        cache->stats.evicted += 1u;
        artifact_unlock_(cache);

        if (destroyValue) {
            artifact_destroy_value_(&type, value);
        }
    }
}

ArtifactStats artifact_cache_stats(ArtifactCache* cache) {
    ArtifactStats result = {};
    if (!cache) {
        return result;
    }

    artifact_lock_(cache);
    result = cache->stats;
    result.liveCount = cache->slots.count;
    for (U32 slot = 0u; slot < cache->slots.capacity; ++slot) {
        if (!slot_map_is_occupied(&cache->slots, slot)) {
            continue;
        }

        ArtifactNode* node = (ArtifactNode*)slot_map_item_at(&cache->slots, slot);
        if (node && artifact_status_is_working_(node->status)) {
            result.workingCount += 1u;
        }
    }
    artifact_unlock_(cache);
    return result;
}

U32 artifact_debug_dump(ArtifactCache* cache, ArtifactDebugEntry* outEntries, U32 maxEntries) {
    U32 result = 0u;
    if (!cache || !outEntries || maxEntries == 0u) {
        return result;
    }

    artifact_lock_(cache);
    for (U32 slot = 0u; slot < cache->slots.capacity && result < maxEntries; ++slot) {
        if (!slot_map_is_occupied(&cache->slots, slot)) {
            continue;
        }

        ArtifactNode* node = (ArtifactNode*)slot_map_item_at(&cache->slots, slot);
        if (!node) {
            continue;
        }

        ArtifactDebugEntry* entry = outEntries + result;
        entry->typeId = node->typeId;
        entry->key = node->key;
        entry->generation = node->readyGeneration;
        entry->lastTouchFrame = node->lastTouchFrame;
        entry->bytes = node->bytes;
        entry->retainCount = node->retainCount;
        entry->status = node->status;
        result += 1u;
    }
    artifact_unlock_(cache);
    return result;
}

static void artifact_build_job_(void* params) {
    ArtifactJobParams job = *(ArtifactJobParams*)params;
    ArtifactCache* cache = job.cache;
    if (!cache) {
        return;
    }

    ArtifactTypeDesc type = {};
    ArtifactKey key = ARTIFACT_KEY_ZERO;
    U8 requestData[ARTIFACT_REQUEST_DATA_MAX] = {};
    U32 requestDataSize = 0u;
    U64* cancelFlag = 0;

    artifact_lock_(cache);
    ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots, job.slot, job.slotGeneration);
    if (!node || node->workingGeneration != job.generation ||
        ATOMIC_LOAD(&cache->shuttingDown, MEMORY_ORDER_ACQUIRE)) {
        if (cache->activeJobCount != 0u) {
            cache->activeJobCount -= 1u;
        }
        artifact_unlock_(cache);
        return;
    }

    ArtifactTypeDesc* typePtr = artifact_type_from_id_locked_(cache, node->typeId);
    if (!typePtr || !typePtr->buildProc) {
        node->failedGeneration = job.generation;
        node->status = (node->readyGeneration != 0u) ? ArtifactStatus_Ready : ArtifactStatus_Error;
        if (cache->activeJobCount != 0u) {
            cache->activeJobCount -= 1u;
        }
        artifact_unlock_(cache);
        return;
    }

    type = *typePtr;
    key = node->key;
    requestDataSize = node->requestDataSize;
    if (requestDataSize != 0u) {
        MEMCPY(requestData, node->requestData, requestDataSize);
    }
    cancelFlag = &node->cancelFlag;
    artifact_unlock_(cache);

    U64 startNs = OS_get_time_nanoseconds();
    ArtifactValue buildValue = {};
    U64 bytes = 0u;
    B32 cancelled = (ATOMIC_LOAD(cancelFlag, MEMORY_ORDER_ACQUIRE) != 0u) ? 1 : 0;
    B32 succeeded = 0;
    if (!cancelled) {
        ArtifactBuildContext buildCtx = {};
        buildCtx.cache = cache;
        buildCtx.content = cache->content;
        buildCtx.typeUserData = type.userData;
        buildCtx.typeId = type.typeId;
        buildCtx.key = key;
        buildCtx.generation = job.generation;
        buildCtx.requestData = requestData;
        buildCtx.requestDataSize = requestDataSize;
        buildCtx.cancelFlag = cancelFlag;
        succeeded = type.buildProc(&buildCtx, &buildValue, &bytes);
        cancelled = (ATOMIC_LOAD(cancelFlag, MEMORY_ORDER_ACQUIRE) != 0u) ? 1 : 0;
    }
    U64 endNs = OS_get_time_nanoseconds();

    ArtifactCompletedJob completed = {};
    completed.slot = job.slot;
    completed.slotGeneration = job.slotGeneration;
    completed.generation = job.generation;
    completed.buildValue = buildValue;
    completed.bytes = bytes;
    completed.buildTimeNs = endNs - startNs;
    completed.succeeded = succeeded;
    completed.cancelled = cancelled;

    artifact_lock_(cache);
    if (!artifact_completed_push_(cache, completed)) {
        node = (ArtifactNode*)slot_map_get(&cache->slots, job.slot, job.slotGeneration);
        if (node && node->workingGeneration == job.generation) {
            node->failedGeneration = job.generation;
            node->status = (node->readyGeneration != 0u) ? ArtifactStatus_Ready : ArtifactStatus_Error;
        }
        cache->stats.failed += 1u;
    }
    if (cache->activeJobCount != 0u) {
        cache->activeJobCount -= 1u;
    }
    artifact_unlock_(cache);
}
