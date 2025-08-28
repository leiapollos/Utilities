//
// Created by André Leite on 26/07/2025.
//

// ////////////////////////
// System Info

static OS_SystemInfo* OS_get_system_info() {
    return &osMacosState.systemInfo;
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

static void* _OS_thread_entry_point(void* arg) {
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*)arg;
    
    thread_entry_point(entity->thread.func, entity->thread.args);
    return 0;
}

static OS_Handle OS_thread_create(OS_ThreadFunc* func, void* arg) {
    OS_MACOS_Entity* entity = alloc_OS_entity();
    entity->type = OS_MACOS_EntityType::Thread;
    entity->thread.func = func;
    entity->thread.args = arg;
    
    int ret = pthread_create(&entity->thread.handle, NULL, _OS_thread_entry_point, (void*)entity);
    if(ret == -1) {
        free_OS_entity(entity);
        entity = 0;
    }
    
    OS_Handle handle= {(U64*)entity};
    return handle;
}

static B32 OS_thread_join(OS_Handle thread) {
    ASSERT_DEBUG(thread.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*)(thread.handle);
    ASSERT_DEBUG(entity->type.has(OS_MACOS_EntityType::Thread));
    int ret = pthread_join(entity->thread.handle, NULL);
    free_OS_entity(entity);
    return ret == 0;
}

static void OS_thread_detach(OS_Handle thread) {
    ASSERT_DEBUG(thread.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*)(thread.handle);
    ASSERT_DEBUG(entity->type.has(OS_MACOS_EntityType::Thread));
    pthread_detach(entity->thread.handle);
    free_OS_entity(entity);
}

static U32 OS_get_thread_id_u32() {
    U64 threadId64 = 0;
    pthread_threadid_np(0, &threadId64);
    return (U32) threadId64;
}

static OS_Handle OS_mutex_create() {
    OS_MACOS_Entity* entity = alloc_OS_entity();
    entity->type = OS_MACOS_EntityType::Mutex;
    pthread_mutex_init(&entity->mutex, 0);
    OS_Handle handle = {(U64*)entity};
    return handle;
}

static void OS_mutex_destroy(OS_Handle mutex) {
    ASSERT_DEBUG(mutex.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*)(mutex.handle);
    ASSERT_DEBUG(entity->type.has(OS_MACOS_EntityType::Mutex));
    pthread_mutex_destroy(&entity->mutex);
    free_OS_entity(entity);
}

static void OS_mutex_lock(OS_Handle mutex) {
    ASSERT_DEBUG(mutex.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*)(mutex.handle);
    ASSERT_DEBUG(entity->type.has(OS_MACOS_EntityType::Mutex));
    pthread_mutex_lock(&entity->mutex);
}

static void OS_mutex_unlock(OS_Handle mutex) {
    ASSERT_DEBUG(mutex.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*)(mutex.handle);
    ASSERT_DEBUG(entity->type.has(OS_MACOS_EntityType::Mutex));
    pthread_mutex_unlock(&entity->mutex);
}


// ////////////////////////
// State

static OS_MACOS_Entity* alloc_OS_entity() {
    if (osMacosState.freeEntities) {
        OS_MACOS_Entity* entity = osMacosState.freeEntities;
        osMacosState.freeEntities = entity->next;
        return entity;
    }
    
    Arena* arena = osMacosState.osEntityArena;
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*)arena_push(arena, sizeof(OS_MACOS_Entity), alignof(OS_MACOS_Entity));
    memset(entity, 0, sizeof(OS_MACOS_Entity));
    return entity;
}

static void free_OS_entity(OS_MACOS_Entity* entity) {
    if (!entity) {
        return;
    }
    entity->next = osMacosState.freeEntities;
    osMacosState.freeEntities = entity;
}


// ////////////////////////
// Entry Point

int main(int argc, char** argv) {
    {
        OS_SystemInfo* info = &osMacosState.systemInfo;
        info->pageSize = static_cast<U64>(sysconf(_SC_PAGESIZE));
        info->logicalCores = static_cast<U32>(sysconf(_SC_NPROCESSORS_ONLN));
    }
    
    scratch_thread_init();
    
    Arena* arena = arena_alloc();
    osMacosState.arena = arena;
    
    Arena* entityArena = arena_alloc();
    osMacosState.osEntityArena = entityArena;

    base_entry_point(argc, argv);
}
