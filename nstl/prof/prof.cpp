#if PROF_ENABLED

#define PROF_EVENT_SITE_MASK 0x3FFFull
#define PROF_TICK_SPAN (1ull << 48u)
#define PROF_CAT_ALL (PROF_CAT_DEFAULT | PROF_CAT_GPU)
#define PROF_NIL_INDEX 0xFFFFFFFFu

struct ProfSiteEntry {
    U64 key;
    const char* label;
    const char* file;
    U32 line;
    U32 category;
};

struct ProfOpenScope {
    U32 site;
    U32 parentSite;
    U32 path;
    U32 nodeIndex;
    U64 startNs;
    U64 sliceStartNs;
    U64 savedInclNs;
    U64 savedPathInclNs;
};

struct ProfPathEntry {
    U64 key;
    U32 lastChild;
};

#define PROF_PATH_TABLE_SIZE 2048u
#define PROF_GPU_THREAD_INDEX PROF_MAX_THREADS

struct ProfThreadEntry {
    B32 used;
    U32 threadId;
    const char* name;
    U64* entries;
    U64 head;
    U64 lastCollatedHead;
    U64 lastFullTick;
    ProfOpenScope open[PROF_OPEN_STACK_DEPTH];
    U32 openDepth;
};

struct ProfFrameLane {
    const char* name;
    U32 firstNode;
    U32 nodeCount;
    U32 threadIndex;
};

struct ProfFrameSlot {
    U64 frameIndex;
    U64 startNs;
    U64 endNs;
    U32 nodeCount;
    U32 gpuNodeCount;
    U32 laneCount;
    ProfFrameNode nodes[PROF_MAX_FRAME_NODES];
    ProfFrameNode gpuNodes[PROF_MAX_GPU_NODES];
    ProfFrameLane lanes[PROF_MAX_THREADS];
};

struct ProfSiteAccum {
    U64 sumInclNs;
    U64 sumExclNs;
    U64 maxInclNs;
    U64 sumHits;
};

struct ProfGlobal {
    B32 initialized;
    Arena* arena;
    OS_Handle mutex;

    U32 enableMask;
    B32 paused;
    B32 recordGateClosed;

    U64 tickFrequencyHz;
    F32 resolutionNs;
    F32 overheadNsPerScope;
    B32 invariantTick;

    ProfSiteEntry sites[PROF_MAX_SITES];
    U32 siteCount;

    ProfThreadEntry threads[PROF_MAX_THREADS];
    U32 threadCount;
    U64 droppedEvents;
    U64 stackResets;
    U64 gpuSpans;
    U64 gpuSpansDropped;

    U64 frameIndex;
    U64 frameStartNs;
    ProfFrameSlot* slots;

    U64 frameInclNs[PROF_MAX_SITES];
    S64 frameExclNs[PROF_MAX_SITES];
    U32 frameHits[PROF_MAX_SITES];

    ProfSiteAccum windowAccum[PROF_MAX_SITES];
    U32 windowFrames;
    ProfSiteStats* stats;
    F32 frameHistoryMs[PROF_HISTORY_FRAMES];

    ProfPathEntry pathEntries[PROF_MAX_PATHS];
    U32 pathSlots[PROF_PATH_TABLE_SIZE];
    U32 pathCount;
    U32 pathRootFirst[PROF_MAX_THREADS + 1u];
    U32 pathRootLast[PROF_MAX_THREADS + 1u];
    U64 framePathInclNs[PROF_MAX_PATHS];
    S64 framePathExclNs[PROF_MAX_PATHS];
    U32 framePathHits[PROF_MAX_PATHS];
    ProfSiteAccum pathAccum[PROF_MAX_PATHS];
    ProfPathStats* pathStats;

    ProfLaneView viewLanes[PROF_MAX_THREADS + 1u];
    ProfFrameView view;
};

static ProfGlobal g_prof;

static U64 prof_mul_div_(U64 value, U64 mul, U64 divisor) {
#if defined(PLATFORM_OS_WINDOWS)
    U64 high = 0u;
    U64 low = _umul128(value, mul, &high);
    U64 remainder = 0u;
    if (high >= divisor) {
        return 0xFFFFFFFFFFFFFFFFull;
    }
    return _udiv128(high, low, divisor, &remainder);
#else
    return (U64)(((__uint128_t)value * mul) / divisor);
#endif
}

U64 prof_tick_to_ns(U64 tick) {
    if (g_prof.tickFrequencyHz == 0u) {
        return tick;
    }
    return prof_mul_div_(tick, 1000000000ull, g_prof.tickFrequencyHz);
}

U64 prof_now_ns() {
    return prof_tick_to_ns(prof_tick());
}

static U64 prof_hash_site_(const char* label, const char* file, U32 line) {
    U64 hash = 0xCBF29CE484222325ull;
    for (const char* at = label; at && *at; ++at) {
        hash = (hash ^ (U64)(U8)*at) * 0x100000001B3ull;
    }
    for (const char* at = file; at && *at; ++at) {
        hash = (hash ^ (U64)(U8)*at) * 0x100000001B3ull;
    }
    hash = (hash ^ (U64)line) * 0x100000001B3ull;
    if (hash == 0ull) {
        hash = 1ull;
    }
    return hash;
}

static const char* prof_intern_string_(const char* source) {
    if (!source || !g_prof.arena) {
        return "";
    }
    StringU8 copy = str8_cpy(g_prof.arena, str8(source));
    return (const char*)copy.data;
}

static void prof_measure_clock_() {
#if defined(PLATFORM_OS_WINDOWS)
    int cpuInfo[4] = {};
    __cpuid(cpuInfo, 0x80000007);
    g_prof.invariantTick = ((cpuInfo[3] >> 8) & 1) != 0;
    if (!g_prof.invariantTick) {
        LOG_WARNING("prof", "Invariant TSC not reported; timings may drift");
    }
    U64 tick0 = prof_tick();
    U64 ns0 = OS_get_time_nanoseconds();
    U64 ns1 = ns0;
    while ((ns1 - ns0) < 100000000ull) {
        ns1 = OS_get_time_nanoseconds();
    }
    U64 tick1 = prof_tick();
    U64 deltaTicks = (tick1 > tick0) ? (tick1 - tick0) : 1u;
    U64 deltaNs = (ns1 > ns0) ? (ns1 - ns0) : 1u;
    g_prof.tickFrequencyHz = prof_mul_div_(deltaTicks, 1000000000ull, deltaNs);
#elif defined(__aarch64__)
    U64 frequency = 0u;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(frequency));
    g_prof.tickFrequencyHz = frequency;
    g_prof.invariantTick = 1;
#else
    g_prof.tickFrequencyHz = 1000000000ull;
    g_prof.invariantTick = 1;
#endif
    if (g_prof.tickFrequencyHz == 0u) {
        g_prof.tickFrequencyHz = 1000000000ull;
    }

    U64 minDelta = 0xFFFFFFFFFFFFFFFFull;
    for (U32 sample = 0u; sample < 100000u; ++sample) {
        U64 a = prof_tick();
        U64 b = prof_tick();
        U64 delta = b - a;
        if (delta != 0u && delta < minDelta) {
            minDelta = delta;
        }
    }
    if (minDelta == 0xFFFFFFFFFFFFFFFFull) {
        minDelta = 1u;
    }
    g_prof.resolutionNs = (F32)prof_mul_div_(minDelta, 1000000000ull, g_prof.tickFrequencyHz);
}

static ProfThreadEntry* prof_thread_entry_for_current_() {
    U32 threadId = OS_get_thread_id_u32();
    for (U32 index = 0u; index < g_prof.threadCount; ++index) {
        if (g_prof.threads[index].used && g_prof.threads[index].threadId == threadId) {
            return g_prof.threads + index;
        }
    }
    if (g_prof.threadCount >= PROF_MAX_THREADS) {
        return 0;
    }

    ProfThreadEntry* entry = g_prof.threads + g_prof.threadCount;
    MEMSET(entry, 0, sizeof(*entry));
    entry->entries = ARENA_PUSH_ARRAY(g_prof.arena, U64, PROF_RING_ENTRIES);
    if (!entry->entries) {
        return 0;
    }
    entry->used = 1;
    entry->threadId = threadId;
    entry->lastFullTick = prof_tick();
    Temp scratch = get_scratch(0, 0);
    if (scratch.arena) {
        StringU8 name = str8_fmt(scratch.arena, "thread {}", threadId);
        StringU8 copy = str8_cpy(g_prof.arena, name);
        entry->name = (const char*)copy.data;
        temp_end(&scratch);
    } else {
        entry->name = "thread";
    }
    g_prof.threadCount += 1u;
    return entry;
}

void prof_thread_bind(ProfTls* outTls) {
    if (!outTls) {
        return;
    }
    MEMSET(outTls, 0, sizeof(*outTls));
    if (!g_prof.initialized) {
        return;
    }

    OS_mutex_lock(g_prof.mutex);
    ProfThreadEntry* entry = prof_thread_entry_for_current_();
    OS_mutex_unlock(g_prof.mutex);
    if (!entry) {
        return;
    }

    outTls->entries = entry->entries;
    outTls->head = &entry->head;
    outTls->enableMask = &g_prof.enableMask;
}

void prof_thread_name(const char* name) {
    if (!g_prof.initialized || !name) {
        return;
    }
    OS_mutex_lock(g_prof.mutex);
    ProfThreadEntry* entry = prof_thread_entry_for_current_();
    if (entry) {
        entry->name = prof_intern_string_(name);
    }
    OS_mutex_unlock(g_prof.mutex);
}

U32 prof_require_site(const char* label, const char* file, U32 line, U32 category) {
    if (!g_prof.initialized || !label) {
        return 0u;
    }

    U64 key = prof_hash_site_(label, file ? file : "", line);
    OS_mutex_lock(g_prof.mutex);
    for (U32 index = 1u; index < g_prof.siteCount; ++index) {
        if (g_prof.sites[index].key == key) {
            OS_mutex_unlock(g_prof.mutex);
            return index;
        }
    }
    if (g_prof.siteCount >= PROF_MAX_SITES) {
        OS_mutex_unlock(g_prof.mutex);
        return 0u;
    }

    U32 index = g_prof.siteCount;
    ProfSiteEntry* site = g_prof.sites + index;
    site->key = key;
    site->label = prof_intern_string_(label);
    site->file = prof_intern_string_(file ? file : "");
    site->line = line;
    site->category = category;

    ProfSiteStats* stats = g_prof.stats + index;
    MEMSET(stats, 0, sizeof(*stats));
    stats->label = site->label;
    stats->file = site->file;
    stats->line = line;
    stats->category = category;

    g_prof.siteCount = index + 1u;
    OS_mutex_unlock(g_prof.mutex);
    return index;
}

U32 prof_site_gpu(const char* passName) {
    return prof_require_site(passName, "gpu", 0u, PROF_CAT_GPU);
}

static void prof_measure_overhead_() {
    ProfTls* tls = &t_profTls;
    if (!tls->entries) {
        prof_thread_bind(tls);
    }
    if (!tls->entries) {
        return;
    }

    U32 probeSite = prof_require_site("prof probe", "prof", 0u, PROF_CAT_DEFAULT);
    U64 start = prof_tick();
    for (U32 sample = 0u; sample < 8192u; ++sample) {
        prof_emit(PROF_EVENT_KIND_BEGIN, probeSite, PROF_CAT_DEFAULT);
        prof_emit(PROF_EVENT_KIND_END, 0ull, PROF_CAT_DEFAULT);
    }
    U64 elapsed = prof_tick() - start;
    U64 elapsedNs = prof_mul_div_(elapsed, 1000000000ull, g_prof.tickFrequencyHz);
    g_prof.overheadNsPerScope = (F32)elapsedNs / 8192.0f;

    ProfThreadEntry* entry = prof_thread_entry_for_current_();
    if (entry) {
        entry->lastCollatedHead = ATOMIC_LOAD(&entry->head, MEMORY_ORDER_ACQUIRE);
    }
}

void prof_init() {
    if (g_prof.initialized) {
        return;
    }
    MEMSET(&g_prof, 0, sizeof(g_prof));

    g_prof.arena = arena_alloc(.arenaSize = MB(96), .committedSize = MB(1), .flags = ArenaFlags_DoChain);
    if (!g_prof.arena) {
        return;
    }
    g_prof.mutex = OS_mutex_create();
    g_prof.slots = ARENA_PUSH_ARRAY(g_prof.arena, ProfFrameSlot, PROF_COLLATION_FRAMES);
    g_prof.stats = ARENA_PUSH_ARRAY(g_prof.arena, ProfSiteStats, PROF_MAX_SITES);
    g_prof.pathStats = ARENA_PUSH_ARRAY(g_prof.arena, ProfPathStats, PROF_MAX_PATHS);
    if (!g_prof.slots || !g_prof.stats || !g_prof.pathStats) {
        return;
    }
    MEMSET(g_prof.slots, 0, sizeof(ProfFrameSlot) * PROF_COLLATION_FRAMES);
    MEMSET(g_prof.pathStats, 0, sizeof(ProfPathStats) * PROF_MAX_PATHS);
    g_prof.pathStats[0].parent = PROF_PATH_NIL;
    g_prof.pathStats[0].firstChild = PROF_PATH_NIL;
    g_prof.pathStats[0].nextSibling = PROF_PATH_NIL;
    g_prof.pathEntries[0].lastChild = PROF_PATH_NIL;
    g_prof.pathCount = 1u;
    for (U32 thread = 0u; thread <= PROF_MAX_THREADS; ++thread) {
        g_prof.pathRootFirst[thread] = PROF_PATH_NIL;
        g_prof.pathRootLast[thread] = PROF_PATH_NIL;
    }

    g_prof.siteCount = 1u;
    g_prof.enableMask = PROF_CAT_ALL;
    g_prof.initialized = 1;

    prof_measure_clock_();
    prof_measure_overhead_();
    prof_thread_name("main");

    g_prof.frameStartNs = prof_now_ns();
    g_prof.slots[0].frameIndex = 0u;
    g_prof.slots[0].startNs = g_prof.frameStartNs;

    LOG_INFO("prof", "Profiler ready: freq {}Hz resolution {}ns overhead {}ns/scope",
             g_prof.tickFrequencyHz, g_prof.resolutionNs, g_prof.overheadNsPerScope);
}

void prof_shutdown() {
    if (!g_prof.initialized) {
        return;
    }
    g_prof.initialized = 0;
    g_prof.enableMask = 0u;
    OS_mutex_destroy(g_prof.mutex);
    if (g_prof.arena) {
        arena_release(g_prof.arena);
        g_prof.arena = 0;
    }
}

void prof_pause(B32 paused) {
    g_prof.paused = paused;
}

B32 prof_is_paused() {
    return g_prof.paused;
}

void prof_record_gate(B32 enabled) {
    g_prof.recordGateClosed = !enabled;
    g_prof.enableMask = (g_prof.paused || g_prof.recordGateClosed) ? 0u : PROF_CAT_ALL;
}

U64 prof_current_frame() {
    return g_prof.frameIndex;
}

static U32 prof_path_require_(U32 parent, U32 site, U32 thread) {
    U64 hash = 0xCBF29CE484222325ull;
    hash = (hash ^ (U64)parent) * 0x100000001B3ull;
    hash = (hash ^ (U64)site) * 0x100000001B3ull;
    hash = (hash ^ (U64)thread) * 0x100000001B3ull;
    hash ^= hash >> 33u;
    hash *= 0xFF51AFD7ED558CCDull;
    hash ^= hash >> 33u;
    if (hash == 0ull) {
        hash = 1ull;
    }
    for (U32 probe = 0u; probe < PROF_PATH_TABLE_SIZE; ++probe) {
        U32 slotIndex = (U32)((hash + probe) & (PROF_PATH_TABLE_SIZE - 1u));
        U32 stored = g_prof.pathSlots[slotIndex];
        if (stored != 0u) {
            if (g_prof.pathEntries[stored].key == hash) {
                return stored;
            }
            continue;
        }
        if (g_prof.pathCount >= PROF_MAX_PATHS) {
            return 0u;
        }
        U32 index = g_prof.pathCount;
        g_prof.pathCount += 1u;
        g_prof.pathSlots[slotIndex] = index;
        g_prof.pathEntries[index].key = hash;
        g_prof.pathEntries[index].lastChild = PROF_PATH_NIL;
        ProfPathStats* path = g_prof.pathStats + index;
        MEMSET(path, 0, sizeof(*path));
        path->site = site;
        path->parent = parent;
        path->thread = thread;
        path->depth = (parent != PROF_PATH_NIL) ? g_prof.pathStats[parent].depth + 1u : 0u;
        path->firstChild = PROF_PATH_NIL;
        path->nextSibling = PROF_PATH_NIL;
        if (parent != PROF_PATH_NIL) {
            U32 last = g_prof.pathEntries[parent].lastChild;
            if (last == PROF_PATH_NIL) {
                g_prof.pathStats[parent].firstChild = index;
            } else {
                g_prof.pathStats[last].nextSibling = index;
            }
            g_prof.pathEntries[parent].lastChild = index;
        } else {
            U32 last = g_prof.pathRootLast[thread];
            if (last == PROF_PATH_NIL) {
                g_prof.pathRootFirst[thread] = index;
            } else {
                g_prof.pathStats[last].nextSibling = index;
            }
            g_prof.pathRootLast[thread] = index;
        }
        return index;
    }
    return 0u;
}

static U64 prof_reconstruct_tick_(ProfThreadEntry* thread, U64 low48) {
    U64 base = thread->lastFullTick;
    U64 candidate = (base & ~PROF_EVENT_TICK_MASK) | low48;
    if (candidate < base) {
        candidate += PROF_TICK_SPAN;
    }
    thread->lastFullTick = candidate;
    return candidate;
}

static ProfFrameNode* prof_push_node_(ProfFrameSlot* slot, U32 site, U32 depth, U32 path, U64 startNs) {
    if (slot->nodeCount >= PROF_MAX_FRAME_NODES) {
        return 0;
    }
    ProfFrameNode* node = slot->nodes + slot->nodeCount;
    slot->nodeCount += 1u;
    node->site = site;
    node->depth = depth;
    node->path = path;
    node->startNs = startNs;
    node->endNs = startNs;
    return node;
}

static void prof_account_slice_(ProfOpenScope* open, U64 endNs, B32 isFinalClose) {
    U64 elapsed = (endNs > open->sliceStartNs) ? (endNs - open->sliceStartNs) : 0u;
    g_prof.frameInclNs[open->site] = open->savedInclNs + elapsed;
    g_prof.frameExclNs[open->site] += (S64)elapsed;
    if (open->parentSite != 0u) {
        g_prof.frameExclNs[open->parentSite] -= (S64)elapsed;
    }
    g_prof.framePathInclNs[open->path] = open->savedPathInclNs + elapsed;
    g_prof.framePathExclNs[open->path] += (S64)elapsed;
    U32 parentPath = g_prof.pathStats[open->path].parent;
    if (parentPath != PROF_PATH_NIL) {
        g_prof.framePathExclNs[parentPath] -= (S64)elapsed;
    }
    if (isFinalClose) {
        g_prof.frameHits[open->site] += 1u;
        g_prof.framePathHits[open->path] += 1u;
    }
}

static void prof_collate_thread_(ProfThreadEntry* thread, ProfFrameSlot* slot, U64 frameEndNs) {
    U64 head = ATOMIC_LOAD(&thread->head, MEMORY_ORDER_ACQUIRE);
    U64 tail = thread->lastCollatedHead;
    U64 count = head - tail;
    if (count > PROF_RING_ENTRIES) {
        U64 dropped = count - PROF_RING_ENTRIES;
        g_prof.droppedEvents += dropped;
        tail = head - PROF_RING_ENTRIES;
        thread->openDepth = 0u;
        g_prof.stackResets += 1u;
    }

    U32 laneFirst = slot->nodeCount;

    for (U32 depth = 0u; depth < thread->openDepth; ++depth) {
        ProfOpenScope* open = thread->open + depth;
        open->sliceStartNs = slot->startNs;
        open->savedInclNs = g_prof.frameInclNs[open->site];
        open->savedPathInclNs = g_prof.framePathInclNs[open->path];
        ProfFrameNode* node = prof_push_node_(slot, open->site, depth, open->path, open->startNs < slot->startNs ? slot->startNs : open->startNs);
        open->nodeIndex = node ? (U32)(node - slot->nodes) : PROF_NIL_INDEX;
    }

    for (U64 at = tail; at < head; ++at) {
        U64 packed = thread->entries[at & (PROF_RING_ENTRIES - 1u)];
        U64 kind = packed >> 62u;
        U32 site = (U32)((packed >> 48u) & PROF_EVENT_SITE_MASK);
        U64 tick = prof_reconstruct_tick_(thread, packed & PROF_EVENT_TICK_MASK);
        U64 ns = prof_tick_to_ns(tick);

        if (kind == PROF_EVENT_KIND_BEGIN) {
            if (thread->openDepth >= PROF_OPEN_STACK_DEPTH || site == 0u || site >= g_prof.siteCount) {
                continue;
            }
            ProfOpenScope* open = thread->open + thread->openDepth;
            open->site = site;
            open->parentSite = (thread->openDepth > 0u) ? thread->open[thread->openDepth - 1u].site : 0u;
            U32 parentPath = (thread->openDepth > 0u) ? thread->open[thread->openDepth - 1u].path : PROF_PATH_NIL;
            open->path = prof_path_require_(parentPath, site, (U32)(thread - g_prof.threads));
            open->startNs = ns;
            open->sliceStartNs = ns;
            open->savedInclNs = g_prof.frameInclNs[site];
            open->savedPathInclNs = g_prof.framePathInclNs[open->path];
            ProfFrameNode* node = prof_push_node_(slot, site, thread->openDepth, open->path, ns);
            open->nodeIndex = node ? (U32)(node - slot->nodes) : PROF_NIL_INDEX;
            thread->openDepth += 1u;
        } else if (kind == PROF_EVENT_KIND_END) {
            if (thread->openDepth == 0u) {
                continue;
            }
            thread->openDepth -= 1u;
            ProfOpenScope* open = thread->open + thread->openDepth;
            prof_account_slice_(open, ns, 1);
            if (open->nodeIndex != PROF_NIL_INDEX) {
                slot->nodes[open->nodeIndex].endNs = ns;
            }
        }
    }

    for (U32 depth = thread->openDepth; depth-- > 0u;) {
        ProfOpenScope* open = thread->open + depth;
        prof_account_slice_(open, frameEndNs, 0);
        if (open->nodeIndex != PROF_NIL_INDEX) {
            slot->nodes[open->nodeIndex].endNs = frameEndNs;
        }
    }

    thread->lastCollatedHead = head;

    if (slot->nodeCount > laneFirst && slot->laneCount < PROF_MAX_THREADS) {
        ProfFrameLane* lane = slot->lanes + slot->laneCount;
        lane->name = thread->name;
        lane->firstNode = laneFirst;
        lane->nodeCount = slot->nodeCount - laneFirst;
        lane->threadIndex = (U32)(thread - g_prof.threads);
        slot->laneCount += 1u;
    }
}

void prof_frame_advance() {
    if (!g_prof.initialized) {
        return;
    }

    U32 desiredMask = (g_prof.paused || g_prof.recordGateClosed) ? 0u : PROF_CAT_ALL;
    B32 wasRecording = (g_prof.enableMask != 0u);
    B32 willRecord = (desiredMask != 0u);

    U64 frameEndNs = prof_now_ns();
    U64 frame = g_prof.frameIndex;
    ProfFrameSlot* slot = g_prof.slots + (frame % PROF_COLLATION_FRAMES);

    if (wasRecording) {
        for (U32 threadIndex = 0u; threadIndex < g_prof.threadCount; ++threadIndex) {
            ProfThreadEntry* thread = g_prof.threads + threadIndex;
            if (thread->used) {
                prof_collate_thread_(thread, slot, frameEndNs);
            }
        }
        slot->endNs = frameEndNs;

        for (U32 site = 1u; site < g_prof.siteCount; ++site) {
            U64 inclNs = g_prof.frameInclNs[site];
            S64 exclNs = g_prof.frameExclNs[site];
            U32 hits = g_prof.frameHits[site];
            ProfSiteStats* stats = g_prof.stats + site;
            ProfSiteAccum* accum = g_prof.windowAccum + site;

            stats->historyMs[frame % PROF_HISTORY_FRAMES] = (F32)((F64)inclNs / 1.0e6);
            stats->lastInclMs = (F32)((F64)inclNs / 1.0e6);
            accum->sumInclNs += inclNs;
            accum->sumExclNs += (exclNs > 0) ? (U64)exclNs : 0u;
            accum->sumHits += hits;
            if (inclNs > accum->maxInclNs) {
                accum->maxInclNs = inclNs;
            }

            g_prof.frameInclNs[site] = 0u;
            g_prof.frameExclNs[site] = 0;
            g_prof.frameHits[site] = 0u;
        }

        for (U32 path = 1u; path < g_prof.pathCount; ++path) {
            U64 inclNs = g_prof.framePathInclNs[path];
            S64 exclNs = g_prof.framePathExclNs[path];
            U32 hits = g_prof.framePathHits[path];
            ProfSiteAccum* accum = g_prof.pathAccum + path;

            g_prof.pathStats[path].lastInclMs = (F32)((F64)inclNs / 1.0e6);
            accum->sumInclNs += inclNs;
            accum->sumExclNs += (exclNs > 0) ? (U64)exclNs : 0u;
            accum->sumHits += hits;
            if (inclNs > accum->maxInclNs) {
                accum->maxInclNs = inclNs;
            }

            g_prof.framePathInclNs[path] = 0u;
            g_prof.framePathExclNs[path] = 0;
            g_prof.framePathHits[path] = 0u;
        }

        g_prof.frameHistoryMs[frame % PROF_HISTORY_FRAMES] =
                (F32)((F64)(frameEndNs - slot->startNs) / 1.0e6);

        g_prof.windowFrames += 1u;
        if (g_prof.windowFrames >= PROF_WINDOW_FRAMES) {
            F32 inverse = 1.0f / (F32)g_prof.windowFrames;
            for (U32 site = 1u; site < g_prof.siteCount; ++site) {
                ProfSiteStats* stats = g_prof.stats + site;
                ProfSiteAccum* accum = g_prof.windowAccum + site;
                stats->avgInclMs = (F32)((F64)accum->sumInclNs / 1.0e6) * inverse;
                stats->avgExclMs = (F32)((F64)accum->sumExclNs / 1.0e6) * inverse;
                stats->maxInclMs = (F32)((F64)accum->maxInclNs / 1.0e6);
                stats->avgHits = (F32)accum->sumHits * inverse;
                MEMSET(accum, 0, sizeof(*accum));
            }
            for (U32 path = 1u; path < g_prof.pathCount; ++path) {
                ProfPathStats* stats = g_prof.pathStats + path;
                ProfSiteAccum* accum = g_prof.pathAccum + path;
                stats->avgInclMs = (F32)((F64)accum->sumInclNs / 1.0e6) * inverse;
                stats->avgExclMs = (F32)((F64)accum->sumExclNs / 1.0e6) * inverse;
                stats->maxInclMs = (F32)((F64)accum->maxInclNs / 1.0e6);
                stats->avgHits = (F32)accum->sumHits * inverse;
                MEMSET(accum, 0, sizeof(*accum));
            }
            g_prof.windowFrames = 0u;
        }

        g_prof.frameIndex = frame + 1u;
        ProfFrameSlot* next = g_prof.slots + (g_prof.frameIndex % PROF_COLLATION_FRAMES);
        next->frameIndex = g_prof.frameIndex;
        next->startNs = frameEndNs;
        next->endNs = frameEndNs;
        next->nodeCount = 0u;
        next->gpuNodeCount = 0u;
        next->laneCount = 0u;
        g_prof.frameStartNs = frameEndNs;
    } else {
        for (U32 threadIndex = 0u; threadIndex < g_prof.threadCount; ++threadIndex) {
            ProfThreadEntry* thread = g_prof.threads + threadIndex;
            if (!thread->used) {
                continue;
            }
            thread->lastCollatedHead = ATOMIC_LOAD(&thread->head, MEMORY_ORDER_ACQUIRE);
            if (willRecord && thread->openDepth != 0u) {
                thread->openDepth = 0u;
            }
        }
        if (willRecord) {
            ProfFrameSlot* next = g_prof.slots + (g_prof.frameIndex % PROF_COLLATION_FRAMES);
            next->startNs = frameEndNs;
            next->nodeCount = 0u;
            next->gpuNodeCount = 0u;
            next->laneCount = 0u;
            g_prof.frameStartNs = frameEndNs;
        }
    }

    g_prof.enableMask = desiredMask;
}

void prof_gpu_span(U32 site, U64 cpuStartNs, U64 cpuEndNs, U64 frameIndex) {
    if (!g_prof.initialized || g_prof.enableMask == 0u || site == 0u || site >= g_prof.siteCount) {
        g_prof.gpuSpansDropped += 1u;
        return;
    }
    ProfFrameSlot* slot = g_prof.slots + (frameIndex % PROF_COLLATION_FRAMES);
    if (slot->frameIndex != frameIndex || slot->gpuNodeCount >= PROF_MAX_GPU_NODES) {
        g_prof.gpuSpansDropped += 1u;
        return;
    }
    g_prof.gpuSpans += 1u;
    ProfFrameNode* node = slot->gpuNodes + slot->gpuNodeCount;
    slot->gpuNodeCount += 1u;
    node->site = site;
    node->depth = 0u;
    node->path = prof_path_require_(PROF_PATH_NIL, site, PROF_GPU_THREAD_INDEX);
    node->startNs = cpuStartNs;
    node->endNs = cpuEndNs;

    U64 elapsed = (cpuEndNs > cpuStartNs) ? (cpuEndNs - cpuStartNs) : 0u;
    g_prof.frameInclNs[site] += elapsed;
    g_prof.frameExclNs[site] += (S64)elapsed;
    g_prof.frameHits[site] += 1u;
    g_prof.framePathInclNs[node->path] += elapsed;
    g_prof.framePathExclNs[node->path] += (S64)elapsed;
    g_prof.framePathHits[node->path] += 1u;
}

const ProfFrameView* prof_frame_view() {
    if (!g_prof.initialized) {
        return 0;
    }
    U64 delay = (U64)PROF_GPU_FRAME_DELAY + 1u;
    if (g_prof.frameIndex < delay) {
        return 0;
    }
    U64 viewFrame = g_prof.frameIndex - delay;
    ProfFrameSlot* slot = g_prof.slots + (viewFrame % PROF_COLLATION_FRAMES);
    if (slot->frameIndex != viewFrame) {
        return 0;
    }

    U32 laneCount = 0u;
    for (U32 laneIndex = 0u; laneIndex < slot->laneCount && laneCount < PROF_MAX_THREADS; ++laneIndex) {
        ProfFrameLane* lane = slot->lanes + laneIndex;
        ProfLaneView* view = g_prof.viewLanes + laneCount;
        view->name = lane->name;
        view->nodes = slot->nodes + lane->firstNode;
        view->nodeCount = lane->nodeCount;
        view->threadIndex = lane->threadIndex;
        view->isGpu = 0;
        laneCount += 1u;
    }
    if (slot->gpuNodeCount != 0u) {
        ProfLaneView* view = g_prof.viewLanes + laneCount;
        view->name = "gpu";
        view->nodes = slot->gpuNodes;
        view->nodeCount = slot->gpuNodeCount;
        view->threadIndex = PROF_GPU_THREAD_INDEX;
        view->isGpu = 1;
        laneCount += 1u;
    }

    g_prof.view.frameIndex = slot->frameIndex;
    g_prof.view.frameStartNs = slot->startNs;
    g_prof.view.frameEndNs = slot->endNs;
    g_prof.view.lanes = g_prof.viewLanes;
    g_prof.view.laneCount = laneCount;
    return &g_prof.view;
}

const F32* prof_frame_history(U32* outCount, U32* outOffset) {
    if (outCount) {
        *outCount = PROF_HISTORY_FRAMES;
    }
    if (outOffset) {
        *outOffset = (U32)(g_prof.frameIndex % PROF_HISTORY_FRAMES);
    }
    return g_prof.frameHistoryMs;
}

const ProfSiteStats* prof_site_stats(U32* outCount) {
    if (outCount) {
        *outCount = g_prof.initialized ? g_prof.siteCount : 0u;
    }
    return g_prof.stats;
}

const ProfPathStats* prof_path_stats(U32* outCount) {
    if (outCount) {
        *outCount = g_prof.initialized ? g_prof.pathCount : 0u;
    }
    return g_prof.pathStats;
}

struct ProfCaptureLane {
    const char* name;
    U32 tid;
};

static U32 prof_capture_lane_tid_(ProfCaptureLane* lanes, U32* laneCount, const char* name) {
    for (U32 index = 0u; index < *laneCount; ++index) {
        if (lanes[index].name == name) {
            return lanes[index].tid;
        }
    }
    U32 tid = *laneCount + 1u;
    if (*laneCount < PROF_MAX_THREADS + 1u) {
        lanes[*laneCount].name = name;
        lanes[*laneCount].tid = tid;
        *laneCount += 1u;
    }
    return tid;
}

static void prof_capture_write_bytes_(U8** at, const void* source, U64 size) {
    MEMCPY(*at, source, size);
    *at += size;
}

static U64 prof_capture_lane_events_(U8* base, U8* at, const ProfFrameNode* nodes, U32 nodeCount,
                                     U64 baseNs, U64* ioFirstTs, U32* ioOpenDepth, U64* openEnds) {
    for (U32 nodeIndex = 0u; nodeIndex < nodeCount; ++nodeIndex) {
        const ProfFrameNode* node = nodes + nodeIndex;
        while (*ioOpenDepth > node->depth) {
            *ioOpenDepth -= 1u;
            U8 endType = 4u;
            U64 when = openEnds[*ioOpenDepth] - baseNs;
            prof_capture_write_bytes_(&at, &endType, 1u);
            prof_capture_write_bytes_(&at, &when, 8u);
        }

        const char* label = g_prof.sites[node->site].label;
        U64 labelLength = 0u;
        while (label[labelLength] && labelLength < 255u) {
            labelLength += 1u;
        }
        U8 beginType = 3u;
        U64 when = node->startNs - baseNs;
        U8 nameLength = (U8)labelLength;
        U8 argsLength = 0u;
        if (*ioFirstTs == 0u) {
            *ioFirstTs = when;
        }
        prof_capture_write_bytes_(&at, &beginType, 1u);
        prof_capture_write_bytes_(&at, &when, 8u);
        prof_capture_write_bytes_(&at, &nameLength, 1u);
        prof_capture_write_bytes_(&at, &argsLength, 1u);
        prof_capture_write_bytes_(&at, label, labelLength);

        if (*ioOpenDepth < PROF_OPEN_STACK_DEPTH) {
            openEnds[*ioOpenDepth] = node->endNs;
            *ioOpenDepth += 1u;
        }
    }
    return (U64)(at - base);
}

B32 prof_capture(U32 frameCount, const char* directory) {
    if (!g_prof.initialized || g_prof.frameIndex == 0u) {
        return 0;
    }
    if (frameCount == 0u || frameCount > PROF_COLLATION_FRAMES) {
        frameCount = PROF_COLLATION_FRAMES;
    }
    U64 newest = g_prof.frameIndex - 1u;
    U64 oldest = (newest + 1u > frameCount) ? (newest + 1u - frameCount) : 0u;

    if (directory) {
        OS_create_directory(directory);
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    ProfCaptureLane lanes[PROF_MAX_THREADS + 1u];
    U32 laneCount = 0u;
    U64 totalNodes = 0u;
    U64 baseNs = 0u;
    for (U64 frame = oldest; frame <= newest; ++frame) {
        ProfFrameSlot* slot = g_prof.slots + (frame % PROF_COLLATION_FRAMES);
        if (slot->frameIndex != frame) {
            continue;
        }
        if (baseNs == 0u) {
            baseNs = slot->startNs;
        }
        totalNodes += slot->nodeCount + slot->gpuNodeCount;
        for (U32 laneIndex = 0u; laneIndex < slot->laneCount; ++laneIndex) {
            prof_capture_lane_tid_(lanes, &laneCount, slot->lanes[laneIndex].name);
        }
        if (slot->gpuNodeCount != 0u) {
            prof_capture_lane_tid_(lanes, &laneCount, "gpu");
        }
    }
    if (totalNodes == 0u || laneCount == 0u) {
        return 0;
    }

    U64 spallBudget = 32u + (U64)laneCount * 16u + totalNodes * (20u + 256u);
    U8* spallData = ARENA_PUSH_ARRAY(scratch.arena, U8, spallBudget);
    if (!spallData) {
        return 0;
    }

    U8* spallAt = spallData;
    {
        U64 magic = 0x0BADF00Dull;
        U64 version = 3ull;
        F64 timestampUnit = 0.001;
        U64 mustBeZero = 0ull;
        prof_capture_write_bytes_(&spallAt, &magic, 8u);
        prof_capture_write_bytes_(&spallAt, &version, 8u);
        prof_capture_write_bytes_(&spallAt, &timestampUnit, 8u);
        prof_capture_write_bytes_(&spallAt, &mustBeZero, 8u);
    }

    for (U32 laneIndex = 0u; laneIndex < laneCount; ++laneIndex) {
        U8* chunkHeader = spallAt;
        spallAt += 20u;
        U8* eventsBase = spallAt;
        U64 firstTs = 0u;

        U64 openEnds[PROF_OPEN_STACK_DEPTH];
        U32 openDepth = 0u;
        for (U64 frame = oldest; frame <= newest; ++frame) {
            ProfFrameSlot* slot = g_prof.slots + (frame % PROF_COLLATION_FRAMES);
            if (slot->frameIndex != frame) {
                continue;
            }
            if (lanes[laneIndex].name[0] == 'g' && lanes[laneIndex].name[1] == 'p' &&
                lanes[laneIndex].name[2] == 'u' && lanes[laneIndex].name[3] == 0) {
                spallAt = eventsBase + prof_capture_lane_events_(eventsBase, spallAt, slot->gpuNodes,
                                                                 slot->gpuNodeCount, baseNs, &firstTs,
                                                                 &openDepth, openEnds);
            } else {
                for (U32 slotLane = 0u; slotLane < slot->laneCount; ++slotLane) {
                    if (slot->lanes[slotLane].name != lanes[laneIndex].name) {
                        continue;
                    }
                    spallAt = eventsBase + prof_capture_lane_events_(eventsBase, spallAt,
                                                                     slot->nodes + slot->lanes[slotLane].firstNode,
                                                                     slot->lanes[slotLane].nodeCount, baseNs,
                                                                     &firstTs, &openDepth, openEnds);
                }
            }
            while (openDepth != 0u) {
                openDepth -= 1u;
                U8 endType = 4u;
                U64 when = openEnds[openDepth] - baseNs;
                prof_capture_write_bytes_(&spallAt, &endType, 1u);
                prof_capture_write_bytes_(&spallAt, &when, 8u);
            }
        }

        U32 chunkSize = (U32)(spallAt - eventsBase);
        U32 tid = lanes[laneIndex].tid;
        U32 pid = 1u;
        U8* headerAt = chunkHeader;
        prof_capture_write_bytes_(&headerAt, &chunkSize, 4u);
        prof_capture_write_bytes_(&headerAt, &tid, 4u);
        prof_capture_write_bytes_(&headerAt, &pid, 4u);
        prof_capture_write_bytes_(&headerAt, &firstTs, 8u);
    }

    Str8List json;
    str8list_init(&json, scratch.arena, 256);
    str8list_push(&json, str8("{\"traceEvents\":[\n"));
    B32 first = 1;
    for (U32 laneIndex = 0u; laneIndex < laneCount; ++laneIndex) {
        if (!first) {
            str8list_push(&json, str8(",\n"));
        }
        first = 0;
        str8list_push(&json, str8("{\"ph\":\"M\",\"name\":\"thread_name\",\"pid\":1,\"tid\":"));
        str8list_push(&json, str8_from_U64(scratch.arena, lanes[laneIndex].tid, 10));
        str8list_push(&json, str8(",\"args\":{\"name\":\""));
        str8list_push(&json, str8(lanes[laneIndex].name));
        str8list_push(&json, str8("\"}}"));
    }
    for (U64 frame = oldest; frame <= newest; ++frame) {
        ProfFrameSlot* slot = g_prof.slots + (frame % PROF_COLLATION_FRAMES);
        if (slot->frameIndex != frame) {
            continue;
        }
        for (U32 laneIndex = 0u; laneIndex < slot->laneCount + 1u; ++laneIndex) {
            const ProfFrameNode* nodes = 0;
            U32 nodeCount = 0u;
            U32 tid = 0u;
            if (laneIndex < slot->laneCount) {
                nodes = slot->nodes + slot->lanes[laneIndex].firstNode;
                nodeCount = slot->lanes[laneIndex].nodeCount;
                tid = prof_capture_lane_tid_(lanes, &laneCount, slot->lanes[laneIndex].name);
            } else if (slot->gpuNodeCount != 0u) {
                nodes = slot->gpuNodes;
                nodeCount = slot->gpuNodeCount;
                tid = prof_capture_lane_tid_(lanes, &laneCount, "gpu");
            }
            for (U32 nodeIndex = 0u; nodeIndex < nodeCount; ++nodeIndex) {
                const ProfFrameNode* node = nodes + nodeIndex;
                str8list_push(&json, str8(",\n{\"name\":\""));
                str8list_push(&json, str8(g_prof.sites[node->site].label));
                str8list_push(&json, str8("\",\"ph\":\"X\",\"ts\":"));
                str8list_push(&json, str8_from_F64(scratch.arena, (F64)(node->startNs - baseNs) / 1000.0, 3));
                str8list_push(&json, str8(",\"dur\":"));
                str8list_push(&json, str8_from_F64(scratch.arena, (F64)(node->endNs - node->startNs) / 1000.0, 3));
                str8list_push(&json, str8(",\"pid\":1,\"tid\":"));
                str8list_push(&json, str8_from_U64(scratch.arena, tid, 10));
                str8list_push(&json, str8("}"));
            }
        }
    }
    str8list_push(&json, str8("\n]}\n"));
    StringU8 jsonText = str8_concat_n(scratch.arena, json.items, json.count);

    StringU8 spallPath = str8_cpy(scratch.arena, str8_fmt(scratch.arena, "{}/prof_{}.spall",
                                  str8(directory ? directory : "captures"), newest));
    StringU8 jsonPath = str8_cpy(scratch.arena, str8_fmt(scratch.arena, "{}/prof_{}.json",
                                 str8(directory ? directory : "captures"), newest));

    OS_Handle spallFile = OS_file_open((const char*)spallPath.data, OS_FileOpenMode_Create);
    if (spallFile.handle) {
        OS_file_write(spallFile, (U64)(spallAt - spallData), spallData);
        OS_file_close(spallFile);
    }
    OS_Handle jsonFile = OS_file_open((const char*)jsonPath.data, OS_FileOpenMode_Create);
    if (jsonFile.handle) {
        OS_file_write(jsonFile, jsonText.size, jsonText.data);
        OS_file_close(jsonFile);
    }

    LOG_INFO("prof", "Captured {} frames -> {} (gpu spans {} dropped {})",
             (U64)(newest - oldest + 1u), spallPath, g_prof.gpuSpans, g_prof.gpuSpansDropped);
    return 1;
}

ProfInfo prof_info() {
    ProfInfo info = {};
    info.tickFrequencyHz = g_prof.tickFrequencyHz;
    info.resolutionNs = g_prof.resolutionNs;
    info.overheadNsPerScope = g_prof.overheadNsPerScope;
    info.droppedEvents = g_prof.droppedEvents;
    info.gpuSpans = g_prof.gpuSpans;
    info.gpuSpansDropped = g_prof.gpuSpansDropped;
    info.siteCount = g_prof.siteCount;
    info.threadCount = g_prof.threadCount;
    info.frameIndex = g_prof.frameIndex;
    info.invariantTick = g_prof.invariantTick;
    return info;
}

#else

void prof_init() {}
void prof_shutdown() {}
U32 prof_require_site(const char*, const char*, U32, U32) { return 0u; }
void prof_thread_bind(ProfTls* outTls) { if (outTls) { MEMSET(outTls, 0, sizeof(*outTls)); } }
void prof_thread_name(const char*) {}
void prof_frame_advance() {}
U64 prof_current_frame() { return 0u; }
void prof_pause(B32) {}
B32 prof_is_paused() { return 0; }
void prof_record_gate(B32) {}
U32 prof_site_gpu(const char*) { return 0u; }
void prof_gpu_span(U32, U64, U64, U64) {}
U64 prof_tick_to_ns(U64 tick) { return tick; }
U64 prof_now_ns() { return 0u; }
const ProfFrameView* prof_frame_view() { return 0; }
const ProfSiteStats* prof_site_stats(U32* outCount) { if (outCount) { *outCount = 0u; } return 0; }
const ProfPathStats* prof_path_stats(U32* outCount) { if (outCount) { *outCount = 0u; } return 0; }
const F32* prof_frame_history(U32* outCount, U32* outOffset) { if (outCount) { *outCount = 0u; } if (outOffset) { *outOffset = 0u; } return 0; }
ProfInfo prof_info() { ProfInfo info = {}; return info; }
B32 prof_capture(U32, const char*) { return 0; }

#endif
