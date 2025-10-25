//
// Created by André Leite on 26/07/2025.
//
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winitializer-overrides"
#endif
#include "nstl/base/base_include.hpp"
#include "nstl/os/os_include.hpp"
#include "nstl/base/base_include.cpp"
#include "nstl/os/os_include.cpp"

#include <iostream>
#include <chrono>

struct opt {
    bool skip;
    int val;
    bool b2;
    char c;
};

void func_(bool shouldPrint, opt o) {
    if (shouldPrint) {
        std::cout << "Hello, World!" << std::endl;
    }
    std::cout << o.skip << " " << o.val << " " << o.b2 << " " << o.c << std::endl;
}

#define func(...)                                             \
    do {                                                     \
        opt _tmp_opt = {.c = 'd', __VA_ARGS__};                           \
        func_(true, _tmp_opt);                                                  \
    } while (0)

void work(void*arg) {
    int val = *(int*)arg;
    std::cout << "Thread working with value: " << val << std::endl;
    {
        Temp a = get_scratch(0, 0);
        void* p = arena_push(a.arena, TB(60));
        std::cout << "Pushed 1MB to scratch arena at " << (void*)a.arena << std::endl;
        temp_end(&a);
    }
    {
        Temp a = get_scratch(0, 0);
        void* p = arena_push(a.arena, MB(4));
        std::cout << "Pushed 1MB to scratch arena at " << (void*)a.arena << std::endl;
        temp_end(&a);
    }
    
    sleep(1);
    std::cout << "Thread finished with value: " << val << std::endl;
}

void entry_point() {
    Arena* a = arena_alloc();
    char test[4];
    test[0] = 'a';
    test[1] = 'b';
    test[2] = '\n';
    test[3] = '\0';
    log(LogLevel_Debug, str8(test));
    set_log_level(LogLevel_Warning);
    log(LogLevel_Error, str8("123\n"));
    log_fmt(LogLevel_Warning, "123 {} lll\n", 1);
    log_fmt(LogLevel_Debug, "no args");
    DEFER(log_fmt(LogLevel_Warning, "321 {} lll\n", 1));

    func(.c = 'A', .val = 42, .skip = true, .b2 = false,);
    int arg = 123;
    OS_Handle handle = OS_thread_create(work, &arg);
    std::cout << "Thread created." << std::endl;
    OS_thread_join(handle);
    std::cout << "Thread joined." << std::endl;
    
    std::chrono::time_point startChrono = std::chrono::high_resolution_clock::now();
    U64 start = OS_get_time_microseconds();
    U64 size = 1024 * 1024 * 1024;
    void* ptr = OS_reserve(size);


    OS_SystemInfo* sysInfo = OS_get_system_info();
    Arena* arena = arena_alloc(
        .arenaSize = TB(79) + GB(575) + MB(1023) + KB(600),
    );
    void* currentPos = arena_push(arena, GB(4) - sysInfo->pageSize/2);
    currentPos = (U8*)currentPos + sysInfo->pageSize - ARENA_HEADER_SIZE;
    U8* res = (U8*)currentPos + MB(4) - ARENA_HEADER_SIZE;
    memset(res, 0, ARENA_HEADER_SIZE);
    Arena* temp = (Arena*)res;
    temp->pos = 69698;
    std::cout << temp->pos << " " << temp->reserved << " " << temp->committed << std::endl;
    arena_release(arena);

    OS_release(ptr, size);
    std::chrono::time_point endChrono = std::chrono::high_resolution_clock::now();
    U64 end = OS_get_time_microseconds();
    std::cout << end - start << " microseconds" << std::endl;
    std::cout << endChrono.time_since_epoch().count() - startChrono.time_since_epoch().count() << " chrono" << std::endl;
}
