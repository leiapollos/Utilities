#pragma once

struct ContentStore;

struct ContentHash {
    U64 hash[2];
};

struct ContentRoot {
    U64 id;
};

struct ContentId {
    U64 u64[2];
};

struct ContentKey {
    ContentRoot root;
    ContentId id;
};

static const ContentHash CONTENT_HASH_ZERO = {{0u, 0u}};
static const ContentRoot CONTENT_ROOT_ZERO = {0u};
static const ContentId CONTENT_ID_ZERO = {{0u, 0u}};
static const ContentKey CONTENT_KEY_ZERO = {{0u}, {{0u, 0u}}};

enum ContentViewFlags {
    ContentViewFlags_None = 0,
};

struct ContentView {
    const U8* data;
    U64 size;
    ContentHash hash;
    U32 flags;
    B32 valid;
};

struct ContentStoreDesc {
    Arena* arena;
    U32 initialBlobCapacity;
    U32 initialKeyCapacity;
};

struct ContentStats {
    U64 payloadBytes;
    U64 committedBytes;
    U32 blobCount;
    U32 keyCount;
    U32 evictCount;
    U32 hitCount;
    U32 missCount;
};

B32 content_hash_equal(ContentHash a, ContentHash b);
B32 content_hash_is_zero(ContentHash hash);
ContentHash content_hash_from_bytes(const void* data, U64 size);
ContentId content_id_from_bytes(const void* data, U64 size);
ContentId content_id_from_u64(U64 value);

B32 content_key_is_zero(ContentKey key);
B32 content_key_equal(ContentKey a, ContentKey b);
ContentKey content_key_make(ContentRoot root, ContentId id);

B32 content_store_create(const ContentStoreDesc* desc, ContentStore* outStore);
ContentStore* content_store_alloc(const ContentStoreDesc* desc);
void content_store_destroy(ContentStore* store);
ContentRoot content_root_alloc(ContentStore* store);
void content_root_release(ContentStore* store, ContentRoot root);
ContentHash content_submit_bytes(ContentStore* store, ContentKey key, const void* data, U64 size, StringU8 debugName);
ContentHash content_hash_from_key(ContentStore* store, ContentKey key, U64 rewindCount);
ContentView content_view_hash(ContentStore* store, ContentHash hash);
B32 content_retain_hash(ContentStore* store, ContentHash hash);
void content_release_hash(ContentStore* store, ContentHash hash);
void content_touch_hash(ContentStore* store, ContentHash hash, U64 frameIndex);
void content_tick_gc(ContentStore* store, U64 frameIndex, U64 targetBytes);
ContentStats content_stats(ContentStore* store);
