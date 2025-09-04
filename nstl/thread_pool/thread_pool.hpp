//
//  Created by André Leite on 28/08/2025.
//

#pragma once

struct WSDeque; // forward declare

struct alignas(CACHE_LINE_SIZE) Job {
    OS_ThreadFunc* func;
    void* params;
    U64 remainingJobs;
    Job* parent;
};

struct JobSystem {
    WSDeque** allQueues;       // index 0 == main thread queue, then 1..workerCount background worker queues
    WSDeque*  mainQueue;       // convenience pointer to allQueues[0]
    OS_Handle* workers;        // background worker threads (count = workerCount)
    U32 workerCount;           // number of background workers (excludes main thread)
    U64 shutdown;              // atomic flag
};

JobSystem* job_system_init(Arena* arena, U32 workerCount);
void job_system_shutdown(JobSystem* jobSystem);
void job_system_worker_thread_entry_point(void* arg);
void job_system_submit(JobSystem* jobSystem, Job* job);       // worker or main (uses TLS or mainQueue)
void job_system_submit_main(JobSystem* jobSystem, Job* job);  // explicit main-thread submission
void job_system_execute(Job* job);
void job_system_wait(JobSystem* jobSystem, Job* root);  // thread helps until root done
void job_system_thread_bind(JobSystem* jobSystem, U32 index); // bind calling thread to worker index
bool job_system_is_worker();
WSDeque* job_system_main_queue(JobSystem* jobSystem);         // returns main queue pointer
