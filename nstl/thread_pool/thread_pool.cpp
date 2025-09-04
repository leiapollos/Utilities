//
//  Created by André Leite on 28/08/2025.
//

#define JOB_SYSTEM_QUEUE_SIZE 1024

#include "../base/base_include.hpp"
#include "../os/os_include.hpp"
#include "../spmc/spmc.hpp"
#include "thread_pool.hpp"
#include <cstring>

struct WorkerParams { JobSystem* js; U32 index; }; // index 0 is main thread (never started as worker)

thread_local U32 tls_workerIndex = 0xffffffffu; // 1..workerCount valid for background workers
thread_local WSDeque* tls_queue = nullptr;      // points into jobSystem->allQueues[index]

JobSystem* job_system_init(Arena* arena, U32 workerCount) {
    ASSERT_DEBUG(workerCount >= 1); // at least main thread
    JobSystem* js = (JobSystem*)arena_push(arena, sizeof(JobSystem), alignof(JobSystem));
    memset(js, 0, sizeof(JobSystem));
    js->workerCount = workerCount; // background workers
    U32 totalQueues = workerCount + 1; // +1 for main queue
    js->allQueues = (WSDeque**)arena_push(arena, sizeof(WSDeque*) * totalQueues, alignof(WSDeque*));
    for (U32 i = 0; i < totalQueues; ++i) {
        js->allQueues[i] = wsdq_create(arena, JOB_SYSTEM_QUEUE_SIZE);
    }
    js->mainQueue = js->allQueues[0];
    if (workerCount > 0) {
        js->workers = (OS_Handle*)arena_push(arena, sizeof(OS_Handle) * workerCount, alignof(OS_Handle));
        for (U32 i = 0; i < workerCount; ++i) {
            WorkerParams* p = (WorkerParams*)arena_push(arena, sizeof(WorkerParams), alignof(WorkerParams));
            p->js = js;
            p->index = i + 1; // worker i gets queue index i+1
            OS_Handle h = OS_thread_create(job_system_worker_thread_entry_point, p);
            js->workers[i] = h;
        }
    }
    // main thread uses index 0 queue directly; it never binds a worker index
    return js;
}

void job_system_shutdown(JobSystem* jobSystem) {
    if (!jobSystem) {
        ASSERT_DEBUG(false && "Job system is null");
        return;
    }

    ATOMIC_STORE(&jobSystem->shutdown, 1, MEMORY_ORDER_RELAXED);

    for (U32 i = 0; i < jobSystem->workerCount; ++i) {
        OS_thread_join(jobSystem->workers[i]);
    }
}

void job_system_worker_thread_entry_point(void* params) {
    ASSERT_DEBUG(params != nullptr);
    WorkerParams* wp = (WorkerParams*)params;
    JobSystem* js = wp->js;
    tls_workerIndex = wp->index; // 1..workerCount
    tls_queue = js->allQueues[tls_workerIndex];
    WSDeque** queues = js->allQueues;
    while (true) {
        if (UNLIKELY(ATOMIC_LOAD(&js->shutdown, MEMORY_ORDER_RELAXED))) {
            break;
        }
        void* jobPtr;
        B32 gotJob = wsdq_pop(tls_queue, &jobPtr);
        if (gotJob && jobPtr) {
            job_system_execute((Job*)jobPtr);
        } else {
            for (U32 i = 0; i < js->workerCount + 1; ++i) { // include main queue 0
                if (i == tls_workerIndex) {
                    continue;
                }
                WSDeque* targetQueue = queues[i];
                B32 stole = wsdq_steal(targetQueue, &jobPtr);
                if (stole && jobPtr) {
                    std::cout << "Stole job from worker " << i << "\n";
                    job_system_execute((Job*)jobPtr);
                    break; // Successfully stole a job, break out of the stealing loop
                }
            }
        }
    }
}

void job_system_submit(JobSystem* jobSystem, Job* job) {
    if (!jobSystem || !job) {
        ASSERT_DEBUG(false && "Job system or job is null");
        return;
    }

    if (job->parent) {
        ATOMIC_FETCH_ADD(&job->parent->remainingJobs, 1, MEMORY_ORDER_RELAXED);
    }
    if (tls_queue) {
        wsdq_push(tls_queue, job); // background worker path
        return;
    }
    // fallback: submit to main queue (main thread or external thread)
    wsdq_push(jobSystem->mainQueue, job);
}

void job_system_execute(Job* job) {
    if (!job) {
        return;
    }
    job->func(job->params);
    if (job->parent) {
        ATOMIC_FETCH_SUB(&job->parent->remainingJobs, 1, MEMORY_ORDER_RELAXED);
    }
}

void job_system_wait(JobSystem* js, Job* root) {
    ASSERT_DEBUG(js && root);
    // Allow main or external threads to assist: steal/pop from all queues.
    WSDeque* local = tls_queue ? tls_queue : js->mainQueue;
    while (ATOMIC_LOAD(&root->remainingJobs, MEMORY_ORDER_ACQUIRE) > 0) {
        void* jobPtr = nullptr;
        if (wsdq_pop(local, &jobPtr) && jobPtr) {
            job_system_execute((Job*)jobPtr);
            continue;
        }
        for (U32 i = 0; i < js->workerCount + 1; ++i) {
            if (js->allQueues[i] == local) continue;
            if (wsdq_steal(js->allQueues[i], &jobPtr) && jobPtr) {
                job_system_execute((Job*)jobPtr);
                break;
            }
        }
    }
}

bool job_system_is_worker() { return tls_workerIndex != 0xffffffffu; }

void job_system_thread_bind(JobSystem* js, U32 index) {
    ASSERT_DEBUG(js);
    ASSERT_DEBUG(index < js->workerCount);
    tls_workerIndex = index + 1; // map 0..workerCount-1 -> queues 1..workerCount
    tls_queue = js->allQueues[tls_workerIndex];
}

void job_system_submit_main(JobSystem* js, Job* job) {
    ASSERT_DEBUG(js && job);
    if (job->parent) {
        ATOMIC_FETCH_ADD(&job->parent->remainingJobs, 1, MEMORY_ORDER_RELAXED);
    }
    wsdq_push(js->mainQueue, job);
}

WSDeque* job_system_main_queue(JobSystem* js) { return js->mainQueue; }
