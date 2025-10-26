//
// Created by André Leite on 04/08/2025.
//

#pragma once

#ifndef PROFILER_ENABLED
#define PROFILER_ENABLED 1
#endif

#ifndef PROFILER_MAX_ANCHORS
#define PROFILER_MAX_ANCHORS 4096
#endif

#ifndef PROFILER_MAX_THREADS
#define PROFILER_MAX_THREADS 256
#endif

#ifndef PROFILER_USE_TSC
#define PROFILER_USE_TSC 0
#endif

#ifndef PROFILER_TSC_SERIALIZE
#define PROFILER_TSC_SERIALIZE 0
#endif

#ifndef PROFILER_TRACE_EVENTS
#define PROFILER_TRACE_EVENTS 0
#endif

#ifndef PROFILER_DEFAULT_TRACE_EVENT_CAPACITY
#define PROFILER_DEFAULT_TRACE_EVENT_CAPACITY (1u << 20)
#endif

#ifndef PROFILER_EVENT_CATEGORY
#define PROFILER_EVENT_CATEGORY "profile"
#endif

#if PROFILER_ENABLED

// Index 0 is reserved as ROOT; anchors start at 1
#define TIME_SCOPE(Label)                                                    \
  TimedScope NAME_CONCAT(ScopedTimer_, __LINE__)((U32)(__COUNTER__ + 1),     \
                                                 (Label))

#define TIME_FUNCTION()                                                      \
  TimedScope NAME_CONCAT(ScopedTimer_, __LINE__)((U32)(__COUNTER__ + 1),     \
                                                 __FUNCTION__)

#define PROFILER_ANCHOR_COUNT_CHECK()                                        \
  static_assert((__COUNTER__ + 1) < PROFILER_MAX_ANCHORS,                    \
                "Too many profiler anchors; increase PROFILER_MAX_ANCHORS")

struct ProfilerEntry {
    U64 microsExclusive;
    U64 microsInclusive;
    U64 callCount;
    const char* label;
};

struct ProfilerThreadState {
    U32 currentParentIndex;
    U32 threadId;
    const char* threadName;

    alignas(64) ProfilerEntry entries[PROFILER_MAX_ANCHORS];

#if PROFILER_TRACE_EVENTS
    struct TraceEvent {
        const char* eventName;
        U64 timestampMicros;
        U64 durationMicros;
        U32 threadId;
        const char* category;
    };

    TraceEvent* events;
    U32 eventCapacity;
    U32 eventCount;
#endif
};

struct ProfClock {
    static U64 now();
    static U64 to_micros(U64 ticksOrMicros);
};

void profiler_init_thread(const char* name, U32 traceEventCapacityIfEnabled);
void profiler_set_thread_name(const char* name);
struct ProfilerThreadState* profiler_get_tls();

struct TimedScope {
    U32 index;
    U32 prevParent;
    U64 startTicks;

    FORCE_INLINE TimedScope(U32 index_, const char* label_) noexcept;
    FORCE_INLINE ~TimedScope() noexcept;
};

void profiler_initialize();
void profiler_shutdown();
void profiler_print_report();
void profiler_dump_trace_json(const char* path);

#else

#define TIME_SCOPE(...)
#define TIME_FUNCTION()
#define PROFILER_ANCHOR_COUNT_CHECK()

inline void profiler_initialize() {
}
inline void profiler_shutdown() {
}
inline void profiler_init_thread(const char*, unsigned) {
}
inline void profiler_set_thread_name(const char*) {
}
inline void profiler_print_report() {
}
inline void profiler_dump_trace_json(const char*) {
}

#endif  // PROFILER_ENABLED
