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


// ////////////////////////
// State

struct OS_MACOS_State {
    OS_SystemInfo system_info;
};

struct OS_Mutex {
    pthread_mutex_t m;
};


// ////////////////////////
// Globals

static OS_MACOS_State os_macos_state = {0};
