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

OS_SystemInfo* OS_get_system_info();


// ////////////////////////
// Executable Path

StringU8 OS_get_executable_directory(Arena* arena);


// ////////////////////////
// Environment

void OS_set_environment_variable(StringU8 name, StringU8 value);
StringU8 OS_get_environment_variable(Arena* arena, StringU8 name);

// ////////////////////////
// Time
U64 OS_get_time_microseconds();
U64 OS_get_time_nanoseconds();
U64 OS_get_counter_frequency_hz();

U64 OS_rdtsc_relaxed();
U64 OS_rdtscp_serialized();

void OS_sleep_milliseconds(U32 milliseconds);

// ////////////////////////
// Aborting

void OS_abort(S32 exit_code);


// ////////////////////////
// Memory allocation

void* OS_reserve(U64 size);
B32 OS_commit(void* addr, U64 size);
void OS_decommit(void* addr, U64 size);
void OS_release(void* addr, U64 size);


// ////////////////////////
// Threads and Synchronization

struct OS_Handle {
    U64* handle;
};

typedef struct OS_SharedLibrary {
    void* handle;
} OS_SharedLibrary;

B32 OS_library_open(StringU8 path, OS_SharedLibrary* outLibrary);
void OS_library_close(OS_SharedLibrary library);
void* OS_library_load_symbol(OS_SharedLibrary library, StringU8 symbolName);
StringU8 OS_library_last_error(Arena* arena);

S32 OS_execute(StringU8 command);

typedef void OS_ThreadFunc(void* ptr);

OS_Handle OS_thread_create(OS_ThreadFunc* func, void* arg);
B32 OS_thread_join(OS_Handle thread);
void OS_thread_detach(OS_Handle thread);
void OS_thread_yield();
void OS_cpu_pause();

U32 OS_get_thread_id_u32();
OS_Handle OS_mutex_create();
void OS_mutex_destroy(OS_Handle mutex);
void OS_mutex_lock(OS_Handle mutex);
void OS_mutex_unlock(OS_Handle mutex);

OS_Handle OS_condition_variable_create();
void OS_condition_variable_destroy(OS_Handle conditionVariable);
void OS_condition_variable_wait(OS_Handle conditionVariable, OS_Handle mutex);
void OS_condition_variable_signal(OS_Handle conditionVariable);
void OS_condition_variable_broadcast(OS_Handle conditionVariable);

OS_Handle OS_barrier_create(U32 threadCount);
void OS_barrier_destroy(OS_Handle barrier);
void OS_barrier_wait(OS_Handle barrier);


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

OS_Handle OS_file_open(const char* path, OS_FileOpenMode mode);
void OS_file_close(OS_Handle h);
U64 OS_file_read(OS_Handle h, RangeU64 range, void* dst);
U64 OS_file_read(OS_Handle fileHandle, U64 size, void* dst);
U64 OS_file_write(OS_Handle h, RangeU64 range, const void* src);
U64 OS_file_write(OS_Handle h, U64 size, const void* src);
U64 OS_file_size(OS_Handle h);
OS_FileMapping OS_file_map_ro(OS_Handle h);
void OS_file_unmap(OS_FileMapping m);
OS_FileInfo OS_get_file_info(const char* path);
B32 OS_file_copy_contents(const char* srcPath, const char* dstPath);
OS_Handle OS_get_log_handle();

bool OS_terminal_supports_color();

enum OS_FileHintFlags {
    OS_FileHint_None = 0,
    OS_FileHint_NoCache = (1ull << 0),
    OS_FileHint_Sequential = (1ull << 1),
};

void OS_file_set_hints(OS_Handle h, U64 hints);


// ////////////////////////
// Entry Point

void entry_point();
