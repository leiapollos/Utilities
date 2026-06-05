//
// Created by André Leite on 26/07/2025.
//

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#include <stdlib.h>

enum OS_WINDOWS_EntityType : U64 {
    OS_WINDOWS_EntityType_Invalid = 0,
    OS_WINDOWS_EntityType_Thread = (1 << 0),
    OS_WINDOWS_EntityType_Mutex = (2 << 0),
    OS_WINDOWS_EntityType_File = (3 << 0),
    OS_WINDOWS_EntityType_ConditionVariable = (4 << 0),
    OS_WINDOWS_EntityType_Barrier = (5 << 0),
};

struct OS_WINDOWS_Entity {
    OS_WINDOWS_Entity* next;
    OS_WINDOWS_EntityType type;

    union {
        struct {
            HANDLE handle;
            DWORD id;
            OS_ThreadFunc* func;
            void* args;
        } thread;

        CRITICAL_SECTION mutex;

        struct {
            HANDLE handle;
        } file;

        CONDITION_VARIABLE conditionVariable;

        struct {
            OS_Handle conditionHandle;
            OS_Handle mutexHandle;
            U32 threadCount;
            U32 waitingCount;
            U32 generation;
        } barrier;
    };
};

static OS_WINDOWS_Entity* alloc_OS_entity();
static void free_OS_entity(OS_WINDOWS_Entity* entity);

struct OS_WINDOWS_State {
    OS_SystemInfo systemInfo;
    Arena* arena;
    Arena* osEntityArena;
    OS_WINDOWS_Entity* freeEntities;
    CRITICAL_SECTION entityMutex;
    LARGE_INTEGER counterFrequency;
    U64 processStartMicroseconds;
};

extern OS_WINDOWS_State g_OS_WindowsState;
