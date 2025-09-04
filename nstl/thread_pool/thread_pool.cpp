//
//  Created by André Leite on 28/08/2025.
//

#define JOB_SYSTEM_QUEUE_SIZE 1024

struct WorkerParams {
    JobSystem* jobSystem;
    U32 workerIndex;
};

thread_local WSDeque* localQueue;

JobSystem* job_system_init(Arena* arena, U32 workerCount) {
    if (workerCount == 0) {
        ASSERT_DEBUG(false && "Worker count cannot be zero");
    }

    JobSystem* jobSystem = (JobSystem*)arena_push(arena, sizeof(JobSystem), alignof(JobSystem));
    memset(jobSystem, 0, sizeof(JobSystem));
    jobSystem->allQueues = (WSDeque**)arena_push(arena, sizeof(WSDeque*) * workerCount, alignof(WSDeque*));
    for (U32 i = 0; i < workerCount; ++i) {
        jobSystem->allQueues[i] = wsdq_create(arena, JOB_SYSTEM_QUEUE_SIZE);
    }
    jobSystem->workerCount = workerCount;

    jobSystem->workers = (OS_Handle*)arena_push(arena, sizeof(OS_Handle) * workerCount, alignof(OS_Handle));
    for (U32 i = 0; i < workerCount; ++i) {
        WorkerParams* params = (WorkerParams*)arena_push(arena, sizeof(WorkerParams), alignof(WorkerParams));
        params->jobSystem = jobSystem;
        params->workerIndex = i;
        OS_Handle thread = OS_thread_create(job_system_worker_thread_entry_point, params);
        jobSystem->workers[i] = thread;
    }

    // Store the job system in a global or thread-local variable as needed
    return jobSystem;
}

void job_system_shutdown(JobSystem* jobSystem) {
    if (!jobSystem) {
        ASSERT_DEBUG(false && "Job system is null");
        return;
    }

    ATOMIC_STORE(&jobSystem->shutdown, 1, MEMORY_ORDER_RELAXED);

    for (U32 i = 0; i < jobSystem->workerCount; ++i) {
        OS_thread_join(jobSystem->workers[i]); // Might consider detaching instead, to not block
    }
}

void job_system_worker_thread_entry_point(void* params) {
    ASSERT_DEBUG(params != nullptr);
    WorkerParams* workerParams = (WorkerParams*)params;
    JobSystem* jobSystem = workerParams->jobSystem;
    U32 workerIndex = workerParams->workerIndex;
    localQueue = jobSystem->allQueues[workerIndex];
    WSDeque** allQueues = jobSystem->allQueues;
    while (true) {
        if (UNLIKELY(ATOMIC_LOAD(&jobSystem->shutdown, MEMORY_ORDER_RELAXED))) {
            break;
        }
        void* jobPtr;
        wsdq_pop(localQueue, &jobPtr);
        if (jobPtr) {
            Job* job = (Job*)jobPtr;
            job->func(job->params);
            if (job->parent) {
                ATOMIC_FETCH_SUB(&job->parent->remainingJobs, 1, MEMORY_ORDER_RELAXED);
            }
            continue;
        } else {
            // Try to steal from other queues
            
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
    // If we're on a worker thread, push to its local queue (single-owner fast path).
    if (LIKELY(localQueue != nullptr)) {
        wsdq_push(localQueue, job);
        return;
    }

    // Submission from an external (non-worker) thread.
    // NOTE: The underlying deque is single-producer for the bottom index.
    // For simple testing we push into queue 0 assuming low contention window.
    // A production system should provide a multi-producer submission queue.
    WSDeque* q0 = jobSystem->allQueues[0];
    wsdq_push(q0, job); // Potential benign race if worker 0 simultaneously pushes/pops.
}
