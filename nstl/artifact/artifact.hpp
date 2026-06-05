#pragma once

#define ARTIFACT_CACHE_API_VERSION 2u
#define ARTIFACT_WAIT_INFINITE 0xFFFFFFFFu
#define ARTIFACT_RELOAD_CHECK_INTERVAL_DEFAULT_NS (250ull * 1000000ull)
#define ARTIFACT_RELOAD_SCANNER_SLEEP_DEFAULT_MS 100u
#define ARTIFACT_RELOAD_SCANNER_BATCH_DEFAULT 16u
#define ARTIFACT_RELOAD_DIRTY_QUEUE_DEFAULT_CAPACITY 256u

struct ArtifactCache;
struct ArtifactUseScope;

struct ArtifactHandle {
    U32 slot;
    U32 generation;
};

static const ArtifactHandle ARTIFACT_HANDLE_INVALID = {0u, 0u};

#define ARTIFACT_HANDLE_IS_INVALID(handle) (((handle).slot == ARTIFACT_HANDLE_INVALID.slot) && \
                                            ((handle).generation == ARTIFACT_HANDLE_INVALID.generation))
#define ARTIFACT_HANDLE_IS_VALID(handle) (!(ARTIFACT_HANDLE_IS_INVALID(handle)))

enum ArtifactStatus {
    ArtifactStatus_Invalid = 0,
    ArtifactStatus_Pending,
    ArtifactStatus_Ready,
    ArtifactStatus_Error,
    ArtifactStatus_Error_NoExecutor,
    ArtifactStatus_InvalidHandle,
};

enum ArtifactAcquireFlags {
    ArtifactAcquireFlags_None = 0,
    ArtifactAcquireFlags_Async = (1u << 0),
    ArtifactAcquireFlags_Sync = (1u << 1),
    ArtifactAcquireFlags_Reloadable = (1u << 2),
};

enum ArtifactViewFlags {
    ArtifactViewFlags_None = 0,
    ArtifactViewFlags_ReloadPending = (1u << 0),
    ArtifactViewFlags_ReloadFailed = (1u << 1),
};

enum ArtifactTypeKind {
    ArtifactTypeKind_Callback = 0,
    ArtifactTypeKind_RawFile,
};

struct ArtifactPayload {
    void* data;
    U64 size;
    Arena* arena;
};

struct ArtifactView {
    const void* data;
    U64 size;
    U64 generation;
    U32 flags;
    ArtifactStatus status;
};

struct ArtifactReloadPolicy {
    U64 checkIntervalNs;
};

struct ArtifactCacheTickResult {
    U32 checkedCount;
    U32 submittedCount;
    U32 publishedCount;
    U32 failedCount;
};

typedef B32 ArtifactLoadProc(void* userData, Arena* arena, U32 typeId, StringU8 key, ArtifactPayload* outPayload);
typedef void ArtifactReleaseProc(void* userData, U32 typeId, StringU8 key, ArtifactPayload payload);

struct ArtifactTypeOps {
    ArtifactTypeKind kind;
    ArtifactLoadProc* load;
    ArtifactReleaseProc* release;
    void* userData;
};

struct ArtifactCacheDesc {
    U32 structSize;
    U32 apiVersion;
    Arena* arena;
    JobSystem* jobSystem;
    U32 initialSlotCapacity;
    U32 initialHashCapacity;
    U64 budgetBytes;
    U32 maxTypeId;
    B32 reloadScannerEnabled;
    U32 reloadScannerSleepMs;
    U32 reloadScannerBatchCount;
    U32 reloadDirtyQueueCapacity;
};

struct ArtifactTouchedEntry {
    ArtifactHandle handle;
};

struct ArtifactUseScope {
    ArtifactCache* cache;
    Arena* arena;
    ArtifactTouchedEntry* touched;
    U32 touchedCount;
    U32 touchedCapacity;
};

B32 artifact_cache_create(const ArtifactCacheDesc* desc, ArtifactCache* outCache);
ArtifactCache* artifact_cache_alloc(const ArtifactCacheDesc* desc);
void artifact_cache_destroy(ArtifactCache* cache);
void artifact_cache_reset(ArtifactCache* cache);

B32 artifact_cache_register_type(ArtifactCache* cache, U32 typeId, const ArtifactTypeOps* ops);

B32 artifact_use_scope_open(ArtifactCache* cache, Arena* arena, ArtifactUseScope* outScope);
void artifact_use_scope_close(ArtifactUseScope* scope);

ArtifactHandle artifact_acquire(ArtifactUseScope* scope, U32 typeId, StringU8 key, U32 acquireFlags);
ArtifactHandle artifact_acquire_with_policy(ArtifactUseScope* scope, U32 typeId, StringU8 key,
                                            U32 acquireFlags, ArtifactReloadPolicy policy);
ArtifactCacheTickResult artifact_cache_tick(ArtifactCache* cache, U64 nowNs, U32 maxChecks, U32 maxPublishes);
ArtifactStatus artifact_status(ArtifactCache* cache, ArtifactHandle handle);
ArtifactStatus artifact_view(ArtifactUseScope* scope, ArtifactHandle handle, ArtifactView* outView);
ArtifactView artifact_resolve_view(ArtifactUseScope* scope, ArtifactHandle handle);

ArtifactStatus artifact_wait(ArtifactCache* cache, ArtifactHandle handle, U32 timeoutMs);
B32 artifact_invalidate(ArtifactCache* cache, U32 typeId, StringU8 key);
void artifact_invalidate_all(ArtifactCache* cache, U32 typeId);
void artifact_cache_evict(ArtifactCache* cache, U32 passCount);
