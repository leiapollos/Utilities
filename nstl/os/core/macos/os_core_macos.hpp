//
// Created by André Leite on 26/07/2025.
//

#pragma once

#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include <cstdlib>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>


// ////////////////////////
// State

enum OS_MACOS_EntityType : U64 {
    OS_MACOS_EntityType_Invalid = (0),
    OS_MACOS_EntityType_Thread = (1 << 0),
    OS_MACOS_EntityType_Mutex = (2 << 0),
    OS_MACOS_EntityType_File = (3 << 0),
};

struct OS_MACOS_Entity {
    OS_MACOS_Entity* next;
    OS_MACOS_EntityType type;

    union {
        struct {
            pthread_t handle;
            OS_ThreadFunc* func;
            void* args;
        } thread;

        pthread_mutex_t mutex;

        struct {
            int fd;
        } file;
    };
};

static OS_MACOS_Entity* alloc_OS_entity();
static void free_OS_entity(OS_MACOS_Entity* entity);

struct OS_MACOS_State {
    OS_SystemInfo systemInfo;

    Arena* arena;

    Arena* osEntityArena;
    OS_MACOS_Entity* freeEntities;
};


// ////////////////////////
// Globals

static OS_MACOS_State g_OS_MacOSState = {};
