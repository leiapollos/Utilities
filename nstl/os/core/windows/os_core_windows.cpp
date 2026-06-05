//
// Created by André Leite on 26/07/2025.
//

OS_WINDOWS_State g_OS_WindowsState = {};

OS_SystemInfo* OS_get_system_info() {
    return &g_OS_WindowsState.systemInfo;
}

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

    U32 capacity = MAX_PATH;
    char* pathBuffer = 0;

    for (;;) {
        arena_pop_to(scratch.arena, scratch.pos);
        pathBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, capacity);
        DWORD length = GetModuleFileNameA(0, pathBuffer, capacity);
        if (length == 0u) {
            return STR8_NIL;
        }
        if (length < capacity - 1u) {
            pathBuffer[length] = 0;
            break;
        }
        capacity *= 2u;
    }

    U64 pathLength = (U64)C_STR_LEN(pathBuffer);
    S64 slashIndex = (S64)pathLength - 1;
    while (slashIndex >= 0 && pathBuffer[slashIndex] != '\\' && pathBuffer[slashIndex] != '/') {
        slashIndex -= 1;
    }

    U64 directoryLength = 0;
    if (slashIndex >= 0) {
        pathBuffer[slashIndex] = 0;
        directoryLength = (U64)slashIndex;
    }

    return str8_cpy(arena, str8((U8*)pathBuffer, directoryLength));
}

void OS_set_environment_variable(StringU8 name, StringU8 value) {
    if (!name.data || name.size == 0u || !value.data) {
        return;
    }

    Temp scratch = get_scratch(0, 0u);
    if (!scratch.arena) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    char* nameBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, name.size + 1u);
    char* valueBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, value.size + 1u);
    if (!nameBuffer || !valueBuffer) {
        return;
    }
    MEMCPY(nameBuffer, name.data, name.size);
    MEMCPY(valueBuffer, value.data, value.size);
    nameBuffer[name.size] = 0;
    valueBuffer[value.size] = 0;
    SetEnvironmentVariableA(nameBuffer, valueBuffer);
}

StringU8 OS_get_environment_variable(Arena* arena, StringU8 name) {
    if (!arena || !name.data || name.size == 0u) {
        return STR8_NIL;
    }

    Arena* excludes[] = {arena};
    Temp scratch = get_scratch(excludes, ARRAY_COUNT(excludes));
    if (!scratch.arena) {
        return STR8_NIL;
    }
    DEFER_REF(temp_end(&scratch));

    char* nameBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, name.size + 1u);
    if (!nameBuffer) {
        return STR8_NIL;
    }
    MEMCPY(nameBuffer, name.data, name.size);
    nameBuffer[name.size] = 0;

    DWORD needed = GetEnvironmentVariableA(nameBuffer, 0, 0);
    if (needed == 0u) {
        return STR8_NIL;
    }

    char* valueBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, needed);
    if (!valueBuffer) {
        return STR8_NIL;
    }
    DWORD written = GetEnvironmentVariableA(nameBuffer, valueBuffer, needed);
    if (written == 0u || written >= needed) {
        return STR8_NIL;
    }

    return str8_cpy(arena, str8((U8*)valueBuffer, (U64)written));
}

B32 OS_library_open(StringU8 path, OS_SharedLibrary* outLibrary) {
    if (!outLibrary) {
        return 0;
    }
    outLibrary->handle = 0;
    if (!path.data || path.size == 0u) {
        return 0;
    }

    Temp scratch = get_scratch(0, 0u);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    char* pathBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, path.size + 1u);
    if (!pathBuffer) {
        return 0;
    }
    MEMCPY(pathBuffer, path.data, path.size);
    pathBuffer[path.size] = 0;

    HMODULE handle = LoadLibraryA(pathBuffer);
    if (!handle) {
        return 0;
    }

    outLibrary->handle = (void*)handle;
    return 1;
}

void OS_library_close(OS_SharedLibrary library) {
    if (!library.handle) {
        return;
    }
    FreeLibrary((HMODULE)library.handle);
}

void* OS_library_load_symbol(OS_SharedLibrary library, StringU8 symbolName) {
    if (!library.handle || !symbolName.data || symbolName.size == 0u) {
        return 0;
    }

    Temp scratch = get_scratch(0, 0u);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    char* symbolBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, symbolName.size + 1u);
    if (!symbolBuffer) {
        return 0;
    }
    MEMCPY(symbolBuffer, symbolName.data, symbolName.size);
    symbolBuffer[symbolName.size] = 0;

    return (void*)GetProcAddress((HMODULE)library.handle, symbolBuffer);
}

StringU8 OS_library_last_error(Arena* arena) {
    DWORD error = GetLastError();
    if (error == 0u) {
        return STR8_NIL;
    }

    char* buffer = 0;
    DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                  FORMAT_MESSAGE_FROM_SYSTEM |
                                  FORMAT_MESSAGE_IGNORE_INSERTS,
                                  0,
                                  error,
                                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                  (LPSTR)&buffer,
                                  0,
                                  0);
    if (length == 0u || !buffer) {
        return STR8_NIL;
    }

    StringU8 result = str8((U8*)buffer, (U64)length);
    if (arena) {
        result = str8_cpy(arena, result);
    }
    LocalFree(buffer);
    return result;
}

S32 OS_execute(StringU8 command) {
    if (!command.data || command.size == 0u) {
        return -1;
    }

    Temp scratch = get_scratch(0, 0u);
    if (!scratch.arena) {
        return -1;
    }
    DEFER_REF(temp_end(&scratch));

    char* commandBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, command.size + 1u);
    if (!commandBuffer) {
        return -1;
    }
    MEMCPY(commandBuffer, command.data, command.size);
    commandBuffer[command.size] = 0;
    return (S32)system(commandBuffer);
}

U64 OS_get_time_microseconds() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    U64 result = ((U64)counter.QuadPart * MILLION(1ULL)) / (U64)g_OS_WindowsState.counterFrequency.QuadPart;
    return result;
}

U64 OS_get_time_nanoseconds() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    U64 result = ((U64)counter.QuadPart * BILLION(1ULL)) / (U64)g_OS_WindowsState.counterFrequency.QuadPart;
    return result;
}

U64 OS_rdtsc_relaxed() {
    return __rdtsc();
}

U64 OS_rdtscp_serialized() {
    unsigned int aux = 0;
    return __rdtscp(&aux);
}

U64 OS_get_counter_frequency_hz() {
    return (U64)g_OS_WindowsState.counterFrequency.QuadPart;
}

void OS_sleep_milliseconds(U32 milliseconds) {
    Sleep((DWORD)milliseconds);
}

void OS_abort(S32 exitCode) {
    ExitProcess((UINT)exitCode);
}

void* OS_reserve(U64 size) {
    return VirtualAlloc(0, (SIZE_T)size, MEM_RESERVE, PAGE_READWRITE);
}

B32 OS_commit(void* ptr, U64 size) {
    return VirtualAlloc(ptr, (SIZE_T)size, MEM_COMMIT, PAGE_READWRITE) ? 1 : 0;
}

void OS_decommit(void* ptr, U64 size) {
    if (ptr && size != 0u) {
        VirtualFree(ptr, (SIZE_T)size, MEM_DECOMMIT);
    }
}

void OS_release(void* ptr, U64 size) {
    (void)size;
    if (ptr) {
        VirtualFree(ptr, 0, MEM_RELEASE);
    }
}

static DWORD WINAPI _OS_thread_entry_point(void* arg) {
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)arg;
#if defined(OS_WINDOWS_STANDALONE_THREAD_ENTRY)
    thread_context_init();
    entity->thread.func(entity->thread.args);
    thread_context_release();
#else
    thread_entry_point(entity->thread.func, entity->thread.args);
#endif
    return 0;
}

OS_Handle OS_thread_create(OS_ThreadFunc* func, void* arg) {
    OS_Handle result = {};
    if (!func) {
        return result;
    }

    OS_WINDOWS_Entity* entity = alloc_OS_entity();
    if (!entity) {
        return result;
    }
    entity->type = OS_WINDOWS_EntityType_Thread;
    entity->thread.func = func;
    entity->thread.args = arg;
    entity->thread.handle = CreateThread(0, 0, _OS_thread_entry_point, entity, 0, &entity->thread.id);
    if (!entity->thread.handle) {
        free_OS_entity(entity);
        return result;
    }

    result.handle = (U64*)entity;
    return result;
}

B32 OS_thread_join(OS_Handle thread) {
    if (!thread.handle) {
        return 0;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)thread.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_Thread);
    DWORD waitResult = WaitForSingleObject(entity->thread.handle, INFINITE);
    CloseHandle(entity->thread.handle);
    free_OS_entity(entity);
    return (waitResult == WAIT_OBJECT_0) ? 1 : 0;
}

void OS_thread_detach(OS_Handle thread) {
    if (!thread.handle) {
        return;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)thread.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_Thread);
    CloseHandle(entity->thread.handle);
    free_OS_entity(entity);
}

void OS_thread_yield() {
    SwitchToThread();
}

void OS_cpu_pause() {
    YieldProcessor();
}

U32 OS_get_thread_id_u32() {
    return (U32)GetCurrentThreadId();
}

OS_Handle OS_mutex_create() {
    OS_Handle result = {};
    OS_WINDOWS_Entity* entity = alloc_OS_entity();
    if (!entity) {
        return result;
    }
    entity->type = OS_WINDOWS_EntityType_Mutex;
    InitializeCriticalSection(&entity->mutex);
    result.handle = (U64*)entity;
    return result;
}

void OS_mutex_destroy(OS_Handle mutex) {
    if (!mutex.handle) {
        return;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)mutex.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_Mutex);
    DeleteCriticalSection(&entity->mutex);
    free_OS_entity(entity);
}

void OS_mutex_lock(OS_Handle mutex) {
    if (!mutex.handle) {
        return;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)mutex.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_Mutex);
    EnterCriticalSection(&entity->mutex);
}

void OS_mutex_unlock(OS_Handle mutex) {
    if (!mutex.handle) {
        return;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)mutex.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_Mutex);
    LeaveCriticalSection(&entity->mutex);
}

OS_Handle OS_condition_variable_create() {
    OS_Handle result = {};
    OS_WINDOWS_Entity* entity = alloc_OS_entity();
    if (!entity) {
        return result;
    }
    entity->type = OS_WINDOWS_EntityType_ConditionVariable;
    InitializeConditionVariable(&entity->conditionVariable);
    result.handle = (U64*)entity;
    return result;
}

void OS_condition_variable_destroy(OS_Handle conditionVariable) {
    if (!conditionVariable.handle) {
        return;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)conditionVariable.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_ConditionVariable);
    free_OS_entity(entity);
}

void OS_condition_variable_wait(OS_Handle conditionVariable, OS_Handle mutex) {
    if (!conditionVariable.handle || !mutex.handle) {
        return;
    }
    OS_WINDOWS_Entity* conditionEntity = (OS_WINDOWS_Entity*)conditionVariable.handle;
    OS_WINDOWS_Entity* mutexEntity = (OS_WINDOWS_Entity*)mutex.handle;
    ASSERT_DEBUG(conditionEntity->type == OS_WINDOWS_EntityType_ConditionVariable);
    ASSERT_DEBUG(mutexEntity->type == OS_WINDOWS_EntityType_Mutex);
    SleepConditionVariableCS(&conditionEntity->conditionVariable, &mutexEntity->mutex, INFINITE);
}

void OS_condition_variable_signal(OS_Handle conditionVariable) {
    if (!conditionVariable.handle) {
        return;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)conditionVariable.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_ConditionVariable);
    WakeConditionVariable(&entity->conditionVariable);
}

void OS_condition_variable_broadcast(OS_Handle conditionVariable) {
    if (!conditionVariable.handle) {
        return;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)conditionVariable.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_ConditionVariable);
    WakeAllConditionVariable(&entity->conditionVariable);
}

OS_Handle OS_barrier_create(U32 threadCount) {
    OS_Handle result = {};
    OS_WINDOWS_Entity* entity = alloc_OS_entity();
    if (!entity) {
        return result;
    }
    entity->type = OS_WINDOWS_EntityType_Barrier;
    entity->barrier.conditionHandle = OS_condition_variable_create();
    entity->barrier.mutexHandle = OS_mutex_create();
    entity->barrier.threadCount = threadCount;
    if (!entity->barrier.conditionHandle.handle || !entity->barrier.mutexHandle.handle) {
        OS_Handle handle = {(U64*)entity};
        OS_barrier_destroy(handle);
        return result;
    }
    result.handle = (U64*)entity;
    return result;
}

void OS_barrier_destroy(OS_Handle barrierHandle) {
    if (!barrierHandle.handle) {
        return;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)barrierHandle.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_Barrier);
    OS_condition_variable_destroy(entity->barrier.conditionHandle);
    OS_mutex_destroy(entity->barrier.mutexHandle);
    free_OS_entity(entity);
}

void OS_barrier_wait(OS_Handle barrierHandle) {
    if (!barrierHandle.handle) {
        return;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)barrierHandle.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_Barrier);

    OS_mutex_lock(entity->barrier.mutexHandle);
    U32 generation = entity->barrier.generation;
    entity->barrier.waitingCount += 1u;
    if (entity->barrier.waitingCount == entity->barrier.threadCount) {
        entity->barrier.waitingCount = 0u;
        entity->barrier.generation += 1u;
        OS_condition_variable_broadcast(entity->barrier.conditionHandle);
    } else {
        while (generation == entity->barrier.generation) {
            OS_condition_variable_wait(entity->barrier.conditionHandle, entity->barrier.mutexHandle);
        }
    }
    OS_mutex_unlock(entity->barrier.mutexHandle);
}

OS_Handle OS_file_open(const char* path, OS_FileOpenMode mode) {
    OS_Handle result = {};
    if (!path) {
        return result;
    }

    DWORD access = GENERIC_READ;
    DWORD creation = OPEN_EXISTING;
    if (mode == OS_FileOpenMode_Write) {
        access = GENERIC_WRITE;
    } else if (mode == OS_FileOpenMode_Create) {
        access = GENERIC_READ | GENERIC_WRITE;
        creation = CREATE_ALWAYS;
    }

    HANDLE file = CreateFileA(path, access, FILE_SHARE_READ, 0, creation, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return result;
    }

    OS_WINDOWS_Entity* entity = alloc_OS_entity();
    if (!entity) {
        CloseHandle(file);
        return result;
    }
    entity->type = OS_WINDOWS_EntityType_File;
    entity->file.handle = file;
    result.handle = (U64*)entity;
    return result;
}

void OS_file_close(OS_Handle fileHandle) {
    if (!fileHandle.handle) {
        return;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)fileHandle.handle;
    if (entity->type == OS_WINDOWS_EntityType_File) {
        if (entity->file.handle && entity->file.handle != INVALID_HANDLE_VALUE) {
            CloseHandle(entity->file.handle);
        }
        free_OS_entity(entity);
    }
}

U64 OS_file_size(OS_Handle fileHandle) {
    if (!fileHandle.handle) {
        return 0u;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)fileHandle.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_File);
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(entity->file.handle, &size)) {
        return 0u;
    }
    return (U64)size.QuadPart;
}

void OS_file_set_hints(OS_Handle h, U64 hints) {
    (void)h;
    (void)hints;
}

OS_FileMapping OS_file_map_ro(OS_Handle fileHandle) {
    OS_FileMapping mapping = {};
    if (!fileHandle.handle) {
        return mapping;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)fileHandle.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_File);

    U64 size = OS_file_size(fileHandle);
    if (size == 0u) {
        return mapping;
    }

    HANDLE mapHandle = CreateFileMappingA(entity->file.handle, 0, PAGE_READONLY, 0, 0, 0);
    if (!mapHandle) {
        return mapping;
    }

    void* ptr = MapViewOfFile(mapHandle, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapHandle);
    if (!ptr) {
        return mapping;
    }

    mapping.ptr = ptr;
    mapping.length = size;
    return mapping;
}

void OS_file_unmap(OS_FileMapping mapping) {
    if (mapping.ptr) {
        UnmapViewOfFile(mapping.ptr);
    }
}

static U64 OS_file_read_write_chunks(HANDLE file, void* data, U64 size, B32 write) {
    U8* at = (U8*)data;
    U64 done = 0u;
    while (done < size) {
        U64 remaining = size - done;
        DWORD chunk = (remaining > 0x7ffff000u) ? 0x7ffff000u : (DWORD)remaining;
        DWORD processed = 0u;
        BOOL ok = write ? WriteFile(file, at + done, chunk, &processed, 0) :
                          ReadFile(file, at + done, chunk, &processed, 0);
        if (!ok || processed == 0u) {
            break;
        }
        done += processed;
    }
    return done;
}

U64 OS_file_read(OS_Handle fileHandle, RangeU64 range, void* dst) {
    if (!fileHandle.handle || !dst) {
        return 0u;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)fileHandle.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_File);

    LARGE_INTEGER distance = {};
    distance.QuadPart = (LONGLONG)range.min;
    if (!SetFilePointerEx(entity->file.handle, distance, 0, FILE_BEGIN)) {
        return 0u;
    }
    return OS_file_read_write_chunks(entity->file.handle, dst, range.max - range.min, 0);
}

U64 OS_file_read(OS_Handle fileHandle, U64 size, void* dst) {
    if (!fileHandle.handle || !dst) {
        return 0u;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)fileHandle.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_File);
    return OS_file_read_write_chunks(entity->file.handle, dst, size, 0);
}

U64 OS_file_write(OS_Handle fileHandle, RangeU64 range, const void* src) {
    if (!fileHandle.handle || !src) {
        return 0u;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)fileHandle.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_File);

    LARGE_INTEGER distance = {};
    distance.QuadPart = (LONGLONG)range.min;
    if (!SetFilePointerEx(entity->file.handle, distance, 0, FILE_BEGIN)) {
        return 0u;
    }
    return OS_file_read_write_chunks(entity->file.handle, (void*)src, range.max - range.min, 1);
}

U64 OS_file_write(OS_Handle fileHandle, U64 size, const void* src) {
    if (!fileHandle.handle || !src) {
        return 0u;
    }
    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)fileHandle.handle;
    ASSERT_DEBUG(entity->type == OS_WINDOWS_EntityType_File);
    return OS_file_read_write_chunks(entity->file.handle, (void*)src, size, 1);
}

OS_Handle OS_get_log_handle() {
    static OS_WINDOWS_Entity entity = {
        .type = OS_WINDOWS_EntityType_File,
    };
    entity.file.handle = GetStdHandle(STD_OUTPUT_HANDLE);
    OS_Handle result = {(U64*)&entity};
    return result;
}

B32 OS_terminal_supports_color() {
    DWORD mode = 0u;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    return (output && GetConsoleMode(output, &mode)) ? 1 : 0;
}

OS_FileInfo OS_get_file_info(const char* path) {
    OS_FileInfo info = {};
    if (!path) {
        return info;
    }

    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
        return info;
    }
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        return info;
    }

    ULARGE_INTEGER size = {};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;

    ULARGE_INTEGER writeTime = {};
    writeTime.HighPart = data.ftLastWriteTime.dwHighDateTime;
    writeTime.LowPart = data.ftLastWriteTime.dwLowDateTime;

    info.exists = 1;
    info.size = size.QuadPart;
    info.lastWriteTimestampNs = writeTime.QuadPart * 100u;
    return info;
}

B32 OS_file_copy_contents(const char* srcPath, const char* dstPath) {
    if (!srcPath || !dstPath) {
        return 0;
    }
    return CopyFileA(srcPath, dstPath, FALSE) ? 1 : 0;
}

static OS_WINDOWS_Entity* alloc_OS_entity() {
    EnterCriticalSection(&g_OS_WindowsState.entityMutex);
    DEFER_REF(LeaveCriticalSection(&g_OS_WindowsState.entityMutex));

    OS_WINDOWS_Entity* entity = g_OS_WindowsState.freeEntities;
    if (entity) {
        g_OS_WindowsState.freeEntities = entity->next;
        MEMSET(entity, 0, sizeof(OS_WINDOWS_Entity));
        return entity;
    }

    Arena* arena = g_OS_WindowsState.osEntityArena;
    if (!arena) {
        return 0;
    }

    entity = ARENA_PUSH_STRUCT(arena, OS_WINDOWS_Entity);
    if (entity) {
        MEMSET(entity, 0, sizeof(OS_WINDOWS_Entity));
    }
    return entity;
}

static void free_OS_entity(OS_WINDOWS_Entity* entity) {
    if (!entity) {
        return;
    }

    EnterCriticalSection(&g_OS_WindowsState.entityMutex);
    DEFER_REF(LeaveCriticalSection(&g_OS_WindowsState.entityMutex));

    entity->next = g_OS_WindowsState.freeEntities;
    g_OS_WindowsState.freeEntities = entity;
}

#if !defined(OS_WINDOWS_NO_ENTRY_POINT)
int main(int argc, char** argv) {
    SYSTEM_INFO systemInfo = {};
    GetSystemInfo(&systemInfo);
    g_OS_WindowsState.systemInfo.logicalCores = systemInfo.dwNumberOfProcessors;
    g_OS_WindowsState.systemInfo.pageSize = systemInfo.dwPageSize;
    QueryPerformanceFrequency(&g_OS_WindowsState.counterFrequency);
    LARGE_INTEGER processStartCounter = {};
    QueryPerformanceCounter(&processStartCounter);
    g_OS_WindowsState.processStartMicroseconds =
        ((U64)processStartCounter.QuadPart * MILLION(1ULL)) / (U64)g_OS_WindowsState.counterFrequency.QuadPart;

    thread_context_alloc();

    Arena* arena = arena_alloc();
    g_OS_WindowsState.arena = arena;
    g_OS_WindowsState.osEntityArena = arena_alloc();
    InitializeCriticalSection(&g_OS_WindowsState.entityMutex);

    base_entry_point(argc, argv);
    return 0;
}
#endif
