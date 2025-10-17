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

static OS_SystemInfo* OS_get_system_info();


// ////////////////////////
// Time
static U64 OS_get_time_microseconds();
static U64 OS_get_time_nanoseconds();
static U64 OS_get_counter_frequency_hz();

static U64 OS_rdtsc_relaxed();
static U64 OS_rdtscp_serialized();

// ////////////////////////
// Aborting

static void OS_abort(S32 exit_code);


// ////////////////////////
// Memory allocation

static void* OS_reserve(U64 size);
static B32 OS_commit(void* addr, U64 size);
static void OS_decommit(void* addr, U64 size);
static void OS_release(void* addr, U64 size);


// ////////////////////////
// Threads and Synchronization

struct OS_Handle {
    U64* handle;
};

typedef void OS_ThreadFunc(void* ptr);

static OS_Handle OS_thread_create(OS_ThreadFunc* func, void* arg);
static B32 OS_thread_join(OS_Handle thread);
static void OS_thread_detach(OS_Handle thread);
static void OS_thread_yield();
static void OS_cpu_pause();

static U32 OS_get_thread_id_u32();
static OS_Handle OS_mutex_create();
static void OS_mutex_destroy(OS_Handle mutex);
static void OS_mutex_lock(OS_Handle mutex);
static void OS_mutex_unlock(OS_Handle mutex);


// ////////////////////////
// File I/O

enum OS_FileOpenMode : U32 {
    OS_FileOpenMode_Read = 0,
    OS_FileOpenMode_Write = 1,
    OS_FileOpenMode_Create = 2,
};

struct OS_FileMapping {
    void* ptr;
    U64 length;
};

static OS_Handle OS_file_open(const char* path, OS_FileOpenMode mode);
static void OS_file_close(OS_Handle h);
static U64 OS_file_read(OS_Handle h, RangeU64 range, void* dst);
static U64 OS_file_read(OS_Handle fileHandle, U64 size, void* dst);
static U64 OS_file_write(OS_Handle h, RangeU64 range, const void* src);
static U64 OS_file_write(OS_Handle h, U64 size, const void* src);
static U64 OS_file_size(OS_Handle h);
static OS_FileMapping OS_file_map_ro(OS_Handle h);
static void OS_file_unmap(OS_FileMapping m);

enum OS_FileHintFlags : U64 {
    OS_FileHint_None = 0,
    OS_FileHint_NoCache = (1ull << 0),
    OS_FileHint_Sequential = (1ull << 1),
};

static void OS_file_set_hints(OS_Handle h, U64 hints);


// ////////////////////////
// Entry Point

static void entry_point();
