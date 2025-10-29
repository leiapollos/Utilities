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

struct Opt {
    bool skip;
    int val;
    bool b2;
    char c;
};

void func_(bool shouldPrint, Opt o) {
    if (shouldPrint) {
        std::cout << "Hello, World!" << std::endl;
    }
    std::cout << o.skip << " " << o.val << " " << o.b2 << " " << o.c << std::endl;
}

#define func(...)                                             \
    do {                                                     \
        Opt _tmp_opt = {.c = 'd', __VA_ARGS__};                           \
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

struct CondVarTestState {
    OS_Handle mutex;
    OS_Handle condVar;
    int flag;
};

void condition_variable_test_thread(void* arg) {
    CondVarTestState* state = (CondVarTestState*) arg;
    
    OS_mutex_lock(state->mutex);
    std::cout << "Thread waiting for condition..." << std::endl;
    while (state->flag == 0) {
        OS_condition_variable_wait(state->condVar, state->mutex);
    }
    std::cout << "Thread woke up! Flag is now: " << state->flag << std::endl;
    OS_mutex_unlock(state->mutex);
}

struct BarrierTestState {
    OS_Handle barrier;
};

void barrier_test_thread(void* arg) {
    BarrierTestState* state = (BarrierTestState*) arg;
    U32 threadId = OS_get_thread_id_u32();
    std::cout << "Thread " << threadId << " reached barrier, waiting..." << std::endl;
    OS_barrier_wait(state->barrier);
    std::cout << "Thread " << threadId << " passed barrier!" << std::endl;
}

void entry_point() {
    TIME_SCOPE("entry_point");
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
    
    // Test condition variable
    {
        TIME_SCOPE("Condition Variable Tests");
        std::cout << "\n=== Testing Condition Variable ===" << std::endl;
        CondVarTestState* cvState = (CondVarTestState*) arena_push(a, sizeof(CondVarTestState));
        cvState->mutex = OS_mutex_create();
        cvState->condVar = OS_condition_variable_create();
        cvState->flag = 0;
        
        OS_Handle cvThread = OS_thread_create(condition_variable_test_thread, cvState);
        
        sleep(1);
        std::cout << "Main: Setting flag and signaling..." << std::endl;
        OS_mutex_lock(cvState->mutex);
        cvState->flag = 1;
        OS_condition_variable_signal(cvState->condVar);
        OS_mutex_unlock(cvState->mutex);
        
        OS_thread_join(cvThread);
        
        OS_condition_variable_destroy(cvState->condVar);
        OS_mutex_destroy(cvState->mutex);
        std::cout << "Condition variable test done!" << std::endl;
    }
    
    {
        TIME_SCOPE("Barrier Tests");
        std::cout << "\n=== Testing Barrier ===" << std::endl;
        U32 numThreads = 3;
        
        BarrierTestState* barrierState = (BarrierTestState*) arena_push(a, sizeof(BarrierTestState));
        barrierState->barrier = OS_barrier_create(numThreads);
        
        OS_Handle* threads = (OS_Handle*) arena_push(a, sizeof(OS_Handle) * numThreads);
        for (U32 i = 0; i < numThreads; i++) {
            threads[i] = OS_thread_create(barrier_test_thread, barrierState);
        }
        
        for (U32 i = 0; i < numThreads; i++) {
            OS_thread_join(threads[i]);
        }
        
        OS_barrier_destroy(barrierState->barrier);
        std::cout << "Barrier test done!" << std::endl;
    }

    {
        TIME_SCOPE("SPMD Group Tests");
        std::cout << "\n=== Testing SPMD Group (sync + broadcast) ===" << std::endl;
        U32 laneCount = 4;
        Temp scratch = get_scratch(0, 0);
        Arena* arena = scratch.arena;

    SPMDGroup* group = spmd_group_create(arena, laneCount, .broadcastScratchSize = KB(4));

        struct SPMDTestState {
            SPMDGroup* group;
            U32 lane;
            U32* broadcastDst1;
            U32* broadcastDst2;
            U32* rootValue1;
            U32* rootValue2;
            U32 iterations;
        };

    U32* rootValue1 = (U32*) arena_push(arena, sizeof(U32));
    U32* rootValue2 = (U32*) arena_push(arena, sizeof(U32));
        *rootValue1 = 0xA5A5A5A5;
        *rootValue2 = 0xDEADBEEF;

        OS_Handle* threadHandles = (OS_Handle*) arena_push(arena, sizeof(OS_Handle) * laneCount);
        SPMDTestState* states = (SPMDTestState*) arena_push(arena, sizeof(SPMDTestState) * laneCount);

        auto spmd_test_thread = [](void* arg) {
            SPMDTestState* st = (SPMDTestState*) arg;
            thread_context_alloc();
            spmd_join_group(st->group, st->lane);

            U32 laneId = (U32) spmd_lane_id();
            U32 laneCountLocal = (U32) spmd_lane_count();
            std::cout << "[Lane " << laneId << "/" << laneCountLocal << "] Joined group" << std::endl;

            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = (rand() % 100) * 1000 * 1000;
            nanosleep(&ts, nullptr);

            for (U32 iter = 0; iter < st->iterations; ++iter) {
                spmd_broadcast(st->group, st->broadcastDst1, st->rootValue1, sizeof(U32), 0);
                if (laneId == 0) {
                    std::cout << "[Lane 0][Iter " << iter << "] Broadcast1 root value " << std::hex << *st->rootValue1 << std::dec << std::endl;
                }
                barrier_wait(st->group->barrier);
                if (*st->broadcastDst1 != *st->rootValue1) {
                    std::cout << "[Lane " << laneId << "][Iter " << iter << "] ERROR broadcast1 mismatch got " << std::hex << *st->broadcastDst1 << " expected " << *st->rootValue1 << std::dec << std::endl;
                } else {
                    std::cout << "[Lane " << laneId << "][Iter " << iter << "] Received broadcast1 value: " << std::hex << *st->broadcastDst1 << std::dec << std::endl;
                }

                barrier_wait(st->group->barrier);
                if (laneId == laneCountLocal - 1) {
                    *st->rootValue2 ^= 0x12345678;
                    std::cout << "[Lane last][Iter " << iter << "] Mutated rootValue2 to " << std::hex << *st->rootValue2 << std::dec << std::endl;
                }

                spmd_broadcast(st->group, st->broadcastDst2, st->rootValue2, sizeof(U32), laneCountLocal - 1);
                barrier_wait(st->group->barrier);
                if (*st->broadcastDst2 != *st->rootValue2) {
                    std::cout << "[Lane " << laneId << "][Iter " << iter << "] ERROR broadcast2 mismatch got " << std::hex << *st->broadcastDst2 << " expected " << *st->rootValue2 << std::dec << std::endl;
                } else {
                    std::cout << "[Lane " << laneId << "][Iter " << iter << "] Received broadcast2 value: " << std::hex << *st->broadcastDst2 << std::dec << std::endl;
                }
                barrier_wait(st->group->barrier);
            }

            spmd_group_leave();
        };

    U32 iterations = 3;
        for (U32 lane = 0; lane < laneCount; ++lane) {
            SPMDTestState* st = &states[lane];
            st->group = group;
            st->lane = lane;
            st->rootValue1 = rootValue1;
            st->rootValue2 = rootValue2;
            st->broadcastDst1 = (U32*) arena_push(arena, sizeof(U32));
            st->broadcastDst2 = (U32*) arena_push(arena, sizeof(U32));
            *st->broadcastDst1 = 0; *st->broadcastDst2 = 0;
            st->iterations = iterations;
            threadHandles[lane] = OS_thread_create(spmd_test_thread, st);
        }

        for (U32 lane = 0; lane < laneCount; ++lane) {
            OS_thread_join(threadHandles[lane]);
        }
        std::cout << "SPMD test done!" << std::endl;
        temp_end(&scratch);
    }
    
    {
        TIME_SCOPE("Arena and Memory Operations");
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
    }
}
