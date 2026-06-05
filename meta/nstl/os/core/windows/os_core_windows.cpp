OS_WINDOWS_State g_OS_WindowsState = {};

static OS_WINDOWS_Entity* alloc_OS_entity();
static void free_OS_entity(OS_WINDOWS_Entity* entity);

OS_SystemInfo* OS_get_system_info() {
    return &g_OS_WindowsState.systemInfo;
}

StringU8 OS_get_executable_directory(Arena* arena) {
    if (!arena) {
        return STR8_NIL;
    }

    Temp scratch = get_scratch(&arena, 1u);
    DEFER_REF(temp_end(&scratch));

    DWORD capacity = MAX_PATH;
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

    U64 length = C_STR_LEN(pathBuffer);
    while (length > 0u && pathBuffer[length - 1u] != '/' && pathBuffer[length - 1u] != '\\') {
        length -= 1u;
    }
    if (length > 0u) {
        length -= 1u;
    }
    return str8_cpy(arena, str8((U8*)pathBuffer, length));
}

void OS_set_environment_variable(StringU8 name, StringU8 value) {
    if (!name.data || name.size == 0u || !value.data) {
        return;
    }

    Temp scratch = get_scratch(0, 0u);
    DEFER_REF(temp_end(&scratch));

    char* nameBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, name.size + 1u);
    char* valueBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, value.size + 1u);
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

    Temp scratch = get_scratch(&arena, 1u);
    DEFER_REF(temp_end(&scratch));

    char* nameBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, name.size + 1u);
    MEMCPY(nameBuffer, name.data, name.size);
    nameBuffer[name.size] = 0;

    DWORD needed = GetEnvironmentVariableA(nameBuffer, 0, 0);
    if (needed == 0u) {
        return STR8_NIL;
    }

    char* valueBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, needed);
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
    *outLibrary = {};
    if (!path.data || path.size == 0u) {
        return 0;
    }

    Temp scratch = get_scratch(0, 0u);
    DEFER_REF(temp_end(&scratch));

    char* pathBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, path.size + 1u);
    MEMCPY(pathBuffer, path.data, path.size);
    pathBuffer[path.size] = 0;

    HMODULE handle = LoadLibraryA(pathBuffer);
    outLibrary->handle = (void*)handle;
    return handle ? 1 : 0;
}

void OS_library_close(OS_SharedLibrary library) {
    if (library.handle) {
        FreeLibrary((HMODULE)library.handle);
    }
}

void* OS_library_load_symbol(OS_SharedLibrary library, StringU8 symbolName) {
    if (!library.handle || !symbolName.data || symbolName.size == 0u) {
        return 0;
    }

    Temp scratch = get_scratch(0, 0u);
    DEFER_REF(temp_end(&scratch));

    char* symbolBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, symbolName.size + 1u);
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
    DEFER_REF(temp_end(&scratch));

    char* commandBuffer = ARENA_PUSH_ARRAY(scratch.arena, char, command.size + 1u);
    MEMCPY(commandBuffer, command.data, command.size);
    commandBuffer[command.size] = 0;
    return (S32)system(commandBuffer);
}

U64 OS_get_time_microseconds() {
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    return ((U64)counter.QuadPart * 1000000ull) / (U64)g_OS_WindowsState.counterFrequency.QuadPart;
}

U64 OS_get_time_nanoseconds() {
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    return ((U64)counter.QuadPart * 1000000000ull) / (U64)g_OS_WindowsState.counterFrequency.QuadPart;
}

U64 OS_get_counter_frequency_hz() {
    return (U64)g_OS_WindowsState.counterFrequency.QuadPart;
}

U64 OS_rdtsc_relaxed() {
    return __rdtsc();
}

U64 OS_rdtscp_serialized() {
    unsigned int aux = 0;
    return __rdtscp(&aux);
}

void OS_sleep_milliseconds(U32 milliseconds) {
    Sleep(milliseconds);
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
    entity->thread.func(entity->thread.args);
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
    DWORD waitResult = WaitForSingleObject(entity->thread.handle, INFINITE);
    CloseHandle(entity->thread.handle);
    free_OS_entity(entity);
    return waitResult == WAIT_OBJECT_0 ? 1 : 0;
}

void OS_thread_detach(OS_Handle thread) {
    if (!thread.handle) {
        return;
    }

    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)thread.handle;
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
    if (entity) {
        entity->type = OS_WINDOWS_EntityType_Mutex;
        InitializeCriticalSection(&entity->mutex);
        result.handle = (U64*)entity;
    }
    return result;
}

void OS_mutex_destroy(OS_Handle mutex) {
    if (!mutex.handle) {
        return;
    }

    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)mutex.handle;
    DeleteCriticalSection(&entity->mutex);
    free_OS_entity(entity);
}

void OS_mutex_lock(OS_Handle mutex) {
    if (mutex.handle) {
        OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)mutex.handle;
        EnterCriticalSection(&entity->mutex);
    }
}

void OS_mutex_unlock(OS_Handle mutex) {
    if (mutex.handle) {
        OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)mutex.handle;
        LeaveCriticalSection(&entity->mutex);
    }
}

OS_Handle OS_condition_variable_create() {
    OS_Handle result = {};
    OS_WINDOWS_Entity* entity = alloc_OS_entity();
    if (entity) {
        entity->type = OS_WINDOWS_EntityType_ConditionVariable;
        InitializeConditionVariable(&entity->conditionVariable);
        result.handle = (U64*)entity;
    }
    return result;
}

void OS_condition_variable_destroy(OS_Handle conditionVariable) {
    if (conditionVariable.handle) {
        free_OS_entity((OS_WINDOWS_Entity*)conditionVariable.handle);
    }
}

void OS_condition_variable_wait(OS_Handle conditionVariable, OS_Handle mutex) {
    if (conditionVariable.handle && mutex.handle) {
        OS_WINDOWS_Entity* conditionEntity = (OS_WINDOWS_Entity*)conditionVariable.handle;
        OS_WINDOWS_Entity* mutexEntity = (OS_WINDOWS_Entity*)mutex.handle;
        SleepConditionVariableCS(&conditionEntity->conditionVariable, &mutexEntity->mutex, INFINITE);
    }
}

void OS_condition_variable_signal(OS_Handle conditionVariable) {
    if (conditionVariable.handle) {
        OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)conditionVariable.handle;
        WakeConditionVariable(&entity->conditionVariable);
    }
}

void OS_condition_variable_broadcast(OS_Handle conditionVariable) {
    if (conditionVariable.handle) {
        OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)conditionVariable.handle;
        WakeAllConditionVariable(&entity->conditionVariable);
    }
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
        OS_barrier_destroy({(U64*)entity});
        return result;
    }

    result.handle = (U64*)entity;
    return result;
}

void OS_barrier_destroy(OS_Handle barrier) {
    if (!barrier.handle) {
        return;
    }

    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)barrier.handle;
    OS_condition_variable_destroy(entity->barrier.conditionHandle);
    OS_mutex_destroy(entity->barrier.mutexHandle);
    free_OS_entity(entity);
}

void OS_barrier_wait(OS_Handle barrier) {
    if (!barrier.handle) {
        return;
    }

    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)barrier.handle;
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

void OS_file_close(OS_Handle h) {
    if (!h.handle) {
        return;
    }

    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)h.handle;
    if (entity->file.handle && entity->file.handle != INVALID_HANDLE_VALUE) {
        CloseHandle(entity->file.handle);
    }
    free_OS_entity(entity);
}

U64 OS_file_size(OS_Handle h) {
    if (!h.handle) {
        return 0u;
    }

    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)h.handle;
    LARGE_INTEGER size = {};
    return GetFileSizeEx(entity->file.handle, &size) ? (U64)size.QuadPart : 0u;
}

OS_FileMapping OS_file_map_ro(OS_Handle h) {
    OS_FileMapping mapping = {};
    if (!h.handle) {
        return mapping;
    }

    U64 size = OS_file_size(h);
    if (size == 0u) {
        return mapping;
    }

    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)h.handle;
    HANDLE mappingHandle = CreateFileMappingA(entity->file.handle, 0, PAGE_READONLY, 0, 0, 0);
    if (!mappingHandle) {
        return mapping;
    }

    void* ptr = MapViewOfFile(mappingHandle, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mappingHandle);
    mapping.ptr = ptr;
    mapping.length = ptr ? size : 0u;
    return mapping;
}

void OS_file_unmap(OS_FileMapping m) {
    if (m.ptr) {
        UnmapViewOfFile(m.ptr);
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

U64 OS_file_read(OS_Handle h, RangeU64 range, void* dst) {
    if (!h.handle || !dst) {
        return 0u;
    }

    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)h.handle;
    LARGE_INTEGER distance = {};
    distance.QuadPart = (LONGLONG)range.min;
    if (!SetFilePointerEx(entity->file.handle, distance, 0, FILE_BEGIN)) {
        return 0u;
    }
    return OS_file_read_write_chunks(entity->file.handle, dst, range.max - range.min, 0);
}

U64 OS_file_read(OS_Handle h, U64 size, void* dst) {
    if (!h.handle || !dst) {
        return 0u;
    }

    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)h.handle;
    return OS_file_read_write_chunks(entity->file.handle, dst, size, 0);
}

U64 OS_file_write(OS_Handle h, RangeU64 range, const void* src) {
    if (!h.handle || !src) {
        return 0u;
    }

    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)h.handle;
    LARGE_INTEGER distance = {};
    distance.QuadPart = (LONGLONG)range.min;
    if (!SetFilePointerEx(entity->file.handle, distance, 0, FILE_BEGIN)) {
        return 0u;
    }
    return OS_file_read_write_chunks(entity->file.handle, (void*)src, range.max - range.min, 1);
}

U64 OS_file_write(OS_Handle h, U64 size, const void* src) {
    if (!h.handle || !src) {
        return 0u;
    }

    OS_WINDOWS_Entity* entity = (OS_WINDOWS_Entity*)h.handle;
    return OS_file_read_write_chunks(entity->file.handle, (void*)src, size, 1);
}

static U64 OS_windows_file_time_to_ns(FILETIME time) {
    ULARGE_INTEGER value = {};
    value.HighPart = time.dwHighDateTime;
    value.LowPart = time.dwLowDateTime;
    const U64 unixEpoch100ns = 116444736000000000ull;
    if (value.QuadPart < unixEpoch100ns) {
        return 0u;
    }
    return (value.QuadPart - unixEpoch100ns) * 100ull;
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
    info.exists = 1;
    info.size = size.QuadPart;
    info.lastWriteTimestampNs = OS_windows_file_time_to_ns(data.ftLastWriteTime);
    return info;
}

B32 OS_file_copy_contents(const char* srcPath, const char* dstPath) {
    if (!srcPath || !dstPath) {
        return 0;
    }
    return CopyFileA(srcPath, dstPath, FALSE) ? 1 : 0;
}

OS_Handle OS_get_log_handle() {
    g_OS_WindowsState.stdoutEntity.type = OS_WINDOWS_EntityType_File;
    g_OS_WindowsState.stdoutEntity.file.handle = GetStdHandle(STD_OUTPUT_HANDLE);
    OS_Handle result = {(U64*)&g_OS_WindowsState.stdoutEntity};
    return result;
}

B32 OS_terminal_supports_color() {
    DWORD mode = 0u;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    return (output && GetConsoleMode(output, &mode)) ? 1 : 0;
}

void OS_file_set_hints(OS_Handle h, U64 hints) {
    (void)h;
    (void)hints;
}

static void OS_dir_iterate_impl(const char* dirPath, OS_DirIterCallback* callback, void* userData, B32 recursive) {
    if (!dirPath || !callback) {
        return;
    }

    char searchPath[MAX_PATH];
    U64 dirLen = C_STR_LEN(dirPath);
    if (dirLen + 3u > MAX_PATH) {
        return;
    }

    MEMCPY(searchPath, dirPath, dirLen);
    if (dirLen > 0u && searchPath[dirLen - 1u] != '/' && searchPath[dirLen - 1u] != '\\') {
        searchPath[dirLen++] = '\\';
    }
    searchPath[dirLen++] = '*';
    searchPath[dirLen] = 0;

    WIN32_FIND_DATAA findData = {};
    HANDLE findHandle = FindFirstFileA(searchPath, &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        const char* name = findData.cFileName;
        if ((name[0] == '.' && name[1] == 0) ||
            (name[0] == '.' && name[1] == '.' && name[2] == 0)) {
            continue;
        }

        U64 nameLen = C_STR_LEN(name);
        char pathBuffer[MAX_PATH];
        U64 pathDirLen = dirLen - 1u;
        if (pathDirLen + nameLen >= MAX_PATH) {
            continue;
        }

        MEMCPY(pathBuffer, searchPath, pathDirLen);
        if (pathDirLen > 0u && pathBuffer[pathDirLen - 1u] == '\\') {
            pathBuffer[pathDirLen - 1u] = '/';
        }
        MEMCPY(pathBuffer + pathDirLen, name, nameLen);
        pathBuffer[pathDirLen + nameLen] = 0;

        B32 isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        callback(pathBuffer, isDirectory, userData);

        if (recursive && isDirectory) {
            OS_dir_iterate_impl(pathBuffer, callback, userData, recursive);
        }
    } while (FindNextFileA(findHandle, &findData));

    FindClose(findHandle);
}

void OS_dir_iterate(const char* dirPath, OS_DirIterCallback* callback, void* userData, B32 recursive) {
    OS_dir_iterate_impl(dirPath, callback, userData, recursive);
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

    entity = ARENA_PUSH_STRUCT(g_OS_WindowsState.osEntityArena, OS_WINDOWS_Entity);
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

static void OS_init() {
    SYSTEM_INFO systemInfo = {};
    GetSystemInfo(&systemInfo);
    g_OS_WindowsState.systemInfo.logicalCores = systemInfo.dwNumberOfProcessors;
    g_OS_WindowsState.systemInfo.pageSize = systemInfo.dwPageSize;
    QueryPerformanceFrequency(&g_OS_WindowsState.counterFrequency);

    g_OS_WindowsState.arena = arena_alloc();
    g_OS_WindowsState.osEntityArena = arena_alloc();
    InitializeCriticalSection(&g_OS_WindowsState.entityMutex);
    g_OS_WindowsState.stdoutEntity.type = OS_WINDOWS_EntityType_File;
    g_OS_WindowsState.stdoutEntity.file.handle = GetStdHandle(STD_OUTPUT_HANDLE);
    g_OS_WindowsState.stderrEntity.type = OS_WINDOWS_EntityType_File;
    g_OS_WindowsState.stderrEntity.file.handle = GetStdHandle(STD_ERROR_HANDLE);
}
