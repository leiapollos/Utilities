//
// Created by André Leite on 26/07/2025.
//

// ////////////////////////
// System Info

static OS_SystemInfo* OS_get_system_info() {
    return &os_macos_state.system_info;
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

int main(int argc, char** argv) { {
        OS_SystemInfo* info = &os_macos_state.system_info;
        info->pageSize = static_cast<U64>(sysconf(_SC_PAGESIZE));
        info->logicalCores = static_cast<U32>(sysconf(_SC_NPROCESSORS_ONLN));
    }

    base_entry_point(argc, argv);
}
