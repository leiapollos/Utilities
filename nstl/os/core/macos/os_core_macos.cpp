//
// Created by André Leite on 26/07/2025.
//

#include <dlfcn.h>

// ////////////////////////
// Globals

OS_MACOS_State g_OS_MacOSState = {};

// ////////////////////////
// System Info

OS_SystemInfo* OS_get_system_info() {
    return &g_OS_MacOSState.systemInfo;
}


// ////////////////////////
// Executable Path

StringU8 OS_get_executable_directory(Arena* arena) {
    if (!arena) {
        return STR8_NIL;
    }

    Arena* excludes[] = {arena};
    Temp scratch = get_scratch(excludes, ARRAY_COUNT(excludes));
    if (!scratch.arena) {
        return STR8_NIL;
    }

    DEFER_REF(temp_end(&scratch));

    U32 bufferCapacity = PATH_MAX;
    char* pathBuffer = 0;

    for (;;) {
        arena_pop_to(scratch.arena, scratch.pos);
        pathBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, (U64) bufferCapacity);

        U32 apiCapacity = bufferCapacity;
        int status = _NSGetExecutablePath(pathBuffer, &apiCapacity);
        if (status == 0) {
            bufferCapacity = apiCapacity;
            break;
        }

        if (status != 0) {
            if (apiCapacity <= bufferCapacity) {
                return STR8_NIL;
            }
            bufferCapacity = apiCapacity;
        }
    }

    U64 pathLength = (U64) C_STR_LEN(pathBuffer);
    S64 slashIndex = (S64) pathLength - 1;
    while (slashIndex >= 0 && pathBuffer[slashIndex] != '/') {
        slashIndex -= 1;
    }

    U64 directoryLength = 0;
    if (slashIndex >= 0) {
        pathBuffer[slashIndex] = '\0';
        directoryLength = (U64) slashIndex;
    } else {
        pathBuffer[0] = '\0';
        directoryLength = 0;
    }

    StringU8 directory = str8((U8*) pathBuffer, directoryLength);
    return str8_cpy(arena, directory);
}

void OS_set_environment_variable(StringU8 name, StringU8 value) {
    if (str8_is_nil(name) || str8_is_empty(name)) {
        return;
    }
    if (str8_is_nil(value)) {
        return;
    }

    setenv((const char*) name.data, (const char*) value.data, 1);
}

StringU8 OS_get_environment_variable(Arena* arena, StringU8 name) {
    if (!arena || !name.data || name.size == 0) {
        return STR8_NIL;
    }

    const char* rawValue = getenv((const char*) name.data);
    if (!rawValue) {
        return STR8_NIL;
    }

    return str8_cpy(arena, str8(rawValue));
}


B32 OS_library_open(StringU8 path, OS_SharedLibrary* outLibrary) {
    if (!outLibrary) {
        return 0;
    }

    outLibrary->handle = 0;
    if (!path.data || path.size == 0) {
        return 0;
    }

    void* handle = dlopen((const char*) path.data, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        return 0;
    }

    outLibrary->handle = handle;
    return 1;
}

void OS_library_close(OS_SharedLibrary library) {
    if (!library.handle) {
        return;
    }

    dlclose(library.handle);
}

void* OS_library_load_symbol(OS_SharedLibrary library, StringU8 symbolName) {
    if (!library.handle || !symbolName.data || symbolName.size == 0) {
        return 0;
    }

    return dlsym(library.handle, (const char*) symbolName.data);
}

StringU8 OS_library_last_error(Arena* arena) {
    const char* error = dlerror();
    if (!error) {
        return STR8_NIL;
    }

    if (!arena) {
        return str8(error);
    }

    return str8_cpy(arena, str8(error));
}

S32 OS_execute(StringU8 command) {
    if (!command.data || command.size == 0) {
        return -1;
    }

    return (S32) system((const char*) command.data);
}


// ////////////////////////
// Time

U64 OS_get_time_microseconds() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    U64 result = ((U64) t.tv_nsec / THOUSAND(1ULL)) + ((U64) t.tv_sec * MILLION(1ULL));
    return result;
}

U64 OS_get_time_nanoseconds() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    U64 result = (U64) t.tv_sec * BILLION(1ULL) + (U64) t.tv_nsec;
    return result;
}

#if defined(PLATFORM_ARCH_ARM64)
U64 OS_rdtsc_relaxed() {
    U64 value = 0;
#if defined(COMPILER_CLANG) || defined(COMPILER_GCC)
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(value));
#else
    // Fallback to microseconds if inline asm unavailable
    value = OS_get_time_nanoseconds();
#endif
    return value;
}

U64 OS_rdtscp_serialized() { // TODO: refactor.
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

U64 OS_get_counter_frequency_hz() {
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

void OS_sleep_milliseconds(U32 milliseconds) {
    struct timespec req = {0, 0};
    req.tv_sec = (time_t)(milliseconds / 1000);
    req.tv_nsec = (long)((milliseconds % 1000) * MILLION(1ULL));
    nanosleep(&req, 0);
}


// ////////////////////////
// Aborting

void OS_abort(S32 exit_code) {
    exit(exit_code);
}


// ////////////////////////
// Memory allocation

void* OS_reserve(U64 size) {
    void* result = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED) {
        result = 0;
    }
    return result;
}

B32 OS_commit(void* ptr, U64 size) {
    mprotect(ptr, size, PROT_READ | PROT_WRITE);
    return 1;
}

void OS_decommit(void* ptr, U64 size) {
    madvise(ptr, size, MADV_DONTNEED);
    mprotect(ptr, size, PROT_NONE);
}

void OS_release(void* ptr, U64 size) {
    munmap(ptr, size);
}


// ////////////////////////
// Threads and Synchronization

static void* _OS_thread_entry_point(void* arg) {
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) arg;

    thread_entry_point(entity->thread.func, entity->thread.args);
    return 0;
}

OS_Handle OS_thread_create(OS_ThreadFunc* func, void* arg) {
    OS_MACOS_Entity* entity = alloc_OS_entity();
    entity->type = OS_MACOS_EntityType_Thread;
    entity->thread.func = func;
    entity->thread.args = arg;

    int ret = pthread_create(&entity->thread.handle, NULL, _OS_thread_entry_point, (void*) entity);
    if (ret == -1) {
        free_OS_entity(entity);
        entity = 0;
    }

    OS_Handle handle = {(U64*) entity};
    return handle;
}

B32 OS_thread_join(OS_Handle thread) {
    ASSERT_DEBUG(thread.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) (thread.handle);
    ASSERT_DEBUG(entity->type == OS_MACOS_EntityType_Thread);
    int ret = pthread_join(entity->thread.handle, NULL);
    free_OS_entity(entity);
    return ret == 0;
}

void OS_thread_detach(OS_Handle thread) {
    ASSERT_DEBUG(thread.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) (thread.handle);
    ASSERT_DEBUG(entity->type == OS_MACOS_EntityType_Thread);
    pthread_detach(entity->thread.handle);
    free_OS_entity(entity);
}

void OS_thread_yield() {
    sched_yield();
}

void OS_cpu_pause() {
#if defined(PLATFORM_ARCH_ARM64)
    __builtin_arm_yield();
#elif defined(PLATFORM_ARCH_X64)
    __builtin_ia32_pause();
#else
    __asm__ __volatile__("nop");
#endif
}

U32 OS_get_thread_id_u32() {
    U64 threadId64 = 0;
    pthread_threadid_np(0, &threadId64);
    return (U32) threadId64;
}

OS_Handle OS_mutex_create() {
    OS_MACOS_Entity* entity = alloc_OS_entity();
    entity->type = OS_MACOS_EntityType_Mutex;
    pthread_mutex_init(&entity->mutex, 0);
    OS_Handle handle = {(U64*) entity};
    return handle;
}

void OS_mutex_destroy(OS_Handle mutex) {
    ASSERT_DEBUG(mutex.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) (mutex.handle);
    ASSERT_DEBUG(entity->type == OS_MACOS_EntityType_Mutex);
    pthread_mutex_destroy(&entity->mutex);
    free_OS_entity(entity);
}

void OS_mutex_lock(OS_Handle mutex) {
    ASSERT_DEBUG(mutex.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) (mutex.handle);
    ASSERT_DEBUG(entity->type == OS_MACOS_EntityType_Mutex);
    pthread_mutex_lock(&entity->mutex);
}

void OS_mutex_unlock(OS_Handle mutex) {
    ASSERT_DEBUG(mutex.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) (mutex.handle);
    ASSERT_DEBUG(entity->type == OS_MACOS_EntityType_Mutex);
    pthread_mutex_unlock(&entity->mutex);
}

OS_Handle OS_condition_variable_create() {
    OS_MACOS_Entity* entity = alloc_OS_entity();
    entity->type = OS_MACOS_EntityType_ConditionVariable;
    pthread_cond_init(&entity->conditionVariable.cond, 0);

    OS_Handle handle = {(U64*) entity};
    return handle;
}

void OS_condition_variable_destroy(OS_Handle conditionVariable) {
    ASSERT_DEBUG(conditionVariable.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) conditionVariable.handle;
    ASSERT_DEBUG(entity->type == OS_MACOS_EntityType_ConditionVariable);
    pthread_cond_destroy(&entity->conditionVariable.cond);
    free_OS_entity(entity);
}

void OS_condition_variable_wait(OS_Handle conditionVariable, OS_Handle mutex) {
    ASSERT_DEBUG(conditionVariable.handle != 0);
    ASSERT_DEBUG(mutex.handle != 0);

    OS_MACOS_Entity* conditionEntity = (OS_MACOS_Entity*) conditionVariable.handle;
    OS_MACOS_Entity* mutexEntity = (OS_MACOS_Entity*) mutex.handle;

    ASSERT_DEBUG(conditionEntity->type == OS_MACOS_EntityType_ConditionVariable);
    ASSERT_DEBUG(mutexEntity->type == OS_MACOS_EntityType_Mutex);

    pthread_cond_wait(&conditionEntity->conditionVariable.cond, &mutexEntity->mutex);
}

void OS_condition_variable_signal(OS_Handle conditionVariable) {
    ASSERT_DEBUG(conditionVariable.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) conditionVariable.handle;
    ASSERT_DEBUG(entity->type == OS_MACOS_EntityType_ConditionVariable);
    pthread_cond_signal(&entity->conditionVariable.cond);
}

void OS_condition_variable_broadcast(OS_Handle conditionVariable) {
    ASSERT_DEBUG(conditionVariable.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) conditionVariable.handle;
    ASSERT_DEBUG(entity->type == OS_MACOS_EntityType_ConditionVariable);
    pthread_cond_broadcast(&entity->conditionVariable.cond);
}

OS_Handle OS_barrier_create(U32 threadCount) {
    OS_MACOS_Entity* entity = alloc_OS_entity();
    entity->type = OS_MACOS_EntityType_Barrier;

    OS_Handle mutexHandle = OS_mutex_create();
    OS_Handle conditionHandle = OS_condition_variable_create();

    entity->barrier.mutexHandle = mutexHandle;
    entity->barrier.conditionHandle = conditionHandle;
    entity->barrier.threadCount = threadCount;
    entity->barrier.waitingCount = 0;
    entity->barrier.generation = 0;

    OS_Handle handle = {(U64*) entity};
    return handle;
}

void OS_barrier_destroy(OS_Handle barrierHandle) {
    ASSERT_DEBUG(barrierHandle.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) barrierHandle.handle;
    ASSERT_DEBUG(entity->type == OS_MACOS_EntityType_Barrier);

    OS_condition_variable_destroy(entity->barrier.conditionHandle);
    OS_mutex_destroy(entity->barrier.mutexHandle);
    free_OS_entity(entity);
}

void OS_barrier_wait(OS_Handle barrierHandle) {
    ASSERT_DEBUG(barrierHandle.handle != 0);
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) barrierHandle.handle;
    ASSERT_DEBUG(entity->type == OS_MACOS_EntityType_Barrier);

    OS_mutex_lock(entity->barrier.mutexHandle);

    U32 generation = entity->barrier.generation;
    entity->barrier.waitingCount += 1;

    if (entity->barrier.waitingCount == entity->barrier.threadCount) {
        entity->barrier.waitingCount = 0;
        entity->barrier.generation += 1;
        OS_condition_variable_broadcast(entity->barrier.conditionHandle);
    } else {
        while (generation == entity->barrier.generation) {
            OS_condition_variable_wait(entity->barrier.conditionHandle, entity->barrier.mutexHandle);
        }
    }

    OS_mutex_unlock(entity->barrier.mutexHandle);
}


// ////////////////////////
// File I/O

OS_Handle OS_file_open(const char* path, OS_FileOpenMode mode) {
    int flags = 0;
    // Permission bits for newly created files: 0666 = rw-rw-rw-.
    int modeBits = 0666;

    if (mode == OS_FileOpenMode_Read) {
        flags |= O_RDONLY;
    } else if (mode == OS_FileOpenMode_Write) {
        flags |= O_WRONLY;
    } else if (mode == OS_FileOpenMode_Create) {
        flags |= (O_CREAT | O_WRONLY | O_TRUNC);
    } else {
        ASSERT_ALWAYS(false && "Invalid OS_FileOpenMode");
    }

    int fd = open(path, flags, modeBits);
    if (fd == -1) {
        OS_Handle empty = {0};
        return empty;
    }

    OS_MACOS_Entity* fileEntity = alloc_OS_entity();
    fileEntity->type = OS_MACOS_EntityType_File;
    fileEntity->file.fd = fd;
    OS_Handle handle = {};
    handle.handle = (U64*) fileEntity;
    return handle;
}

void OS_file_close(OS_Handle fileHandle) {
    if (!fileHandle.handle) {
        return;
    }
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) fileHandle.handle;
    if (entity->type == OS_MACOS_EntityType_File) {
        if (entity->file.fd != -1) {
            close(entity->file.fd);
        }
    }
    free_OS_entity(entity);
}

U64 OS_file_size(OS_Handle fileHandle) {
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) fileHandle.handle;
    ASSERT_DEBUG(entity && entity->type == OS_MACOS_EntityType_File);
    struct stat fileStat;
    if (fstat(entity->file.fd, &fileStat) != 0) {
        return 0;
    }
    return (U64) fileStat.st_size;
}

void OS_file_set_hints(OS_Handle fileHandle, U64 hints) {
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) fileHandle.handle;
    ASSERT_DEBUG(entity && entity->type == OS_MACOS_EntityType_File);
    int enableValue = 1, disableValue = 0;
    fcntl(entity->file.fd, F_NOCACHE, FLAGS_HAS(hints, OS_FileHint_NoCache) ? enableValue : disableValue);
    fcntl(entity->file.fd, F_RDAHEAD, FLAGS_HAS(hints, OS_FileHint_Sequential) ? enableValue : disableValue);
}

OS_FileMapping OS_file_map_ro(OS_Handle fileHandle) {
    OS_FileMapping mapping = {0, 0};
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) fileHandle.handle;
    ASSERT_DEBUG(entity && entity->type == OS_MACOS_EntityType_File);
    U64 length = OS_file_size(fileHandle);
    if (length == 0) {
        return mapping;
    }
    void* mappedPtr = mmap(0, length, PROT_READ, MAP_PRIVATE, entity->file.fd, 0);
    if (mappedPtr == MAP_FAILED) {
        return mapping;
    }
    mapping.ptr = mappedPtr;
    mapping.length = length;
    return mapping;
}

void OS_file_unmap(OS_FileMapping mapping) {
    if (mapping.ptr && mapping.length) {
        munmap(mapping.ptr, mapping.length);
    }
}

static B32 OS_is_seekable(int fd) {
    return lseek(fd, 0, SEEK_CUR) != -1;
}

U64 OS_file_read(OS_Handle fileHandle, RangeU64 range, void* dst) {
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) fileHandle.handle;
    ASSERT_DEBUG(entity && entity->type == OS_MACOS_EntityType_File);
    ASSERT_DEBUG(OS_is_seekable(entity->file.fd));

    U8* destinationBytes = (U8*) dst;
    U64 totalTransferred = 0;
    U64 bytesToTransfer = (range.max >= range.min) ? (range.max - range.min) : 0;

    while (totalTransferred < bytesToTransfer) {
        size_t chunkSize = (size_t) MIN(bytesToTransfer - totalTransferred, (U64)SSIZE_MAX);
        ssize_t bytesRead = pread(entity->file.fd, destinationBytes + totalTransferred,
                                  chunkSize, (off_t) (range.min + totalTransferred));
        if (bytesRead < 0)
            return totalTransferred;
        if (bytesRead == 0)
            break;
        totalTransferred += (U64) bytesRead;
    }
    return totalTransferred;
}

U64 OS_file_read(OS_Handle fileHandle, U64 size, void* dst) {
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) fileHandle.handle;
    ASSERT_DEBUG(entity && entity->type == OS_MACOS_EntityType_File);
    ASSERT_DEBUG(!OS_is_seekable(entity->file.fd));

    U8* destinationBytes = (U8*) dst;
    U64 totalTransferred = 0;

    while (totalTransferred < size) {
        size_t chunkSize = (size_t) MIN(size - totalTransferred, (U64)SSIZE_MAX);
        ssize_t bytesRead = read(entity->file.fd, destinationBytes + totalTransferred, chunkSize);
        if (bytesRead < 0)
            return totalTransferred;
        if (bytesRead == 0)
            break;
        totalTransferred += (U64) bytesRead;
    }
    return totalTransferred;
}

U64 OS_file_write(OS_Handle fileHandle, RangeU64 range, const void* src) {
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) fileHandle.handle;
    ASSERT_DEBUG(entity && entity->type == OS_MACOS_EntityType_File);
    ASSERT_DEBUG(OS_is_seekable(entity->file.fd));

    const U8* sourceBytes = (const U8*) src;
    U64 totalTransferred = 0;
    U64 bytesToTransfer = (range.max >= range.min) ? (range.max - range.min) : 0;

    while (totalTransferred < bytesToTransfer) {
        size_t chunkSize = (size_t) MIN(bytesToTransfer - totalTransferred, (U64)SSIZE_MAX);
        ssize_t bytesWritten = pwrite(entity->file.fd, sourceBytes + totalTransferred,
                                      chunkSize, (off_t) (range.min + totalTransferred));
        if (bytesWritten < 0)
            return totalTransferred;
        if (bytesWritten == 0)
            break;
        totalTransferred += (U64) bytesWritten;
    }
    return totalTransferred;
}

U64 OS_file_write(OS_Handle fileHandle, U64 size, const void* src) {
    OS_MACOS_Entity* entity = (OS_MACOS_Entity*) fileHandle.handle;
    ASSERT_DEBUG(entity && entity->type == OS_MACOS_EntityType_File);
    ASSERT_DEBUG(!OS_is_seekable(entity->file.fd));

    const U8* sourceBytes = (const U8*) src;
    U64 totalTransferred = 0;

    while (totalTransferred < size) {
        size_t chunkSize = (size_t) MIN(size - totalTransferred, (U64)SSIZE_MAX);
        ssize_t bytesWritten = write(entity->file.fd, sourceBytes + totalTransferred, chunkSize);
        if (bytesWritten < 0)
            return totalTransferred;
        if (bytesWritten == 0)
            break;
        totalTransferred += (U64) bytesWritten;
    }
    return totalTransferred;
}

OS_Handle OS_get_log_handle() {
    static OS_MACOS_Entity entity{
        .type = OS_MACOS_EntityType_File,
        .file{
            .fd = STDOUT_FILENO,
        },
    };
    static OS_Handle handle{
        .handle = (U64*) &entity,
    };
    return handle;
}

B32 OS_terminal_supports_color() {
    if (getenv("NO_COLOR") != NULL) {
        return false;
    }
    if (!isatty(STDOUT_FILENO)) {
        return false;
    }

    const char* term = getenv("TERM");
    if (term && c_str_cmp(term, "dumb") == 0) {
        return false;
    }

    return true;
}


// ////////////////////////
// File Metadata / Copy

OS_FileInfo OS_get_file_info(const char* path) {
    OS_FileInfo info = {};
    if (!path) {
        return info;
    }

    struct stat fileStat;
    if (stat(path, &fileStat) != 0) {
        return info;
    }

    info.exists = 1;
    info.size = (U64) fileStat.st_size;
    info.lastWriteTimestampNs = ((U64) fileStat.st_mtimespec.tv_sec * BILLION(1ULL)) + (U64) fileStat.st_mtimespec.tv_nsec;
    return info;
}

B32 OS_file_copy_contents(const char* srcPath, const char* dstPath) {
    if (!srcPath || !dstPath) {
        return 0;
    }

    OS_FileInfo sourceInfo = OS_get_file_info(srcPath);
    if (!sourceInfo.exists) {
        errno = ENOENT;
        return 0;
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    Arena* scratchArena = scratch.arena;

    const U64 chunkSize = 64u * 1024u;
    U8* buffer = ARENA_PUSH_ARRAY(scratchArena, U8, chunkSize);
    if (!buffer) {
        return 0;
    }

    OS_Handle source = OS_file_open(srcPath, OS_FileOpenMode_Read);
    if (!source.handle) {
        return 0;
    }

    OS_Handle destination = OS_file_open(dstPath, OS_FileOpenMode_Create);
    if (!destination.handle) {
        int openErrno = errno;
        OS_file_close(source);
        errno = openErrno;
        return 0;
    }

    U64 offset = 0;
    B32 ok = 1;
    int savedErrno = 0;

    while (offset < sourceInfo.size && ok) {
        U64 remaining = sourceInfo.size - offset;
        U64 toTransfer = (remaining > chunkSize) ? chunkSize : remaining;

        RangeU64 range = {offset, offset + toTransfer};

        U64 readBytes = OS_file_read(source, range, buffer);
        if (readBytes != toTransfer) {
            ok = 0;
            savedErrno = errno;
            break;
        }

        U64 writtenBytes = OS_file_write(destination, range, buffer);
        if (writtenBytes != toTransfer) {
            ok = 0;
            savedErrno = errno;
            break;
        }

        offset += toTransfer;
    }

    OS_file_close(destination);
    OS_file_close(source);

    if (!ok) {
        unlink(dstPath);
        errno = savedErrno;
        return 0;
    }

    return 1;
}


// ////////////////////////
// State

static OS_MACOS_Entity* alloc_OS_entity() {
    pthread_mutex_lock(&g_OS_MacOSState.entityMutex);
    DEFER_REF(pthread_mutex_unlock(&g_OS_MacOSState.entityMutex));

    OS_MACOS_Entity* entity = g_OS_MacOSState.freeEntities;
    if (entity) {
        g_OS_MacOSState.freeEntities = entity->next;
        MEMSET(entity, 0, sizeof(OS_MACOS_Entity));
    } else {
        Arena* arena = g_OS_MacOSState.osEntityArena;
        entity = (OS_MACOS_Entity*) arena_push(arena, sizeof(OS_MACOS_Entity), alignof(OS_MACOS_Entity));
        if (entity) {
            MEMSET(entity, 0, sizeof(OS_MACOS_Entity));
        }
    }
    return entity;
}

static void free_OS_entity(OS_MACOS_Entity* entity) {
    if (!entity) {
        return;
    }
    pthread_mutex_lock(&g_OS_MacOSState.entityMutex);
    DEFER_REF(pthread_mutex_unlock(&g_OS_MacOSState.entityMutex));

    entity->next = g_OS_MacOSState.freeEntities;
    g_OS_MacOSState.freeEntities = entity;
}


// ////////////////////////
// Entry Point

int main(int argc, char** argv) {
    {
        OS_SystemInfo* info = &g_OS_MacOSState.systemInfo;
        info->pageSize = static_cast<U64>(sysconf(_SC_PAGESIZE));
        info->logicalCores = static_cast<U32>(sysconf(_SC_NPROCESSORS_ONLN));
    }

    thread_context_alloc();

    Arena* arena = arena_alloc();
    g_OS_MacOSState.arena = arena;

    Arena* entityArena = arena_alloc();
    g_OS_MacOSState.osEntityArena = entityArena;

    pthread_mutex_init(&g_OS_MacOSState.entityMutex, 0);

    base_entry_point(argc, argv);
}
