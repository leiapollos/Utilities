//
// Created by André Leite on 31/05/2025.
//

#pragma once

#include "platform.h"

#if defined(PLATFORM_OS_MACOS)
#include <sys/sysctl.h>
#elif defined(PLATFORM_OS_WINDOWS)
#include <stdlib.h>
#include <windows.h>
#elif defined(PLATFORM_OS_LINUX)
#include <stdio.h>
#endif

namespace nstl {
    size_t cache_line_size();
#if defined(PLATFORM_OS_MACOS)
    size_t cache_line_size() {
        size_t line_size = 0;
        size_t sizeof_line_size = sizeof(line_size);
        sysctlbyname("hw.cachelinesize", &line_size, &sizeof_line_size, 0, 0);
        return line_size;
    }

#elif defined(PLATFORM_OS_WINDOWS)
    size_t cache_line_size() {
        size_t line_size = 0;
        DWORD buffer_size = 0;
        DWORD i = 0;
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION * buffer = 0;

        GetLogicalProcessorInformation(0, &buffer_size);
        buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)malloc(buffer_size);
        GetLogicalProcessorInformation(&buffer[0], &buffer_size);

        for (i = 0; i != buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); ++i) {
            if (buffer[i].Relationship == RelationCache && buffer[i].Cache.Level == 1) {
                line_size = buffer[i].Cache.LineSize;
                break;
            }
        }

        free(buffer);
        return line_size;
    }

#elif defined(PLATFORM_OS_LINUX)
    size_t cache_line_size() {
        FILE * p = 0;
        p = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
        unsigned int i = 0;
        if (p) {
            fscanf(p, "%d", &i);
            fclose(p);
        }
        return i;
    }
#endif

}