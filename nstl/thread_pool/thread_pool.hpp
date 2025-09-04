//
//  Created by André Leite on 28/08/2025.
//

#pragma once

struct alignas(CACHE_LINE_SIZE) Job {
    OS_ThreadFunc* func;
    void* params;
    U64 remainingJobs;
    Job* parent;
};

struct JobSystem {
    WSDeque** allQueues;
    U32 workerCount;
    OS_Handle* workers;
    U64 shutdown;
};

JobSystem* job_system_init(Arena* arena, U32 workerCount);
void job_system_shutdown(JobSystem* jobSystem);
void job_system_worker_thread_entry_point(void* arg);
void job_system_submit(JobSystem* jobSystem, Job* job);
