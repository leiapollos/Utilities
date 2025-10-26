//
// Created by André Leite on 04/08/2025.
//

#if PROFILER_ENABLED

struct ProfilerGlobalState {
    U64 globalStartMicros;
    U32 threadCount;
    ProfilerThreadState* threadStates[PROFILER_MAX_THREADS];

#if PROFILER_USE_TSC
    U64 tscFrequencyHz;
    F64 tscTicksToMicros;
#endif

    OS_Handle registryMutex;
};

static ProfilerGlobalState g_profiler = {};

thread_local ProfilerThreadState* g_tlsProfilerState = nullptr;

static void profiler_register_tls_(ProfilerThreadState* tls) {
    if (!tls) {
        return;
    }
    OS_mutex_lock(g_profiler.registryMutex);
    if (g_profiler.threadCount < PROFILER_MAX_THREADS) {
        g_profiler.threadStates[g_profiler.threadCount++] = tls;
    }
    OS_mutex_unlock(g_profiler.registryMutex);
}

U64 ProfClock::now() {
#if PROFILER_USE_TSC
#if PROFILER_TSC_SERIALIZE
    return OS_rdtscp_serialized();
#else
    return OS_rdtsc_relaxed();
#endif
#else
    return OS_get_time_microseconds();
#endif
}

U64 ProfClock::to_micros(U64 ticksOrMicros) {
#if PROFILER_USE_TSC
    return (U64) (g_profiler.tscTicksToMicros * (F64) ticksOrMicros);
#else
    return ticksOrMicros;
#endif
}

void profiler_initialize() {
    memset(&g_profiler, 0, sizeof(g_profiler));
    g_profiler.registryMutex = OS_mutex_create();

#if PROFILER_USE_TSC
    // Calibrate TSC-equivalent frequency using platform counters
    {
        U64 hz = 0;
        hz = OS_get_counter_frequency_hz();
        if (hz == 0) {
            const U64 targetNs = MILLION(50ULL);

#if PROFILER_TSC_SERIALIZE
    U64 t0 = OS_rdtscp_serialized();
#else
    U64 t0 = OS_rdtsc_relaxed();
#endif
    U64 ns0 = OS_get_time_nanoseconds();

    U64 ns1 = ns0;
    while ((ns1 - ns0) < targetNs) {
        ns1 = OS_get_time_nanoseconds();
    }

#if PROFILER_TSC_SERIALIZE
    U64 t1 = OS_rdtscp_serialized();
#else
    U64 t1 = OS_rdtsc_relaxed();
#endif

    U64 dt = (t1 >= t0) ? (t1 - t0) : 0;
    U64 dns = (ns1 >= ns0) ? (ns1 - ns0) : 1;
    if (dns != 0 && dt != 0) {
        __uint128_t num = (__uint128_t) dt * BILLION(1ULL);
        hz = (U64) (num / dns);
    }
    }

    g_profiler.tscFrequencyHz = hz;
    g_profiler.tscTicksToMicros = (hz != 0) ? (1000000.0 / (F64) hz) : 1.0;
  }
#endif

    g_profiler.globalStartMicros = ProfClock::to_micros(ProfClock::now());
}

void profiler_shutdown() {
    OS_mutex_destroy(g_profiler.registryMutex);
    g_profiler.registryMutex = {0};
}

ProfilerThreadState* profiler_get_tls() {
    if (g_tlsProfilerState) {
        return g_tlsProfilerState;
    }

    profiler_init_thread(nullptr,
#if PROFILER_TRACE_EVENTS
                       PROFILER_DEFAULT_TRACE_EVENT_CAPACITY
#else
                       0
#endif
    );
    return g_tlsProfilerState;
}

void profiler_init_thread(const char* name, U32 traceEventCapacityIfEnabled) {
    if (g_tlsProfilerState) {
        if (name) {
            g_tlsProfilerState->threadName = name;
        }
        return;
    }

    ProfilerThreadState* tls =
            (ProfilerThreadState*) malloc(sizeof(ProfilerThreadState));
    memset(tls, 0, sizeof(*tls));
    tls->currentParentIndex = 0;
    tls->threadId = OS_get_thread_id_u32();
    tls->threadName = name;

#if PROFILER_TRACE_EVENTS
    U32 cap = traceEventCapacityIfEnabled
                  ? traceEventCapacityIfEnabled
                  : PROFILER_DEFAULT_TRACE_EVENT_CAPACITY;
    tls->events = nullptr;
    tls->eventCapacity = 0;
    tls->eventCount = 0;
    if (cap > 0) {
        tls->events = (ProfilerThreadState::TraceEvent*) malloc(
            sizeof(ProfilerThreadState::TraceEvent) * (size_t) cap);
        tls->eventCapacity = cap;
        tls->eventCount = 0;
    }
#endif

    g_tlsProfilerState = tls;
    profiler_register_tls_(tls);
}

void profiler_set_thread_name(const char* name) {
    ProfilerThreadState* tls = profiler_get_tls();
    tls->threadName = name;
}

FORCE_INLINE TimedScope::TimedScope(U32 index_, const char* label_) noexcept {
    ProfilerThreadState* tls = profiler_get_tls();
    index = index_;

    prevParent = tls->currentParentIndex;
    tls->currentParentIndex = index;

    ProfilerEntry* entry = &tls->entries[index];
    entry->label = label_;

    startTicks = ProfClock::now();
}

FORCE_INLINE TimedScope::~TimedScope() noexcept {
    ProfilerThreadState* tls = g_tlsProfilerState;
    if (!tls) {
        return;
    }

    const U64 endTicks = ProfClock::now();
    const U64 elapsedTicks = (endTicks >= startTicks) ? (endTicks - startTicks) : 0;
    const U64 elapsedMicros = ProfClock::to_micros(elapsedTicks);

    const U32 parentIndex = prevParent;
    tls->currentParentIndex = prevParent;

    ProfilerEntry* entry = &tls->entries[index];
    entry->microsExclusive += elapsedMicros;
    entry->microsInclusive += elapsedMicros;
    entry->callCount += 1;

    if (parentIndex != 0) {
        ProfilerEntry* parent = &tls->entries[parentIndex];
        parent->microsExclusive -= elapsedMicros;
    }

#if PROFILER_TRACE_EVENTS
    if (tls->events && tls->eventCount < tls->eventCapacity) {
        ProfilerThreadState::TraceEvent* e = &tls->events[tls->eventCount++];
        e->eventName = entry->label ? entry->label : "(null)";
        const U64 startMicros = ProfClock::to_micros(startTicks);
        e->timestampMicros = startMicros;
        e->durationMicros = elapsedMicros;
        e->threadId = tls->threadId;
        e->category = PROFILER_EVENT_CATEGORY;
    }
#endif
}

void profiler_print_report() {
    ProfilerEntry agg[PROFILER_MAX_ANCHORS];
    memset(agg, 0, sizeof(agg));

    OS_mutex_lock(g_profiler.registryMutex);
    const U32 tcount = g_profiler.threadCount;

    for (U32 t = 0; t < tcount; ++t) {
        ProfilerThreadState* tls = g_profiler.threadStates[t];
        if (!tls) {
            continue;
        }

        for (U32 i = 1; i < PROFILER_MAX_ANCHORS; ++i) {
            const ProfilerEntry* src = &tls->entries[i];
            if (src->callCount == 0) {
                continue;
            }

            ProfilerEntry* dst = &agg[i];
            dst->microsExclusive += src->microsExclusive;
            dst->microsInclusive += src->microsInclusive;
            dst->callCount += src->callCount;
            if (!dst->label && src->label) {
                dst->label = src->label;
            }
        }
    }
    OS_mutex_unlock(g_profiler.registryMutex);

    const U64 nowMicros = ProfClock::to_micros(ProfClock::now());
    const U64 totalMicros =
            (nowMicros >= g_profiler.globalStartMicros)
                ? (nowMicros - g_profiler.globalStartMicros)
                : 0;

    printf("\n=== Performance Report ===\n");
    if (totalMicros == 0) {
        printf("Total Duration: 0.000 ms\n");
        printf("(No time elapsed; nothing to report.)\n");
        printf("==========================\n");
        return;
    }

    printf("Total Duration: %.3f ms\n", (F64) totalMicros / 1000.0);
    printf("--------------------------\n");

    U32 sorted[PROFILER_MAX_ANCHORS];
    U32 count = 0;
    for (U32 i = 1; i < PROFILER_MAX_ANCHORS; ++i) {
        if (agg[i].callCount > 0) {
            sorted[count++] = i;
        }
    }

    for (U32 i = 0; i + 1 < count; ++i) {
        for (U32 j = i + 1; j < count; ++j) {
            if (agg[sorted[i]].microsExclusive < agg[sorted[j]].microsExclusive) {
                U32 tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    for (U32 k = 0; k < count; ++k) {
        const U32 i = sorted[k];
        const ProfilerEntry* e = &agg[i];

        const F64 msExclusive = (F64) e->microsExclusive / 1000.0;
        const F64 msInclusive = (F64) e->microsInclusive / 1000.0;
        const F64 pctExclusive =
                100.0 * (F64) e->microsExclusive / (F64) totalMicros;
        const F64 pctInclusive =
                100.0 * (F64) e->microsInclusive / (F64) totalMicros;

        printf("%-32s %8llu calls | %8.3f ms (%5.1f%%) | %8.3f ms (%5.1f%% incl)\n",
               e->label ? e->label : "(null)",
               (unsigned long long) e->callCount,
               msExclusive,
               pctExclusive,
               msInclusive,
               pctInclusive);
    }

    printf("==========================\n");
}

void profiler_dump_trace_json(const char* path) {
#if !PROFILER_TRACE_EVENTS
    (void) path;
    return;
#else
    FILE* f = fopen(path, "wb");
    if (!f) {
        return;
    }

    fprintf(f, "{ \"traceEvents\": [\n");

    bool first = true;

    OS_mutex_lock(g_profiler.registryMutex);
    const U32 tcount = g_profiler.threadCount;

    for (U32 t = 0; t < tcount; ++t) {
        ProfilerThreadState* tls = g_profiler.threadStates[t];
        if (!tls || !tls->threadName) {
            continue;
        }

        if (!first) {
            fprintf(f, ",\n");
        }
        first = false;

        fprintf(f,
                "{\"ph\":\"M\",\"name\":\"thread_name\",\"pid\":1,"
                "\"tid\":%u,\"args\":{\"name\":\"%s\"}}",
                tls->threadId,
                tls->threadName);
    }

    for (U32 t = 0; t < tcount; ++t) {
        ProfilerThreadState* tls = g_profiler.threadStates[t];
        if (!tls || !tls->events) {
            continue;
        }

        for (U32 i = 0; i < tls->eventCount; ++i) {
            const ProfilerThreadState::TraceEvent* e = &tls->events[i];

            if (!first) {
                fprintf(f, ",\n");
            }
            first = false;

            fprintf(f,
                    "{\"name\":\"%s\",\"cat\":\"%s\",\"ph\":\"X\","
                    "\"ts\":%llu,\"dur\":%llu,\"pid\":1,\"tid\":%u}",
                    e->eventName ? e->eventName : "",
                    e->category ? e->category : PROFILER_EVENT_CATEGORY,
                    (unsigned long long) e->timestampMicros,
                    (unsigned long long) e->durationMicros,
                    e->threadId);
        }
    }

    OS_mutex_unlock(g_profiler.registryMutex);

    fprintf(f, "\n] }\n");
    fclose(f);
#endif
}

#endif // PROFILER_ENABLED
