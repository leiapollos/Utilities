//
//  Created by André Leite on 28/08/2025.
//
//  Inspired by: Molecular Musings blog post series "Job System 2.0: Lock-Free Work Stealing"
//  https://blog.molecular-matters.com/2015/08/24/job-system-2-0-lock-free-work-stealing-part-1-basics/

static_assert(is_power_of_two(JOB_SYSTEM_QUEUE_SIZE), "JOB_SYSTEM_QUEUE_SIZE must be a power of two");
#define JOB_SYSTEM_INVALID_WORKER_INDEX 0xFFFFFFFFu

// ////////////////////////
// JobSystem struct definition

struct JobSystem {
    WSDeque** queues;
    OS_Handle* workers;
    U32 workerCount;
    U64 shutdown;
#ifndef NDEBUG
    JobSystemStats* workerStats; // length = workerCount + 1 (main + workers)
    JobSystemStats totals;
#endif
};

// ////////////////////////
// TLS state and helpers

struct JobSystemThreadState {
    U32 workerIndex;
    WSDeque* queue;
    XorShift randomGenerator;
    JobSystem* jobSystem;
#ifndef NDEBUG
    JobSystemStats stats;
#endif
};

thread_local JobSystemThreadState g_tlsJobState = {};

static
void job_execute(const Job* job) {
    ASSERT_DEBUG(job && job->function);
    job->function((void*) job->parameters);
    if (job->parent) {
        ATOMIC_FETCH_SUB(&job->parent->remainingJobs, 1, MEMORY_ORDER_ACQ_REL);
    }
}

static U32 job_system_pick_victim(U32 selfIndex,
                                  U32 totalQueues,
                                  XorShift* rng,
                                  U32 attempt) {
    if (totalQueues <= 1u) {
        return 0u;
    }

    U32 victim = xorshift_bounded(rng, totalQueues);
    victim = (victim + attempt) % totalQueues;

    if (victim == selfIndex) {
        victim = (victim + 1u) % totalQueues;
    }

    return victim;
}

// ////////////////////////
// Worker thread parameters

struct WorkerParameters {
    JobSystem* jobSystem;
    U32 workerIndex;
};

static void job_system_worker_entry(void* params);

// ////////////////////////
// Lifecycle

static
JobSystem* job_system_create(Arena* arena, U32 workerCount) {
    ASSERT_DEBUG(workerCount >= 1);
    ASSERT_DEBUG(g_tlsJobState.jobSystem == nullptr && "JobSystem already initialized on this thread");
    JobSystem* jobSystem = (JobSystem*) arena_push(arena, sizeof(JobSystem), alignof(JobSystem));
    memset(jobSystem, 0, sizeof(JobSystem));
    jobSystem->workerCount = workerCount;

    U32 totalQueues = workerCount + 1u;
    jobSystem->queues = (WSDeque**) arena_push(arena, sizeof(WSDeque*) * totalQueues, alignof(WSDeque*));
    for (U32 i = 0; i < totalQueues; ++i) {
        jobSystem->queues[i] = wsdq_create(arena, JOB_SYSTEM_QUEUE_SIZE, sizeof(Job));
    }

    jobSystem->workers = (OS_Handle*) arena_push(arena, sizeof(OS_Handle) * workerCount, alignof(OS_Handle));
#ifndef NDEBUG
    jobSystem->workerStats = (JobSystemStats*) arena_push(arena, sizeof(JobSystemStats) * totalQueues,
                                                          alignof(JobSystemStats));
    memset(jobSystem->workerStats, 0, sizeof(JobSystemStats) * totalQueues);
    memset(&jobSystem->totals, 0, sizeof(jobSystem->totals));
#endif

    g_tlsJobState.workerIndex = 0;
    g_tlsJobState.queue = jobSystem->queues[0];
    g_tlsJobState.jobSystem = jobSystem;
    g_tlsJobState.randomGenerator = xorshift_seed(((U64) (uintptr_t) jobSystem) ^ 0xD1B54A32D192ED03ull);
#ifndef NDEBUG
    memset(&g_tlsJobState.stats, 0, sizeof(g_tlsJobState.stats));
#endif

    for (U32 i = 0; i < workerCount; ++i) {
        WorkerParameters* workerParameters = (WorkerParameters*) arena_push(
            arena, sizeof(WorkerParameters), alignof(WorkerParameters));
        workerParameters->jobSystem = jobSystem;
        workerParameters->workerIndex = i + 1u;
        jobSystem->workers[i] = OS_thread_create(job_system_worker_entry, workerParameters);
#ifndef NDEBUG
        memset(&jobSystem->workerStats[i], 0, sizeof(JobSystemStats));
#endif
    }

    return jobSystem;
}

static
void job_system_destroy(JobSystem* jobSystem) {
    if (!jobSystem) {
        return;
    }

    ATOMIC_STORE(&jobSystem->shutdown, 1, MEMORY_ORDER_RELEASE);
    for (U32 i = 0; i < jobSystem->workerCount; ++i) {
        OS_thread_join(jobSystem->workers[i]);
    }

#ifndef NDEBUG
    jobSystem->workerStats[0].pops += g_tlsJobState.stats.pops;
    jobSystem->workerStats[0].steals += g_tlsJobState.stats.steals;
    jobSystem->workerStats[0].yields += g_tlsJobState.stats.yields;

    memset(&jobSystem->totals, 0, sizeof(jobSystem->totals));
    for (U32 i = 0; i < (jobSystem->workerCount + 1u); ++i) {
        jobSystem->totals.pops += jobSystem->workerStats[i].pops;
        jobSystem->totals.steals += jobSystem->workerStats[i].steals;
        jobSystem->totals.yields += jobSystem->workerStats[i].yields;
    }
#endif

    g_tlsJobState.workerIndex = JOB_SYSTEM_INVALID_WORKER_INDEX;
    memset(&g_tlsJobState, 0, sizeof(g_tlsJobState));
}

// ////////////////////////
// Submission

static
B32 job_system_submit_(const Job& job) {
    ASSERT_DEBUG(g_tlsJobState.queue);
    if (job.parent) {
        ATOMIC_FETCH_ADD(&job.parent->remainingJobs, 1, MEMORY_ORDER_RELEASE);
    }
    B32 pushOk = wsdq_push(g_tlsJobState.queue, &job);
    ASSERT_ALWAYS(pushOk && "WSDeque overflow. Increase JOB_SYSTEM_QUEUE_SIZE.");
    return pushOk;
}

static
void job_system_worker_entry(void* params) {
    ASSERT_DEBUG(params);
    WorkerParameters* workerParameters = (WorkerParameters*) params;
    JobSystem* jobSystem = workerParameters->jobSystem;
    U32 workerIndex = workerParameters->workerIndex;

    ASSERT_DEBUG(g_tlsJobState.jobSystem == nullptr && "JobSystem already initialized on this thread");

    g_tlsJobState.workerIndex = workerIndex;
    g_tlsJobState.queue = jobSystem->queues[workerIndex];
    g_tlsJobState.jobSystem = jobSystem;
    U64 seed = ((U64) (uintptr_t) workerParameters) ^ ((U64) (uintptr_t) jobSystem) ^ (
                   (U64) workerIndex * 0x9E3779B97F4A7C15ull);
    g_tlsJobState.randomGenerator = xorshift_seed(seed);
#ifndef NDEBUG
    memset(&g_tlsJobState.stats, 0, sizeof(g_tlsJobState.stats));
#endif

    WSDeque** queues = jobSystem->queues;
    U32 totalQueues = jobSystem->workerCount + 1u;

    U32 backoff = 1u;

    while (LIKELY(!ATOMIC_LOAD(&jobSystem->shutdown, MEMORY_ORDER_ACQUIRE))) {
        Job job = {};

        if (LIKELY(wsdq_pop(g_tlsJobState.queue, &job))) {
#ifndef NDEBUG
            ++g_tlsJobState.stats.pops;
#endif
            job_execute(&job);
            backoff = 1u;
            continue;
        }

        B32 stolen = 0;
        for (U32 attempt = 0; attempt < JOB_SYSTEM_STEAL_TRIES; ++attempt) {
            U32 victimIndex = job_system_pick_victim(workerIndex, totalQueues, &g_tlsJobState.randomGenerator, attempt);
            if (LIKELY(wsdq_steal(queues[victimIndex], &job))) {
#ifndef NDEBUG
                ++g_tlsJobState.stats.steals;
#endif
                job_execute(&job);
                stolen = 1;
                backoff = 1u;
                break;
            }
        }

        if (UNLIKELY(!stolen)) {
            U32 spins = backoff;
            if (spins > JOB_SYSTEM_BACKOFF_MAX) {
                spins = JOB_SYSTEM_BACKOFF_MAX;
            }
            for (U32 spin = 0; spin < spins; ++spin) {
                OS_cpu_pause();
            }
#ifndef NDEBUG
            ++g_tlsJobState.stats.yields;
#endif
            OS_thread_yield();
            if (backoff < JOB_SYSTEM_BACKOFF_MAX) {
                U32 next = backoff << 1;
                backoff = (next > JOB_SYSTEM_BACKOFF_MAX) ? JOB_SYSTEM_BACKOFF_MAX : next;
            }
        }
    }

#ifndef NDEBUG
    if (jobSystem->workerStats && workerIndex < (jobSystem->workerCount + 1u)) {
        JobSystemStats* s = &jobSystem->workerStats[workerIndex];
        s->pops += g_tlsJobState.stats.pops;
        s->steals += g_tlsJobState.stats.steals;
        s->yields += g_tlsJobState.stats.yields;
    }
#endif

    memset(&g_tlsJobState, 0, sizeof(g_tlsJobState));
}

// ////////////////////////
// Waiting

static
void job_system_wait(JobSystem* jobSystem, Job* root) {
    ASSERT_DEBUG(jobSystem && root);
    WSDeque* localQueue = g_tlsJobState.queue;
    ASSERT_DEBUG(localQueue);

    WSDeque** queues = jobSystem->queues;
    U32 totalQueues = jobSystem->workerCount + 1u;
    U32 workerIndex = g_tlsJobState.workerIndex;
    if (g_tlsJobState.randomGenerator.state == 0) {
        U64 seed = ((U64) (uintptr_t) jobSystem) ^ OS_get_time_nanoseconds();
        g_tlsJobState.randomGenerator = xorshift_seed(seed);
    }

    U32 backoff = 1u;

    while (ATOMIC_LOAD(&root->remainingJobs, MEMORY_ORDER_ACQUIRE) > 0) {
        Job job = {};

        if (LIKELY(wsdq_pop(localQueue, &job))) {
#ifndef NDEBUG
            ++g_tlsJobState.stats.pops;
#endif
            job_execute(&job);
            backoff = 1u;
            continue;
        }

        B32 stolen = 0;
        for (U32 attempt = 0; attempt < JOB_SYSTEM_STEAL_TRIES; ++attempt) {
            U32 victimIndex = job_system_pick_victim(workerIndex, totalQueues, &g_tlsJobState.randomGenerator, attempt);
            if (LIKELY(wsdq_steal(queues[victimIndex], &job))) {
#ifndef NDEBUG
                ++g_tlsJobState.stats.steals;
#endif
                job_execute(&job);
                stolen = 1;
                backoff = 1u;
                break;
            }
        }

        if (UNLIKELY(!stolen)) {
            U32 spins = backoff;
            if (spins > JOB_SYSTEM_BACKOFF_MAX) {
                spins = JOB_SYSTEM_BACKOFF_MAX;
            }
            for (U32 spin = 0; spin < spins; ++spin) {
                OS_cpu_pause();
            }
#ifndef NDEBUG
            ++g_tlsJobState.stats.yields;
#endif
            OS_thread_yield();
            if (backoff < JOB_SYSTEM_BACKOFF_MAX) {
                U32 next = backoff << 1;
                backoff = (next > JOB_SYSTEM_BACKOFF_MAX) ? JOB_SYSTEM_BACKOFF_MAX : next;
            }
        }
    }
}

#ifndef NDEBUG
static JobSystemStats job_system_get_totals(JobSystem* jobSystem) {
    return jobSystem->totals;
}
#endif

