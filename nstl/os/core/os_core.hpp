//
// Created by André Leite on 26/07/2025.
//

#pragma once

// ////////////////////////
// System Info

struct OS_SystemInfo {
    U32 logicalCores;
    U64 pageSize;
};

UTILITIES_SHARED_API OS_SystemInfo* OS_get_system_info();


// ////////////////////////
// Executable Path

UTILITIES_SHARED_API StringU8 OS_get_executable_directory(Arena* arena);


// ////////////////////////
// Environment

UTILITIES_SHARED_API void OS_set_environment_variable(StringU8 name, StringU8 value);
UTILITIES_SHARED_API StringU8 OS_get_environment_variable(Arena* arena, StringU8 name);

// ////////////////////////
// Time
UTILITIES_SHARED_API U64 OS_get_time_microseconds();
UTILITIES_SHARED_API U64 OS_get_time_nanoseconds();
UTILITIES_SHARED_API U64 OS_get_counter_frequency_hz();

UTILITIES_SHARED_API U64 OS_rdtsc_relaxed();
UTILITIES_SHARED_API U64 OS_rdtscp_serialized();

UTILITIES_SHARED_API void OS_sleep_milliseconds(U32 milliseconds);

// ////////////////////////
// Aborting

UTILITIES_SHARED_API void OS_abort(S32 exit_code);


// ////////////////////////
// Memory allocation

UTILITIES_SHARED_API void* OS_reserve(U64 size);
UTILITIES_SHARED_API B32 OS_commit(void* addr, U64 size);
UTILITIES_SHARED_API void OS_decommit(void* addr, U64 size);
UTILITIES_SHARED_API void OS_release(void* addr, U64 size);


// ////////////////////////
// Threads and Synchronization

struct OS_Handle {
    U64* handle;
};

typedef struct OS_SharedLibrary {
    void* handle;
} OS_SharedLibrary;

UTILITIES_SHARED_API B32 OS_library_open(StringU8 path, OS_SharedLibrary* outLibrary);
UTILITIES_SHARED_API void OS_library_close(OS_SharedLibrary library);
UTILITIES_SHARED_API void* OS_library_load_symbol(OS_SharedLibrary library, StringU8 symbolName);
UTILITIES_SHARED_API StringU8 OS_library_last_error(Arena* arena);

UTILITIES_SHARED_API S32 OS_execute(StringU8 command);

typedef void OS_ThreadFunc(void* ptr);

UTILITIES_SHARED_API OS_Handle OS_thread_create(OS_ThreadFunc* func, void* arg);
UTILITIES_SHARED_API B32 OS_thread_join(OS_Handle thread);
UTILITIES_SHARED_API void OS_thread_detach(OS_Handle thread);
UTILITIES_SHARED_API void OS_thread_yield();
UTILITIES_SHARED_API void OS_cpu_pause();

UTILITIES_SHARED_API U32 OS_get_thread_id_u32();
UTILITIES_SHARED_API OS_Handle OS_mutex_create();
UTILITIES_SHARED_API void OS_mutex_destroy(OS_Handle mutex);
UTILITIES_SHARED_API void OS_mutex_lock(OS_Handle mutex);
UTILITIES_SHARED_API void OS_mutex_unlock(OS_Handle mutex);

UTILITIES_SHARED_API OS_Handle OS_condition_variable_create();
UTILITIES_SHARED_API void OS_condition_variable_destroy(OS_Handle conditionVariable);
UTILITIES_SHARED_API void OS_condition_variable_wait(OS_Handle conditionVariable, OS_Handle mutex);
UTILITIES_SHARED_API void OS_condition_variable_signal(OS_Handle conditionVariable);
UTILITIES_SHARED_API void OS_condition_variable_broadcast(OS_Handle conditionVariable);

UTILITIES_SHARED_API OS_Handle OS_barrier_create(U32 threadCount);
UTILITIES_SHARED_API void OS_barrier_destroy(OS_Handle barrier);
UTILITIES_SHARED_API void OS_barrier_wait(OS_Handle barrier);

// ////////////////////////
// File I/O

enum OS_FileOpenMode {
    OS_FileOpenMode_Read = 0,
    OS_FileOpenMode_Write = 1,
    OS_FileOpenMode_Create = 2,
};

struct OS_FileMapping {
    void* ptr;
    U64 length;
};

struct OS_FileInfo {
    B32 exists;
    U64 size;
    U64 lastWriteTimestampNs;
};

UTILITIES_SHARED_API B32 OS_create_directory(const char* path);
UTILITIES_SHARED_API OS_Handle OS_file_open(const char* path, OS_FileOpenMode mode);
UTILITIES_SHARED_API void OS_file_close(OS_Handle h);
UTILITIES_SHARED_API U64 OS_file_read(OS_Handle h, RangeU64 range, void* dst);
UTILITIES_SHARED_API U64 OS_file_read(OS_Handle fileHandle, U64 size, void* dst);
UTILITIES_SHARED_API U64 OS_file_write(OS_Handle h, RangeU64 range, const void* src);
UTILITIES_SHARED_API U64 OS_file_write(OS_Handle h, U64 size, const void* src);
UTILITIES_SHARED_API U64 OS_file_size(OS_Handle h);
UTILITIES_SHARED_API OS_FileMapping OS_file_map_ro(OS_Handle h);
UTILITIES_SHARED_API void OS_file_unmap(OS_FileMapping m);
UTILITIES_SHARED_API OS_FileInfo OS_get_file_info(const char* path);
UTILITIES_SHARED_API B32 OS_file_copy_contents(const char* srcPath, const char* dstPath);
UTILITIES_SHARED_API OS_Handle OS_get_log_handle();

UTILITIES_SHARED_API B32 OS_terminal_supports_color();

enum OS_FileHintFlags {
    OS_FileHint_None = 0,
    OS_FileHint_NoCache = (1ull << 0),
    OS_FileHint_Sequential = (1ull << 1),
};

UTILITIES_SHARED_API void OS_file_set_hints(OS_Handle h, U64 hints);


// ////////////////////////
// Entry Point

void entry_point();
