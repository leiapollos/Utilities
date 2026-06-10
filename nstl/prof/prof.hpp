#pragma once

#ifndef PROF_ENABLED
#define PROF_ENABLED 1
#endif

#define PROF_MAX_SITES 4096u
#define PROF_MAX_THREADS 32u
#define PROF_RING_ENTRIES 65536u
#define PROF_MAX_FRAME_NODES 8192u
#define PROF_MAX_GPU_NODES 128u
#define PROF_COLLATION_FRAMES 64u
#define PROF_HISTORY_FRAMES 256u
#define PROF_WINDOW_FRAMES 60u
#define PROF_OPEN_STACK_DEPTH 64u
#define PROF_GPU_FRAME_DELAY 2u

#define PROF_CAT_DEFAULT (1u << 0u)
#define PROF_CAT_GPU (1u << 1u)

struct ProfTls {
    U64* entries;
    U64* head;
    const U32* enableMask;
};

struct ProfFrameNode {
    U32 site;
    U32 depth;
    U64 startNs;
    U64 endNs;
};

struct ProfLaneView {
    const char* name;
    const ProfFrameNode* nodes;
    U32 nodeCount;
    B32 isGpu;
};

struct ProfFrameView {
    U64 frameIndex;
    U64 frameStartNs;
    U64 frameEndNs;
    const ProfLaneView* lanes;
    U32 laneCount;
};

struct ProfSiteStats {
    const char* label;
    U32 category;
    U32 line;
    const char* file;
    F32 avgInclMs;
    F32 avgExclMs;
    F32 maxInclMs;
    F32 avgHits;
    F32 lastInclMs;
    F32 historyMs[PROF_HISTORY_FRAMES];
};

struct ProfInfo {
    U64 tickFrequencyHz;
    F32 resolutionNs;
    F32 overheadNsPerScope;
    U64 droppedEvents;
    U64 gpuSpans;
    U64 gpuSpansDropped;
    U32 siteCount;
    U32 threadCount;
    U64 frameIndex;
    B32 invariantTick;
};

UTILITIES_SHARED_API void prof_init();
UTILITIES_SHARED_API void prof_shutdown();
UTILITIES_SHARED_API U32 prof_require_site(const char* label, const char* file, U32 line, U32 category);
UTILITIES_SHARED_API void prof_thread_bind(ProfTls* outTls);
UTILITIES_SHARED_API void prof_thread_name(const char* name);
UTILITIES_SHARED_API void prof_frame_advance();
UTILITIES_SHARED_API U64 prof_current_frame();
UTILITIES_SHARED_API void prof_pause(B32 paused);
UTILITIES_SHARED_API B32 prof_is_paused();
UTILITIES_SHARED_API void prof_record_gate(B32 enabled);
UTILITIES_SHARED_API U32 prof_site_gpu(const char* passName);
UTILITIES_SHARED_API void prof_gpu_span(U32 site, U64 cpuStartNs, U64 cpuEndNs, U64 frameIndex);
UTILITIES_SHARED_API U64 prof_tick_to_ns(U64 tick);
UTILITIES_SHARED_API U64 prof_now_ns();
UTILITIES_SHARED_API const ProfFrameView* prof_frame_view();
UTILITIES_SHARED_API const ProfSiteStats* prof_site_stats(U32* outCount);
UTILITIES_SHARED_API const F32* prof_frame_history(U32* outCount, U32* outOffset);
UTILITIES_SHARED_API ProfInfo prof_info();
UTILITIES_SHARED_API B32 prof_capture(U32 frameCount, const char* directory);

#if PROF_ENABLED

#if defined(PLATFORM_OS_WINDOWS)
#include <intrin.h>
#endif

static inline U64 prof_tick() {
#if defined(PLATFORM_OS_WINDOWS)
    return (U64)__rdtsc();
#elif defined(__aarch64__)
    U64 value;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(value));
    return value;
#else
    return OS_get_time_nanoseconds();
#endif
}

#define PROF_EVENT_KIND_BEGIN 1ull
#define PROF_EVENT_KIND_END 2ull
#define PROF_EVENT_TICK_MASK 0x0000FFFFFFFFFFFFull

static thread_local ProfTls t_profTls;

static inline void prof_emit(U64 kind, U64 site, U32 category) {
    ProfTls* tls = &t_profTls;
    if (!tls->entries) {
        prof_thread_bind(tls);
        if (!tls->entries) {
            return;
        }
    }
    if ((*tls->enableMask & category) == 0u) {
        return;
    }
    U64 packed = (kind << 62u) | (site << 48u) | (prof_tick() & PROF_EVENT_TICK_MASK);
    U64 head = *tls->head;
    tls->entries[head & (PROF_RING_ENTRIES - 1u)] = packed;
    ATOMIC_STORE(tls->head, head + 1u, MEMORY_ORDER_RELEASE);
}

struct ProfScope {
    U32 site;
    U32 category;

    inline ProfScope(U32 site_, U32 category_) {
        site = site_;
        category = category_;
        prof_emit(PROF_EVENT_KIND_BEGIN, site_, category_);
    }
    inline ~ProfScope() {
        prof_emit(PROF_EVENT_KIND_END, 0ull, category);
    }
};

#define PROF_SCOPE_CAT_(label, category, counter) \
    static U32 NAME_CONCAT(profSite_, counter) = 0u; \
    if (NAME_CONCAT(profSite_, counter) == 0u) { \
        NAME_CONCAT(profSite_, counter) = prof_require_site((label), __FILE__, (U32)__LINE__, (category)); \
    } \
    ProfScope NAME_CONCAT(profScope_, counter)(NAME_CONCAT(profSite_, counter), (category))

#define PROF_SCOPE(label) PROF_SCOPE_CAT_((label), PROF_CAT_DEFAULT, __LINE__)
#define PROF_FUNCTION() PROF_SCOPE_CAT_(__func__, PROF_CAT_DEFAULT, __LINE__)

#define PROF_BEGIN(label) \
    do { \
        static U32 profBeginSite_ = 0u; \
        if (profBeginSite_ == 0u) { \
            profBeginSite_ = prof_require_site((label), __FILE__, (U32)__LINE__, PROF_CAT_DEFAULT); \
        } \
        prof_emit(PROF_EVENT_KIND_BEGIN, profBeginSite_, PROF_CAT_DEFAULT); \
    } while (0)

#define PROF_END() prof_emit(PROF_EVENT_KIND_END, 0ull, PROF_CAT_DEFAULT)

#else

struct ProfScope {};
#define PROF_SCOPE(label)
#define PROF_FUNCTION()
#define PROF_BEGIN(label)
#define PROF_END()

static inline U64 prof_tick() { return 0ull; }

#endif
