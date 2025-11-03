//
// Created by André Leite on 02/11/2025.
//

#include "app_tests.hpp"
#include "nstl/base/base_include.hpp"
#include "nstl/base/base_log.hpp"
#include "nstl/base/base_job_system.hpp"
#include "nstl/base/base_math.hpp"
#include "app_state.hpp"

#include <math.h>

#define APP_MAX_JOB_SLOTS 8u
#define APP_TEST_ENTITY_COUNT 128u

struct TestEntity {
    F32 x;
    F32 y;
    F32 speed;
    F32 padding;
};

struct AppTestsState {
    U32 entityCount;
    TestEntity entities[APP_TEST_ENTITY_COUNT];
    F32 expectedSpeedSum;
    F32 lastVelocitySum;
    F32 lastVelocityError;
    F32 lastPositionSum;
    U64 jobDispatchCount;
    F32 chunkSums[APP_MAX_JOB_SLOTS + 1u];
};

struct EntityJobParams {
    AppTestsState* tests;
    U32 startIndex;
    U32 count;
    F32 deltaSeconds;
    Arena* programArena;
    F32* sumOut;
};

static void app_tests_step_entities_single_thread(AppRuntime* runtime, AppCoreState* core, AppTestsState* tests, F32 deltaSeconds);
static void app_tests_step_entities_jobs(AppRuntime* runtime, AppCoreState* core, AppTestsState* tests, F32 deltaSeconds);
static void app_tests_job_update(void* userData);

U64 app_tests_permanent_size(void) {
    return sizeof(AppTestsState);
}

void app_tests_initialize(AppRuntime* runtime, AppCoreState* core, AppTestsState* tests) {
    (void) runtime;
    if (!core || !tests) {
        return;
    }

    MEMSET(tests, 0, sizeof(AppTestsState));
    tests->entityCount = APP_TEST_ENTITY_COUNT;

    for (U32 i = 0; i < tests->entityCount; ++i) {
        TestEntity* entity = &tests->entities[i];
        entity->x = (F32) i;
        entity->y = 0.0f;
        entity->speed = 1.0f + (F32) i * 0.25f;
        entity->padding = 0.0f;
        tests->expectedSpeedSum += entity->speed;
    }

    LOG_INFO("tests", "Initialized test entities (count={})", tests->entityCount);
    (void) core;
}

void app_tests_reload(AppRuntime* runtime, AppCoreState* core, AppTestsState* tests) {
    (void) runtime;
    if (!core || !tests) {
        return;
    }

    LOG_INFO("tests", "Reloaded module (dispatches={} totalPos={:.2f})",
             tests->jobDispatchCount, tests->lastPositionSum);
}

void app_tests_tick(AppRuntime* runtime, AppCoreState* core, AppTestsState* tests, F32 deltaSeconds) {
    if (!core || !tests) {
        return;
    }

    if (core->jobSystem) {
        app_tests_step_entities_jobs(runtime, core, tests, deltaSeconds);
    } else {
        app_tests_step_entities_single_thread(runtime, core, tests, deltaSeconds);
    }

    if (tests->lastVelocityError > 0.05f) {
        LOG_WARNING("tests", "Velocity sum drift ({:.3f} vs expected {:.3f})",
                    tests->lastVelocitySum, tests->expectedSpeedSum);
    }

    if ((core->frameCounter % 120ull) == 0ull) {
        TestEntity* first = (tests->entityCount > 0u) ? &tests->entities[0] : 0;
        F32 x = first ? first->x : 0.0f;
        F32 y = first ? first->y : 0.0f;
        LOG_INFO("tests", "Frame {} | jobs={} | vSum={:.2f} | pos0=({:.2f}, {:.2f})",
                 core->frameCounter, tests->jobDispatchCount, tests->lastVelocitySum, x, y);
    }
}

void app_tests_shutdown(AppRuntime* runtime, AppCoreState* core, AppTestsState* tests) {
    (void) runtime;
    if (!core || !tests) {
        return;
    }

    LOG_INFO("tests", "Shutdown (frames={} dispatches={} lastPos={:.2f})",
             core->frameCounter, tests->jobDispatchCount, tests->lastPositionSum);
}

static void app_tests_step_entities_single_thread(AppRuntime* runtime, AppCoreState* core, AppTestsState* tests, F32 deltaSeconds) {
    (void) core;
    if (!tests || tests->entityCount == 0u) {
        tests->lastVelocitySum = 0.0f;
        tests->lastVelocityError = tests->expectedSpeedSum;
        tests->lastPositionSum = 0.0f;
        return;
    }

    Arena* excludes[1] = { runtime && runtime->memory ? runtime->memory->programArena : 0 };
    Temp scratch = get_scratch(excludes, ARRAY_COUNT(excludes));

    F32* snapshot = (tests->entityCount > 0u)
        ? ARENA_PUSH_ARRAY(scratch.arena, F32, tests->entityCount)
        : NULL;

    F64 positionSum = 0.0;
    F32 speedSum = 0.0f;

    for (U32 i = 0; i < tests->entityCount; ++i) {
        TestEntity* entity = &tests->entities[i];
        entity->x += entity->speed * deltaSeconds;
        entity->y += 0.5f * deltaSeconds;
        speedSum += entity->speed;
        if (snapshot) {
            snapshot[i] = entity->x;
        }
        positionSum += (F64) entity->x;
    }

    tests->lastVelocitySum = speedSum;
    tests->lastVelocityError = fabsf(tests->expectedSpeedSum - speedSum);
    tests->lastPositionSum = (F32) positionSum;

    temp_end(&scratch);
}

static void app_tests_job_update(void* userData) {
    EntityJobParams* params = (EntityJobParams*) userData;
    if (!params || !params->tests || params->count == 0u) {
        return;
    }

    AppTestsState* tests = params->tests;
    if (params->sumOut) {
        *params->sumOut = 0.0f;
    }

    Arena* excludes[1] = { params->programArena };
    Temp scratch = get_scratch(excludes, ARRAY_COUNT(excludes));

    F32* displacements = ARENA_PUSH_ARRAY(scratch.arena, F32, params->count);
    F32 localSpeedSum = 0.0f;

    for (U32 i = 0; i < params->count; ++i) {
        U32 entityIndex = params->startIndex + i;
        if (entityIndex >= tests->entityCount) {
            break;
        }
        TestEntity* entity = &tests->entities[entityIndex];
        F32 distance = entity->speed * params->deltaSeconds;
        displacements[i] = distance;
        entity->x += distance;
        entity->y += 0.5f * params->deltaSeconds;
        localSpeedSum += entity->speed;
    }

    if (params->sumOut) {
        *params->sumOut = localSpeedSum;
    }

    temp_end(&scratch);
}

static void app_tests_step_entities_jobs(AppRuntime* runtime, AppCoreState* core, AppTestsState* tests, F32 deltaSeconds) {
    if (!runtime || !core || !tests || !core->jobSystem || tests->entityCount == 0u) {
        app_tests_step_entities_single_thread(runtime, core, tests, deltaSeconds);
        return;
    }

    U32 jobSlots = core->workerCount + 1u;
    if (jobSlots > (APP_MAX_JOB_SLOTS + 1u)) {
        jobSlots = APP_MAX_JOB_SLOTS + 1u;
    }
    if (jobSlots == 0u) {
        jobSlots = 1u;
    }

    U32 chunkSize = (tests->entityCount + jobSlots - 1u) / jobSlots;
    if (chunkSize == 0u) {
        chunkSize = tests->entityCount;
    }

    Job rootJob = {};
    rootJob.remainingJobs = 0;

    U32 submittedJobs = 0u;

    for (U32 jobIndex = 0; jobIndex < jobSlots; ++jobIndex) {
        U32 start = jobIndex * chunkSize;
        if (start >= tests->entityCount) {
            break;
        }
        U32 count = tests->entityCount - start;
        if (count > chunkSize) {
            count = chunkSize;
        }

        tests->chunkSums[jobIndex] = 0.0f;
        EntityJobParams params = {
            .tests = tests,
            .startIndex = start,
            .count = count,
            .deltaSeconds = deltaSeconds,
            .programArena = runtime->memory ? runtime->memory->programArena : 0,
            .sumOut = &tests->chunkSums[jobIndex],
        };

        job_system_submit((.function = app_tests_job_update, .parent = &rootJob), params);
        submittedJobs += 1u;
    }

    if (submittedJobs > 0u) {
        job_system_wait(core->jobSystem, &rootJob);
        tests->jobDispatchCount += 1ull;
    }

    F32 totalSpeed = 0.0f;
    for (U32 i = 0; i < submittedJobs; ++i) {
        totalSpeed += tests->chunkSums[i];
    }

    tests->lastVelocitySum = totalSpeed;
    tests->lastVelocityError = fabsf(tests->expectedSpeedSum - totalSpeed);

    Arena* excludes[1] = { runtime->memory ? runtime->memory->programArena : 0 };
    Temp scratch = get_scratch(excludes, ARRAY_COUNT(excludes));
    F32* snapshot = (tests->entityCount > 0u)
        ? ARENA_PUSH_ARRAY(scratch.arena, F32, tests->entityCount)
        : NULL;

    F64 positionSum = 0.0;
    for (U32 i = 0; i < tests->entityCount; ++i) {
        if (snapshot) {
            snapshot[i] = tests->entities[i].x;
        }
        positionSum += (F64) tests->entities[i].x;
    }

    tests->lastPositionSum = (F32) positionSum;
    temp_end(&scratch);
}

