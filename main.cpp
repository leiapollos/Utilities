//
// Created by André Leite on 26/07/2025.
//

/*

Stuff to work on:
- Networking layer (Not started)
- SPMD (ongoing)
- Windows OS core (Not started)
- Linux OS core (Not started)
- Graphics abstraction layer (Not started) - consider using sokol_gfx

*/

#include "nstl/base/base_include.hpp"
#include "nstl/os/os_include.hpp"
#include "nstl/base/base_include.cpp"
#include "nstl/os/os_include.cpp"


struct Opt {
    bool skip;
    int val;
    bool b2;
    char c;
};

void func_(bool shouldPrint, Opt o) {
    if (shouldPrint) {
        LOG_INFO("main", "Hello, World!");
    }
    LOG_INFO("main", "{} {} {} {}", o.skip, o.val, o.b2, o.c);
}

#define func(...)                                             \
    do {                                                     \
        Opt _tmp_opt = {.c = 'd', __VA_ARGS__};                           \
        func_(true, _tmp_opt);                                                  \
    } while (0)

void work(void*arg) {
    int val = *(int*)arg;
    LOG_INFO("threading", "Thread working with value: {}", val);
    {
        Temp a = get_scratch(0, 0);
        void* p = arena_push(a.arena, TB(60));
        LOG_INFO("threading", "Pushed 1MB to scratch arena at {}", (void*)a.arena);
        temp_end(&a);
    }
    {
        Temp a = get_scratch(0, 0);
        void* p = arena_push(a.arena, MB(4));
        LOG_INFO("threading", "Pushed 1MB to scratch arena at {}", (void*)a.arena);
        temp_end(&a);
    }
    
    sleep(1);
    LOG_INFO("threading", "Thread finished with value: {}", val);
}

struct CondVarTestState {
    OS_Handle mutex;
    OS_Handle condVar;
    int flag;
};

void condition_variable_test_thread(void* arg) {
    CondVarTestState* state = (CondVarTestState*) arg;
    
    OS_mutex_lock(state->mutex);
    LOG_INFO("threading", "Thread waiting for condition...");
    while (state->flag == 0) {
        OS_condition_variable_wait(state->condVar, state->mutex);
    }
    LOG_INFO("threading", "Thread woke up! Flag is now: {}", state->flag);
    OS_mutex_unlock(state->mutex);
}

struct BarrierTestState {
    OS_Handle barrier;
};

void barrier_test_thread(void* arg) {
    BarrierTestState* state = (BarrierTestState*) arg;
    U32 threadId = OS_get_thread_id_u32();
    LOG_INFO("threading", "Thread {} reached barrier, waiting...", threadId);
    OS_barrier_wait(state->barrier);
    LOG_INFO("threading", "Thread {} passed barrier!", threadId);
}

struct SPMDDispatchTestParams {
    U32* resultArray;
    U32 arraySize;
    U32* broadcastValue;
    U32* broadcastDst;
};

void spmd_dispatch_kernel(void* params) {
    SPMDDispatchTestParams* p = (SPMDDispatchTestParams*) params;
    U64 laneId = spmd_lane_id();
    U64 laneCount = spmd_lane_count();

    RangeU64 range = SPMD_SPLIT_RANGE(p->arraySize);
    for (U64 i = range.min; i < range.max; ++i) {
        p->resultArray[i] = (U32)(laneId + 1);
    }

    SPMD_BROADCAST(p->broadcastDst, p->broadcastValue, 0);
    SPMD_SYNC();

    if (SPMD_IS_ROOT(0)) {
        LOG_INFO("spmd_dispatch", "[Lane 0] Broadcast received: 0x{:X}", *p->broadcastDst);
    }
}

void entry_point() {
    TIME_SCOPE("entry_point");
    Arena* a = arena_alloc();
    char test[4];
    test[0] = 'a';
    test[1] = 'b';
    test[2] = '\n';
    test[3] = '\0';
    log(LogLevel_Debug, str8("main"), str8(test));
    set_log_level(LogLevel_Info);
    log(LogLevel_Error, str8("main"), str8("123"));
    log_fmt(LogLevel_Warning, "main", 1, "123 {} lll", 1);
    log_fmt(LogLevel_Debug, "main", 1, "no args");
    DEFER(log_fmt(LogLevel_Warning, "main", 1, "321 {} lll", 1));

    func(.c = 'A', .val = 42, .skip = true, .b2 = false,);
    int arg = 123;
    OS_Handle handle = OS_thread_create(work, &arg);
    LOG_INFO("main", "Thread created.");
    OS_thread_join(handle);
    LOG_INFO("main", "Thread joined.");
    
    // Test condition variable
    {
        TIME_SCOPE("Condition Variable Tests");
        LOG_INFO("main", "\n=== Testing Condition Variable ===");
        CondVarTestState* cvState = (CondVarTestState*) arena_push(a, sizeof(CondVarTestState));
        cvState->mutex = OS_mutex_create();
        cvState->condVar = OS_condition_variable_create();
        cvState->flag = 0;
        
        OS_Handle cvThread = OS_thread_create(condition_variable_test_thread, cvState);
        
        sleep(1);
        LOG_INFO("main", "Main: Setting flag and signaling...");
        OS_mutex_lock(cvState->mutex);
        cvState->flag = 1;
        OS_condition_variable_signal(cvState->condVar);
        OS_mutex_unlock(cvState->mutex);
        
        OS_thread_join(cvThread);
        
        OS_condition_variable_destroy(cvState->condVar);
        OS_mutex_destroy(cvState->mutex);
        LOG_INFO("main", "Condition variable test done!");
    }
    
    {
        TIME_SCOPE("Barrier Tests");
        LOG_INFO("main", "\n=== Testing Barrier ===");
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
        LOG_INFO("main", "Barrier test done!");
    }

    {
        TIME_SCOPE("SPMD Group Tests");
        LOG_INFO("main", "\n=== Testing SPMD Group (sync + broadcast) ===");
        U32 laneCount = 4;
        Temp scratch = get_scratch(0, 0);
        Arena* arena = scratch.arena;
        DEFER_REF(temp_end(&scratch));

        SPMDGroup* group = spmd_group_create(arena, laneCount, .broadcastScratchSize = KB(4));

        struct SPMDTestState {
            SPMDGroup* group;
            U32* broadcastDst1;
            U32* broadcastDst2;
            U32* rootValue1;
            U32* rootValue2;
            U32 iterations;
            U32 totalTasks;
            U32* taskAssignments;
        };

        U32* rootValue1 = (U32*) arena_push(arena, sizeof(U32));
        U32* rootValue2 = (U32*) arena_push(arena, sizeof(U32));
        *rootValue1 = 0xA5A5A5A5;
        *rootValue2 = 0xDEADBEEF;

        U32 totalTasks = 38;
        U32* taskAssignments = (U32*) arena_push(arena, sizeof(U32) * totalTasks);
        MEMSET(taskAssignments, 0xFF, sizeof(U32) * totalTasks);

        OS_Handle* threadHandles = (OS_Handle*) arena_push(arena, sizeof(OS_Handle) * laneCount);
        SPMDTestState* states = (SPMDTestState*) arena_push(arena, sizeof(SPMDTestState) * laneCount);

        auto spmd_test_thread = [](void* arg) {
            SPMDTestState* st = (SPMDTestState*) arg;
            thread_context_alloc();
            U64 lane = spmd_join_group_auto(st->group);
            ASSERT_DEBUG(lane != (U64)-1 && "Failed to join SPMD group");
            DEFER(spmd_group_leave());

            Temp scratch = get_scratch(0, 0);
            DEFER_REF(temp_end(&scratch));
            Arena* arena = scratch.arena;

            U32 laneId = (U32) spmd_lane_id();
            U32 laneCountLocal = (U32) spmd_lane_count();
            LOG_INFO("spmd", "[Lane {}/{}] Joined group", laneId, laneCountLocal);

            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = (rand() % 100) * 1000 * 1000;
            nanosleep(&ts, nullptr);

            for (U32 iter = 0; iter < st->iterations; ++iter) {
                spmd_broadcast(st->group, st->broadcastDst1, st->rootValue1, sizeof(U32), 0);
                if (SPMD_IS_ROOT(0)) {
                    LOG_INFO("spmd", "[Lane 0][Iter {}] Broadcast1 root value 0x{:X}", iter, *st->rootValue1);
                }
                SPMD_SYNC();
                if (*st->broadcastDst1 != *st->rootValue1) {
                    LOG_ERROR("spmd", "[Lane {}][Iter {}] ERROR broadcast1 mismatch got 0x{:X} expected 0x{:X}", laneId, iter, *st->broadcastDst1, *st->rootValue1);
                } else {
                    LOG_INFO("spmd", "[Lane {}][Iter {}] Received broadcast1 value: 0x{:X}", laneId, iter, *st->broadcastDst1);
                }

                SPMD_SYNC();
                if (SPMD_IS_ROOT(laneCountLocal - 1)) {
                    *st->rootValue2 ^= 0x12345678;
                    LOG_INFO("spmd", "[Lane last][Iter {}] Mutated rootValue2 to 0x{:X}", iter, *st->rootValue2);
                }

                spmd_broadcast(st->group, st->broadcastDst2, st->rootValue2, sizeof(U32), laneCountLocal - 1);
                SPMD_SYNC();
                if (*st->broadcastDst2 != *st->rootValue2) {
                    LOG_ERROR("spmd", "[Lane {}][Iter {}] ERROR broadcast2 mismatch got 0x{:X} expected 0x{:X}", laneId, iter, *st->broadcastDst2, *st->rootValue2);
                } else {
                    LOG_INFO("spmd", "[Lane {}][Iter {}] Received broadcast2 value: 0x{:X}", laneId, iter, *st->broadcastDst2);
                }
                SPMD_SYNC();
            }

            RangeU64 taskRange = SPMD_SPLIT_RANGE(st->totalTasks);
            LOG_INFO("spmd", "Task range: {} - {}", taskRange.min, taskRange.max);
            for (U64 taskIndex = taskRange.min; taskIndex < taskRange.max; ++taskIndex) {
                st->taskAssignments[taskIndex] = (U32) laneId;
            }
            SPMD_SYNC();
            if (SPMD_IS_ROOT(0)) {
                B32 coverageOk = 1;
                for (U32 taskIndex = 0; taskIndex < st->totalTasks; ++taskIndex) {
                    if (st->taskAssignments[taskIndex] >= laneCountLocal) {
                        coverageOk = 0;
                        LOG_ERROR("spmd", "[SPMD Split] ERROR task {} unassigned", taskIndex);
                    }
                }
                if (coverageOk) {
                    LOG_INFO("spmd", "[SPMD Split] All {} tasks assigned across {} lanes", st->totalTasks, laneCountLocal);
                }
            }
            SPMD_SYNC();
        };

        U32 iterations = 3;
        for (U32 i = 0; i < laneCount; ++i) {
            SPMDTestState* st = &states[i];
            st->group = group;
            st->rootValue1 = rootValue1;
            st->rootValue2 = rootValue2;
            st->broadcastDst1 = (U32*) arena_push(arena, sizeof(U32));
            st->broadcastDst2 = (U32*) arena_push(arena, sizeof(U32));
            *st->broadcastDst1 = 0; *st->broadcastDst2 = 0;
            st->iterations = iterations;
            st->totalTasks = totalTasks;
            st->taskAssignments = taskAssignments;
            threadHandles[i] = OS_thread_create(spmd_test_thread, st);
        }

        for (U32 i = 0; i < laneCount; ++i) {
            OS_thread_join(threadHandles[i]);
        }
        LOG_INFO("main", "SPMD test done!");
    }

    {
        TIME_SCOPE("SPMD Dispatch via Job System");
        LOG_INFO("main", "\n=== Testing SPMD Dispatch via Job System ===");
        
        Temp scratch = get_scratch(0, 0);
        Arena* arena = scratch.arena;
        DEFER_REF(temp_end(&scratch));

        U32 workerCount = 4;
        JobSystem* jobSystem = job_system_create(arena, workerCount);
        ASSERT_DEBUG(jobSystem != nullptr);

        struct SPMDDispatchTestParams {
            U32* resultArray;
            U32 arraySize;
            U32* broadcastValue;
            U32* broadcastDst;
        };

        U32 arraySize = 1000;
        U32* resultArray = (U32*) arena_push(arena, sizeof(U32) * arraySize);
        MEMSET(resultArray, 0, sizeof(U32) * arraySize);

        U32* broadcastValue = (U32*) arena_push(arena, sizeof(U32));
        *broadcastValue = 0xCAFEBABE;
        U32* broadcastDst = (U32*) arena_push(arena, sizeof(U32));
        *broadcastDst = 0;

        SPMDDispatchTestParams testParams = {};
        testParams.resultArray = resultArray;
        testParams.arraySize = arraySize;
        testParams.broadcastValue = broadcastValue;
        testParams.broadcastDst = broadcastDst;

        U32 laneCount = 4;

        spmd_dispatch(jobSystem, arena,
            .laneCount = laneCount,
            .kernel = spmd_dispatch_kernel,
            .kernelParameters = &testParams,
            .groupParams = {.broadcastScratchSize = KB(4)}
        );

        B32 allAssigned = 1;
        for (U32 i = 0; i < arraySize; ++i) {
            if (resultArray[i] == 0 || resultArray[i] > laneCount) {
                allAssigned = 0;
                LOG_ERROR("spmd_dispatch", "ERROR: resultArray[{}] = {} (expected 1-{})", i, resultArray[i], laneCount);
                break;
            }
        }
        if (allAssigned) {
            LOG_INFO("spmd_dispatch", "All {} array elements assigned across {} lanes", arraySize, laneCount);
        }

        if (*broadcastDst == *broadcastValue) {
            LOG_INFO("spmd_dispatch", "Broadcast test passed: 0x{:X}", *broadcastDst);
        } else {
            LOG_ERROR("spmd_dispatch", "Broadcast test failed: got 0x{:X}, expected 0x{:X}", *broadcastDst, *broadcastValue);
        }

        job_system_destroy(jobSystem);
        LOG_INFO("main", "SPMD dispatch test done!");
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
        LOG_INFO("main", "{} {} {}", temp->pos, temp->reserved, temp->committed);
        arena_release(arena);

        OS_release(ptr, size);
    }

    {
        LOG_INFO("main", "Testing format specifiers:");
        F64 pi = 3.141592653589793;
        U64 hexValue = 0xDEADBEEF;
        S64 signedValue = -42;
        U64 binaryValue = 13;
        
        LOG_INFO("main", "Default float: {}", pi);                    // "Default float: 3.1416"
        LOG_INFO("main", "Float with 2 decimals: {:.2f}", pi);        // "Float with 2 decimals: 3.1"
        LOG_INFO("main", "Float with 6 decimals: {:.6f}", pi);        // "Float with 6 decimals: 3.14159"
        LOG_INFO("main", "Float with 0 decimals: {:.0f}", pi);        // "Float with 0 decimals: 3"
        
        LOG_INFO("main", "Default integer: {}", hexValue);            // "Default integer: 3735928559"
        LOG_INFO("main", "Decimal: {:d}", hexValue);                 // "Decimal: 3735928559"
        LOG_INFO("main", "Hex lowercase: {:x}", hexValue);           // "Hex lowercase: deadbeef"
        LOG_INFO("main", "Hex uppercase: {:X}", hexValue);           // "Hex uppercase: DEADBEEF"
        LOG_INFO("main", "Binary: {:b}", binaryValue);                // "Binary: 1101"
        LOG_INFO("main", "Octal: {:o}", binaryValue);                 // "Octal: 15"
        
        LOG_INFO("main", "Signed default: {}", signedValue);          // "Signed default: -42"
        LOG_INFO("main", "Signed hex: {:x}", signedValue);           // "Signed hex: -2a"
        LOG_INFO("main", "Signed binary: {:b}", signedValue);        // "Signed binary: -101010"
        
        LOG_INFO("main", "Mixed format: pos=({:.2f}, {:.2f}) id=0x{:X}", 12.3456, -4.0123, hexValue);  // "Mixed format: pos=(12, -4) id=0xDEADBEEF"
    }
}
