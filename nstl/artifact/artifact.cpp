struct ArtifactNode {
    U32 typeId;
    StringU8 key;
    U64 keyHash;

    ArtifactPayload payload;

    ArtifactStatus status;
    U32 touchCount;
    U32 inFlight;
    U64 lastTouchSerial;
    U64 publishedGeneration;
    U64 failedGeneration;
    U32 acquireFlags;
    ArtifactReloadPolicy reloadPolicy;
    U64 lastReloadCheckNs;
    U64 lastWriteTimestampNs;
    U64 lastWriteSize;
    U64 sourceGeneration;
    U64 dirtySourceGeneration;
    U64 queuedSourceGeneration;
    U64 loadingSourceGeneration;
    U64 dirtyWriteTimestampNs;
    U64 dirtyFileSize;
    U64 failedSourceGeneration;
    B32 reloadFailed;

    ArtifactReleaseProc* releaseProc;
    void* releaseUserData;
};

struct ArtifactReloadDirtyEntry {
    ArtifactHandle handle;
    U64 sourceGeneration;
    U64 writeTimestampNs;
    U64 fileSize;
};

struct ArtifactReloadScanCandidate {
    ArtifactHandle handle;
    StringU8 key;
};

enum ArtifactHashEntryState {
    ArtifactHashEntryState_Empty = 0,
    ArtifactHashEntryState_Occupied,
    ArtifactHashEntryState_Tombstone,
};

struct ArtifactHashEntry {
    U64 keyHash;
    U32 typeId;
    ArtifactHandle handle;
    U32 state;
};

struct ArtifactCache {
    Arena* arena;
    JobSystem* jobSystem;

    SlotMap slots;

    ArtifactTypeOps* typeOps;
    U8* typeRegistered;
    U32 maxTypeId;

    ArtifactHashEntry* hashEntries;
    U32 hashCapacity;
    U32 hashCount;
    U32 hashTombstones;

    U64 budgetBytes;
    U64 residentBytes;
    U64 touchSerial;
    U32 reloadScanCursor;
    U32 reloadScannerBatchCount;
    U32 reloadScannerSleepMs;
    U32 reloadScannerShutdown;
    U32 reloadScannerRunning;
    OS_Handle reloadScannerThread;
    ArtifactReloadDirtyEntry* reloadDirtyQueue;
    U32 reloadDirtyQueueCapacity;
    U32 reloadDirtyRead;
    U32 reloadDirtyWrite;
    U32 reloadDirtyCount;

    U32 pendingAsyncJobs;

    OS_Handle mutex;
    OS_Handle loadArenaMutex;
    OS_Handle condition;
};

struct ArtifactAsyncJobParams {
    ArtifactCache* cache;
    ArtifactHandle handle;
};

static_assert(sizeof(ArtifactAsyncJobParams) <= JOB_PARAMETER_SPACE,
              "ArtifactAsyncJobParams exceeds job inline storage");

enum ArtifactRawFileLoadStatus {
    ArtifactRawFileLoadStatus_Error = 0,
    ArtifactRawFileLoadStatus_Ready,
    ArtifactRawFileLoadStatus_Retry,
};

#define ARTIFACT_RELOAD_SCANNER_BATCH_MAX 64u

static U64 artifact_hash_key_(U32 typeId, StringU8 key);
static U32 artifact_hash_capacity_from_requested_(U32 requested);
static B32 artifact_hash_rebuild_(ArtifactCache* cache, U32 requestedCapacity);
static B32 artifact_hash_ensure_insert_capacity_(ArtifactCache* cache);
static void artifact_hash_mark_tombstone_(ArtifactCache* cache, U32 index);
static B32 artifact_hash_find_index_locked_(ArtifactCache* cache,
                                            U32 typeId,
                                            StringU8 key,
                                            U64 keyHash,
                                            U32* outIndex,
                                            B32* outFound);
static B32 artifact_hash_insert_locked_(ArtifactCache* cache,
                                        U32 typeId,
                                        StringU8 key,
                                        U64 keyHash,
                                        ArtifactHandle handle);
static void artifact_hash_remove_locked_(ArtifactCache* cache, U32 typeId, StringU8 key, U64 keyHash);
static void artifact_release_node_payload_locked_(ArtifactNode* node);
static void artifact_release_payload_(ArtifactReleaseProc* releaseProc, void* releaseUserData,
                                      U32 typeId, StringU8 key, ArtifactPayload payload);
static ArtifactRawFileLoadStatus artifact_load_raw_file_(Arena* arena, StringU8 key, ArtifactPayload* outPayload,
                                                         U64* outWriteTimestampNs, U64* outFileSize);
static B32 artifact_reload_enqueue_dirty_locked_(ArtifactCache* cache, ArtifactHandle handle, ArtifactNode* node);
static B32 artifact_reload_pop_dirty_locked_(ArtifactCache* cache, ArtifactReloadDirtyEntry* outEntry);
static void artifact_reload_scanner_thread_(void* userData);
static void artifact_reload_scanner_stop_(ArtifactCache* cache);
static void artifact_finish_async_job_locked_(ArtifactCache* cache, B32 countAsAsyncJob);
static ArtifactStatus artifact_run_load_for_handle_(ArtifactCache* cache, ArtifactHandle handle, B32 countAsAsyncJob);
static void artifact_async_load_job_(void* parameters);
static void artifact_scope_touch_(ArtifactUseScope* scope, ArtifactHandle handle);

static U64 artifact_hash_key_(U32 typeId, StringU8 key) {
    U64 hash = 1469598103934665603ull;

    U8 typeBytes[4] = {
        (U8)(typeId & 0xFFu),
        (U8)((typeId >> 8) & 0xFFu),
        (U8)((typeId >> 16) & 0xFFu),
        (U8)((typeId >> 24) & 0xFFu),
    };

    for (U32 i = 0; i < 4u; ++i) {
        hash ^= (U64)typeBytes[i];
        hash *= 1099511628211ull;
    }

    for (U64 i = 0; i < key.size; ++i) {
        hash ^= (U64)key.data[i];
        hash *= 1099511628211ull;
    }

    if (hash == 0u) {
        hash = 1u;
    }

    return hash;
}

static U32 artifact_hash_capacity_from_requested_(U32 requested) {
    U32 capacity = (requested < 64u) ? 64u : requested;
    if (!is_power_of_two(capacity)) {
        U32 next = 1u;
        while (next < capacity) {
            next <<= 1u;
        }
        capacity = next;
    }
    return capacity;
}

static B32 artifact_hash_rebuild_(ArtifactCache* cache, U32 requestedCapacity) {
    ASSERT_ALWAYS(cache != 0);

    U32 newCapacity = artifact_hash_capacity_from_requested_(requestedCapacity);
    ArtifactHashEntry* newEntries = ARENA_PUSH_ARRAY(cache->arena, ArtifactHashEntry, newCapacity);
    if (!newEntries) {
        return 0;
    }

    MEMSET(newEntries, 0, sizeof(ArtifactHashEntry) * newCapacity);

    ArtifactHashEntry* oldEntries = cache->hashEntries;
    U32 oldCapacity = cache->hashCapacity;

    cache->hashEntries = newEntries;
    cache->hashCapacity = newCapacity;
    cache->hashCount = 0u;
    cache->hashTombstones = 0u;

    if (!oldEntries || oldCapacity == 0u) {
        return 1;
    }

    for (U32 i = 0; i < oldCapacity; ++i) {
        ArtifactHashEntry* oldEntry = &oldEntries[i];
        if (oldEntry->state != ArtifactHashEntryState_Occupied) {
            continue;
        }

        ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots,
                                                          oldEntry->handle.slot,
                                                          oldEntry->handle.generation);
        if (!node) {
            continue;
        }

        if (!artifact_hash_insert_locked_(cache,
                                          node->typeId,
                                          node->key,
                                          node->keyHash,
                                          oldEntry->handle)) {
            return 0;
        }
    }

    return 1;
}

static B32 artifact_hash_ensure_insert_capacity_(ArtifactCache* cache) {
    ASSERT_ALWAYS(cache != 0);

    if (!cache->hashEntries || cache->hashCapacity == 0u) {
        return artifact_hash_rebuild_(cache, 64u);
    }

    U64 loadNumerator = (U64)cache->hashCount + (U64)cache->hashTombstones + 1ull;
    U64 loadPercent = (loadNumerator * 100ull) / (U64)cache->hashCapacity;

    if (loadPercent >= 70ull) {
        return artifact_hash_rebuild_(cache, cache->hashCapacity * 2u);
    }

    if (cache->hashTombstones > cache->hashCount) {
        return artifact_hash_rebuild_(cache, cache->hashCapacity);
    }

    return 1;
}

static void artifact_hash_mark_tombstone_(ArtifactCache* cache, U32 index) {
    ASSERT_ALWAYS(cache != 0);
    ASSERT_ALWAYS(index < cache->hashCapacity);

    ArtifactHashEntry* entry = &cache->hashEntries[index];
    if (entry->state == ArtifactHashEntryState_Occupied) {
        entry->state = ArtifactHashEntryState_Tombstone;
        if (cache->hashCount > 0u) {
            cache->hashCount -= 1u;
        }
        cache->hashTombstones += 1u;
    }
}

static B32 artifact_hash_find_index_locked_(ArtifactCache* cache,
                                            U32 typeId,
                                            StringU8 key,
                                            U64 keyHash,
                                            U32* outIndex,
                                            B32* outFound) {
    ASSERT_ALWAYS(cache != 0);
    ASSERT_ALWAYS(outIndex != 0);
    ASSERT_ALWAYS(outFound != 0);

    *outFound = 0;
    *outIndex = 0u;

    if (!cache->hashEntries || cache->hashCapacity == 0u) {
        return 0;
    }

    U32 firstTombstone = SLOT_MAP_INVALID_INDEX;
    U32 mask = cache->hashCapacity - 1u;
    U32 start = (U32)(keyHash & (U64)mask);

    for (U32 probe = 0; probe < cache->hashCapacity; ++probe) {
        U32 index = (start + probe) & mask;
        ArtifactHashEntry* entry = &cache->hashEntries[index];

        if (entry->state == ArtifactHashEntryState_Empty) {
            *outFound = 0;
            *outIndex = (firstTombstone != SLOT_MAP_INVALID_INDEX) ? firstTombstone : index;
            return 1;
        }

        if (entry->state == ArtifactHashEntryState_Tombstone) {
            if (firstTombstone == SLOT_MAP_INVALID_INDEX) {
                firstTombstone = index;
            }
            continue;
        }

        ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots,
                                                          entry->handle.slot,
                                                          entry->handle.generation);
        if (!node) {
            artifact_hash_mark_tombstone_(cache, index);
            if (firstTombstone == SLOT_MAP_INVALID_INDEX) {
                firstTombstone = index;
            }
            continue;
        }

        if (entry->typeId != typeId) {
            continue;
        }
        if (entry->keyHash != keyHash) {
            continue;
        }
        if (node->typeId != typeId || node->keyHash != keyHash) {
            continue;
        }
        if (!str8_equal(node->key, key)) {
            continue;
        }

        *outFound = 1;
        *outIndex = index;
        return 1;
    }

    if (firstTombstone != SLOT_MAP_INVALID_INDEX) {
        *outFound = 0;
        *outIndex = firstTombstone;
        return 1;
    }

    return 0;
}

static B32 artifact_hash_insert_locked_(ArtifactCache* cache,
                                        U32 typeId,
                                        StringU8 key,
                                        U64 keyHash,
                                        ArtifactHandle handle) {
    ASSERT_ALWAYS(cache != 0);

    if (!artifact_hash_ensure_insert_capacity_(cache)) {
        return 0;
    }

    U32 index = 0u;
    B32 found = 0;
    if (!artifact_hash_find_index_locked_(cache, typeId, key, keyHash, &index, &found)) {
        return 0;
    }

    ArtifactHashEntry* entry = &cache->hashEntries[index];
    if (!found) {
        if (entry->state == ArtifactHashEntryState_Tombstone) {
            if (cache->hashTombstones > 0u) {
                cache->hashTombstones -= 1u;
            }
        }
        entry->state = ArtifactHashEntryState_Occupied;
        cache->hashCount += 1u;
    }

    entry->typeId = typeId;
    entry->keyHash = keyHash;
    entry->handle = handle;

    return 1;
}

static void artifact_hash_remove_locked_(ArtifactCache* cache, U32 typeId, StringU8 key, U64 keyHash) {
    ASSERT_ALWAYS(cache != 0);

    if (!cache->hashEntries || cache->hashCapacity == 0u) {
        return;
    }

    U32 index = 0u;
    B32 found = 0;
    if (!artifact_hash_find_index_locked_(cache, typeId, key, keyHash, &index, &found)) {
        return;
    }

    if (!found) {
        return;
    }

    artifact_hash_mark_tombstone_(cache, index);
}

static void artifact_release_payload_(ArtifactReleaseProc* releaseProc, void* releaseUserData,
                                      U32 typeId, StringU8 key, ArtifactPayload payload) {
    if (!payload.data) {
        return;
    }

    if (releaseProc) {
        releaseProc(releaseUserData, typeId, key, payload);
    } else if (payload.arena) {
        arena_release(payload.arena);
    }
}

static void artifact_release_node_payload_locked_(ArtifactNode* node) {
    ASSERT_ALWAYS(node != 0);

    if (node->status != ArtifactStatus_Ready || !node->payload.data) {
        return;
    }

    artifact_release_payload_(node->releaseProc, node->releaseUserData, node->typeId, node->key, node->payload);

    node->payload.data = 0;
    node->payload.size = 0u;
    node->payload.arena = 0;
}

static B32 artifact_reload_enqueue_dirty_locked_(ArtifactCache* cache, ArtifactHandle handle, ArtifactNode* node) {
    ASSERT_ALWAYS(cache != 0);
    ASSERT_ALWAYS(node != 0);

    if (!cache->reloadDirtyQueue || cache->reloadDirtyQueueCapacity == 0u) {
        return 0;
    }
    if (node->dirtySourceGeneration == 0u ||
        node->queuedSourceGeneration == node->dirtySourceGeneration) {
        return 0;
    }
    if (cache->reloadDirtyCount >= cache->reloadDirtyQueueCapacity) {
        return 0;
    }

    ArtifactReloadDirtyEntry* entry = &cache->reloadDirtyQueue[cache->reloadDirtyWrite];
    entry->handle = handle;
    entry->sourceGeneration = node->dirtySourceGeneration;
    entry->writeTimestampNs = node->dirtyWriteTimestampNs;
    entry->fileSize = node->dirtyFileSize;

    cache->reloadDirtyWrite = (cache->reloadDirtyWrite + 1u) % cache->reloadDirtyQueueCapacity;
    cache->reloadDirtyCount += 1u;
    node->queuedSourceGeneration = node->dirtySourceGeneration;
    return 1;
}

static B32 artifact_reload_pop_dirty_locked_(ArtifactCache* cache, ArtifactReloadDirtyEntry* outEntry) {
    ASSERT_ALWAYS(cache != 0);
    ASSERT_ALWAYS(outEntry != 0);

    if (!cache->reloadDirtyQueue || cache->reloadDirtyQueueCapacity == 0u || cache->reloadDirtyCount == 0u) {
        return 0;
    }

    *outEntry = cache->reloadDirtyQueue[cache->reloadDirtyRead];
    cache->reloadDirtyRead = (cache->reloadDirtyRead + 1u) % cache->reloadDirtyQueueCapacity;
    cache->reloadDirtyCount -= 1u;
    return 1;
}

static ArtifactRawFileLoadStatus artifact_load_raw_file_(Arena* arena, StringU8 key, ArtifactPayload* outPayload,
                                                         U64* outWriteTimestampNs, U64* outFileSize) {
    (void)arena;
    if (outWriteTimestampNs) {
        *outWriteTimestampNs = 0u;
    }
    if (outFileSize) {
        *outFileSize = 0u;
    }
    if (outPayload == 0 || key.data == 0 || key.size == 0u) {
        return ArtifactRawFileLoadStatus_Error;
    }

    OS_FileInfo preInfo = OS_get_file_info((const char*)key.data);
    if (!preInfo.exists) {
        LOG_ERROR("artifact", "Raw file missing '{}'", key);
        return ArtifactRawFileLoadStatus_Error;
    }

    OS_Handle file = OS_file_open((const char*) key.data, OS_FileOpenMode_Read);
    if (!file.handle) {
        LOG_WARNING("artifact", "Raw file '{}' could not be opened; retrying later", key);
        return ArtifactRawFileLoadStatus_Retry;
    }

    U64 fileSize = preInfo.size;
    Arena* payloadArena = arena_alloc();
    if (!payloadArena) {
        OS_file_close(file);
        return ArtifactRawFileLoadStatus_Error;
    }

    U8* data = ARENA_PUSH_ARRAY(payloadArena, U8, fileSize + 1u);
    if (data == 0) {
        OS_file_close(file);
        arena_release(payloadArena);
        return ArtifactRawFileLoadStatus_Error;
    }

    U64 readSize = 0u;
    if (fileSize != 0u) {
        RangeU64 range = {0u, fileSize};
        readSize = OS_file_read(file, range, data);
    }

    OS_file_close(file);

    OS_FileInfo postInfo = OS_get_file_info((const char*)key.data);
    if (readSize != fileSize) {
        LOG_WARNING("artifact", "Raw file '{}' changed during read; retrying later", key);
        arena_release(payloadArena);
        return ArtifactRawFileLoadStatus_Retry;
    }
    if (!postInfo.exists ||
        postInfo.lastWriteTimestampNs != preInfo.lastWriteTimestampNs ||
        postInfo.size != preInfo.size) {
        LOG_WARNING("artifact", "Raw file '{}' was not stable during read; retrying later", key);
        arena_release(payloadArena);
        return ArtifactRawFileLoadStatus_Retry;
    }

    data[fileSize] = 0;
    outPayload->data = data;
    outPayload->size = fileSize;
    outPayload->arena = payloadArena;
    if (outWriteTimestampNs) {
        *outWriteTimestampNs = postInfo.lastWriteTimestampNs;
    }
    if (outFileSize) {
        *outFileSize = postInfo.size;
    }
    return ArtifactRawFileLoadStatus_Ready;
}

static void artifact_reload_scanner_thread_(void* userData) {
    ArtifactCache* cache = (ArtifactCache*)userData;
    if (!cache) {
        return;
    }

    ArtifactReloadScanCandidate candidates[ARTIFACT_RELOAD_SCANNER_BATCH_MAX] = {};

    for (;;) {
        U32 candidateCount = 0u;

        OS_mutex_lock(cache->mutex);
        if (cache->reloadScannerShutdown) {
            cache->reloadScannerRunning = 0u;
            OS_condition_variable_broadcast(cache->condition);
            OS_mutex_unlock(cache->mutex);
            return;
        }

        U32 capacity = cache->slots.capacity;
        U32 batchCount = cache->reloadScannerBatchCount;
        if (batchCount == 0u) {
            batchCount = ARTIFACT_RELOAD_SCANNER_BATCH_DEFAULT;
        }
        if (batchCount > ARTIFACT_RELOAD_SCANNER_BATCH_MAX) {
            batchCount = ARTIFACT_RELOAD_SCANNER_BATCH_MAX;
        }

        U64 nowNs = OS_get_time_nanoseconds();
        U32 attempts = 0u;
        while (candidateCount < batchCount && attempts < capacity) {
            U32 slot = (cache->reloadScanCursor + attempts) % capacity;
            attempts += 1u;
            cache->reloadScanCursor = (slot + 1u) % capacity;

            if (!slot_map_is_occupied(&cache->slots, slot)) {
                continue;
            }

            ArtifactNode* node = (ArtifactNode*)slot_map_item_at(&cache->slots, slot);
            if (!node || !(node->acquireFlags & ArtifactAcquireFlags_Reloadable)) {
                continue;
            }
            if (node->typeId > cache->maxTypeId || !cache->typeRegistered[node->typeId]) {
                continue;
            }

            ArtifactTypeOps ops = cache->typeOps[node->typeId];
            if (ops.kind != ArtifactTypeKind_RawFile) {
                continue;
            }

            ArtifactHandle handle = {};
            handle.slot = slot;
            handle.generation = cache->slots.generations[slot];

            if (node->dirtySourceGeneration != 0u &&
                node->queuedSourceGeneration != node->dirtySourceGeneration) {
                artifact_reload_enqueue_dirty_locked_(cache, handle, node);
                continue;
            }

            if (node->inFlight != 0u) {
                continue;
            }

            U64 intervalNs = node->reloadPolicy.checkIntervalNs ?
                node->reloadPolicy.checkIntervalNs :
                ARTIFACT_RELOAD_CHECK_INTERVAL_DEFAULT_NS;
            if (node->lastReloadCheckNs != 0u && nowNs - node->lastReloadCheckNs < intervalNs) {
                continue;
            }

            node->lastReloadCheckNs = nowNs;
            candidates[candidateCount].handle = handle;
            candidates[candidateCount].key = node->key;
            candidateCount += 1u;
        }
        OS_mutex_unlock(cache->mutex);

        for (U32 candidateIndex = 0u; candidateIndex < candidateCount; ++candidateIndex) {
            ArtifactReloadScanCandidate candidate = candidates[candidateIndex];
            OS_FileInfo info = OS_get_file_info((const char*)candidate.key.data);

            OS_mutex_lock(cache->mutex);
            ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots,
                                                              candidate.handle.slot,
                                                              candidate.handle.generation);
            if (!node ||
                !(node->acquireFlags & ArtifactAcquireFlags_Reloadable) ||
                node->typeId > cache->maxTypeId ||
                !cache->typeRegistered[node->typeId]) {
                OS_mutex_unlock(cache->mutex);
                continue;
            }

            ArtifactTypeOps ops = cache->typeOps[node->typeId];
            if (ops.kind != ArtifactTypeKind_RawFile) {
                OS_mutex_unlock(cache->mutex);
                continue;
            }

            U64 writeTimestampNs = info.exists ? info.lastWriteTimestampNs : 0u;
            U64 fileSize = info.exists ? info.size : 0u;
            B32 changed = (writeTimestampNs != node->lastWriteTimestampNs ||
                           fileSize != node->lastWriteSize) ? 1 : 0;
            B32 alreadyDirtyForFacts =
                (node->dirtySourceGeneration != 0u &&
                 node->dirtyWriteTimestampNs == writeTimestampNs &&
                 node->dirtyFileSize == fileSize);

            if (changed && !alreadyDirtyForFacts) {
                node->sourceGeneration += 1u;
                if (node->sourceGeneration == 0u) {
                    node->sourceGeneration = 1u;
                }
                node->dirtySourceGeneration = node->sourceGeneration;
                node->dirtyWriteTimestampNs = writeTimestampNs;
                node->dirtyFileSize = fileSize;
                node->queuedSourceGeneration = 0u;
                node->reloadFailed = 0;
            }

            if (node->dirtySourceGeneration != 0u &&
                node->queuedSourceGeneration != node->dirtySourceGeneration) {
                artifact_reload_enqueue_dirty_locked_(cache, candidate.handle, node);
            }

            OS_mutex_unlock(cache->mutex);
        }

        U32 sleepMs = cache->reloadScannerSleepMs;
        if (sleepMs == 0u) {
            sleepMs = ARTIFACT_RELOAD_SCANNER_SLEEP_DEFAULT_MS;
        }
        OS_sleep_milliseconds(sleepMs);
    }
}

static void artifact_reload_scanner_stop_(ArtifactCache* cache) {
    if (!cache || !cache->reloadScannerThread.handle) {
        return;
    }

    OS_mutex_lock(cache->mutex);
    cache->reloadScannerShutdown = 1u;
    OS_mutex_unlock(cache->mutex);

    OS_thread_join(cache->reloadScannerThread);
    cache->reloadScannerThread.handle = 0;
    cache->reloadScannerRunning = 0u;
}

static void artifact_finish_async_job_locked_(ArtifactCache* cache, B32 countAsAsyncJob) {
    if (!countAsAsyncJob) {
        return;
    }

    ASSERT_DEBUG(cache != 0);
    ASSERT_DEBUG(cache->pendingAsyncJobs > 0u);
    if (cache && cache->pendingAsyncJobs > 0u) {
        cache->pendingAsyncJobs -= 1u;
    }
}

static ArtifactStatus artifact_run_load_for_handle_(ArtifactCache* cache, ArtifactHandle handle, B32 countAsAsyncJob) {
    if (!cache || ARTIFACT_HANDLE_IS_INVALID(handle)) {
        return ArtifactStatus_InvalidHandle;
    }

    ArtifactTypeOps ops = {};
    StringU8 key = STR8_NIL;
    U32 typeId = 0u;

    OS_mutex_lock(cache->mutex);

    ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots, handle.slot, handle.generation);
    if (!node) {
        artifact_finish_async_job_locked_(cache, countAsAsyncJob);
        OS_condition_variable_broadcast(cache->condition);
        OS_mutex_unlock(cache->mutex);
        return ArtifactStatus_InvalidHandle;
    }

    if (node->inFlight == 0u ||
        (node->status != ArtifactStatus_Pending &&
         node->status != ArtifactStatus_Ready &&
         node->status != ArtifactStatus_Error)) {
        ArtifactStatus status = node->status;
        artifact_finish_async_job_locked_(cache, countAsAsyncJob);
        OS_condition_variable_broadcast(cache->condition);
        OS_mutex_unlock(cache->mutex);
        return status;
    }

    typeId = node->typeId;
    key = node->key;
    U64 loadSourceGeneration = node->loadingSourceGeneration;

    if (typeId <= cache->maxTypeId && cache->typeRegistered[typeId]) {
        ops = cache->typeOps[typeId];
    }

    OS_mutex_unlock(cache->mutex);

    U64 loadWriteTimestampNs = 0u;
    U64 loadFileSize = 0u;
    ArtifactPayload loadedPayload = {};
    B32 loadOk = 0;
    B32 loadRetry = 0;
    if (ops.kind == ArtifactTypeKind_RawFile) {
        if (cache->loadArenaMutex.handle) {
            OS_mutex_lock(cache->loadArenaMutex);
        }
        ArtifactRawFileLoadStatus rawStatus = artifact_load_raw_file_(cache->arena,
                                                                       key,
                                                                       &loadedPayload,
                                                                       &loadWriteTimestampNs,
                                                                       &loadFileSize);
        if (cache->loadArenaMutex.handle) {
            OS_mutex_unlock(cache->loadArenaMutex);
        }
        loadOk = (rawStatus == ArtifactRawFileLoadStatus_Ready) ? 1 : 0;
        loadRetry = (rawStatus == ArtifactRawFileLoadStatus_Retry) ? 1 : 0;
    } else if (ops.load) {
        if (cache->loadArenaMutex.handle) {
            OS_mutex_lock(cache->loadArenaMutex);
        }
        loadOk = ops.load(ops.userData, cache->arena, typeId, key, &loadedPayload);
        if (cache->loadArenaMutex.handle) {
            OS_mutex_unlock(cache->loadArenaMutex);
        }
    }

    ArtifactStatus finalStatus = loadOk ? ArtifactStatus_Ready : ArtifactStatus_Error;
    ArtifactPayload stalePayloadToRelease = {};
    ArtifactReleaseProc* staleReleaseProc = 0;
    void* staleReleaseUserData = 0;

    OS_mutex_lock(cache->mutex);

    ArtifactNode* commitNode = (ArtifactNode*)slot_map_get(&cache->slots, handle.slot, handle.generation);
    if (!commitNode) {
        if (loadOk) {
            stalePayloadToRelease = loadedPayload;
            staleReleaseProc = ops.release;
            staleReleaseUserData = ops.userData;
        }
        artifact_finish_async_job_locked_(cache, countAsAsyncJob);
        OS_condition_variable_broadcast(cache->condition);
        OS_mutex_unlock(cache->mutex);

        if (stalePayloadToRelease.data) {
            artifact_release_payload_(staleReleaseProc, staleReleaseUserData, typeId, key, stalePayloadToRelease);
        }
        return ArtifactStatus_InvalidHandle;
    }

    if (commitNode->inFlight != 0u) {
        commitNode->inFlight = 0u;

        if (loadOk) {
            if (commitNode->status == ArtifactStatus_Ready && commitNode->payload.data) {
                stalePayloadToRelease = commitNode->payload;
                staleReleaseProc = commitNode->releaseProc;
                staleReleaseUserData = commitNode->releaseUserData;
                if (cache->residentBytes >= commitNode->payload.size) {
                    cache->residentBytes -= commitNode->payload.size;
                } else {
                    cache->residentBytes = 0u;
                }
            }

            commitNode->payload = loadedPayload;
            commitNode->status = ArtifactStatus_Ready;
            commitNode->releaseProc = ops.release;
            commitNode->releaseUserData = ops.userData;
            commitNode->lastTouchSerial = ++cache->touchSerial;
            commitNode->publishedGeneration += 1u;
            if (commitNode->publishedGeneration == 0u) {
                commitNode->publishedGeneration = 1u;
            }
            commitNode->lastWriteTimestampNs = loadWriteTimestampNs;
            commitNode->lastWriteSize = loadFileSize;
            if (loadSourceGeneration != 0u &&
                commitNode->dirtySourceGeneration <= loadSourceGeneration) {
                commitNode->dirtySourceGeneration = 0u;
                commitNode->queuedSourceGeneration = 0u;
                commitNode->dirtyWriteTimestampNs = 0u;
                commitNode->dirtyFileSize = 0u;
            }
            commitNode->loadingSourceGeneration = 0u;
            commitNode->failedSourceGeneration = 0u;
            commitNode->reloadFailed = 0;
            cache->residentBytes += loadedPayload.size;
            finalStatus = ArtifactStatus_Ready;
        } else if (loadRetry && loadSourceGeneration != 0u) {
            commitNode->queuedSourceGeneration = 0u;
            commitNode->loadingSourceGeneration = 0u;
            if (commitNode->status != ArtifactStatus_Ready || !commitNode->payload.data) {
                commitNode->status = ArtifactStatus_Error;
                commitNode->reloadFailed = 1;
                finalStatus = ArtifactStatus_Error;
            } else {
                finalStatus = ArtifactStatus_Pending;
            }
        } else {
            if (commitNode->status != ArtifactStatus_Ready || !commitNode->payload.data) {
                commitNode->payload = {};
                commitNode->status = ArtifactStatus_Error;
                commitNode->publishedGeneration = 0u;
            }
            commitNode->reloadFailed = 1;
            commitNode->failedGeneration = commitNode->publishedGeneration;
            commitNode->failedSourceGeneration = loadSourceGeneration;
            if (loadSourceGeneration != 0u &&
                commitNode->dirtySourceGeneration <= loadSourceGeneration) {
                commitNode->dirtySourceGeneration = 0u;
                commitNode->queuedSourceGeneration = 0u;
                commitNode->dirtyWriteTimestampNs = 0u;
                commitNode->dirtyFileSize = 0u;
            }
            commitNode->loadingSourceGeneration = 0u;
            commitNode->lastWriteTimestampNs = loadWriteTimestampNs;
            commitNode->lastWriteSize = loadFileSize;
            finalStatus = ArtifactStatus_Error;
        }
    } else {
        if (loadOk) {
            stalePayloadToRelease = loadedPayload;
            staleReleaseProc = ops.release;
            staleReleaseUserData = ops.userData;
        }
        finalStatus = commitNode->status;
    }

    artifact_finish_async_job_locked_(cache, countAsAsyncJob);

    OS_condition_variable_broadcast(cache->condition);
    OS_mutex_unlock(cache->mutex);

    if (stalePayloadToRelease.data) {
        artifact_release_payload_(staleReleaseProc, staleReleaseUserData, typeId, key, stalePayloadToRelease);
    }

    return finalStatus;
}

static void artifact_async_load_job_(void* parameters) {
    ArtifactAsyncJobParams* params = (ArtifactAsyncJobParams*)parameters;
    if (!params || !params->cache) {
        return;
    }

    artifact_run_load_for_handle_(params->cache, params->handle, 1);
}

static void artifact_scope_touch_(ArtifactUseScope* scope, ArtifactHandle handle) {
    if (!scope || !scope->cache || !scope->arena || ARTIFACT_HANDLE_IS_INVALID(handle)) {
        return;
    }

    if (scope->touchedCount >= scope->touchedCapacity) {
        U32 nextCapacity = (scope->touchedCapacity == 0u) ? 32u : (scope->touchedCapacity * 2u);
        ArtifactTouchedEntry* next = ARENA_PUSH_ARRAY(scope->arena, ArtifactTouchedEntry, nextCapacity);
        ASSERT_ALWAYS(next != 0);
        if (!next) {
            return;
        }

        if (scope->touchedCount > 0u && scope->touched) {
            MEMCPY(next, scope->touched, sizeof(ArtifactTouchedEntry) * scope->touchedCount);
        }

        scope->touched = next;
        scope->touchedCapacity = nextCapacity;
    }

    scope->touched[scope->touchedCount].handle = handle;
    scope->touchedCount += 1u;

    ArtifactCache* cache = scope->cache;
    OS_mutex_lock(cache->mutex);
    ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots, handle.slot, handle.generation);
    if (node) {
        node->touchCount += 1u;
        node->lastTouchSerial = ++cache->touchSerial;
    }
    OS_mutex_unlock(cache->mutex);
}

B32 artifact_cache_create(const ArtifactCacheDesc* desc, ArtifactCache* outCache) {
    if (!desc || !outCache || !desc->arena) {
        return 0;
    }
    if (desc->structSize != sizeof(ArtifactCacheDesc)) {
        return 0;
    }
    if (desc->apiVersion != ARTIFACT_CACHE_API_VERSION) {
        return 0;
    }

    MEMSET(outCache, 0, sizeof(*outCache));

    outCache->arena = desc->arena;
    outCache->jobSystem = desc->jobSystem;
    outCache->budgetBytes = desc->budgetBytes;
    outCache->maxTypeId = desc->maxTypeId;
    outCache->reloadScannerBatchCount = desc->reloadScannerBatchCount ?
        desc->reloadScannerBatchCount :
        ARTIFACT_RELOAD_SCANNER_BATCH_DEFAULT;
    if (outCache->reloadScannerBatchCount > ARTIFACT_RELOAD_SCANNER_BATCH_MAX) {
        outCache->reloadScannerBatchCount = ARTIFACT_RELOAD_SCANNER_BATCH_MAX;
    }
    outCache->reloadScannerSleepMs = desc->reloadScannerSleepMs ?
        desc->reloadScannerSleepMs :
        ARTIFACT_RELOAD_SCANNER_SLEEP_DEFAULT_MS;

    if (outCache->maxTypeId == 0u) {
        outCache->maxTypeId = 256u;
    }

    U32 slotCapacity = (desc->initialSlotCapacity == 0u) ? 128u : desc->initialSlotCapacity;
    if (!slot_map_init(&outCache->slots, outCache->arena, sizeof(ArtifactNode), slotCapacity)) {
        MEMSET(outCache, 0, sizeof(*outCache));
        return 0;
    }

    outCache->typeOps = ARENA_PUSH_ARRAY(outCache->arena, ArtifactTypeOps, outCache->maxTypeId + 1u);
    outCache->typeRegistered = ARENA_PUSH_ARRAY(outCache->arena, U8, outCache->maxTypeId + 1u);
    if (!outCache->typeOps || !outCache->typeRegistered) {
        MEMSET(outCache, 0, sizeof(*outCache));
        return 0;
    }

    MEMSET(outCache->typeOps, 0, sizeof(ArtifactTypeOps) * (outCache->maxTypeId + 1u));
    MEMSET(outCache->typeRegistered, 0, sizeof(U8) * (outCache->maxTypeId + 1u));

    U32 hashCapacity = (desc->initialHashCapacity == 0u) ? 256u : desc->initialHashCapacity;
    if (!artifact_hash_rebuild_(outCache, hashCapacity)) {
        MEMSET(outCache, 0, sizeof(*outCache));
        return 0;
    }

    if (desc->reloadScannerEnabled) {
        U32 dirtyCapacity = desc->reloadDirtyQueueCapacity ?
            desc->reloadDirtyQueueCapacity :
            ARTIFACT_RELOAD_DIRTY_QUEUE_DEFAULT_CAPACITY;
        outCache->reloadDirtyQueue = ARENA_PUSH_ARRAY(outCache->arena, ArtifactReloadDirtyEntry, dirtyCapacity);
        if (!outCache->reloadDirtyQueue) {
            MEMSET(outCache, 0, sizeof(*outCache));
            return 0;
        }
        outCache->reloadDirtyQueueCapacity = dirtyCapacity;
    }

    outCache->mutex = OS_mutex_create();
    outCache->loadArenaMutex = OS_mutex_create();
    outCache->condition = OS_condition_variable_create();

    if (!outCache->mutex.handle || !outCache->loadArenaMutex.handle || !outCache->condition.handle) {
        if (outCache->condition.handle) {
            OS_condition_variable_destroy(outCache->condition);
        }
        if (outCache->loadArenaMutex.handle) {
            OS_mutex_destroy(outCache->loadArenaMutex);
        }
        if (outCache->mutex.handle) {
            OS_mutex_destroy(outCache->mutex);
        }
        MEMSET(outCache, 0, sizeof(*outCache));
        return 0;
    }

    if (desc->reloadScannerEnabled) {
        outCache->reloadScannerRunning = 1u;
        outCache->reloadScannerThread = OS_thread_create(artifact_reload_scanner_thread_, outCache);
        if (!outCache->reloadScannerThread.handle) {
            outCache->reloadScannerRunning = 0u;
            OS_condition_variable_destroy(outCache->condition);
            OS_mutex_destroy(outCache->loadArenaMutex);
            OS_mutex_destroy(outCache->mutex);
            MEMSET(outCache, 0, sizeof(*outCache));
            return 0;
        }
    }

    return 1;
}

ArtifactCache* artifact_cache_alloc(const ArtifactCacheDesc* desc) {
    if (!desc || !desc->arena) {
        return 0;
    }

    ArtifactCache* cache = ARENA_PUSH_STRUCT(desc->arena, ArtifactCache);
    if (!cache) {
        return 0;
    }

    if (!artifact_cache_create(desc, cache)) {
        return 0;
    }

    return cache;
}

void artifact_cache_destroy(ArtifactCache* cache) {
    if (!cache) {
        return;
    }

    artifact_reload_scanner_stop_(cache);
    artifact_cache_reset(cache);

    if (cache->condition.handle) {
        OS_condition_variable_destroy(cache->condition);
        cache->condition.handle = 0;
    }
    if (cache->loadArenaMutex.handle) {
        OS_mutex_destroy(cache->loadArenaMutex);
        cache->loadArenaMutex.handle = 0;
    }
    if (cache->mutex.handle) {
        OS_mutex_destroy(cache->mutex);
        cache->mutex.handle = 0;
    }

    MEMSET(cache, 0, sizeof(*cache));
}

void artifact_cache_reset(ArtifactCache* cache) {
    if (!cache || !cache->mutex.handle) {
        return;
    }

    artifact_reload_scanner_stop_(cache);

    OS_mutex_lock(cache->mutex);

    while (cache->pendingAsyncJobs > 0u) {
        OS_condition_variable_wait(cache->condition, cache->mutex);
    }

    for (U32 slot = 0; slot < cache->slots.capacity; ++slot) {
        if (!slot_map_is_occupied(&cache->slots, slot)) {
            continue;
        }

        ArtifactNode* node = (ArtifactNode*)slot_map_item_at(&cache->slots, slot);
        if (!node) {
            continue;
        }

        if (node->status == ArtifactStatus_Ready && node->payload.data) {
            U64 payloadSize = node->payload.size;
            artifact_release_node_payload_locked_(node);
            if (cache->residentBytes >= payloadSize) {
                cache->residentBytes -= payloadSize;
            } else {
                cache->residentBytes = 0u;
            }
        }
    }

    slot_map_clear(&cache->slots);

    if (cache->hashEntries && cache->hashCapacity > 0u) {
        MEMSET(cache->hashEntries, 0, sizeof(ArtifactHashEntry) * cache->hashCapacity);
    }

    cache->hashCount = 0u;
    cache->hashTombstones = 0u;
    cache->residentBytes = 0u;
    cache->touchSerial = 0u;
    cache->reloadScanCursor = 0u;
    cache->reloadDirtyRead = 0u;
    cache->reloadDirtyWrite = 0u;
    cache->reloadDirtyCount = 0u;

    if (cache->typeRegistered && cache->typeOps && cache->maxTypeId > 0u) {
        MEMSET(cache->typeOps, 0, sizeof(ArtifactTypeOps) * (cache->maxTypeId + 1u));
        MEMSET(cache->typeRegistered, 0, sizeof(U8) * (cache->maxTypeId + 1u));
    }

    OS_condition_variable_broadcast(cache->condition);
    OS_mutex_unlock(cache->mutex);
}

B32 artifact_cache_register_type(ArtifactCache* cache, U32 typeId, const ArtifactTypeOps* ops) {
    if (!cache || !ops || typeId > cache->maxTypeId ||
        (ops->kind == ArtifactTypeKind_Callback && !ops->load)) {
        return 0;
    }

    OS_mutex_lock(cache->mutex);
    cache->typeOps[typeId] = *ops;
    cache->typeRegistered[typeId] = 1u;

    for (U32 slot = 0; slot < cache->slots.capacity; ++slot) {
        if (!slot_map_is_occupied(&cache->slots, slot)) {
            continue;
        }

        ArtifactNode* node = (ArtifactNode*)slot_map_item_at(&cache->slots, slot);
        if (!node || node->typeId != typeId) {
            continue;
        }

        node->releaseProc = ops->release;
        node->releaseUserData = ops->userData;
    }

    OS_mutex_unlock(cache->mutex);

    return 1;
}

B32 artifact_use_scope_open(ArtifactCache* cache, Arena* arena, ArtifactUseScope* outScope) {
    if (!cache || !arena || !outScope) {
        return 0;
    }

    MEMSET(outScope, 0, sizeof(*outScope));
    outScope->cache = cache;
    outScope->arena = arena;
    return 1;
}

void artifact_use_scope_close(ArtifactUseScope* scope) {
    if (!scope || !scope->cache) {
        return;
    }

    ArtifactCache* cache = scope->cache;

    OS_mutex_lock(cache->mutex);
    for (U32 i = 0; i < scope->touchedCount; ++i) {
        ArtifactHandle handle = scope->touched[i].handle;
        ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots, handle.slot, handle.generation);
        if (!node) {
            continue;
        }
        if (node->touchCount > 0u) {
            node->touchCount -= 1u;
        }
    }
    OS_mutex_unlock(cache->mutex);

    scope->cache = 0;
    scope->arena = 0;
    scope->touched = 0;
    scope->touchedCount = 0u;
    scope->touchedCapacity = 0u;
}

ArtifactHandle artifact_acquire(ArtifactUseScope* scope, U32 typeId, StringU8 key, U32 acquireFlags) {
    ArtifactReloadPolicy policy = {};
    return artifact_acquire_with_policy(scope, typeId, key, acquireFlags, policy);
}

ArtifactHandle artifact_acquire_with_policy(ArtifactUseScope* scope, U32 typeId, StringU8 key,
                                            U32 acquireFlags, ArtifactReloadPolicy policy) {
    ArtifactHandle invalid = ARTIFACT_HANDLE_INVALID;

    if (!scope || !scope->cache || !scope->arena || str8_is_nil(key) || key.size == 0u) {
        return invalid;
    }

    ArtifactCache* cache = scope->cache;

    B32 wantsAsync = (acquireFlags & ArtifactAcquireFlags_Async) ? 1 : 0;
    B32 wantsSync = (acquireFlags & ArtifactAcquireFlags_Sync) ? 1 : 0;
    B32 wantsReloadable = (acquireFlags & ArtifactAcquireFlags_Reloadable) ? 1 : 0;
    if (policy.checkIntervalNs == 0u) {
        policy.checkIntervalNs = ARTIFACT_RELOAD_CHECK_INTERVAL_DEFAULT_NS;
    }
    if (!wantsAsync && !wantsSync) {
        wantsSync = 1;
    }

    U64 keyHash = artifact_hash_key_(typeId, key);
    ArtifactHandle handle = ARTIFACT_HANDLE_INVALID;
    B32 created = 0;

    OS_mutex_lock(cache->mutex);

    if (typeId > cache->maxTypeId || !cache->typeRegistered[typeId]) {
        OS_mutex_unlock(cache->mutex);
        return invalid;
    }

    U32 entryIndex = 0u;
    B32 found = 0;

    if (!artifact_hash_find_index_locked_(cache, typeId, key, keyHash, &entryIndex, &found)) {
        OS_mutex_unlock(cache->mutex);
        return invalid;
    }

    if (found) {
        ArtifactHashEntry* entry = &cache->hashEntries[entryIndex];
        handle = entry->handle;
        ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots, handle.slot, handle.generation);
        if (node && wantsReloadable) {
            node->acquireFlags |= ArtifactAcquireFlags_Reloadable;
            node->reloadPolicy = policy;
        }
    } else {
        void* slotItem = 0;
        U32 slotIndex = 0u;
        U32 generation = 0u;

        if (!slot_map_alloc(&cache->slots, &slotItem, &slotIndex, &generation)) {
            OS_mutex_unlock(cache->mutex);
            return invalid;
        }

        ArtifactNode* node = (ArtifactNode*)slotItem;
        node->typeId = typeId;
        node->key = str8_cpy(cache->arena, key);
        node->keyHash = keyHash;
        node->payload = {};
        node->touchCount = 0u;
        node->inFlight = 0u;
        node->lastTouchSerial = ++cache->touchSerial;
        node->publishedGeneration = 0u;
        node->failedGeneration = 0u;
        node->acquireFlags = acquireFlags;
        node->reloadPolicy = policy;
        node->lastReloadCheckNs = 0u;
        node->lastWriteTimestampNs = 0u;
        node->lastWriteSize = 0u;
        node->sourceGeneration = 0u;
        node->dirtySourceGeneration = 0u;
        node->queuedSourceGeneration = 0u;
        node->loadingSourceGeneration = 0u;
        node->dirtyWriteTimestampNs = 0u;
        node->dirtyFileSize = 0u;
        node->failedSourceGeneration = 0u;
        node->reloadFailed = 0;
        node->releaseProc = 0;
        node->releaseUserData = 0;

        if (wantsAsync && !cache->jobSystem) {
            node->status = ArtifactStatus_Error_NoExecutor;
        } else {
            node->status = ArtifactStatus_Pending;
            node->inFlight = 1u;
            if (wantsAsync) {
                cache->pendingAsyncJobs += 1u;
            }
        }

        handle.slot = slotIndex;
        handle.generation = generation;

        if (!artifact_hash_insert_locked_(cache, typeId, key, keyHash, handle)) {
            void* released = 0;
            slot_map_release(&cache->slots, handle.slot, handle.generation, &released);
            if (wantsAsync && cache->pendingAsyncJobs > 0u) {
                cache->pendingAsyncJobs -= 1u;
            }
            OS_mutex_unlock(cache->mutex);
            return invalid;
        }

        created = 1;
    }

    OS_mutex_unlock(cache->mutex);

    if (ARTIFACT_HANDLE_IS_INVALID(handle)) {
        return invalid;
    }

    artifact_scope_touch_(scope, handle);

    if (created) {
        if (wantsAsync && cache->jobSystem) {
            Job job = {};
            job.function = artifact_async_load_job_;

            ArtifactAsyncJobParams params = {};
            params.cache = cache;
            params.handle = handle;
            MEMCPY(job.parameters, &params, sizeof(params));

            B32 submitOk = job_system_submit_(job);
            if (!submitOk) {
                OS_mutex_lock(cache->mutex);
                ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots, handle.slot, handle.generation);
                if (node && node->status == ArtifactStatus_Pending && node->inFlight != 0u) {
                    node->inFlight = 0u;
                    node->status = ArtifactStatus_Error;
                }
                if (cache->pendingAsyncJobs > 0u) {
                    cache->pendingAsyncJobs -= 1u;
                }
                OS_condition_variable_broadcast(cache->condition);
                OS_mutex_unlock(cache->mutex);
            }
        } else if (!wantsAsync) {
            artifact_run_load_for_handle_(cache, handle, 0);
        }
    }

    if (wantsSync) {
        artifact_wait(cache, handle, ARTIFACT_WAIT_INFINITE);
    }

    return handle;
}

ArtifactCacheTickResult artifact_cache_tick(ArtifactCache* cache, U64 nowNs, U32 maxChecks, U32 maxPublishes) {
    ArtifactCacheTickResult result = {};
    (void)nowNs;
    if (!cache || !cache->mutex.handle) {
        return result;
    }

    if (maxChecks == 0u) {
        maxChecks = 0xFFFFFFFFu;
    }
    if (maxPublishes == 0u) {
        maxPublishes = 0xFFFFFFFFu;
    }

    for (;;) {
        if (result.checkedCount >= maxChecks || result.submittedCount >= maxPublishes) {
            break;
        }

        ArtifactReloadDirtyEntry dirty = {};

        OS_mutex_lock(cache->mutex);
        if (!artifact_reload_pop_dirty_locked_(cache, &dirty)) {
            OS_mutex_unlock(cache->mutex);
            break;
        }
        OS_mutex_unlock(cache->mutex);

        result.checkedCount += 1u;

        OS_mutex_lock(cache->mutex);
        ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots,
                                                          dirty.handle.slot,
                                                          dirty.handle.generation);
        if (!node ||
            node->dirtySourceGeneration != dirty.sourceGeneration ||
            node->failedSourceGeneration == dirty.sourceGeneration) {
            OS_mutex_unlock(cache->mutex);
            continue;
        }
        if (node->inFlight != 0u) {
            if (node->queuedSourceGeneration == dirty.sourceGeneration) {
                node->queuedSourceGeneration = 0u;
            }
            OS_mutex_unlock(cache->mutex);
            continue;
        }
        node->inFlight = 1u;
        node->loadingSourceGeneration = dirty.sourceGeneration;
        if (node->status != ArtifactStatus_Ready) {
            node->status = ArtifactStatus_Pending;
        }
        OS_mutex_unlock(cache->mutex);

        result.submittedCount += 1u;
        ArtifactStatus loadStatus = artifact_run_load_for_handle_(cache, dirty.handle, 0);
        if (loadStatus == ArtifactStatus_Ready) {
            result.publishedCount += 1u;
        } else if (loadStatus == ArtifactStatus_Error) {
            result.failedCount += 1u;
        }
    }

    return result;
}

ArtifactStatus artifact_status(ArtifactCache* cache, ArtifactHandle handle) {
    if (!cache || ARTIFACT_HANDLE_IS_INVALID(handle)) {
        return ArtifactStatus_InvalidHandle;
    }

    OS_mutex_lock(cache->mutex);
    ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots, handle.slot, handle.generation);
    ArtifactStatus status = node ? node->status : ArtifactStatus_InvalidHandle;
    OS_mutex_unlock(cache->mutex);

    return status;
}

ArtifactStatus artifact_view(ArtifactUseScope* scope, ArtifactHandle handle, ArtifactView* outView) {
    if (!scope || !scope->cache || !outView || ARTIFACT_HANDLE_IS_INVALID(handle)) {
        return ArtifactStatus_InvalidHandle;
    }

    outView->data = 0;
    outView->size = 0u;
    outView->generation = 0u;
    outView->flags = ArtifactViewFlags_None;
    outView->status = ArtifactStatus_Invalid;

    ArtifactCache* cache = scope->cache;

    OS_mutex_lock(cache->mutex);
    ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots, handle.slot, handle.generation);
    if (!node) {
        OS_mutex_unlock(cache->mutex);
        return ArtifactStatus_InvalidHandle;
    }

    ArtifactStatus status = node->status;
    outView->status = status;
    outView->generation = node->publishedGeneration;
    if (node->inFlight != 0u && node->status == ArtifactStatus_Ready) {
        outView->flags |= ArtifactViewFlags_ReloadPending;
    }
    if (node->reloadFailed) {
        outView->flags |= ArtifactViewFlags_ReloadFailed;
    }
    if (status == ArtifactStatus_Ready && node->payload.data) {
        outView->data = node->payload.data;
        outView->size = node->payload.size;
    }
    OS_mutex_unlock(cache->mutex);

    if (status == ArtifactStatus_Ready) {
        artifact_scope_touch_(scope, handle);
    }

    return status;
}

ArtifactView artifact_resolve_view(ArtifactUseScope* scope, ArtifactHandle handle) {
    ArtifactView result = {};
    ArtifactStatus status = artifact_view(scope, handle, &result);
    if (status != ArtifactStatus_Ready) {
        result = {};
    }
    return result;
}

ArtifactStatus artifact_wait(ArtifactCache* cache, ArtifactHandle handle, U32 timeoutMs) {
    if (!cache || ARTIFACT_HANDLE_IS_INVALID(handle)) {
        return ArtifactStatus_InvalidHandle;
    }

    U64 startNs = OS_get_time_nanoseconds();

    for (;;) {
        OS_mutex_lock(cache->mutex);
        ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots, handle.slot, handle.generation);
        if (!node) {
            OS_mutex_unlock(cache->mutex);
            return ArtifactStatus_InvalidHandle;
        }

        ArtifactStatus status = node->status;
        if (status != ArtifactStatus_Pending) {
            OS_mutex_unlock(cache->mutex);
            return status;
        }

        if (timeoutMs == ARTIFACT_WAIT_INFINITE) {
            OS_condition_variable_wait(cache->condition, cache->mutex);
            OS_mutex_unlock(cache->mutex);
            continue;
        }

        OS_mutex_unlock(cache->mutex);

        U64 elapsedNs = OS_get_time_nanoseconds() - startNs;
        U64 timeoutNs = (U64)timeoutMs * MILLION(1ull);
        if (elapsedNs >= timeoutNs) {
            return ArtifactStatus_Pending;
        }

        OS_sleep_milliseconds(1u);
    }
}

B32 artifact_invalidate(ArtifactCache* cache, U32 typeId, StringU8 key) {
    if (!cache || str8_is_nil(key) || key.size == 0u) {
        return 0;
    }

    U64 keyHash = artifact_hash_key_(typeId, key);

    OS_mutex_lock(cache->mutex);

    U32 entryIndex = 0u;
    B32 found = 0;
    if (!artifact_hash_find_index_locked_(cache, typeId, key, keyHash, &entryIndex, &found) || !found) {
        OS_mutex_unlock(cache->mutex);
        return 0;
    }

    ArtifactHashEntry* entry = &cache->hashEntries[entryIndex];
    ArtifactHandle handle = entry->handle;
    artifact_hash_mark_tombstone_(cache, entryIndex);

    void* slotItem = 0;
    if (slot_map_release(&cache->slots, handle.slot, handle.generation, &slotItem)) {
        ArtifactNode* node = (ArtifactNode*)slotItem;
        if (node && node->status == ArtifactStatus_Ready && node->payload.data) {
            U64 payloadSize = node->payload.size;
            artifact_release_node_payload_locked_(node);
            if (cache->residentBytes >= payloadSize) {
                cache->residentBytes -= payloadSize;
            } else {
                cache->residentBytes = 0u;
            }
        }
    }

    OS_condition_variable_broadcast(cache->condition);
    OS_mutex_unlock(cache->mutex);

    return 1;
}

void artifact_invalidate_all(ArtifactCache* cache, U32 typeId) {
    if (!cache) {
        return;
    }

    OS_mutex_lock(cache->mutex);

    for (U32 index = 0; index < cache->hashCapacity; ++index) {
        ArtifactHashEntry* entry = &cache->hashEntries[index];
        if (entry->state != ArtifactHashEntryState_Occupied) {
            continue;
        }

        ArtifactNode* node = (ArtifactNode*)slot_map_get(&cache->slots,
                                                          entry->handle.slot,
                                                          entry->handle.generation);
        if (!node) {
            artifact_hash_mark_tombstone_(cache, index);
            continue;
        }

        if (typeId != 0u && node->typeId != typeId) {
            continue;
        }

        ArtifactHandle handle = entry->handle;
        artifact_hash_mark_tombstone_(cache, index);

        void* slotItem = 0;
        if (slot_map_release(&cache->slots, handle.slot, handle.generation, &slotItem)) {
            ArtifactNode* releasedNode = (ArtifactNode*)slotItem;
            if (releasedNode && releasedNode->status == ArtifactStatus_Ready && releasedNode->payload.data) {
                U64 payloadSize = releasedNode->payload.size;
                artifact_release_node_payload_locked_(releasedNode);
                if (cache->residentBytes >= payloadSize) {
                    cache->residentBytes -= payloadSize;
                } else {
                    cache->residentBytes = 0u;
                }
            }
        }
    }

    OS_condition_variable_broadcast(cache->condition);
    OS_mutex_unlock(cache->mutex);
}

void artifact_cache_evict(ArtifactCache* cache, U32 passCount) {
    if (!cache || passCount == 0u) {
        return;
    }

    if (cache->budgetBytes == 0u) {
        return;
    }

    for (U32 pass = 0; pass < passCount; ++pass) {
        ArtifactNode nodeCopy = {};
        ArtifactHandle handle = ARTIFACT_HANDLE_INVALID;
        B32 hasCandidate = 0;

        OS_mutex_lock(cache->mutex);

        if (cache->residentBytes <= cache->budgetBytes) {
            OS_mutex_unlock(cache->mutex);
            return;
        }

        U64 bestSerial = UINT64_MAX;
        for (U32 slot = 0; slot < cache->slots.capacity; ++slot) {
            if (!slot_map_is_occupied(&cache->slots, slot)) {
                continue;
            }

            ArtifactNode* node = (ArtifactNode*)slot_map_item_at(&cache->slots, slot);
            if (!node) {
                continue;
            }

            if (node->status != ArtifactStatus_Ready) {
                continue;
            }
            if (!node->payload.data) {
                continue;
            }
            if (node->touchCount != 0u) {
                continue;
            }
            if (node->inFlight != 0u) {
                continue;
            }

            if (node->lastTouchSerial < bestSerial) {
                bestSerial = node->lastTouchSerial;
                handle.slot = slot;
                handle.generation = cache->slots.generations[slot];
                nodeCopy = *node;
                hasCandidate = 1;
            }
        }

        if (!hasCandidate) {
            OS_mutex_unlock(cache->mutex);
            return;
        }

        artifact_hash_remove_locked_(cache, nodeCopy.typeId, nodeCopy.key, nodeCopy.keyHash);

        void* slotItem = 0;
        slot_map_release(&cache->slots, handle.slot, handle.generation, &slotItem);

        U64 payloadSize = nodeCopy.payload.size;
        if (cache->residentBytes >= payloadSize) {
            cache->residentBytes -= payloadSize;
        } else {
            cache->residentBytes = 0u;
        }

        OS_mutex_unlock(cache->mutex);

        artifact_release_payload_(nodeCopy.releaseProc,
                                  nodeCopy.releaseUserData,
                                  nodeCopy.typeId,
                                  nodeCopy.key,
                                  nodeCopy.payload);
    }
}
