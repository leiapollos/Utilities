#pragma once

struct ArtifactCache;

typedef U32 ArtifactTypeId;

struct ArtifactKey {
    U64 hash[2];
};

static const ArtifactTypeId ARTIFACT_TYPE_ID_ZERO = 0u;
static const ArtifactKey ARTIFACT_KEY_ZERO = {{0u, 0u}};

struct ArtifactValue {
    U64 u64[4];
};

enum ArtifactStatus {
    ArtifactStatus_Null = 0,
    ArtifactStatus_Queued,
    ArtifactStatus_Building,
    ArtifactStatus_Publishing,
    ArtifactStatus_Ready,
    ArtifactStatus_Error,
    ArtifactStatus_Cancelled,
};

enum ArtifactTypeFlags {
    ArtifactTypeFlags_None = 0,
    ArtifactTypeFlags_Wide = (1u << 0u),
};

enum ArtifactGetFlags {
    ArtifactGetFlags_None = 0,
    ArtifactGetFlags_WaitFresh = (1u << 0u),
    ArtifactGetFlags_HighPriority = (1u << 1u),
    ArtifactGetFlags_NoQueue = (1u << 2u),
    ArtifactGetFlags_InvalidateFailed = (1u << 3u),
};

enum ArtifactResultFlags {
    ArtifactResultFlags_None = 0,
    ArtifactResultFlags_Stale = (1u << 0u),
    ArtifactResultFlags_Queued = (1u << 1u),
    ArtifactResultFlags_ErrorCached = (1u << 2u),
    ArtifactResultFlags_TimedOut = (1u << 3u),
};

struct ArtifactResult {
    ArtifactValue value;
    U64 generation;
    U64 requestedGeneration;
    ArtifactStatus status;
    U32 flags;
};

struct ArtifactBuildContext {
    ArtifactCache* cache;
    ContentStore* content;
    void* typeUserData;
    ArtifactTypeId typeId;
    ArtifactKey key;
    U64 generation;
    const U8* requestData;
    U32 requestDataSize;
    U64* cancelFlag;
};

struct ArtifactPublishContext {
    ArtifactCache* cache;
    ContentStore* content;
    void* typeUserData;
    ArtifactTypeId typeId;
    ArtifactKey key;
    U64 generation;
    const U8* requestData;
    U32 requestDataSize;
};

typedef B32 ArtifactBuildProc(ArtifactBuildContext* ctx, ArtifactValue* outValue, U64* outBytes);
typedef B32 ArtifactPublishProc(ArtifactPublishContext* ctx, ArtifactValue buildValue, ArtifactValue* outValue, U64* outBytes);
typedef void ArtifactDestroyProc(void* typeUserData, ArtifactValue value);

struct ArtifactTypeDesc {
    ArtifactTypeId typeId;
    StringU8 name;
    ArtifactBuildProc* buildProc;
    ArtifactPublishProc* publishProc;
    ArtifactDestroyProc* destroyProc;
    void* userData;
    U32 flags;
    U32 evictionTargetCount;
    U64 evictionTargetBytes;
    U64 evictionMaxIdleFrames;
};

struct ArtifactCacheDesc {
    Arena* arena;
    JobSystem* jobSystem;
    ContentStore* content;
    U32 initialSlotCapacity;
    U32 initialTableCapacity;
    U32 initialTypeCapacity;
    U32 requestDataSize;
};

struct ArtifactStats {
    U64 hits;
    U64 staleHits;
    U64 misses;
    U64 queued;
    U64 built;
    U64 published;
    U64 failed;
    U64 cancelled;
    U64 evicted;
    U64 bytesLive;
    U64 buildTimeNsTotal;
    U32 liveCount;
    U32 workingCount;
};

struct ArtifactDebugEntry {
    ArtifactTypeId typeId;
    ArtifactKey key;
    U64 generation;
    U64 lastTouchFrame;
    U64 bytes;
    U32 retainCount;
    ArtifactStatus status;
};

B32 artifact_key_equal(ArtifactKey a, ArtifactKey b);
B32 artifact_key_is_zero(ArtifactKey key);
ArtifactKey artifact_key_from_bytes(const void* data, U64 size);
ArtifactKey artifact_key_mix(ArtifactKey a, ArtifactKey b);
ArtifactKey artifact_key_mix_u64(ArtifactKey key, U64 value);

B32 artifact_cache_create(const ArtifactCacheDesc* desc, ArtifactCache* outCache);
ArtifactCache* artifact_cache_alloc(const ArtifactCacheDesc* desc);
void artifact_cache_destroy(ArtifactCache* cache);
B32 artifact_register_type(ArtifactCache* cache, const ArtifactTypeDesc* desc);
ArtifactResult artifact_get(ArtifactCache* cache,
                            ArtifactTypeId typeId,
                            ArtifactKey key,
                            U64 generation,
                            const void* requestData,
                            U32 requestDataSize,
                            U32 flags,
                            U64 deadlineNs);
ArtifactResult artifact_view(ArtifactCache* cache, ArtifactTypeId typeId, ArtifactKey key);
void artifact_touch(ArtifactCache* cache, ArtifactTypeId typeId, ArtifactKey key, U64 frameIndex);
B32 artifact_retain(ArtifactCache* cache, ArtifactTypeId typeId, ArtifactKey key);
void artifact_release(ArtifactCache* cache, ArtifactTypeId typeId, ArtifactKey key);
void artifact_cache_tick(ArtifactCache* cache, U64 frameIndex, U32 maxSubmits, U32 maxPublishes);
void artifact_cache_evict(ArtifactCache* cache, U64 frameIndex, U32 targetCount);
ArtifactStats artifact_cache_stats(ArtifactCache* cache);
U32 artifact_debug_dump(ArtifactCache* cache, ArtifactDebugEntry* outEntries, U32 maxEntries);
