//
//  Created by André Leite on 28/08/2025.
//
//  Inspired by: Molecular Musings blog post series "Job System 2.0: Lock-Free Work Stealing"
//  https://blog.molecular-matters.com/2015/08/24/job-system-2-0-lock-free-work-stealing-part-1-basics/
//

#pragma once

// ////////////////////////
// Configuration

#ifndef JOB_SYSTEM_QUEUE_SIZE
#define JOB_SYSTEM_QUEUE_SIZE (1 << 14)
#endif

#ifndef JOB_SYSTEM_STEAL_TRIES
#define JOB_SYSTEM_STEAL_TRIES 3u
#endif

#ifndef JOB_SYSTEM_IDLE_SPIN_CYCLES
#define JOB_SYSTEM_IDLE_SPIN_CYCLES 64u
#endif

#ifndef JOB_SYSTEM_BACKOFF_MAX
#define JOB_SYSTEM_BACKOFF_MAX 1024u
#endif


// ////////////////////////
// Types & Data

#ifndef NDEBUG
struct JobSystemStats {
    U64 pops;
    U64 steals;
    U64 yields;
};
#endif

typedef void JobFunc(void*);

struct alignas(CACHE_LINE_SIZE) Job {
    JobFunc* function;
    U64 remainingJobs;
    Job* parent;
    U8 parameters[CACHE_LINE_SIZE - sizeof(JobFunc*) - sizeof(U64) - sizeof(Job*)];
};

#define JOB_PARAMETER_SPACE (CACHE_LINE_SIZE - sizeof(JobFunc*) - sizeof(U64) - sizeof(Job*))
static_assert(sizeof(Job) == CACHE_LINE_SIZE, "Job struct must exactly fill cache line");

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

static JobSystem* job_system_create(Arena* arena, U32 workerCount);
static void job_system_destroy(JobSystem* jobSystem);

static B32 job_system_submit_(const Job& job);

static void job_system_wait(JobSystem* jobSystem, Job* root);

#ifndef NDEBUG
static JobSystemStats job_system_get_totals(JobSystem* jobSystem);
#endif

// ////////////////////////
// Single submission macro with optional parameter argument
// Usage:
//   job_system_submit((.function = myFunc));
//   job_system_submit((.function = myFunc, .parent = &root), myParams);

#define JOB_REMOVE_PARENS(...) __VA_ARGS__

#define job_system_submit(jobInit, ...) \
    do { \
        Job _jobTmp = (Job){ JOB_REMOVE_PARENS jobInit }; \
        __VA_OPT__( static_assert(sizeof(__VA_ARGS__) <= JOB_PARAMETER_SPACE, "Parameter too large for inline storage"); ) \
        __VA_OPT__( memcpy(_jobTmp.parameters, &(__VA_ARGS__), (U32)sizeof(__VA_ARGS__)); ) \
        job_system_submit_(_jobTmp); \
    } while (0)
