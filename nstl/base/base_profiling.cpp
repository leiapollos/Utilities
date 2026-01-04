//
// Created by André Leite on 04/08/2025.
//

#if PROFILER_ENABLED

struct ProfilerGlobalState {
    U64 globalStartMicros;
    U32 threadCount;
    ProfilerThreadState* threadStates[PROFILER_MAX_THREADS];
    Arena* labelArena;

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
    MEMSET(&g_profiler, 0, sizeof(g_profiler));
    g_profiler.registryMutex = OS_mutex_create();
    
    ArenaParameters params = {};
    params.arenaSize = MB(1);
    params.committedSize = KB(64);
    params.flags = ArenaFlags_DoChain;
    g_profiler.labelArena = arena_alloc(params);

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
    
    if (g_profiler.labelArena) {
        arena_release(g_profiler.labelArena);
        g_profiler.labelArena = 0;
    }
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

    if (!g_profiler.labelArena) {
        return;
    }

    ProfilerThreadState* tls = ARENA_PUSH_STRUCT(g_profiler.labelArena, ProfilerThreadState);
    MEMSET(tls, 0, sizeof(*tls));
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
        tls->events = ARENA_PUSH_ARRAY(g_profiler.labelArena, ProfilerThreadState::TraceEvent, cap);
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

TimedScope::TimedScope(U32 index_, const char* label_) noexcept {
    ProfilerThreadState* tls = profiler_get_tls();
    index = index_;

    prevParent = tls->currentParentIndex;
    tls->currentParentIndex = index;

    ProfilerEntry* entry = &tls->entries[index];
    
    // Copy label to permanent storage so it persists after module unload
    if (label_ && !entry->label && g_profiler.labelArena) {
        StringU8 labelStr = str8((const char*) label_);
        StringU8 labelCopy = str8_cpy(g_profiler.labelArena, labelStr);
        entry->label = (const char*) labelCopy.data;
    } else if (!entry->label) {
        entry->label = label_;
    }

    startTicks = ProfClock::now();
}

TimedScope::~TimedScope() noexcept {
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
    MEMSET(agg, 0, sizeof(agg));

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

    LOG_INFO("profiler", "=== Performance Report ===");
    if (totalMicros == 0) {
        LOG_INFO("profiler", "Total Duration: 0.000 ms");
        LOG_INFO("profiler", "(No time elapsed; nothing to report.)");
        LOG_INFO("profiler", "==========================");
        return;
    }

    LOG_INFO("profiler", "Total Duration: {:.3f} ms", (F64) totalMicros / 1000.0);
    LOG_INFO("profiler", "--------------------------");

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

        LOG_INFO("profiler", "{} {} calls | {:.3f} ms ({:.1f}%) | {:.3f} ms ({:.1f}% incl)",
                 e->label ? e->label : "(null)",
                 e->callCount,
                 msExclusive,
                 pctExclusive,
                 msInclusive,
                 pctInclusive);
    }

    LOG_INFO("profiler", "==========================");
}

void profiler_dump_trace_json(const char* path) {
#if !PROFILER_TRACE_EVENTS
    (void) path;
    return;
#else
    Arena* excludes[1] = {g_profiler.labelArena};
    Temp tmp = get_scratch(excludes, ARRAY_COUNT(excludes));
    DEFER_REF(temp_end(&tmp));
    Arena* arena = tmp.arena;

    Str8List pieces;
    str8list_init(&pieces, arena, 64);
    str8list_push(&pieces, str8("{ \"traceEvents\": [\n"));

    B32 first = true;

    OS_mutex_lock(g_profiler.registryMutex);
    const U32 tcount = g_profiler.threadCount;

    for (U32 t = 0; t < tcount; ++t) {
        ProfilerThreadState* tls = g_profiler.threadStates[t];
        if (!tls || !tls->threadName) {
            continue;
        }

        if (!first) {
            str8list_push(&pieces, str8(",\n"));
        }
        first = false;

        str8list_push(&pieces, str8("{\"ph\":\"M\",\"name\":\"thread_name\","
                                     "\"pid\":1,\"tid\":"));
        str8list_push(&pieces, str8_from_U64(arena, (U64) tls->threadId, 10));
        str8list_push(&pieces, str8(",\"args\":{\"name\":\""));
        str8list_push(&pieces, str8(tls->threadName));
        str8list_push(&pieces, str8("\"}}"));
    }

    for (U32 t = 0; t < tcount; ++t) {
        ProfilerThreadState* tls = g_profiler.threadStates[t];
        if (!tls || !tls->events) {
            continue;
        }

        for (U32 i = 0; i < tls->eventCount; ++i) {
            const ProfilerThreadState::TraceEvent* e = &tls->events[i];

            if (!first) {
                str8list_push(&pieces, str8(",\n"));
            }
            first = false;

            const char* eventName = e->eventName ? e->eventName : "";
            const char* category = e->category ? e->category : PROFILER_EVENT_CATEGORY;

            str8list_push(&pieces, str8("{\"name\":\""));
            str8list_push(&pieces, str8(eventName));
            str8list_push(&pieces, str8("\",\"cat\":\""));
            str8list_push(&pieces, str8(category));
            str8list_push(&pieces, str8("\",\"ph\":\"X\",\"ts\":"));
            str8list_push(&pieces, str8_from_U64(arena, (U64) e->timestampMicros, 10));
            str8list_push(&pieces, str8(",\"dur\":"));
            str8list_push(&pieces, str8_from_U64(arena, (U64) e->durationMicros, 10));
            str8list_push(&pieces, str8(",\"pid\":1,\"tid\":"));
            str8list_push(&pieces, str8_from_U64(arena, (U64) e->threadId, 10));
            str8list_push(&pieces, str8("}"));
        }
    }

    OS_mutex_unlock(g_profiler.registryMutex);

    str8list_push(&pieces, str8("\n] }\n"));

    StringU8 json = str8_concat_n(arena, pieces.items, pieces.count);

    OS_Handle file = OS_file_open(path, OS_FileOpenMode_Create);
    if (!file.handle) {
        return;
    }

    OS_file_write(file, json.size, json.data);
    OS_file_close(file);
#endif
}

#endif // PROFILER_ENABLED
