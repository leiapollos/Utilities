#pragma once

struct FileStream;

struct FileHandle {
    U32 index;
    U32 generation;
};

static const FileHandle FILE_HANDLE_ZERO = {0u, 0u};

enum FileStatus {
    FileStatus_Null = 0,
    FileStatus_Ready,
    FileStatus_Error,
};

enum FileViewFlags {
    FileViewFlags_None = 0,
    FileViewFlags_ReloadFailed = (1u << 0),
};

struct FileView {
    const U8* data;
    U64 size;
    ContentKey key;
    ContentHash hash;
    U64 generation;
    U32 flags;
    FileStatus status;
};

struct FileStreamDesc {
    Arena* arena;
    ContentStore* content;
    U32 initialFileCapacity;
    U64 defaultCheckIntervalNs;
};

struct FileStreamStats {
    U32 fileCount;
    U32 checkedCount;
    U32 publishCount;
    U32 failedCount;
};

B32 file_handle_is_zero(FileHandle handle);
B32 file_stream_create(const FileStreamDesc* desc, FileStream* outStream);
FileStream* file_stream_alloc(const FileStreamDesc* desc);
void file_stream_destroy(FileStream* stream);
FileHandle file_watch(FileStream* stream, StringU8 path, U64 checkIntervalNs);
ContentKey file_key_from_path_range(FileStream* stream, StringU8 path, RangeU64 range, U64 checkIntervalNs);
ContentHash file_hash_from_path_range(FileStream* stream, StringU8 path, RangeU64 range, U64 checkIntervalNs);
void file_stream_tick(FileStream* stream, U64 nowNs, U32 maxChecks);
FileView file_view(FileStream* stream, FileHandle handle);
FileStreamStats file_stream_stats(FileStream* stream);
