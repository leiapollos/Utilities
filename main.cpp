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
#include "nstl/spmc/spmc.hpp"
#include "nstl/thread_pool/thread_pool.hpp"
#include "nstl/spmc/spmc.cpp"
#include "nstl/thread_pool/thread_pool.cpp"

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

#define func(...) func_(true, (opt){.c = 'd', __VA_ARGS__})

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
    func(.c = 'A', .val = 42, .skip = true, .b2 = false,);
    int arg = 123;
    OS_Handle handle = OS_thread_create(work, &arg);
    std::cout << "Thread created." << std::endl;
    OS_thread_join(handle);
    std::cout << "Thread joined." << std::endl;

    // ===== Job System Tests =====
    {
        std::cout << "Starting Job System Tests" << std::endl;
        Arena* arena = arena_alloc(.arenaSize = MB(64));
        U32 workerCount = 4;
    JobSystem* js = job_system_init(arena, workerCount);
        job_system_thread_bind(js, workerCount); // bind main thread as worker 0

    struct Counter { U64 value; };
    Counter counter{0};

        auto jobFunc = [](void* p) {
            Counter* c = (Counter*)p;
            U64 val = ATOMIC_FETCH_ADD(&c->value, 1, MEMORY_ORDER_RELAXED);
            usleep(10); // simulate work
//            std::cout << "Job executed, counter: " << val << std::endl;
        };

        const int totalJobs = 32;
        Job root{}; memset(&root, 0, sizeof(root));
        root.remainingJobs = 0;
        root.parent = nullptr;
        root.func = nullptr;
        root.params = nullptr;

        Job jobs[totalJobs];
        memset(jobs, 0, sizeof(jobs));
        for (int i = 0; i < totalJobs; ++i) {
            jobs[i].func = (OS_ThreadFunc*)jobFunc;
            jobs[i].params = &counter;
            jobs[i].parent = &root;
            job_system_submit_main(js, &jobs[i]);
        }

        // Spin-wait for completion (test helper)
    job_system_wait(js, &root);
        U64 count = ATOMIC_LOAD(&counter.value, MEMORY_ORDER_RELAXED);
        std::cout << "Executed jobs: " << count << " / " << totalJobs << std::endl;

        // Submit nested jobs: each job spawns 2 more (single level)
    Counter counter2{0};
        Job root2{}; memset(&root2, 0, sizeof(root2));
        auto parentSpawn = [](void* p) {
            struct Payload { JobSystem* js; Counter* c; Job* root; };
            Payload* pl = (Payload*)p;
            U64 val = ATOMIC_FETCH_ADD(&pl->c->value, 1, MEMORY_ORDER_RELAXED);
//            std::cout << "Counter: " << val << std::endl;
            for (int k = 0; k < 2000; ++k) {
                Job* j = (Job*)arena_push(get_scratch(0,0).arena, sizeof(Job), alignof(Job));
                memset(j, 0, sizeof(Job));
                j->func = (OS_ThreadFunc*)[](void* p2){
                    Counter* c2 = (Counter*)p2;
                    U64 val = ATOMIC_FETCH_ADD(&c2->value, 1, MEMORY_ORDER_RELAXED);
//                    std::cout << "Counter: " << val << std::endl;
                    usleep(1000); // simulate work
                };
                j->params = pl->c;
                j->parent = pl->root;
                job_system_submit(pl->js, j);
            }
//            usleep(10); // simulate work
        };
        const int parents = 8;
        struct Payload { JobSystem* js; Counter* c; Job* root; } payload{js, &counter2, &root2};
        for (int i = 0; i < parents; ++i) {
            Job* pj = (Job*)arena_push(arena, sizeof(Job), alignof(Job));
            memset(pj, 0, sizeof(Job));
            pj->func = (OS_ThreadFunc*)parentSpawn;
            pj->params = &payload;
            pj->parent = &root2;
            job_system_submit_main(js, pj);
        }
        // Wait
    job_system_wait(js, &root2);
        U64 count2 = ATOMIC_LOAD(&counter2.value, MEMORY_ORDER_RELAXED);
        std::cout << "Nested jobs executed: " << count2 << " (expected >= " << parents << ")" << std::endl;

        job_system_shutdown(js);
        arena_release(arena);
        std::cout << "Job System Tests Finished" << std::endl;
    }
    
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
