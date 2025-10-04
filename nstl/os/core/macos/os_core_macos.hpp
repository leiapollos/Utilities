//
// Created by André Leite on 26/07/2025.
//

#pragma once

#include <sys/mman.h>
#include <unistd.h>
#include <cstdlib>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>


// ////////////////////////
// State

struct OS_Mutex {
    pthread_mutex_t m;
};

enum class OS_MACOS_EntityType : U64 {
    Invalid = (0),
    Thread  = (1 << 0),
    Mutex   = (2 << 0),
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

static OS_MACOS_State osMacosState = {0};
