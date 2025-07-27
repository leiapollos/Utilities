//
// Created by André Leite on 26/07/2025.
//

#pragma once

#include <sys/mman.h>
#include <unistd.h>
#include <cstdlib>
#include <math.h>
#include <time.h>

// ////////////////////////
// State

struct OS_MACOS_State
{
    // Arena *arena;
    OS_SystemInfo system_info;
};


// ////////////////////////
// Globals

static OS_MACOS_State os_macos_state = {0};