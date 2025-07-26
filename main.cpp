//
// Created by André Leite on 26/07/2025.
//

#include "nstl/base/base_include.hpp"
#include "nstl/os/os_include.hpp"
#include "nstl/base/base_include.cpp"
#include "nstl/os/os_include.cpp"

#include <iostream>

void entry_point() {
    U64 size = sqrt_f32(1024 * 1024 * 1024);
    void* ptr = OS_reserve(size);


    OS_SystemInfo* sysInfo = OS_get_system_info();
    Arena* arena = arena_alloc();
    void* currentPos = arena_push(arena, MB(4));
    currentPos = (U8*)currentPos + sysInfo->pageSize - ARENA_HEADER_SIZE;
    U8* res = (U8*)currentPos + MB(4) - ARENA_HEADER_SIZE;
    memset(res, 0, ARENA_HEADER_SIZE);
    Arena* temp = (Arena*)res;
    temp->pos = 69698;
    std::cout << temp->pos << " " << temp->reserved << " " << temp->committed << std::endl;
    arena_release(arena);

    OS_release(ptr, size);
}