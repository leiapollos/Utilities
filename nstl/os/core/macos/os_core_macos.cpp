//
// Created by André Leite on 26/07/2025.
//

// ////////////////////////
// System Info

static OS_SystemInfo* OS_get_system_info() {
    return &os_macos_state.system_info;
}


// ////////////////////////
// Time

static U64 OS_get_time_microseconds() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    U64 result = (t.tv_nsec / THOUSAND(1ULL)) + (t.tv_sec * MILLION(1ULL));
    return result;
}

static U64 OS_get_time_nanoseconds() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    U64 result = (U64) t.tv_sec * BILLION(1ULL) + (U64) t.tv_nsec;
    return result;
}

#if defined(PLATFORM_ARCH_ARM64)
static U64 OS_rdtsc_relaxed() {
    U64 value = 0;
#if defined(COMPILER_CLANG) || defined(COMPILER_GCC)
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(value));
#else
    // Fallback to microseconds if inline asm unavailable
    value = OS_get_time_nanoseconds();
#endif
    return value;
}

static U64 OS_rdtscp_serialized() {
    U64 value = 0;
#if defined(COMPILER_CLANG) || defined(COMPILER_GCC)
    __asm__ __volatile__("isb");
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(value));
    __asm__ __volatile__("isb");
#else
    value = OS_get_time_nanoseconds();
#endif
    return value;
}
#endif

static U64 OS_get_counter_frequency_hz() {
#if defined(PLATFORM_ARCH_ARM64)
    U64 freq = 0;
#if defined(COMPILER_CLANG) || defined(COMPILER_GCC)
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
#endif
    return freq;
#else
    return 0;
#endif
}


// ////////////////////////
// Aborting

static void OS_abort(S32 exit_code) {
    exit(exit_code);
}


// ////////////////////////
// Memory allocation

static void* OS_reserve(U64 size) {
    void* result = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED) {
        result = 0;
    }
    return result;
}

static B32 OS_commit(void* ptr, U64 size) {
    mprotect(ptr, size, PROT_READ | PROT_WRITE);
    return 1;
}

static void OS_decommit(void* ptr, U64 size) {
    madvise(ptr, size, MADV_DONTNEED);
    mprotect(ptr, size, PROT_NONE);
}

static void OS_release(void* ptr, U64 size) {
    munmap(ptr, size);
}


// ////////////////////////
// Threads and Synchronization

static U32 OS_get_thread_id_u32() {
    U64 threadId64 = 0;
    pthread_threadid_np(0, &threadId64);
    return (U32) threadId64;
}

static void OS_mutex_init(void** m) {
    OS_Mutex* mm = (OS_Mutex*) malloc(sizeof(OS_Mutex));
    pthread_mutex_init(&mm->m, 0);
    *m = (void*) mm;
}

static void OS_mutex_destroy(void* m) {
    if (!m)
        return;
    OS_Mutex* mm = (OS_Mutex*) m;
    pthread_mutex_destroy(&mm->m);
    free(mm);
}

static void OS_mutex_lock(void* m) {
    OS_Mutex* mm = (OS_Mutex*) m;
    pthread_mutex_lock(&mm->m);
}

static void OS_mutex_unlock(void* m) {
    OS_Mutex* mm = (OS_Mutex*) m;
    pthread_mutex_unlock(&mm->m);
}


// ////////////////////////
// Entry Point

int main(int argc, char** argv) {
    {
        OS_SystemInfo* info = &os_macos_state.system_info;
        info->pageSize = static_cast<U64>(sysconf(_SC_PAGESIZE));
        info->logicalCores = static_cast<U32>(sysconf(_SC_NPROCESSORS_ONLN));
    }

    base_entry_point(argc, argv);
}
