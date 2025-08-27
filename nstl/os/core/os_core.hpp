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

typedef void OS_ThreadFunc(void *ptr);

static OS_Handle OS_thread_create(OS_ThreadFunc* func, void* arg);
static B32 OS_thread_join(OS_Handle thread);
static void OS_thread_detach(OS_Handle thread);

static U32 OS_get_thread_id_u32();
static void OS_mutex_init(void **m);
static void OS_mutex_destroy(void *m);
static void OS_mutex_lock(void *m);
static void OS_mutex_unlock(void *m);


// ////////////////////////
// Entry Point

static void entry_point();
