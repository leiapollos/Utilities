#define FILE_STREAM_DEFAULT_CHECK_INTERVAL_NS (250ull * 1000000ull)

static const RangeU64 FILE_STREAM_FULL_RANGE = {0u, UINT64_MAX};

struct FileNode {
    StringU8 path;
    RangeU64 range;
    ContentKey key;
    ContentHash hash;
    ContentHash lastGoodHash;
    U64 generation;
    U64 lastCheckNs;
    U64 checkIntervalNs;
    U64 lastWriteTimestampNs;
    U64 lastWriteSize;
    FileStatus status;
    U32 flags;
};

struct FileStream {
    Arena* arena;
    ContentStore* content;
    ContentRoot root;
    SlotMap files;
    U32 scanCursor;
    U64 defaultCheckIntervalNs;
    FileStreamStats stats;
};

B32 file_handle_is_zero(FileHandle handle) {
    return (handle.index == 0u && handle.generation == 0u) ? 1 : 0;
}


static B32 file_stream_range_equal_(RangeU64 a, RangeU64 b) {
    return (a.min == b.min && a.max == b.max) ? 1 : 0;
}

static ContentId file_stream_content_id_from_path_range_(StringU8 path, RangeU64 range) {
    ContentId result = CONTENT_ID_ZERO;
    U64 rangeValues[2] = {range.min, range.max};
    result.u64[0] = hash_fnv1a(path.data, path.size, 1469598103934665603ull ^ 0xD6E8FEB86659FD93ull);
    result.u64[1] = hash_fnv1a(rangeValues, sizeof(rangeValues), result.u64[0] ^ 0xA0761D6478BD642Full);
    if (result.u64[0] == 0u && result.u64[1] == 0u) {
        result.u64[0] = 1u;
    }
    return result;
}

static FileNode* file_stream_resolve_(FileStream* stream, FileHandle handle) {
    if (!stream || file_handle_is_zero(handle)) {
        return 0;
    }
    return (FileNode*)slot_map_get(&stream->files, handle.index, handle.generation);
}

static FileNode* file_stream_find_path_range_(FileStream* stream,
                                              StringU8 path,
                                              RangeU64 range,
                                              FileHandle* outHandle) {
    if (!stream || str8_is_nil(path) || path.size == 0u) {
        return 0;
    }

    for (U32 slot = 0u; slot < stream->files.capacity; ++slot) {
        if (!slot_map_is_occupied(&stream->files, slot)) {
            continue;
        }

        FileNode* node = (FileNode*)slot_map_item_at(&stream->files, slot);
        if (node && str8_equal(node->path, path) && file_stream_range_equal_(node->range, range)) {
            if (outHandle) {
                outHandle->index = slot;
                outHandle->generation = stream->files.generations[slot];
            }
            return node;
        }
    }

    return 0;
}

static RangeU64 file_stream_read_range_from_info_(RangeU64 requested, OS_FileInfo info) {
    RangeU64 result = {};
    U64 fileSize = info.size;
    if (requested.max == UINT64_MAX || requested.max <= requested.min) {
        result.min = 0u;
        result.max = fileSize;
    } else {
        result.min = MIN(requested.min, fileSize);
        result.max = MIN(requested.max, fileSize);
        if (result.max < result.min) {
            result.max = result.min;
        }
    }
    return result;
}

static void file_stream_mark_error_(FileNode* node) {
    if (node) {
        node->status = FileStatus_Error;
        node->flags |= FileViewFlags_ReloadFailed;
    }
}

static B32 file_stream_load_stable_(FileStream* stream, FileNode* node, U64 nowNs) {
    if (!stream || !stream->content || !node || str8_is_nil(node->path) || node->path.size == 0u) {
        return 0;
    }

    OS_FileInfo preInfo = OS_get_file_info((const char*)node->path.data);
    if (!preInfo.exists) {
        file_stream_mark_error_(node);
        return 0;
    }

    if (node->status == FileStatus_Ready &&
        preInfo.lastWriteTimestampNs == node->lastWriteTimestampNs &&
        preInfo.size == node->lastWriteSize) {
        node->lastCheckNs = nowNs;
        return 1;
    }

    RangeU64 readRange = file_stream_read_range_from_info_(node->range, preInfo);
    U64 readSize = readRange.max - readRange.min;

    OS_Handle file = OS_file_open((const char*)node->path.data, OS_FileOpenMode_Read);
    if (!file.handle) {
        file_stream_mark_error_(node);
        return 0;
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        OS_file_close(file);
        file_stream_mark_error_(node);
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    U8* bytes = ARENA_PUSH_ARRAY(scratch.arena, U8, readSize + 1u);
    if (!bytes) {
        OS_file_close(file);
        file_stream_mark_error_(node);
        return 0;
    }

    U64 bytesRead = 0u;
    if (readSize != 0u) {
        bytesRead = OS_file_read(file, readRange, bytes);
    }
    OS_file_close(file);

    OS_FileInfo postInfo = OS_get_file_info((const char*)node->path.data);
    if (bytesRead != readSize ||
        !postInfo.exists ||
        postInfo.lastWriteTimestampNs != preInfo.lastWriteTimestampNs ||
        postInfo.size != preInfo.size) {
        file_stream_mark_error_(node);
        return 0;
    }

    bytes[readSize] = 0;
    ContentHash hash = content_submit_bytes(stream->content, node->key, bytes, readSize, node->path);
    if (content_hash_is_zero(hash)) {
        file_stream_mark_error_(node);
        return 0;
    }

    node->lastCheckNs = nowNs;
    node->lastWriteTimestampNs = postInfo.lastWriteTimestampNs;
    node->lastWriteSize = postInfo.size;
    node->status = FileStatus_Ready;
    node->flags &= ~FileViewFlags_ReloadFailed;

    if (!content_hash_equal(node->hash, hash)) {
        node->hash = hash;
        node->lastGoodHash = hash;
        node->generation += 1u;
        if (node->generation == 0u) {
            node->generation = 1u;
        }
        stream->stats.publishCount += 1u;
    }

    return 1;
}

B32 file_stream_create(const FileStreamDesc* desc, FileStream* outStream) {
    if (!desc || !desc->arena || !desc->content || !outStream) {
        return 0;
    }

    MEMSET(outStream, 0, sizeof(*outStream));
    outStream->arena = desc->arena;
    outStream->content = desc->content;
    outStream->root = content_root_alloc(desc->content);
    outStream->defaultCheckIntervalNs = desc->defaultCheckIntervalNs ?
        desc->defaultCheckIntervalNs :
        FILE_STREAM_DEFAULT_CHECK_INTERVAL_NS;

    if (outStream->root.id == 0u) {
        MEMSET(outStream, 0, sizeof(*outStream));
        return 0;
    }

    U32 capacity = desc->initialFileCapacity ? desc->initialFileCapacity : 32u;
    if (!slot_map_init(&outStream->files, outStream->arena, sizeof(FileNode), capacity)) {
        content_root_release(desc->content, outStream->root);
        MEMSET(outStream, 0, sizeof(*outStream));
        return 0;
    }

    return 1;
}

FileStream* file_stream_alloc(const FileStreamDesc* desc) {
    if (!desc || !desc->arena) {
        return 0;
    }

    FileStream* result = ARENA_PUSH_STRUCT(desc->arena, FileStream);
    if (!result || !file_stream_create(desc, result)) {
        return 0;
    }
    return result;
}

void file_stream_destroy(FileStream* stream) {
    if (!stream) {
        return;
    }

    if (stream->content && stream->root.id != 0u) {
        content_root_release(stream->content, stream->root);
    }
    MEMSET(stream, 0, sizeof(*stream));
}

ContentKey file_key_from_path_range(FileStream* stream, StringU8 path, RangeU64 range, U64 checkIntervalNs) {
    ContentKey result = CONTENT_KEY_ZERO;
    if (!stream || str8_is_nil(path) || path.size == 0u || stream->root.id == 0u) {
        return result;
    }

    FileHandle existingHandle = FILE_HANDLE_ZERO;
    FileNode* existing = file_stream_find_path_range_(stream, path, range, &existingHandle);
    if (existing) {
        return existing->key;
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&stream->files, &slotItem, &slotIndex, &generation)) {
        return result;
    }

    FileNode* node = (FileNode*)slotItem;
    node->path = str8_cpy(stream->arena, path);
    node->range = range;
    node->key = content_key_make(stream->root, file_stream_content_id_from_path_range_(path, range));
    node->checkIntervalNs = checkIntervalNs ? checkIntervalNs : stream->defaultCheckIntervalNs;
    node->generation = 0u;
    node->status = FileStatus_Null;

    (void)slotIndex;
    (void)generation;
    file_stream_load_stable_(stream, node, OS_get_time_nanoseconds());
    return node->key;
}

ContentHash file_hash_from_path_range(FileStream* stream, StringU8 path, RangeU64 range, U64 checkIntervalNs) {
    ContentHash result = CONTENT_HASH_ZERO;
    if (!stream || str8_is_nil(path) || path.size == 0u) {
        return result;
    }

    ContentKey key = file_key_from_path_range(stream, path, range, checkIntervalNs);
    if (!content_key_is_zero(key)) {
        result = content_hash_from_key(stream->content, key, 0u);
    }
    return result;
}

FileHandle file_watch(FileStream* stream, StringU8 path, U64 checkIntervalNs) {
    FileHandle result = FILE_HANDLE_ZERO;
    if (!stream || str8_is_nil(path) || path.size == 0u) {
        return result;
    }

    FileNode* existing = file_stream_find_path_range_(stream, path, FILE_STREAM_FULL_RANGE, &result);
    if (existing) {
        return result;
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&stream->files, &slotItem, &slotIndex, &generation)) {
        return result;
    }

    FileNode* node = (FileNode*)slotItem;
    node->path = str8_cpy(stream->arena, path);
    node->range = FILE_STREAM_FULL_RANGE;
    node->key = content_key_make(stream->root, file_stream_content_id_from_path_range_(path, node->range));
    node->checkIntervalNs = checkIntervalNs ? checkIntervalNs : stream->defaultCheckIntervalNs;
    node->generation = 0u;
    node->status = FileStatus_Null;

    result.index = slotIndex;
    result.generation = generation;
    file_stream_load_stable_(stream, node, OS_get_time_nanoseconds());
    return result;
}

void file_stream_tick(FileStream* stream, U64 nowNs, U32 maxChecks) {
    if (!stream || stream->files.capacity == 0u || maxChecks == 0u) {
        return;
    }

    U32 checked = 0u;
    U32 attempts = 0u;
    while (checked < maxChecks && attempts < stream->files.capacity) {
        U32 slot = stream->scanCursor % stream->files.capacity;
        stream->scanCursor = (slot + 1u) % stream->files.capacity;
        attempts += 1u;

        if (!slot_map_is_occupied(&stream->files, slot)) {
            continue;
        }

        FileNode* node = (FileNode*)slot_map_item_at(&stream->files, slot);
        if (!node) {
            continue;
        }
        if (node->lastCheckNs != 0u && nowNs - node->lastCheckNs < node->checkIntervalNs) {
            continue;
        }

        checked += 1u;
        stream->stats.checkedCount += 1u;
        if (!file_stream_load_stable_(stream, node, nowNs)) {
            stream->stats.failedCount += 1u;
        }
    }
}

FileView file_view(FileStream* stream, FileHandle handle) {
    FileView result = {};
    FileNode* node = file_stream_resolve_(stream, handle);
    if (!node) {
        return result;
    }

    ContentHash hash = content_hash_is_zero(node->hash) ? node->lastGoodHash : node->hash;
    ContentView content = content_view_hash(stream->content, hash);
    if (content.valid) {
        result.data = content.data;
        result.size = content.size;
        result.key = node->key;
        result.hash = content.hash;
        result.generation = node->generation;
    }
    result.flags = node->flags;
    result.status = node->status;
    return result;
}

FileStreamStats file_stream_stats(FileStream* stream) {
    FileStreamStats result = {};
    if (stream) {
        result = stream->stats;
        result.fileCount = stream->files.count;
    }
    return result;
}

U32 file_stream_capacity(FileStream* stream) {
    return stream ? stream->files.capacity : 0u;
}

B32 file_stream_entry_at(FileStream* stream, U32 slot, FileEntryInfo* outInfo) {
    if (!stream || !outInfo || !slot_map_is_occupied(&stream->files, slot)) {
        return 0;
    }
    FileNode* node = (FileNode*)slot_map_item_at(&stream->files, slot);
    if (!node) {
        return 0;
    }
    outInfo->path = node->path;
    outInfo->size = node->lastWriteSize;
    outInfo->generation = node->generation;
    outInfo->status = node->status;
    outInfo->flags = node->flags;
    return 1;
}
