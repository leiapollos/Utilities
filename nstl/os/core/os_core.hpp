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
// Aborting

static void OS_abort(S32 exit_code);


// ////////////////////////
// Memory allocation

static void* OS_reserve(U64 size);
static B32 OS_commit(void* addr, U64 size);
static void OS_decommit(void* addr, U64 size);
static void OS_release(void* addr, U64 size);


// ////////////////////////
// Entry Point

static void entry_point();
