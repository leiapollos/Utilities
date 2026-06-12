/*
 * sob.h - Single-header build system
 *
 * Inspired by nob.h by Alexey Kutepov (Tsoding)
 * https://github.com/tsoding/nob.h
 *
 * USAGE:
 *   In ONE .c file, before including:
 *     #define SOB_IMPLEMENTATION
 *     #include "sob.h"
 *
 *   In all other files, just:
 *     #include "sob.h"
 *
 * OPTIONS:
 *   #define SOB_STATIC      - Make all functions static
 *   #define SOB_ASSERT(x)   - Custom assert (default: C assert)
 *
 * LICENSE:
 *   ------------------------------------------------------------------------------
 *   This software is available under 2 licenses -- choose whichever you prefer.
 *   ------------------------------------------------------------------------------
 *   ALTERNATIVE A - MIT License
 *   Copyright (c) 2026 Andre Leite
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of
 *   this software and associated documentation files (the "Software"), to deal in
 *   the Software without restriction, including without limitation the rights to
 *   use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *   of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions:
 *   The above copyright notice and this permission notice shall be included in all
 *   copies or substantial portions of the Software.
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *   SOFTWARE.
 *   ------------------------------------------------------------------------------
 *   ALTERNATIVE B - Public Domain (www.unlicense.org)
 *   This is free and unencumbered software released into the public domain.
 *   Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
 *   software, either in source code form or as a compiled binary, for any purpose,
 *   commercial or non-commercial, and by any means.
 *   In jurisdictions that recognize copyright laws, the author or authors of this
 *   software dedicate any and all copyright interest in the software to the public
 *   domain. We make this dedication for the benefit of the public at large and to
 *   the detriment of our heirs and successors. We intend this dedication to be an
 *   overt act of relinquishment in perpetuity of all present and future rights to
 *   this software under copyright law.
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *   ------------------------------------------------------------------------------
 */

#ifndef SOB_H
#define SOB_H

#include <stdint.h>

/* ========================================================================= */
/*                              CONFIGURATION                                */
/* ========================================================================= */

#ifdef SOB_STATIC
#define SOBDEF static
#else
#define SOBDEF extern
#endif

#ifndef SOB_ASSERT
#include <assert.h>
#define SOB_ASSERT(x) assert(x)
#endif

/* ========================================================================= */
/*                              SIZED TYPES                                  */
/* ========================================================================= */

typedef int8_t   S8;
typedef int16_t  S16;
typedef int32_t  S32;
typedef int64_t  S64;
typedef uint8_t  U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;

/* ========================================================================= */
/*                              PLATFORM DETECTION                           */
/* ========================================================================= */

#if defined(_WIN32)
#define SOB_WINDOWS 1
#elif defined(__APPLE__)
#define SOB_MACOS 1
#elif defined(__linux__)
#define SOB_LINUX 1
#elif defined(__FreeBSD__)
#define SOB_FREEBSD 1
#else
#define SOB_UNKNOWN_OS 1
#endif

/* ========================================================================= */
/*                              FORWARD DECLARATIONS                         */
/* ========================================================================= */

typedef struct Sob_Arena Sob_Arena;
typedef struct Sob_BuildContext Sob_BuildContext;
typedef struct Sob_Target Sob_Target;
typedef struct Sob_Cmd Sob_Cmd;
typedef struct Sob_Procs Sob_Procs;
typedef struct Sob_Proc Sob_Proc;

/* ========================================================================= */
/*                              ARENA ALLOCATOR                              */
/* ========================================================================= */

/*
 * sob_arena_create - Create a new memory arena
 *
 * Returns: New arena, or NULL on failure.
 *
 * All allocations from the arena are freed together when the arena is
 * destroyed. This is much faster than individual malloc/free calls.
 */
SOBDEF Sob_Arena* sob_arena_create(void);

/*
 * sob_arena_destroy - Destroy an arena and free all its memory
 *
 * @arena: Arena to destroy
 */
SOBDEF void sob_arena_destroy(Sob_Arena* arena);

/*
 * sob_arena_alloc - Allocate memory from an arena
 *
 * @arena: Arena to allocate from
 * @size:  Number of bytes to allocate
 *
 * Returns: Pointer to allocated memory, or NULL on failure.
 */
SOBDEF void* sob_arena_alloc(Sob_Arena* arena, U64 size);

/* ========================================================================= */
/*                              BUILD CONTEXT                                */
/* ========================================================================= */

/*
 * sob_build_create - Create a new build context
 *
 * @arena: Arena for allocations (must outlive the context)
 *
 * Returns: New build context, or NULL on failure.
 */
SOBDEF Sob_BuildContext* sob_build_create(Sob_Arena* arena);

/*
 * sob_build_destroy - Destroy a build context
 *
 * @ctx: Context to destroy
 */
SOBDEF void sob_build_destroy(Sob_BuildContext* ctx);

/*
 * sob_build_run - Execute the build
 *
 * @ctx: Build context with configured targets
 *
 * Returns: 0 on success, non-zero on failure.
 */
SOBDEF S32 sob_build_run(Sob_BuildContext* ctx);

/*
 * sob_build_error_count - Get the number of build errors
 *
 * @ctx: Build context
 *
 * Returns: Number of errors encountered during build.
 */
SOBDEF S32 sob_build_error_count(Sob_BuildContext* ctx);

/* ========================================================================= */
/*                              COMPILER CONFIGURATION                       */
/* ========================================================================= */

typedef enum Sob_CompilerKind {
    Sob_CompilerKind_Auto,       /* Full auto-detection */
    Sob_CompilerKind_Clang,
    Sob_CompilerKind_GCC,
    Sob_CompilerKind_MSVC,
} Sob_CompilerKind;

typedef enum Sob_OptLevel {
    Sob_OptLevel_Debug,          /* -O0 + debug symbols */
    Sob_OptLevel_Release,        /* -O2 */
    Sob_OptLevel_ReleaseFast,    /* -O3 */
    Sob_OptLevel_ReleaseSmall,   /* -Os */
} Sob_OptLevel;

typedef enum Sob_Sanitizer {
    Sob_Sanitizer_None    = 0,
    Sob_Sanitizer_Address = (1 << 0),
    Sob_Sanitizer_Thread  = (1 << 1),
    Sob_Sanitizer_UB      = (1 << 2),
} Sob_Sanitizer;

typedef struct Sob_CompilerConfig {
    Sob_CompilerKind kind;
    Sob_OptLevel     optLevel;
    U32              sanitizers;       /* Sob_Sanitizer flags OR'd */
    S32              warningsAsErrors;
    S32              enableLTO;
    const char*      sysroot;          /* Optional cross-compilation */
    const char*      targetTriple;     /* e.g., "arm64-apple-macos14" */
} Sob_CompilerConfig;

SOBDEF void sob_build_set_compiler(Sob_BuildContext* ctx, Sob_CompilerConfig config);
SOBDEF Sob_CompilerKind sob_detect_compiler(void);

/* ========================================================================= */
/*                              TARGETS                                      */
/* ========================================================================= */

typedef enum Sob_TargetKind {
    Sob_TargetKind_Executable,
    Sob_TargetKind_StaticLib,
    Sob_TargetKind_DynamicLib,
    Sob_TargetKind_ObjectFiles,
} Sob_TargetKind;

typedef struct Sob_TargetOpts {
    const char* outputDir;    /* NULL = "build/" */
    const char* outputName;   /* NULL = target name */
} Sob_TargetOpts;

SOBDEF Sob_Target* sob_target_create_(Sob_BuildContext* ctx, const char* name, 
                                       Sob_TargetKind kind, Sob_TargetOpts opts);
#define sob_target_create(ctx, name, kind, ...) \
    sob_target_create_((ctx), (name), (kind), (Sob_TargetOpts){ __VA_ARGS__ })

/* Source files */
typedef struct Sob_SourceOpts {
    const char** paths;       /* Array of paths */
    S32          count;       /* Array count (0 if single path) */
    const char*  glob;        /* Glob pattern like "src/[star].c" */
} Sob_SourceOpts;

SOBDEF void sob_target_add_source_(Sob_Target* t, const char* path, Sob_SourceOpts opts);
#define sob_target_add_source(t, path, ...) \
    sob_target_add_source_((t), (path), (Sob_SourceOpts){ __VA_ARGS__ })

/* Include paths */
SOBDEF void sob_target_add_include_(Sob_Target* t, const char* path, Sob_SourceOpts opts);
#define sob_target_add_include(t, path, ...) \
    sob_target_add_include_((t), (path), (Sob_SourceOpts){ __VA_ARGS__ })

/* Preprocessor defines */
typedef struct Sob_DefineOpts {
    const char* value;        /* NULL = no value */
} Sob_DefineOpts;

SOBDEF void sob_target_define_(Sob_Target* t, const char* name, Sob_DefineOpts opts);
#define sob_target_define(t, name, ...) \
    sob_target_define_((t), (name), (Sob_DefineOpts){ __VA_ARGS__ })

/* Compiler/linker flags */
SOBDEF void sob_target_add_cflags(Sob_Target* t, const char* flags);
SOBDEF void sob_target_add_ldflags(Sob_Target* t, const char* flags);

/* Language standard (platform-independent) */
typedef enum Sob_Standard {
    Sob_Standard_Default = 0,
    Sob_Standard_C89,
    Sob_Standard_C99,
    Sob_Standard_C11,
    Sob_Standard_C17,
    Sob_Standard_C23,
    Sob_Standard_Cpp11,
    Sob_Standard_Cpp14,
    Sob_Standard_Cpp17,
    Sob_Standard_Cpp20,
    Sob_Standard_Cpp23,
} Sob_Standard;

SOBDEF void sob_target_set_standard(Sob_Target* t, Sob_Standard std);

/* Dependencies */
SOBDEF void sob_target_depends_on(Sob_Target* t, Sob_Target* dependency);

/* Library linking */
typedef enum Sob_LibKind {
    Sob_LibKind_System,       /* -lname */
    Sob_LibKind_Framework,    /* -framework name (macOS) */
    Sob_LibKind_Static,       /* Full path to .a */
    Sob_LibKind_Dynamic,      /* Full path to .so/.dylib/.dll */
} Sob_LibKind;

typedef struct Sob_LinkOpts {
    Sob_LibKind kind;         /* Default: System */
    const char* searchPath;   /* Add lib search path */
    S32 useRpath;             /* For dynamic libs: auto-add rpath for runtime lookup */
} Sob_LinkOpts;

SOBDEF void sob_target_link_(Sob_Target* t, const char* name, Sob_LinkOpts opts);
#define sob_target_link(t, name, ...) \
    sob_target_link_((t), (name), (Sob_LinkOpts){ __VA_ARGS__ })

/* Link a target's output (handles static/dynamic automatically) */
SOBDEF void sob_target_link_target(Sob_Target* t, Sob_Target* libTarget);

SOBDEF void sob_target_link_platform_libs(Sob_Target* t);
SOBDEF const char* sob_target_get_output_path(Sob_Target* t);

/* ========================================================================= */
/*                              COMMAND EXECUTION                            */
/* ========================================================================= */

SOBDEF Sob_Cmd* sob_cmd_create(Sob_Arena* arena);
SOBDEF void sob_cmd_append(Sob_Cmd* cmd, const char* arg);

typedef struct Sob_CmdOpts {
    Sob_Procs*  async;        /* NULL = sync, else add to procs */
    S32         maxProcs;     /* 0 = auto CPU count */
    const char* cwd;          /* NULL = inherit */
    const char* stdoutPath;   /* NULL = inherit */
    const char* stderrPath;   /* NULL = inherit */
} Sob_CmdOpts;

SOBDEF S32 sob_cmd_run_(Sob_Cmd* cmd, Sob_CmdOpts opts);
#define sob_cmd_run(cmd, ...) sob_cmd_run_((cmd), (Sob_CmdOpts){ __VA_ARGS__ })

SOBDEF const char* sob_cmd_to_string(Sob_Cmd* cmd, Sob_Arena* arena);

/* ========================================================================= */
/*                              ASYNC PROCESSES                              */
/* ========================================================================= */

typedef void (*Sob_ProcCallback)(void* userData, Sob_Cmd* cmd, S32 exitCode);

typedef struct Sob_ProcsOpts {
    S32              maxProcs;  /* 0 = auto CPU count */
    Sob_ProcCallback callback;
    void*            userData;
} Sob_ProcsOpts;

SOBDEF Sob_Procs* sob_procs_create_(Sob_Arena* arena, Sob_ProcsOpts opts);
#define sob_procs_create(arena, ...) \
    sob_procs_create_((arena), (Sob_ProcsOpts){ __VA_ARGS__ })

SOBDEF S32 sob_procs_count(Sob_Procs* procs);
SOBDEF S32 sob_procs_wait(Sob_Procs* procs);

/* Raw process handle */
SOBDEF Sob_Proc* sob_cmd_spawn(Sob_Cmd* cmd);
SOBDEF S32 sob_proc_poll(Sob_Proc* p);    /* -1 = running, else exit code */
SOBDEF S32 sob_proc_wait(Sob_Proc* p);
SOBDEF void sob_proc_kill(Sob_Proc* p);

/* ========================================================================= */
/*                              FILE SYSTEM                                  */
/* ========================================================================= */

SOBDEF S32 sob_fs_exists(const char* path);
SOBDEF S32 sob_fs_is_dir(const char* path);
SOBDEF S32 sob_fs_mkdir(const char* path);
SOBDEF S32 sob_fs_mkdir_p(const char* path);  /* Create parents */
SOBDEF S32 sob_fs_remove(const char* path);
SOBDEF S32 sob_fs_remove_tree(const char* path);
SOBDEF S32 sob_fs_copy(const char* src, const char* dst);
SOBDEF S32 sob_fs_write_text(const char* path, const char* text);
SOBDEF U64 sob_fs_mtime(const char* path);
SOBDEF U64 sob_fs_newest_mtime(const char* const* paths, S32 count);  /* missing files contribute 0 */

SOBDEF const char* sob_path_join(Sob_Arena* arena, const char* a, const char* b);
SOBDEF const char* sob_path_dirname(Sob_Arena* arena, const char* path);
SOBDEF const char* sob_path_basename(const char* path);
SOBDEF const char* sob_path_ext(const char* path);

/* ========================================================================= */
/*                              ASSET EMBEDDING                              */
/* ========================================================================= */

typedef struct Sob_EmbedOpts {
    S32         compress;       /* 0 = raw, 1 = reserved for compression */
    S32         nullTerminate;  /* Add \0 at end */
    const char* alignment;      /* e.g., "16" */
    const char* extension;      /* Filter: NULL = all, ".glsl" = only .glsl */
    S32         manifest;       /* Generate lookup table */
    const char* manifestName;   /* e.g., "ASSETS" */
} Sob_EmbedOpts;

SOBDEF void sob_embed_(Sob_Arena* arena, const char* input, const char* varName, 
                        const char* output, Sob_EmbedOpts opts);
#define sob_embed(arena, input, varName, output, ...) \
    sob_embed_((arena), (input), (varName), (output), (Sob_EmbedOpts){ __VA_ARGS__ })

/* ========================================================================= */
/*                              CODE GENERATION                              */
/* ========================================================================= */

typedef void (*Sob_CodeGenFunc)(Sob_Arena* arena, void* userData);

typedef struct Sob_CodeGenOpts {
    const char** inputs;      /* Dependencies to check for changes */
    S32          inputCount;
    void*        userData;
} Sob_CodeGenOpts;

SOBDEF void sob_target_add_codegen_(Sob_Target* t, const char* output, 
                                     Sob_CodeGenFunc func, Sob_CodeGenOpts opts);
#define sob_target_add_codegen(t, output, func, ...) \
    sob_target_add_codegen_((t), (output), (func), (Sob_CodeGenOpts){ __VA_ARGS__ })

/* ========================================================================= */
/*                              LOGGING                                      */
/* ========================================================================= */

typedef enum Sob_LogLevel {
    Sob_LogLevel_Trace,
    Sob_LogLevel_Info,
    Sob_LogLevel_Warning,
    Sob_LogLevel_Error,
    Sob_LogLevel_Silent,
} Sob_LogLevel;

SOBDEF void sob_build_set_log_level(Sob_BuildContext* ctx, Sob_LogLevel level);
SOBDEF void sob_log(Sob_BuildContext* ctx, Sob_LogLevel level, const char* fmt, ...);

/* ========================================================================= */
/*                              UTILITIES                                    */
/* ========================================================================= */

SOBDEF S32 sob_nprocs(void);  /* Get CPU count */

#if SOB_WINDOWS
SOBDEF S32 sob_command_exists(const char* name);  /* findable on PATH */
#endif

/*
 * sob_bootstrap - toolchain environment + self-rebuilding build script
 *
 * Call first in main(). On Windows, when no MSVC environment is active,
 * re-launches the script under VsDevCmd and returns its exit code. Then,
 * when any watched input is newer than the running binary, recompiles
 * inputs[0], re-runs it with the same arguments, and replaces the running
 * binary (on Windows via a batch scheduled after the child exits, since a
 * running exe cannot be overwritten). scratchDir holds rebuilt binaries
 * and launch batches. Returns 1 when the caller must exit with
 * *outExitCode; set SOB_SKIP_SELF_REBUILD to suppress.
 */
SOBDEF S32 sob_bootstrap(int argc, char** argv,
                         const char* const* inputs, S32 inputCount,
                         const char* scratchDir, S32* outExitCode);

#endif /* SOB_H */

/* ========================================================================= */
/*                             IMPLEMENTATION                                */
#ifdef SOB_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#if SOB_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>  /* For _spawnvp */
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#endif

/* ========================================================================= */
/*                              VIRTUAL MEMORY HELPERS                       */
/* ========================================================================= */

#define SOB_ARENA_RESERVE_SIZE (1ULL << 30)  /* 1GB reserve */
#define SOB_ARENA_COMMIT_SIZE  (64 * 1024)   /* 64KB commit granularity */

static void* sob__vm_reserve(U64 size) {
#if SOB_WINDOWS
    return VirtualAlloc(0, (SIZE_T)size, MEM_RESERVE, PAGE_NOACCESS);
#else
    void* ptr = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (ptr == MAP_FAILED) ? 0 : ptr;
#endif
}

static S32 sob__vm_commit(void* ptr, U64 size) {
#if SOB_WINDOWS
    return VirtualAlloc(ptr, (SIZE_T)size, MEM_COMMIT, PAGE_READWRITE) != 0;
#else
    return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
#endif
}

static void sob__vm_release(void* ptr, U64 size) {
#if SOB_WINDOWS
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

/* ========================================================================= */
/*                              ARENA IMPLEMENTATION                         */
/* ========================================================================= */

struct Sob_Arena {
    U8*  base;       /* Reserved memory base */
    U64  reserved;   /* Total reserved size */
    U64  committed;  /* Currently committed size */
    U64  used;       /* Currently used size */
};

SOBDEF Sob_Arena* sob_arena_create(void) {
    void* base = sob__vm_reserve(SOB_ARENA_RESERVE_SIZE);
    if (!base) {
        return 0;
    }
    
    if (!sob__vm_commit(base, SOB_ARENA_COMMIT_SIZE)) {
        sob__vm_release(base, SOB_ARENA_RESERVE_SIZE);
        return 0;
    }
    
    Sob_Arena* arena = (Sob_Arena*)base;
    arena->base = (U8*)base;
    arena->reserved = SOB_ARENA_RESERVE_SIZE;
    arena->committed = SOB_ARENA_COMMIT_SIZE;
    arena->used = sizeof(Sob_Arena);
    
    return arena;
}

SOBDEF void sob_arena_destroy(Sob_Arena* arena) {
    if (!arena) {
        return;
    }
    sob__vm_release(arena->base, arena->reserved);
}

SOBDEF void* sob_arena_alloc(Sob_Arena* arena, U64 size) {
    if (!arena) {
        return 0;
    }

    if (size > ~7ULL) {
        return 0;
    }
    
    size = (size + 7) & ~7ULL;

    if (arena->used > arena->reserved || size > arena->reserved - arena->used) {
        return 0;
    }
    
    U64 newUsed = arena->used + size;
    
    if (newUsed > arena->committed) {
        U64 needed = newUsed - arena->committed;
        U64 toCommit = ((needed + SOB_ARENA_COMMIT_SIZE - 1) / SOB_ARENA_COMMIT_SIZE) * SOB_ARENA_COMMIT_SIZE;
        
        if (arena->committed + toCommit > arena->reserved) {
            return 0;
        }
        
        if (!sob__vm_commit(arena->base + arena->committed, toCommit)) {
            return 0;
        }
        arena->committed += toCommit;
    }
    
    void* ptr = arena->base + arena->used;
    arena->used = newUsed;
    return ptr;
}

/* ========================================================================= */
/*                              UTILITIES                                    */
/* ========================================================================= */

SOBDEF S32 sob_nprocs(void) {
#if SOB_WINDOWS
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (S32)info.dwNumberOfProcessors;
#else
    return (S32)sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

/* ========================================================================= */
/*                              FILE SYSTEM                                  */
/* ========================================================================= */

SOBDEF S32 sob_fs_exists(const char* path) {
#if SOB_WINDOWS
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

SOBDEF S32 sob_fs_is_dir(const char* path) {
#if SOB_WINDOWS
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
#endif
}

SOBDEF S32 sob_fs_mkdir(const char* path) {
#if SOB_WINDOWS
    return CreateDirectoryA(path, NULL) ? 0 : -1;
#else
    return mkdir(path, 0755);
#endif
}

SOBDEF U64 sob_fs_mtime(const char* path) {
#if SOB_WINDOWS
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
        return 0;
    }
    ULARGE_INTEGER t;
    t.LowPart = data.ftLastWriteTime.dwLowDateTime;
    t.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return t.QuadPart;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return (U64)st.st_mtime;
#endif
}

/* ========================================================================= */
/*                              STRING HELPERS                               */
/* ========================================================================= */

static U64 sob__strlen(const char* s) {
    U64 len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

static void sob__memcpy(void* dst, const void* src, U64 size) {
    U8* d = (U8*)dst;
    const U8* s = (const U8*)src;
    while (size--) {
        *d++ = *s++;
    }
}

static void sob__memset(void* dst, S32 val, U64 size) {
    U8* d = (U8*)dst;
    while (size--) {
        *d++ = (U8)val;
    }
}

static void* sob__grow_array(Sob_Arena* arena, void* items, int count, int* capacity,
                             int initCap, U64 itemSize) {
    if (!arena || !capacity) {
        return 0;
    }
    if (count < *capacity) {
        return items;
    }
    int newCap = (*capacity == 0) ? initCap : (*capacity * 2);
    void* newItems = sob_arena_alloc(arena, (U64)newCap * itemSize);
    if (!newItems) {
        return 0;
    }
    if (items && count > 0) {
        sob__memcpy(newItems, items, (U64)count * itemSize);
    }
    *capacity = newCap;
    return newItems;
}

static char* sob__strdup(Sob_Arena* arena, const char* s) {
    U64 len = sob__strlen(s);
    char* copy = (char*)sob_arena_alloc(arena, len + 1);
    if (copy) {
        sob__memcpy(copy, s, len + 1);
    }
    return copy;
}

static const char* sob__concatv(Sob_Arena* arena, const char* const* parts, int partCount) {
    if (!arena || !parts || partCount <= 0) {
        return "";
    }

    U64 totalLen = 0;
    for (int i = 0; i < partCount; i++) {
        const char* part = parts[i] ? parts[i] : "";
        totalLen += sob__strlen(part);
    }

    char* out = (char*)sob_arena_alloc(arena, totalLen + 1);
    if (!out) {
        return "";
    }

    char* p = out;
    for (int i = 0; i < partCount; i++) {
        const char* part = parts[i] ? parts[i] : "";
        U64 len = sob__strlen(part);
        sob__memcpy(p, part, len);
        p += len;
    }
    *p = '\0';
    return out;
}

#define sob__concat(arena, ...) \
    sob__concatv((arena), (const char*[]){ __VA_ARGS__ }, \
        (int)(sizeof((const char*[]){ __VA_ARGS__ }) / sizeof(const char*)))

/* ========================================================================= */
/*                              DYNAMIC ARRAY                                */
/* ========================================================================= */

#define SOB_DA_INIT_CAP 8
#define SOB__VEC_ENSURE(arena, items, count, capacity, initCap) \
    (((count) < (capacity)) ? 0 : (((items) = sob__grow_array((arena), (items), (count), \
        &(capacity), (initCap), sizeof(*(items)))) ? 0 : -1))

typedef struct {
    const char** items;
    S32          count;
    S32          capacity;
    Sob_Arena*   arena;
} Sob_StringArray;

static S32 sob__da_push(Sob_StringArray* da, const char* item) {
    if (!da || !item) {
        return -1;
    }
    if (SOB__VEC_ENSURE(da->arena, da->items, da->count, da->capacity, SOB_DA_INIT_CAP) != 0) {
        return -1;
    }
    da->items[da->count++] = item;
    return 0;
}

static void sob__da_push_paths(Sob_StringArray* da, Sob_Arena* arena, const char* path,
                               const char** paths, int count) {
    if (!da || !arena) {
        return;
    }
    if (path) {
        const char* dup = sob__strdup(arena, path);
        if (dup) {
            sob__da_push(da, dup);
        }
    }
    if (paths && count > 0) {
        for (int i = 0; i < count; i++) {
            if (paths[i]) {
                const char* dup = sob__strdup(arena, paths[i]);
                if (dup) {
                    sob__da_push(da, dup);
                }
            }
        }
    }
}

/* ========================================================================= */
/*                              COMMAND IMPLEMENTATION                       */
/* ========================================================================= */

struct Sob_Cmd {
    Sob_StringArray args;
    Sob_Arena*      arena;
    Sob_BuildContext* logCtx;
    S32             hadError;
};

SOBDEF Sob_Cmd* sob_cmd_create(Sob_Arena* arena) {
    if (!arena) {
        return 0;
    }
    Sob_Cmd* cmd = (Sob_Cmd*)sob_arena_alloc(arena, sizeof(Sob_Cmd));
    if (!cmd) {
        return 0;
    }
    sob__memset(cmd, 0, sizeof(Sob_Cmd));
    cmd->arena = arena;
    cmd->args.arena = arena;
    return cmd;
}

SOBDEF void sob_cmd_append(Sob_Cmd* cmd, const char* arg) {
    if (!cmd || !arg) {
        if (cmd) {
            cmd->hadError = 1;
        }
        return;
    }
    const char* copy = sob__strdup(cmd->arena, arg);
    if (!copy || sob__da_push(&cmd->args, copy) != 0) {
        cmd->hadError = 1;
    }
}

SOBDEF const char* sob_cmd_to_string(Sob_Cmd* cmd, Sob_Arena* arena) {
    if (!cmd || cmd->args.count == 0) {
        return "";
    }
    
    U64 totalLen = 0;
    for (S32 i = 0; i < cmd->args.count; i++) {
        totalLen += sob__strlen(cmd->args.items[i]) + 1;
    }
    
    char* buf = (char*)sob_arena_alloc(arena, totalLen + 1);
    if (!buf) {
        return "";
    }
    
    char* p = buf;
    for (S32 i = 0; i < cmd->args.count; i++) {
        const char* arg = cmd->args.items[i];
        U64 len = sob__strlen(arg);
        sob__memcpy(p, arg, len);
        p += len;
        *p++ = (i < cmd->args.count - 1) ? ' ' : '\0';
    }
    
    return buf;
}

/* ========================================================================= */
/*                              PROCESS IMPLEMENTATION                       */
/* ========================================================================= */

typedef struct Sob_SpawnConfig {
    const char* cwd;
    const char* stdoutPath;
    const char* stderrPath;
} Sob_SpawnConfig;

static Sob_Proc* sob__cmd_spawn_with_opts(Sob_Cmd* cmd, const Sob_SpawnConfig* spawnCfg);

#if SOB_WINDOWS

struct Sob_Proc {
    HANDLE hProcess;
    DWORD  pid;
    S32    done;
    S32    exitCode;
    Sob_Cmd* cmd;
};

static int sob__is_windows_path_like_cmd(const char* path) {
    if (!path || !*path) {
        return 0;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return 1;
    }
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':') {
        return 1;
    }
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            return 1;
        }
    }
    return 0;
}

static const char* sob__resolve_windows_cmd_arg0(Sob_Arena* arena, const char* arg0) {
    if (!arena || !arg0 || !*arg0) {
        return arg0;
    }
    if (!sob__is_windows_path_like_cmd(arg0)) {
        return arg0;
    }
    if (sob_path_ext(arg0)[0] != '\0') {
        return arg0;
    }

    unsigned long long len = sob__strlen(arg0);
    char* withExe = (char*)sob_arena_alloc(arena, len + 5);
    if (!withExe) {
        return arg0;
    }

    sob__memcpy(withExe, arg0, len);
    withExe[len + 0] = '.';
    withExe[len + 1] = 'e';
    withExe[len + 2] = 'x';
    withExe[len + 3] = 'e';
    withExe[len + 4] = '\0';

    if (sob_fs_exists(withExe)) {
        return withExe;
    }
    return arg0;
}

static S32 sob__windows_arg_needs_quotes(const char* arg) {
    if (!arg || !*arg) {
        return 1;
    }
    for (const char* p = arg; *p; p++) {
        char c = *p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '"') {
            return 1;
        }
    }
    return 0;
}

static char* sob__windows_append_arg_escaped(char* dst, const char* arg) {
    if (!dst) {
        return 0;
    }
    if (!arg) {
        arg = "";
    }

    if (!sob__windows_arg_needs_quotes(arg)) {
        U64 len = sob__strlen(arg);
        sob__memcpy(dst, arg, len);
        return dst + len;
    }

    *dst++ = '"';
    U64 backslashes = 0;
    for (const char* p = arg; *p; p++) {
        if (*p == '\\') {
            backslashes++;
            continue;
        }
        if (*p == '"') {
            for (U64 i = 0; i < backslashes * 2 + 1; i++) {
                *dst++ = '\\';
            }
            *dst++ = '"';
            backslashes = 0;
            continue;
        }
        for (U64 i = 0; i < backslashes; i++) {
            *dst++ = '\\';
        }
        backslashes = 0;
        *dst++ = *p;
    }
    for (U64 i = 0; i < backslashes * 2; i++) {
        *dst++ = '\\';
    }
    *dst++ = '"';
    return dst;
}

static Sob_Proc* sob__cmd_spawn_with_opts(Sob_Cmd* cmd, const Sob_SpawnConfig* spawnCfg) {
    const char* cwd = spawnCfg ? spawnCfg->cwd : 0;
    const char* stdoutPath = spawnCfg ? spawnCfg->stdoutPath : 0;
    const char* stderrPath = spawnCfg ? spawnCfg->stderrPath : 0;

    if (!cmd || cmd->hadError || cmd->args.count == 0) {
        return 0;
    }

    const char* resolvedArg0 = sob__resolve_windows_cmd_arg0(cmd->arena, cmd->args.items[0]);
    
    U64 cmdLineLen = 0;
    for (S32 i = 0; i < cmd->args.count; i++) {
        const char* arg = (i == 0) ? resolvedArg0 : cmd->args.items[i];
        cmdLineLen += (sob__strlen(arg) * 2) + 3;
    }
    
    char* cmdLine = (char*)sob_arena_alloc(cmd->arena, cmdLineLen + 1);
    if (!cmdLine) {
        return 0;
    }
    
    char* p = cmdLine;
    for (S32 i = 0; i < cmd->args.count; i++) {
        const char* arg = (i == 0) ? resolvedArg0 : cmd->args.items[i];
        p = sob__windows_append_arg_escaped(p, arg);
        if (!p) {
            return 0;
        }
        if (i + 1 < cmd->args.count) {
            *p++ = ' ';
        }
    }
    *p = '\0';
    
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    sob__memset(&si, 0, sizeof(si));
    sob__memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    
    HANDLE hStdout = INVALID_HANDLE_VALUE;
    HANDLE hStderr = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa;
    sob__memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    
    if (stdoutPath) {
        hStdout = CreateFileA(stdoutPath, GENERIC_WRITE, FILE_SHARE_READ, &sa, 
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hStdout != INVALID_HANDLE_VALUE) {
            si.hStdOutput = hStdout;
        }
    }
    
    if (stderrPath) {
        hStderr = CreateFileA(stderrPath, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hStderr != INVALID_HANDLE_VALUE) {
            si.hStdError = hStderr;
        }
    }
    
    BOOL success = CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE, 0, NULL, 
                                   cwd, &si, &pi);
    
    if (hStdout != INVALID_HANDLE_VALUE) {
        CloseHandle(hStdout);
    }
    if (hStderr != INVALID_HANDLE_VALUE) {
        CloseHandle(hStderr);
    }
    
    if (!success) {
        return 0;
    }
    
    CloseHandle(pi.hThread);
    
    Sob_Proc* proc = (Sob_Proc*)sob_arena_alloc(cmd->arena, sizeof(Sob_Proc));
    if (!proc) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        return 0;
    }
    
    proc->hProcess = pi.hProcess;
    proc->pid = pi.dwProcessId;
    proc->done = 0;
    proc->exitCode = 0;
    proc->cmd = cmd;
    return proc;
}

SOBDEF S32 sob_proc_poll(Sob_Proc* p) {
    if (!p) {
        return -1;
    }
    if (p->done) {
        return p->exitCode;
    }
    DWORD exitCode;
    if (!GetExitCodeProcess(p->hProcess, &exitCode)) {
        return -1;
    }
    if (exitCode == STILL_ACTIVE) {
        return -1;
    }
    CloseHandle(p->hProcess);
    p->hProcess = NULL;
    p->done = 1;
    p->exitCode = (S32)exitCode;
    return p->exitCode;
}

SOBDEF S32 sob_proc_wait(Sob_Proc* p) {
    if (!p) {
        return -1;
    }
    if (p->done) {
        return p->exitCode;
    }
    WaitForSingleObject(p->hProcess, INFINITE);
    DWORD exitCode;
    GetExitCodeProcess(p->hProcess, &exitCode);
    CloseHandle(p->hProcess);
    p->hProcess = NULL;
    p->done = 1;
    p->exitCode = (S32)exitCode;
    return p->exitCode;
}

SOBDEF void sob_proc_kill(Sob_Proc* p) {
    if (!p || !p->hProcess) {
        return;
    }
    TerminateProcess(p->hProcess, 1);
    CloseHandle(p->hProcess);
    p->hProcess = NULL;
    p->done = 1;
    p->exitCode = -1;
}

#else /* POSIX */

struct Sob_Proc {
    pid_t pid;
    S32   done;
    S32   exitCode;
    Sob_Cmd* cmd;
};

static Sob_Proc* sob__cmd_spawn_with_opts(Sob_Cmd* cmd, const Sob_SpawnConfig* spawnCfg) {
    const char* cwd = spawnCfg ? spawnCfg->cwd : 0;
    const char* stdoutPath = spawnCfg ? spawnCfg->stdoutPath : 0;
    const char* stderrPath = spawnCfg ? spawnCfg->stderrPath : 0;

    if (!cmd || cmd->hadError || cmd->args.count == 0) {
        return 0;
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        return 0;
    }
    
    if (pid == 0) {
        if (cwd) {
            if (chdir(cwd) != 0) {
                _exit(127);
            }
        }
        
        if (stdoutPath) {
            FILE* f = fopen(stdoutPath, "w");
            if (f) {
                dup2(fileno(f), STDOUT_FILENO);
                fclose(f);
            }
        }
        
        if (stderrPath) {
            FILE* f = fopen(stderrPath, "w");
            if (f) {
                dup2(fileno(f), STDERR_FILENO);
                fclose(f);
            }
        }
        
        const char** argv = (const char**)sob_arena_alloc(cmd->arena, 
            (U64)(cmd->args.count + 1) * sizeof(const char*));
        if (!argv) {
            _exit(127);
        }
        
        for (S32 i = 0; i < cmd->args.count; i++) {
            argv[i] = cmd->args.items[i];
        }
        argv[cmd->args.count] = 0;
        
        execvp(argv[0], (char* const*)argv);
        _exit(127);
    }
    
    Sob_Proc* proc = (Sob_Proc*)sob_arena_alloc(cmd->arena, sizeof(Sob_Proc));
    if (!proc) {
        return 0;
    }
    
    proc->pid = pid;
    proc->done = 0;
    proc->exitCode = 0;
    proc->cmd = cmd;
    return proc;
}

SOBDEF S32 sob_proc_poll(Sob_Proc* p) {
    if (!p) {
        return -1;
    }
    if (p->done) {
        return p->exitCode;
    }
    
    S32 status;
    pid_t result = waitpid(p->pid, &status, WNOHANG);
    if (result == 0) {
        return -1;
    }
    if (result < 0) {
        return -1;
    }
    
    p->done = 1;
    p->exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 127;
    return p->exitCode;
}

SOBDEF S32 sob_proc_wait(Sob_Proc* p) {
    if (!p) {
        return -1;
    }
    if (p->done) {
        return p->exitCode;
    }
    
    S32 status;
    waitpid(p->pid, &status, 0);
    p->done = 1;
    p->exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 127;
    return p->exitCode;
}

SOBDEF void sob_proc_kill(Sob_Proc* p) {
    if (!p || p->done) {
        return;
    }
    kill(p->pid, SIGKILL);
    p->done = 1;
    p->exitCode = -1;
}

#endif /* SOB_WINDOWS */

SOBDEF Sob_Proc* sob_cmd_spawn(Sob_Cmd* cmd) {
    Sob_SpawnConfig cfg;
    sob__memset(&cfg, 0, sizeof(cfg));
    return sob__cmd_spawn_with_opts(cmd, &cfg);
}

/* ========================================================================= */
/*                              COMMAND RUN                                  */
/* ========================================================================= */

static void sob__procs_add(Sob_Procs* procs, Sob_Proc* proc, int maxProcs);
static void sob__log_cmd_line(Sob_Cmd* cmd) {
    if (!cmd) {
        return;
    }
    const char* cmdLine = sob_cmd_to_string(cmd, cmd->arena);
    if (cmd->logCtx) {
        sob_log(cmd->logCtx, Sob_LogLevel_Info, "[CMD] %s", cmdLine);
    } else {
        printf("[CMD] %s\n", cmdLine);
        fflush(stdout);
    }
}

SOBDEF S32 sob_cmd_run_(Sob_Cmd* cmd, Sob_CmdOpts opts) {
    if (!cmd || cmd->args.count == 0 || cmd->hadError) {
        return -1;
    }
    
    sob__log_cmd_line(cmd);

    Sob_SpawnConfig spawnCfg;
    spawnCfg.cwd = opts.cwd;
    spawnCfg.stdoutPath = opts.stdoutPath;
    spawnCfg.stderrPath = opts.stderrPath;

    Sob_Proc* proc = sob__cmd_spawn_with_opts(cmd, &spawnCfg);
    
    if (!proc) {
        return -1;
    }
    
    if (opts.async) {
        sob__procs_add(opts.async, proc, opts.maxProcs);
        return 0;
    }
    
    return sob_proc_wait(proc);
}

/* ========================================================================= */
/*                              PROCS IMPLEMENTATION                         */
/* ========================================================================= */

struct Sob_Procs {
    Sob_Proc**       items;
    int              count;
    int              capacity;
    int              maxProcs;
    Sob_ProcCallback callback;
    void*            userData;
    Sob_Arena*       arena;
};

static void sob__procs_remove_index(Sob_Procs* procs, int index) {
    if (!procs || index < 0 || index >= procs->count) {
        return;
    }
    procs->items[index] = procs->items[procs->count - 1];
    procs->count--;
}

static void sob__procs_call_callback(Sob_Procs* procs, Sob_Proc* proc, int exitCode) {
    if (procs && procs->callback) {
        procs->callback(procs->userData, proc ? proc->cmd : 0, exitCode);
    }
}

static void sob__procs_prune_finished(Sob_Procs* procs) {
    if (!procs) {
        return;
    }
    for (int i = 0; i < procs->count; ) {
        Sob_Proc* proc = procs->items[i];
        int status = sob_proc_poll(proc);
        if (status >= 0) {
            sob__procs_call_callback(procs, proc, status);
            sob__procs_remove_index(procs, i);
        } else {
            i++;
        }
    }
}

static void sob__procs_wait_one(Sob_Procs* procs) {
    if (!procs || procs->count == 0) {
        return;
    }
    Sob_Proc* proc = procs->items[0];
    int exitCode = sob_proc_wait(proc);
    sob__procs_call_callback(procs, proc, exitCode);
    sob__procs_remove_index(procs, 0);
}

static void sob__procs_add(Sob_Procs* procs, Sob_Proc* proc, int maxProcs) {
    if (!procs || !proc) { return; }
    
    if (maxProcs <= 0) {
        maxProcs = procs->maxProcs;
    }
    
    while (maxProcs > 0 && procs->count >= maxProcs) {
        sob__procs_prune_finished(procs);
        if (procs->count >= maxProcs) {
            sob__procs_wait_one(procs);
        }
    }
    
    if (SOB__VEC_ENSURE(procs->arena, procs->items, procs->count, procs->capacity, 8) != 0) {
        return;
    }
    procs->items[procs->count++] = proc;
}

SOBDEF Sob_Procs* sob_procs_create_(Sob_Arena* arena, Sob_ProcsOpts opts) {
    Sob_Procs* procs = (Sob_Procs*)sob_arena_alloc(arena, sizeof(Sob_Procs));
    if (!procs) { return 0; }
    sob__memset(procs, 0, sizeof(Sob_Procs));
    procs->arena = arena;
    procs->maxProcs = (opts.maxProcs > 0) ? opts.maxProcs : sob_nprocs();
    procs->callback = opts.callback;
    procs->userData = opts.userData;
    return procs;
}

SOBDEF int sob_procs_count(Sob_Procs* procs) {
    return procs ? procs->count : 0;
}

SOBDEF int sob_procs_wait(Sob_Procs* procs) {
    if (!procs) { return 0; }
    
    int failCount = 0;
    while (procs->count > 0) {
        Sob_Proc* proc = procs->items[0];
        int exitCode = sob_proc_wait(proc);
        if (exitCode != 0) {
            failCount++;
        }
        sob__procs_call_callback(procs, proc, exitCode);
        sob__procs_remove_index(procs, 0);
    }
    return failCount;
}

/* ========================================================================= */
/*                              PATH UTILITIES                               */
/* ========================================================================= */

SOBDEF const char* sob_path_join(Sob_Arena* arena, const char* a, const char* b) {
    if (!a || !*a) { return sob__strdup(arena, b ? b : ""); }
    if (!b || !*b) { return sob__strdup(arena, a); }
    
    unsigned long long lenA = sob__strlen(a);
    const char* sep = (a[lenA - 1] != '/' && a[lenA - 1] != '\\') ? "/" : "";
    return sob__concat(arena, a, sep, b);
}

SOBDEF const char* sob_path_basename(const char* path) {
    if (!path) { return ""; }
    const char* last = path;
    while (*path) {
        if (*path == '/' || *path == '\\') { last = path + 1; }
        path++;
    }
    return last;
}

SOBDEF const char* sob_path_dirname(Sob_Arena* arena, const char* path) {
    if (!path || !*path) { return sob__strdup(arena, "."); }
    
    unsigned long long len = sob__strlen(path);
    const char* lastSlash = 0;
    for (unsigned long long i = 0; i < len; i++) {
        if (path[i] == '/' || path[i] == '\\') { lastSlash = path + i; }
    }
    
    if (!lastSlash) { return sob__strdup(arena, "."); }
    if (lastSlash == path) { return sob__strdup(arena, "/"); }
    
    unsigned long long dirLen = (unsigned long long)(lastSlash - path);
    char* result = (char*)sob_arena_alloc(arena, dirLen + 1);
    if (!result) { return ""; }
    sob__memcpy(result, path, dirLen);
    result[dirLen] = '\0';
    return result;
}

SOBDEF const char* sob_path_ext(const char* path) {
    if (!path) { return ""; }
    const char* dot = 0;
    while (*path) {
        if (*path == '.') { dot = path; }
        if (*path == '/' || *path == '\\') { dot = 0; }
        path++;
    }
    return dot ? dot : "";
}

static int sob__is_abs_path(const char* path) {
    if (!path || !*path) { return 0; }
    if (path[0] == '/' || path[0] == '\\') { return 1; }
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':') {
        return 1;
    }
    return 0;
}

static const char* sob__strip_leading_dot_slash(const char* path) {
    if (!path) { return ""; }
    if (path[0] == '.' && (path[1] == '/' || path[1] == '\\')) {
        return path + 2;
    }
    return path;
}

static const char* sob__rpath_from_search(Sob_Arena* arena, const char* origin, const char* searchPath) {
    if (!origin || !*origin) { return sob__strdup(arena, searchPath ? searchPath : ""); }
    if (!searchPath || !*searchPath) { return sob__strdup(arena, origin); }
    if (sob__is_abs_path(searchPath)) { return sob__strdup(arena, searchPath); }

    const char* rel = sob__strip_leading_dot_slash(searchPath);
    if (!*rel) { return sob__strdup(arena, origin); }
    return sob__concat(arena, origin, "/", rel);
}

static const char* sob__flag_with_value(Sob_Arena* arena, const char* prefix, const char* value) {
    return sob__concat(arena, prefix, value);
}

static void sob__append_prefixed_flags(Sob_Cmd* cmd, Sob_Arena* arena, const char* prefix,
                                       Sob_StringArray* list) {
    if (!cmd || !arena || !prefix || !list) {
        return;
    }
    for (int i = 0; i < list->count; i++) {
        const char* flag = sob__flag_with_value(arena, prefix, list->items[i]);
        if (flag && flag[0]) {
            sob_cmd_append(cmd, flag);
        }
    }
}

static const char* sob__make_rpath_flag(Sob_Arena* arena, const char* rpath) {
    return sob__flag_with_value(arena, "-Wl,-rpath,", rpath);
}

typedef void (*Sob_ForEachFileCallback)(void* userData, const char* fileName,
                                        const char* fullPath);

static void sob__for_each_file(Sob_Arena* arena, const char* dir, const char* extFilter,
                               Sob_ForEachFileCallback callback, void* userData) {
    if (!arena || !dir || !callback) {
        return;
    }

    if (extFilter && !*extFilter) {
        extFilter = 0;
    }

#if SOB_WINDOWS
    WIN32_FIND_DATAA findData;
    const char* searchPath = sob__concat(arena, dir, "/", "*");
    if (!searchPath[0]) {
        return;
    }

    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (findData.cFileName[0] == '.') { continue; }
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { continue; }

            if (extFilter) {
                const char* fileExt = sob_path_ext(findData.cFileName);
                if (strcmp(fileExt, extFilter) != 0) { continue; }
            }

            const char* fullPath = sob_path_join(arena, dir, findData.cFileName);
            callback(userData, findData.cFileName, fullPath);
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
#else
    DIR* d = opendir(dir);
    if (d) {
        struct dirent* entry;
        while ((entry = readdir(d)) != NULL) {
            if (entry->d_name[0] == '.') { continue; }

            const char* fullPath = sob_path_join(arena, dir, entry->d_name);
            if (sob_fs_is_dir(fullPath)) { continue; }

            if (extFilter) {
                const char* fileExt = sob_path_ext(entry->d_name);
                if (strcmp(fileExt, extFilter) != 0) { continue; }
            }

            callback(userData, entry->d_name, fullPath);
        }
        closedir(d);
    }
#endif
}

SOBDEF int sob_fs_mkdir_p(const char* path) {
    if (!path) { return -1; }
    if (sob_fs_is_dir(path)) { return 0; }
    
    unsigned long long len = sob__strlen(path);
    Sob_Arena* tempArena = sob_arena_create();
    if (!tempArena) { return -1; }
    
    char* buf = (char*)sob_arena_alloc(tempArena, len + 1);
    if (!buf) {
        sob_arena_destroy(tempArena);
        return -1;
    }
    sob__memcpy(buf, path, len + 1);
    
    int result = 0;
    for (unsigned long long i = 1; i <= len; i++) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0') {
            char c = buf[i];
            buf[i] = '\0';
            if (!sob_fs_is_dir(buf)) {
                if (sob_fs_mkdir(buf) != 0 && !sob_fs_is_dir(buf)) {
                    result = -1;
                    break;
                }
            }
            buf[i] = c;
        }
    }
    
    sob_arena_destroy(tempArena);
    return result;
}

SOBDEF int sob_fs_remove(const char* path) {
    if (!path) { return -1; }
#if SOB_WINDOWS
    if (sob_fs_is_dir(path)) {
        return RemoveDirectoryA(path) ? 0 : -1;
    } else {
        return DeleteFileA(path) ? 0 : -1;
    }
#else
    return remove(path);
#endif
}

SOBDEF int sob_fs_remove_tree(const char* path) {
    if (!path) { return -1; }

#if SOB_WINDOWS
    if (!sob_fs_exists(path)) { return 0; }
    if (!sob_fs_is_dir(path)) { return sob_fs_remove(path); }

    WIN32_FIND_DATAA findData;
    char searchPath[4096];
    int searchLen = snprintf(searchPath, sizeof(searchPath), "%s\\*", path);
    if (searchLen <= 0 || (size_t)searchLen >= sizeof(searchPath)) {
        return -1;
    }

    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
                continue;
            }

            char childPath[4096];
            int childLen = snprintf(childPath, sizeof(childPath), "%s/%s", path, findData.cFileName);
            if (childLen <= 0 || (size_t)childLen >= sizeof(childPath)) {
                FindClose(hFind);
                return -1;
            }

            if (sob_fs_remove_tree(childPath) != 0) {
                FindClose(hFind);
                return -1;
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
#else
    struct stat st;
    if (lstat(path, &st) != 0) {
        return (errno == ENOENT) ? 0 : -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        return remove(path);
    }

    DIR* d = opendir(path);
    if (!d) { return -1; }

    struct dirent* entry;
    while ((entry = readdir(d)) != 0) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char childPath[4096];
        int childLen = snprintf(childPath, sizeof(childPath), "%s/%s", path, entry->d_name);
        if (childLen <= 0 || (size_t)childLen >= sizeof(childPath)) {
            closedir(d);
            return -1;
        }

        if (sob_fs_remove_tree(childPath) != 0) {
            closedir(d);
            return -1;
        }
    }
    closedir(d);
#endif

#if SOB_WINDOWS
    return (sob_fs_remove(path) == 0 || !sob_fs_exists(path)) ? 0 : -1;
#else
    return rmdir(path);
#endif
}

SOBDEF int sob_fs_copy(const char* src, const char* dst) {
    if (!src || !dst) { return -1; }
    
    FILE* in = fopen(src, "rb");
    if (!in) { return -1; }
    
    FILE* out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    
    Sob_Arena* tempArena = sob_arena_create();
    if (!tempArena) {
        fclose(in);
        fclose(out);
        return -1;
    }
    
    unsigned long long bufSize = 64 * 1024;
    char* buf = (char*)sob_arena_alloc(tempArena, bufSize);
    if (!buf) {
        sob_arena_destroy(tempArena);
        fclose(in);
        fclose(out);
        return -1;
    }
    
    unsigned long long n;
    int result = 0;
    while ((n = fread(buf, 1, bufSize, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            result = -1;
            break;
        }
    }
    
    sob_arena_destroy(tempArena);
    fclose(in);
    fclose(out);
    return result;
}

SOBDEF int sob_fs_write_text(const char* path, const char* text) {
    if (!path) { return -1; }

    FILE* out = fopen(path, "wb");
    if (!out) { return -1; }

    if (text && text[0]) {
        size_t len = strlen(text);
        if (fwrite(text, 1, len, out) != len) {
            fclose(out);
            return -1;
        }
    }

    fclose(out);
    return 0;
}

/* ========================================================================= */
/*                              BUILD CONTEXT                                */
/* ========================================================================= */

struct Sob_BuildContext {
    Sob_Arena*       arena;
    Sob_Target**     targets;
    int              targetCount;
    int              targetCapacity;
    int              errorCount;
    Sob_CompilerConfig compiler;
    int              jobCount;
    Sob_LogLevel     logLevel;
};

typedef struct Sob_CodeGenEntry {
    const char*     output;
    Sob_CodeGenFunc func;
    const char**    inputs;
    int             inputCount;
    void*           userData;
} Sob_CodeGenEntry;

struct Sob_Target {
    const char*      name;
    Sob_TargetKind   kind;
    Sob_BuildContext* ctx;
    Sob_StringArray  sources;
    Sob_StringArray  includes;
    Sob_StringArray  defines;
    Sob_StringArray  cflags;
    Sob_StringArray  ldflags;
    Sob_StringArray  libs;
    Sob_Target**     deps;
    int              depCount;
    int              depCapacity;
    const char*      outputDir;
    const char*      outputName;
    Sob_Standard     standard;
    Sob_CodeGenEntry* codegens;
    int              codegenCount;
    int              codegenCapacity;
};

SOBDEF Sob_BuildContext* sob_build_create(Sob_Arena* arena) {
    Sob_BuildContext* ctx = (Sob_BuildContext*)sob_arena_alloc(arena, sizeof(Sob_BuildContext));
    if (!ctx) { return 0; }
    sob__memset(ctx, 0, sizeof(Sob_BuildContext));
    ctx->arena = arena;
    ctx->compiler.kind = sob_detect_compiler();
    ctx->jobCount = sob_nprocs();
    ctx->logLevel = Sob_LogLevel_Info;
    return ctx;
}

SOBDEF void sob_build_destroy(Sob_BuildContext* ctx) {
    (void)ctx;
}

SOBDEF void sob_build_set_compiler(Sob_BuildContext* ctx, Sob_CompilerConfig config) {
    if (!ctx) { return; }
    if (config.kind == Sob_CompilerKind_Auto) {
        config.kind = sob_detect_compiler();
    }
    ctx->compiler = config;
}

static int sob__str_ends_with(const char* str, const char* suffix) {
    unsigned long long strLen = sob__strlen(str);
    unsigned long long suffixLen = sob__strlen(suffix);
    if (suffixLen > strLen) { return 0; }
    return strcmp(str + strLen - suffixLen, suffix) == 0;
}

SOBDEF Sob_CompilerKind sob_detect_compiler(void) {
    const char* cc = getenv("CC");
    if (cc) {
        if (strstr(cc, "clang")) { return Sob_CompilerKind_Clang; }
        if (strstr(cc, "gcc")) { return Sob_CompilerKind_GCC; }
        if (strcmp(cc, "cl") == 0 || strcmp(cc, "cl.exe") == 0 ||
            sob__str_ends_with(cc, "/cl") || sob__str_ends_with(cc, "/cl.exe") ||
            sob__str_ends_with(cc, "\\cl") || sob__str_ends_with(cc, "\\cl.exe")) {
            return Sob_CompilerKind_MSVC;
        }
    }
    
#if SOB_WINDOWS
    return Sob_CompilerKind_MSVC;
#elif SOB_MACOS
    return Sob_CompilerKind_Clang;
#else
    return Sob_CompilerKind_GCC;
#endif
}

/* ========================================================================= */
/*                              TARGET IMPLEMENTATION                        */
/* ========================================================================= */

SOBDEF Sob_Target* sob_target_create_(Sob_BuildContext* ctx, const char* name,
                                       Sob_TargetKind kind, Sob_TargetOpts opts) {
    if (!ctx || !name || !name[0]) { return 0; }
    
    Sob_Target* t = (Sob_Target*)sob_arena_alloc(ctx->arena, sizeof(Sob_Target));
    if (!t) { return 0; }
    sob__memset(t, 0, sizeof(Sob_Target));
    
    t->name = sob__strdup(ctx->arena, name);
    if (!t->name) { return 0; }
    t->kind = kind;
    t->ctx = ctx;
    t->sources.arena = ctx->arena;
    t->includes.arena = ctx->arena;
    t->defines.arena = ctx->arena;
    t->cflags.arena = ctx->arena;
    t->ldflags.arena = ctx->arena;
    t->libs.arena = ctx->arena;
    t->outputDir = opts.outputDir ? sob__strdup(ctx->arena, opts.outputDir) : "build";
    t->outputName = opts.outputName ? sob__strdup(ctx->arena, opts.outputName) : t->name;
    if (!t->outputDir || !t->outputName) { return 0; }
    
    if (SOB__VEC_ENSURE(ctx->arena, ctx->targets, ctx->targetCount, ctx->targetCapacity, 8) != 0) {
        return 0;
    }
    ctx->targets[ctx->targetCount++] = t;
    
    return t;
}

static void sob__add_source_file_cb(void* userData, const char* fileName, const char* fullPath) {
    (void)fileName;
    Sob_Target* t = (Sob_Target*)userData;
    if (!t || !fullPath) {
        return;
    }
    sob__da_push(&t->sources, fullPath);
}

SOBDEF void sob_target_add_source_(Sob_Target* t, const char* path, Sob_SourceOpts opts) {
    if (!t) { return; }
    
    sob__da_push_paths(&t->sources, t->ctx->arena, path, opts.paths, opts.count);
    
    if (opts.glob) {
        const char* lastSlash = opts.glob;
        const char* p = opts.glob;
        while (*p) {
            if (*p == '/' || *p == '\\') { lastSlash = p; }
            p++;
        }
        
        const char* dir;
        const char* filePattern;
        if (lastSlash != opts.glob) {
            unsigned long long dirLen = (unsigned long long)(lastSlash - opts.glob);
            char* dirBuf = (char*)sob_arena_alloc(t->ctx->arena, dirLen + 1);
            if (!dirBuf) { return; }
            sob__memcpy(dirBuf, opts.glob, dirLen);
            dirBuf[dirLen] = '\0';
            dir = dirBuf;
            filePattern = lastSlash + 1;
        } else {
            dir = ".";
            filePattern = opts.glob;
        }
        
        const char* ext = 0;
        if (filePattern[0] == '*' && filePattern[1] == '.') {
            ext = filePattern + 1;
        }
        
        sob__for_each_file(t->ctx->arena, dir, ext, sob__add_source_file_cb, t);
    }
}

SOBDEF void sob_target_add_include_(Sob_Target* t, const char* path, Sob_SourceOpts opts) {
    if (!t) { return; }
    
    sob__da_push_paths(&t->includes, t->ctx->arena, path, opts.paths, opts.count);
    (void)opts.glob;
}

SOBDEF void sob_target_define_(Sob_Target* t, const char* name, Sob_DefineOpts opts) {
    if (!t || !name) { return; }
    
    if (opts.value) {
        const char* def = sob__concat(t->ctx->arena, name, "=", opts.value);
        if (def && def[0]) {
            sob__da_push(&t->defines, def);
        }
    } else {
        sob__da_push(&t->defines, sob__strdup(t->ctx->arena, name));
    }
}

SOBDEF void sob_target_add_cflags(Sob_Target* t, const char* flags) {
    if (!t || !flags) { return; }
    sob__da_push(&t->cflags, sob__strdup(t->ctx->arena, flags));
}

SOBDEF void sob_target_add_ldflags(Sob_Target* t, const char* flags) {
    if (!t || !flags) { return; }
    sob__da_push(&t->ldflags, sob__strdup(t->ctx->arena, flags));
}

SOBDEF void sob_target_set_standard(Sob_Target* t, Sob_Standard std) {
    if (!t) { return; }
    t->standard = std;
}

SOBDEF void sob_target_depends_on(Sob_Target* t, Sob_Target* dep) {
    if (!t || !dep) { return; }
    
    if (SOB__VEC_ENSURE(t->ctx->arena, t->deps, t->depCount, t->depCapacity, 4) != 0) {
        return;
    }
    t->deps[t->depCount++] = dep;
}

SOBDEF void sob_target_add_codegen_(Sob_Target* t, const char* output,
                                     Sob_CodeGenFunc func, Sob_CodeGenOpts opts) {
    if (!t || !output || !func) { return; }
    
    if (SOB__VEC_ENSURE(t->ctx->arena, t->codegens, t->codegenCount, t->codegenCapacity, 4) != 0) {
        return;
    }
    
    Sob_CodeGenEntry* entry = &t->codegens[t->codegenCount++];
    entry->output = sob__strdup(t->ctx->arena, output);
    entry->func = func;
    entry->userData = opts.userData;
    entry->inputCount = opts.inputCount;
    
    if (opts.inputs && opts.inputCount > 0) {
        entry->inputs = (const char**)sob_arena_alloc(t->ctx->arena,
            (unsigned long long)opts.inputCount * sizeof(const char*));
        if (entry->inputs) {
            for (int i = 0; i < opts.inputCount; i++) {
                entry->inputs[i] = sob__strdup(t->ctx->arena, opts.inputs[i]);
            }
        }
    } else {
        entry->inputs = 0;
    }
}

typedef struct Sob_CompilerOps {
    int isMsvc;
    const char* includePrefix;
    const char* definePrefix;
    const char* libSearchPrefix;
    const char* systemLibPrefix;
    const char* systemLibSuffix;
    const char* compileOnlyFlag;
    const char* compileOutPrefix;
    int         compileOutSeparateArg;
    const char* linkOutPrefix;
    int         linkOutSeparateArg;
    const char* linkDynlibFlag;
    const char* linkLtoFlag;
    const char* archiveCmd;
    const char* archiveMode;
    const char* archiveOutPrefix;
    int         archiveOutSeparateArg;
    const char* objExt;
} Sob_CompilerOps;

static const Sob_CompilerOps* sob__get_compiler_ops(Sob_CompilerKind kind) {
    static const Sob_CompilerOps sob__opsMsvc = {
        1, "/I", "/D", "/LIBPATH:", "", ".lib",
        "/c",
        "/Fo", 0,
        "/Fe", 0,
        "/LD", "/LTCG",
        "lib", "", "/OUT:", 0,
        ".obj"
    };

    static const Sob_CompilerOps sob__opsGnu = {
        0, "-I", "-D", "-L", "-l", "",
        "-c",
        "-o", 1,
        "-o", 1,
        "-shared", "",
        "ar", "rcs", "", 1,
        ".o"
    };

    if (kind == Sob_CompilerKind_MSVC) {
        return &sob__opsMsvc;
    }
    return &sob__opsGnu;
}

SOBDEF void sob_target_link_(Sob_Target* t, const char* name, Sob_LinkOpts opts) {
    if (!t || !name) { return; }
    
    const Sob_CompilerOps* ops = sob__get_compiler_ops(t->ctx->compiler.kind);
    char* lib = 0;
    
    switch (opts.kind) {
        case Sob_LibKind_Framework:
            sob__da_push(&t->libs, sob__strdup(t->ctx->arena, "-framework"));
            sob__da_push(&t->libs, sob__strdup(t->ctx->arena, name));
            break;
        case Sob_LibKind_Static:
        case Sob_LibKind_Dynamic:
            lib = sob__strdup(t->ctx->arena, name);
            break;
        case Sob_LibKind_System:
        default:
            if (ops->isMsvc && sob__str_ends_with(name, ".lib")) {
                lib = sob__strdup(t->ctx->arena, name);
            } else {
                lib = (char*)sob__concat(t->ctx->arena, ops->systemLibPrefix, name,
                                         ops->systemLibSuffix);
            }
            break;
    }
    
    if (lib && lib[0]) {
        sob__da_push(&t->libs, lib);
    }
    
    if (opts.searchPath) {
        const char* pathFlag = sob__flag_with_value(t->ctx->arena, ops->libSearchPrefix, opts.searchPath);
        if (pathFlag && pathFlag[0]) {
            sob__da_push(&t->ldflags, pathFlag);
        }
    }
    
    if (opts.useRpath && opts.searchPath) {
#if SOB_MACOS
        const char* rpath = sob__rpath_from_search(t->ctx->arena, "@executable_path", opts.searchPath);
        sob__da_push(&t->ldflags, sob__make_rpath_flag(t->ctx->arena, rpath));
#elif SOB_LINUX
        const char* rpath = sob__rpath_from_search(t->ctx->arena, "$ORIGIN", opts.searchPath);
        sob__da_push(&t->ldflags, sob__make_rpath_flag(t->ctx->arena, rpath));
#endif
    }
}

static const char* sob__get_import_lib_path(Sob_Arena* arena, Sob_Target* t) {
    return sob__concat(arena, t->outputDir, "/", t->outputName, ".lib");
}

SOBDEF void sob_target_link_target(Sob_Target* t, Sob_Target* libTarget) {
    if (!t || !libTarget) { return; }
    const Sob_CompilerOps* ops = sob__get_compiler_ops(t->ctx->compiler.kind);
    
    sob_target_depends_on(t, libTarget);
    
    const char* libPath = sob_target_get_output_path(libTarget);
    const char* libDir = sob_path_dirname(t->ctx->arena, libPath);
    
    if (libTarget->kind == Sob_TargetKind_StaticLib) {
        sob_target_link_(t, libPath, (Sob_LinkOpts){ .kind = Sob_LibKind_Static });
    } else if (libTarget->kind == Sob_TargetKind_DynamicLib) {
        if (ops->isMsvc) {
            const char* importLib = sob__get_import_lib_path(t->ctx->arena, libTarget);
            sob_target_link_(t, importLib, (Sob_LinkOpts){ .kind = Sob_LibKind_Static });
            return;
        }
        const char* basename = sob_path_basename(libPath);
        const char* libName = 0;
        
        if (basename[0] == 'l' && basename[1] == 'i' && basename[2] == 'b') {
            const char* start = basename + 3;
            const char* dot = 0;
            for (const char* p = start; *p; p++) {
                if (*p == '.') { dot = p; break; }
            }
            if (dot) {
                unsigned long long len = (unsigned long long)(dot - start);
                char* name = (char*)sob_arena_alloc(t->ctx->arena, len + 1);
                if (name) {
                    sob__memcpy(name, start, len);
                    name[len] = '\0';
                    libName = name;
                }
            }
        }
        
        if (libName) {
            sob_target_link_(t, libName, (Sob_LinkOpts){ 
                .kind = Sob_LibKind_System, 
                .searchPath = libDir,
                .useRpath = 1 
            });
        } else {
            sob_target_link_(t, libPath, (Sob_LinkOpts){ .kind = Sob_LibKind_Dynamic });
        }
    }
}

SOBDEF void sob_target_link_platform_libs(Sob_Target* t) {
    if (!t) { return; }
#if SOB_MACOS
    sob_target_link_(t, "Cocoa", (Sob_LinkOpts){ .kind = Sob_LibKind_Framework });
#elif SOB_LINUX
    sob_target_link_(t, "m", (Sob_LinkOpts){0});
    sob_target_link_(t, "pthread", (Sob_LinkOpts){0});
#elif SOB_WINDOWS
    sob_target_link_(t, "kernel32", (Sob_LinkOpts){0});
    sob_target_link_(t, "user32", (Sob_LinkOpts){0});
#endif
}

SOBDEF const char* sob_target_get_output_path(Sob_Target* t) {
    if (!t) { return ""; }
    
    const char* ext = "";
    switch (t->kind) {
        case Sob_TargetKind_Executable:
#if SOB_WINDOWS
            ext = ".exe";
#endif
            break;
        case Sob_TargetKind_StaticLib:
#if SOB_WINDOWS
            ext = ".lib";
#else
            ext = ".a";
#endif
            break;
        case Sob_TargetKind_DynamicLib:
#if SOB_WINDOWS
            ext = ".dll";
#elif SOB_MACOS
            ext = ".dylib";
#else
            ext = ".so";
#endif
            break;
        case Sob_TargetKind_ObjectFiles:
#if SOB_WINDOWS
            ext = ".obj";
#else
            ext = ".o";
#endif
            break;
    }
    
    return sob__concat(t->ctx->arena, t->outputDir, "/", t->outputName, ext);
}

/* ========================================================================= */
/*                              BUILD EXECUTION                              */
/* ========================================================================= */

static int sob__is_cpp_file(const char* path) {
    const char* ext = sob_path_ext(path);
    if (!ext || !*ext) { return 0; }
    return (strcmp(ext, ".cpp") == 0 || strcmp(ext, ".cxx") == 0 ||
            strcmp(ext, ".cc") == 0 || strcmp(ext, ".C") == 0 ||
            strcmp(ext, ".c++") == 0 || strcmp(ext, ".mm") == 0);
}

static int sob__target_has_cpp(Sob_Target* t) {
    for (int i = 0; i < t->sources.count; i++) {
        if (sob__is_cpp_file(t->sources.items[i])) {
            return 1;
        }
    }
    return 0;
}

#define SOB__COMPILER_CMD_TABLE(X) \
    X(Sob_CompilerKind_Clang, "clang", "clang++") \
    X(Sob_CompilerKind_GCC, "gcc", "g++") \
    X(Sob_CompilerKind_MSVC, "cl", "cl")

static const char* sob__get_compiler_cmd(Sob_CompilerKind kind, int isCpp) {
    switch (kind) {
    #define SOB__CASE_COMPILER_CMD(k, cCmd, cppCmd) \
        case k: { \
            return isCpp ? cppCmd : cCmd; \
        }
        SOB__COMPILER_CMD_TABLE(SOB__CASE_COMPILER_CMD)
    #undef SOB__CASE_COMPILER_CMD
        case Sob_CompilerKind_Auto:
        default: {
            const char* cc = getenv(isCpp ? "CXX" : "CC");
            if (cc) {
                return cc;
            }
            return isCpp ? "g++" : "gcc";
        }
    }
}
#undef SOB__COMPILER_CMD_TABLE

static void sob__append_sanitizer_flags(Sob_Cmd* cmd, Sob_CompilerConfig* compiler, int isMsvc) {
    if (!compiler) {
        return;
    }

    if (isMsvc) {
        if (compiler->sanitizers & Sob_Sanitizer_Address) {
            sob_cmd_append(cmd, "/fsanitize=address");
        }
        return;
    }

    if (compiler->sanitizers & Sob_Sanitizer_Address) {
        sob_cmd_append(cmd, "-fsanitize=address");
    }
    if (compiler->sanitizers & Sob_Sanitizer_Thread) {
        sob_cmd_append(cmd, "-fsanitize=thread");
    }
    if (compiler->sanitizers & Sob_Sanitizer_UB) {
        sob_cmd_append(cmd, "-fsanitize=undefined");
    }
}

static void sob__append_sysroot_target_flags(Sob_Cmd* cmd, Sob_CompilerConfig* compiler,
                                             Sob_Arena* arena) {
    if (!compiler || !arena) {
        return;
    }

    if (compiler->sysroot) {
        sob_cmd_append(cmd, sob__flag_with_value(arena, "--sysroot=", compiler->sysroot));
    }

    if (compiler->targetTriple) {
        sob_cmd_append(cmd, sob__flag_with_value(arena, "--target=", compiler->targetTriple));
    }
}

#define SOB__STANDARD_FLAG_TABLE(X) \
    X(Sob_Standard_C89,   "/std:c11",      "-std=c89") \
    X(Sob_Standard_C99,   "/std:c11",      "-std=c99") \
    X(Sob_Standard_C11,   "/std:c11",      "-std=c11") \
    X(Sob_Standard_C17,   "/std:c17",      "-std=c17") \
    X(Sob_Standard_C23,   "/std:clatest",  "-std=c23") \
    X(Sob_Standard_Cpp11, "/std:c++14",    "-std=c++11") \
    X(Sob_Standard_Cpp14, "/std:c++14",    "-std=c++14") \
    X(Sob_Standard_Cpp17, "/std:c++17",    "-std=c++17") \
    X(Sob_Standard_Cpp20, "/std:c++20",    "-std=c++20") \
    X(Sob_Standard_Cpp23, "/std:c++latest","-std=c++23")

static const char* sob__standard_flag_msvc(Sob_Standard standard) {
    switch (standard) {
    #define SOB__CASE_STANDARD_MSVC(stdKind, msvcFlag, gnuFlag) \
        case stdKind: { \
            return msvcFlag; \
        }
        SOB__STANDARD_FLAG_TABLE(SOB__CASE_STANDARD_MSVC)
    #undef SOB__CASE_STANDARD_MSVC
        default: {
            return 0;
        }
    }
}

static const char* sob__standard_flag_gnu(Sob_Standard standard) {
    switch (standard) {
    #define SOB__CASE_STANDARD_GNU(stdKind, msvcFlag, gnuFlag) \
        case stdKind: { \
            return gnuFlag; \
        }
        SOB__STANDARD_FLAG_TABLE(SOB__CASE_STANDARD_GNU)
    #undef SOB__CASE_STANDARD_GNU
        default: {
            return 0;
        }
    }
}

static void sob__append_target_cflags(Sob_Cmd* cmd, Sob_Target* t) {
    for (int i = 0; i < t->cflags.count; i++) {
        sob_cmd_append(cmd, t->cflags.items[i]);
    }
}

static void sob__add_common_flags_msvc(Sob_Cmd* cmd, Sob_Target* t) {
    Sob_BuildContext* ctx = t->ctx;
    Sob_Arena* arena = ctx->arena;
    const Sob_CompilerOps* ops = sob__get_compiler_ops(ctx->compiler.kind);

    switch (ctx->compiler.optLevel) {
        case Sob_OptLevel_Debug: {
            sob_cmd_append(cmd, "/Od");
            sob_cmd_append(cmd, "/Zi");
            break;
        }
        case Sob_OptLevel_Release: {
            sob_cmd_append(cmd, "/O2");
            break;
        }
        case Sob_OptLevel_ReleaseFast: {
            sob_cmd_append(cmd, "/O2");
            break;
        }
        case Sob_OptLevel_ReleaseSmall: {
            sob_cmd_append(cmd, "/O1");
            break;
        }
    }

    /* Avoid PDB write contention in parallel builds. */
    sob_cmd_append(cmd, "/FS");
    sob__append_sanitizer_flags(cmd, &ctx->compiler, 1);
    if (ctx->compiler.enableLTO) {
        sob_cmd_append(cmd, "/GL");
    }
    sob_cmd_append(cmd, "/W4");
    if (ctx->compiler.warningsAsErrors) {
        sob_cmd_append(cmd, "/WX");
    }

    sob__append_prefixed_flags(cmd, arena, ops->includePrefix, &t->includes);
    sob__append_prefixed_flags(cmd, arena, ops->definePrefix, &t->defines);

    if (t->standard != Sob_Standard_Default) {
        const char* stdFlag = sob__standard_flag_msvc(t->standard);
        if (stdFlag) {
            sob_cmd_append(cmd, stdFlag);
        }
    }

    sob__append_target_cflags(cmd, t);
}

static void sob__add_common_flags_gnu(Sob_Cmd* cmd, Sob_Target* t) {
    Sob_BuildContext* ctx = t->ctx;
    Sob_Arena* arena = ctx->arena;
    const Sob_CompilerOps* ops = sob__get_compiler_ops(ctx->compiler.kind);

    switch (ctx->compiler.optLevel) {
        case Sob_OptLevel_Debug: {
            sob_cmd_append(cmd, "-O0");
            sob_cmd_append(cmd, "-g");
            break;
        }
        case Sob_OptLevel_Release: {
            sob_cmd_append(cmd, "-O2");
            break;
        }
        case Sob_OptLevel_ReleaseFast: {
            sob_cmd_append(cmd, "-O3");
            break;
        }
        case Sob_OptLevel_ReleaseSmall: {
            sob_cmd_append(cmd, "-Os");
            break;
        }
    }
    
    sob__append_sanitizer_flags(cmd, &ctx->compiler, 0);
    
    if (ctx->compiler.enableLTO) {
        sob_cmd_append(cmd, "-flto");
    }
    
    sob__append_sysroot_target_flags(cmd, &ctx->compiler, arena);

#if !SOB_WINDOWS
    if (t->kind == Sob_TargetKind_DynamicLib) {
        sob_cmd_append(cmd, "-fPIC");
    }
#endif
    
    sob_cmd_append(cmd, "-Wall");
    sob_cmd_append(cmd, "-Wextra");
    if (ctx->compiler.warningsAsErrors) {
        sob_cmd_append(cmd, "-Werror");
    }
    
    sob__append_prefixed_flags(cmd, arena, ops->includePrefix, &t->includes);
    sob__append_prefixed_flags(cmd, arena, ops->definePrefix, &t->defines);
    
    if (t->standard != Sob_Standard_Default) {
        const char* stdFlag = sob__standard_flag_gnu(t->standard);
        if (stdFlag) {
            sob_cmd_append(cmd, stdFlag);
        }
    }

    sob__append_target_cflags(cmd, t);
}

static void sob__add_common_flags(Sob_Cmd* cmd, Sob_Target* t) {
    const Sob_CompilerOps* ops = sob__get_compiler_ops(t->ctx->compiler.kind);
    if (ops->isMsvc) {
        sob__add_common_flags_msvc(cmd, t);
    } else {
        sob__add_common_flags_gnu(cmd, t);
    }
}
#undef SOB__STANDARD_FLAG_TABLE

static void sob__add_link_common_flags_msvc(Sob_Cmd* cmd, Sob_Target* t) {
    sob__append_sanitizer_flags(cmd, &t->ctx->compiler, 1);
}

static void sob__add_link_common_flags_gnu(Sob_Cmd* cmd, Sob_Target* t) {
    Sob_BuildContext* ctx = t->ctx;
    Sob_Arena* arena = ctx->arena;
    sob__append_sanitizer_flags(cmd, &ctx->compiler, 0);
    if (ctx->compiler.enableLTO) {
        sob_cmd_append(cmd, "-flto");
    }
    sob__append_sysroot_target_flags(cmd, &ctx->compiler, arena);
}

static void sob__add_link_common_flags(Sob_Cmd* cmd, Sob_Target* t) {
    const Sob_CompilerOps* ops = sob__get_compiler_ops(t->ctx->compiler.kind);
    if (ops->isMsvc) {
        sob__add_link_common_flags_msvc(cmd, t);
    } else {
        sob__add_link_common_flags_gnu(cmd, t);
    }
}

static void sob__append_output_arg(Sob_Cmd* cmd, Sob_Arena* arena, const char* prefix,
                                   int separateArg, const char* value) {
    if (separateArg) {
        if (prefix && prefix[0]) {
            sob_cmd_append(cmd, prefix);
        }
        sob_cmd_append(cmd, value);
    } else {
        if (prefix && prefix[0]) {
            sob_cmd_append(cmd, sob__flag_with_value(arena, prefix, value));
        } else {
            sob_cmd_append(cmd, value);
        }
    }
}

static void sob__append_compile_output_flags(Sob_Cmd* cmd, Sob_Target* t, const char* objPath) {
    const Sob_CompilerOps* ops = sob__get_compiler_ops(t->ctx->compiler.kind);
    sob_cmd_append(cmd, ops->compileOnlyFlag);
    sob__append_output_arg(cmd, t->ctx->arena, ops->compileOutPrefix,
                           ops->compileOutSeparateArg, objPath);
}

static void sob__append_link_output_flags(Sob_Cmd* cmd, Sob_Target* t) {
    Sob_BuildContext* ctx = t->ctx;
    const Sob_CompilerOps* ops = sob__get_compiler_ops(ctx->compiler.kind);
    if (t->kind == Sob_TargetKind_DynamicLib) {
        sob_cmd_append(cmd, ops->linkDynlibFlag);
    }
    sob__append_output_arg(cmd, t->ctx->arena, ops->linkOutPrefix,
                           ops->linkOutSeparateArg, sob_target_get_output_path(t));
    if (ctx->compiler.enableLTO && ops->linkLtoFlag[0]) {
        sob_cmd_append(cmd, ops->linkLtoFlag);
    }
}

static void sob__append_link_flags(Sob_Cmd* cmd, Sob_Target* t) {
    for (int i = 0; i < t->ldflags.count; i++) {
        sob_cmd_append(cmd, t->ldflags.items[i]);
    }
    for (int i = 0; i < t->libs.count; i++) {
        sob_cmd_append(cmd, t->libs.items[i]);
    }
}

static Sob_Cmd* sob__make_compile_cmd(Sob_Target* t, const char* src, const char* objPath) {
    Sob_BuildContext* ctx = t->ctx;
    Sob_Arena* arena = ctx->arena;

    Sob_Cmd* cmd = sob_cmd_create(arena);
    if (!cmd) {
        return 0;
    }
    cmd->logCtx = ctx;

    sob_cmd_append(cmd, sob__get_compiler_cmd(ctx->compiler.kind, sob__is_cpp_file(src)));
    sob__add_common_flags(cmd, t);
    sob__append_compile_output_flags(cmd, t, objPath);
    sob_cmd_append(cmd, src);
    return cmd;
}

static Sob_Cmd* sob__make_single_pass_cmd(Sob_Target* t) {
    Sob_BuildContext* ctx = t->ctx;
    Sob_Arena* arena = ctx->arena;

    Sob_Cmd* cmd = sob_cmd_create(arena);
    if (!cmd) {
        return 0;
    }
    cmd->logCtx = ctx;

    sob_cmd_append(cmd, sob__get_compiler_cmd(ctx->compiler.kind, sob__target_has_cpp(t)));
    sob__add_common_flags(cmd, t);
    sob__append_link_output_flags(cmd, t);

    for (int i = 0; i < t->sources.count; i++) {
        sob_cmd_append(cmd, t->sources.items[i]);
    }

    sob__append_link_flags(cmd, t);
    return cmd;
}

static Sob_Cmd* sob__make_link_cmd(Sob_Target* t, const char** inputs, int inputCount) {
    Sob_BuildContext* ctx = t->ctx;
    Sob_Arena* arena = ctx->arena;

    Sob_Cmd* cmd = sob_cmd_create(arena);
    if (!cmd) {
        return 0;
    }
    cmd->logCtx = ctx;

    sob_cmd_append(cmd, sob__get_compiler_cmd(ctx->compiler.kind, sob__target_has_cpp(t)));
    sob__add_link_common_flags(cmd, t);
    sob__append_link_output_flags(cmd, t);

    for (int i = 0; i < inputCount; i++) {
        sob_cmd_append(cmd, inputs[i]);
    }

    sob__append_link_flags(cmd, t);
    return cmd;
}

static Sob_Cmd* sob__make_archive_cmd(Sob_Target* t, const char** objFiles, int objCount) {
    Sob_Arena* arena = t->ctx->arena;
    const Sob_CompilerOps* ops = sob__get_compiler_ops(t->ctx->compiler.kind);
    Sob_Cmd* cmd = sob_cmd_create(arena);
    if (!cmd) {
        return 0;
    }
    cmd->logCtx = t->ctx;

    sob_cmd_append(cmd, ops->archiveCmd);
    if (ops->archiveMode[0]) {
        sob_cmd_append(cmd, ops->archiveMode);
    }
    sob__append_output_arg(cmd, arena, ops->archiveOutPrefix,
                           ops->archiveOutSeparateArg, sob_target_get_output_path(t));

    for (int i = 0; i < objCount; i++) {
        sob_cmd_append(cmd, objFiles[i]);
    }
    return cmd;
}

static const char* sob__get_obj_path(Sob_Arena* arena, const char* outputDir, const char* srcPath) {
#if SOB_WINDOWS
    const Sob_CompilerOps* ops = sob__get_compiler_ops(Sob_CompilerKind_MSVC);
#else
    const Sob_CompilerOps* ops = sob__get_compiler_ops(Sob_CompilerKind_GCC);
#endif
    const char* basename = sob_path_basename(srcPath);
    unsigned long long len = sob__strlen(basename);
    
    const char* dot = 0;
    for (unsigned long long i = 0; i < len; i++) {
        if (basename[i] == '.') { dot = basename + i; }
    }
    
    unsigned long long baseLen = dot ? (unsigned long long)(dot - basename) : len;
    unsigned long long dirLen = sob__strlen(outputDir);
    
    unsigned long long srcLen = sob__strlen(srcPath);
    unsigned int hash = 0;
    for (unsigned long long i = 0; i < srcLen; i++) {
        hash = hash * 31 + (unsigned char)srcPath[i];
    }
    
    const char* ext = ops->objExt;
    unsigned long long extLen = sob__strlen(ext);
    
    char* objPath = (char*)sob_arena_alloc(arena, dirLen + baseLen + 10 + extLen + 2);
    if (!objPath) { return ""; }
    
    sob__memcpy(objPath, outputDir, dirLen);
    objPath[dirLen] = '/';
    sob__memcpy(objPath + dirLen + 1, basename, baseLen);
    
    char* p = objPath + dirLen + 1 + baseLen;
    *p++ = '_';
    const char* hexDigits = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        p[i] = hexDigits[hash & 0xF];
        hash >>= 4;
    }
    p += 8;
    sob__memcpy(p, ext, extLen + 1);
    
    return objPath;
}

static int sob__compile_sources_serial(Sob_Target* t, const char** objFiles) {
    Sob_Arena* arena = t->ctx->arena;

    for (int i = 0; i < t->sources.count; i++) {
        const char* src = t->sources.items[i];
        const char* objPath = sob__get_obj_path(arena, t->outputDir, src);
        objFiles[i] = objPath;

        Sob_Cmd* compileCmd = sob__make_compile_cmd(t, src, objPath);
        if (!compileCmd) {
            return -1;
        }

        int result = sob_cmd_run_(compileCmd, (Sob_CmdOpts){0});
        if (result != 0) {
            return result;
        }
    }

    return 0;
}

static int sob__compile_sources_parallel(Sob_Target* t, const char** objFiles, int jobCount) {
    Sob_Arena* arena = t->ctx->arena;

    Sob_Proc** procs = (Sob_Proc**)sob_arena_alloc(arena,
        (unsigned long long)jobCount * sizeof(Sob_Proc*));
    if (!procs) { return -1; }
    sob__memset(procs, 0, (unsigned long long)jobCount * sizeof(Sob_Proc*));

    int activeProcs = 0;
    int nextSource = 0;
    int failCount = 0;

    while (nextSource < t->sources.count || activeProcs > 0) {
        while (activeProcs < jobCount && nextSource < t->sources.count) {
            const char* src = t->sources.items[nextSource];
            const char* obj = sob__get_obj_path(arena, t->outputDir, src);
            objFiles[nextSource] = obj;

            Sob_Cmd* cmd = sob__make_compile_cmd(t, src, obj);
            if (!cmd) {
                sob_log(t->ctx, Sob_LogLevel_Error, "Failed to build compile command: %s", src);
                failCount++;
                nextSource++;
                continue;
            }

            sob__log_cmd_line(cmd);

            Sob_Proc* proc = sob_cmd_spawn(cmd);
            if (!proc) {
                sob_log(t->ctx, Sob_LogLevel_Error, "Failed to spawn: %s", src);
                failCount++;
                nextSource++;
                continue;
            }

            for (int i = 0; i < jobCount; i++) {
                if (!procs[i]) {
                    procs[i] = proc;
                    break;
                }
            }
            activeProcs++;
            nextSource++;
        }

        if (activeProcs > 0) {
            int finishedAny = 0;
            for (int i = 0; i < jobCount; i++) {
                if (procs[i]) {
                    int status = sob_proc_poll(procs[i]);
                    if (status >= 0) {
                        if (status != 0) { failCount++; }
                        procs[i] = 0;
                        activeProcs--;
                        finishedAny = 1;
                    }
                }
            }
            if (activeProcs > 0 && !finishedAny) {
#if SOB_WINDOWS
                Sleep(1);
#else
                usleep(1000);  /* 1ms if nothing finished */
#endif
            }
        }
    }

    if (failCount > 0) {
        sob_log(t->ctx, Sob_LogLevel_Error, "%d compilation(s) failed", failCount);
        return -1;
    }

    return 0;
}

static int sob__run_codegens(Sob_Target* t) {
    Sob_Arena* arena = t->ctx->arena;
    
    for (int i = 0; i < t->codegenCount; i++) {
        Sob_CodeGenEntry* entry = &t->codegens[i];
        
        unsigned long long outputTime = sob_fs_mtime(entry->output);
        int needsRegen = (outputTime == 0);
        
        if (!needsRegen && entry->inputs) {
            for (int j = 0; j < entry->inputCount; j++) {
                unsigned long long inputTime = sob_fs_mtime(entry->inputs[j]);
                if (inputTime > outputTime) {
                    needsRegen = 1;
                    break;
                }
            }
        }
        
        if (needsRegen) {
            sob_log(t->ctx, Sob_LogLevel_Info, "Running codegen: %s", entry->output);
            entry->func(arena, entry->userData);
        }
    }
    
    return 0;
}

static int sob__build_target(Sob_Target* t) {
    Sob_BuildContext* ctx = t->ctx;
    Sob_Arena* arena = ctx->arena;
    int jobCount = ctx->jobCount;
    
    if (t->codegenCount > 0) {
        int result = sob__run_codegens(t);
        if (result != 0) { return result; }
    }
    
    if (sob_fs_mkdir_p(t->outputDir) != 0 && !sob_fs_is_dir(t->outputDir)) {
        sob_log(ctx, Sob_LogLevel_Error, "Failed to create output directory: %s", t->outputDir);
        return -1;
    }
    
    if (t->sources.count == 0) {
        sob_log(ctx, Sob_LogLevel_Error, "Target has no sources: %s", t->name);
        return -1;
    }

    int canParallel = (t->sources.count > 1 && jobCount > 1);
    int needsObjects = (t->kind == Sob_TargetKind_StaticLib ||
        t->kind == Sob_TargetKind_ObjectFiles || canParallel);

    if (!needsObjects) {
        Sob_Cmd* cmd = sob__make_single_pass_cmd(t);
        if (!cmd) { return -1; }
        return sob_cmd_run_(cmd, (Sob_CmdOpts){0});
    }

    const char** objFiles = (const char**)sob_arena_alloc(arena,
        (unsigned long long)t->sources.count * sizeof(const char*));
    if (!objFiles) { return -1; }

    int result = 0;
    if (canParallel) {
        sob_log(ctx, Sob_LogLevel_Info, "Parallel build with %d jobs...", jobCount);
        result = sob__compile_sources_parallel(t, objFiles, jobCount);
    } else {
        result = sob__compile_sources_serial(t, objFiles);
    }
    if (result != 0) {
        return result;
    }

    if (t->kind == Sob_TargetKind_ObjectFiles) {
        return 0;
    }

    if (t->kind == Sob_TargetKind_StaticLib) {
        if (canParallel) {
            sob_log(ctx, Sob_LogLevel_Info, "Archiving static library...");
        }
        Sob_Cmd* arCmd = sob__make_archive_cmd(t, objFiles, t->sources.count);
        if (!arCmd) { return -1; }
        return sob_cmd_run_(arCmd, (Sob_CmdOpts){0});
    }

    if (canParallel) {
        sob_log(ctx, Sob_LogLevel_Info, "Linking...");
    }

    Sob_Cmd* linkCmd = sob__make_link_cmd(t, objFiles, t->sources.count);
    if (!linkCmd) { return -1; }
    return sob_cmd_run_(linkCmd, (Sob_CmdOpts){0});
}

static int sob__build_target_recursive(Sob_Target* t, int* state, int targetIdx) {
    if (state[targetIdx] == 2) { return 0; }
    if (state[targetIdx] == 1) {
        sob_log(t->ctx, Sob_LogLevel_Error, "Cyclic dependency detected at target: %s", t->name);
        return -1;
    }
    state[targetIdx] = 1;
    
    for (int i = 0; i < t->depCount; i++) {
        Sob_Target* dep = t->deps[i];
        for (int j = 0; j < t->ctx->targetCount; j++) {
            if (t->ctx->targets[j] == dep) {
                int result = sob__build_target_recursive(dep, state, j);
                if (result != 0) { return result; }
                break;
            }
        }
    }
    
    int result = sob__build_target(t);
    state[targetIdx] = 2;
    return result;
}

SOBDEF int sob_build_run(Sob_BuildContext* ctx) {
    if (!ctx) { return -1; }
    
    ctx->errorCount = 0;
    
    int* state = (int*)sob_arena_alloc(ctx->arena, 
        (unsigned long long)ctx->targetCount * sizeof(int));
    if (!state) { return -1; }
    sob__memset(state, 0, (unsigned long long)ctx->targetCount * sizeof(int));
    
    for (int i = 0; i < ctx->targetCount; i++) {
        int result = sob__build_target_recursive(ctx->targets[i], state, i);
        if (result != 0) {
            ctx->errorCount++;
            sob_log(ctx, Sob_LogLevel_Error, "Failed to build target: %s", ctx->targets[i]->name);
        }
    }
    
    return ctx->errorCount;
}

SOBDEF int sob_build_error_count(Sob_BuildContext* ctx) {
    return ctx ? ctx->errorCount : 0;
}

SOBDEF void sob_build_set_log_level(Sob_BuildContext* ctx, Sob_LogLevel level) {
    if (ctx) { ctx->logLevel = level; }
}

SOBDEF void sob_log(Sob_BuildContext* ctx, Sob_LogLevel level, const char* fmt, ...) {
    if (!ctx || level < ctx->logLevel) { return; }
    
    const char* prefix = "";
    switch (level) {
        case Sob_LogLevel_Trace:   prefix = "[TRACE] "; break;
        case Sob_LogLevel_Info:    prefix = "[INFO] "; break;
        case Sob_LogLevel_Warning: prefix = "[WARN] "; break;
        case Sob_LogLevel_Error:   prefix = "[ERROR] "; break;
        case Sob_LogLevel_Silent:  return;
    }
    
    printf("%s", prefix);
    
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    
    printf("\n");
    fflush(stdout);
}

/* ========================================================================= */
/*                              ASSET EMBEDDING                              */
/* ========================================================================= */

typedef struct Sob_EmbedManifestEntry {
    const char* name;
    const char* dataSymbol;
    const char* sizeExpr;
} Sob_EmbedManifestEntry;

typedef struct Sob_EmbedManifest {
    Sob_EmbedManifestEntry* entries;
    int count;
    int capacity;
    Sob_Arena* arena;
} Sob_EmbedManifest;

static void sob__embed_manifest_push(Sob_EmbedManifest* manifest, const char* name,
                                     const char* dataSymbol, const char* sizeExpr) {
    if (!manifest || !manifest->arena || !name || !dataSymbol || !sizeExpr) {
        return;
    }

    if (SOB__VEC_ENSURE(manifest->arena, manifest->entries, manifest->count, manifest->capacity, 8) != 0) {
        return;
    }

    manifest->entries[manifest->count].name = name;
    manifest->entries[manifest->count].dataSymbol = dataSymbol;
    manifest->entries[manifest->count].sizeExpr = sizeExpr;
    manifest->count++;
}

static const char* sob__embed_size_expr(Sob_Arena* arena, const char* dataSymbol) {
    if (!arena || !dataSymbol) {
        return "";
    }

    const char* prefix = "(unsigned long)sizeof(";
    unsigned long long prefixLen = sob__strlen(prefix);
    unsigned long long symbolLen = sob__strlen(dataSymbol);
    char* expr = (char*)sob_arena_alloc(arena, prefixLen + symbolLen + 2);
    if (!expr) {
        return "";
    }

    sob__memcpy(expr, prefix, prefixLen);
    sob__memcpy(expr + prefixLen, dataSymbol, symbolLen);
    expr[prefixLen + symbolLen] = ')';
    expr[prefixLen + symbolLen + 1] = '\0';
    return expr;
}

static void sob__emit_c_string(FILE* outFile, const char* text) {
    fputc('"', outFile);
    if (text) {
        for (const char* p = text; *p; p++) {
            char c = *p;
            if (c == '\\' || c == '"') {
                fputc('\\', outFile);
                fputc(c, outFile);
            } else if (c == '\n') {
                fputs("\\n", outFile);
            } else if (c == '\r') {
                fputs("\\r", outFile);
            } else if (c == '\t') {
                fputs("\\t", outFile);
            } else {
                fputc(c, outFile);
            }
        }
    }
    fputc('"', outFile);
}

static void sob__embed_manifest_emit(FILE* outFile, Sob_EmbedManifest* manifest,
                                     const char* manifestName) {
    if (!outFile || !manifest || !manifestName || !*manifestName) {
        return;
    }

    fprintf(outFile, "#ifndef SOB_EMBED_ENTRY_DEFINED\n");
    fprintf(outFile, "#define SOB_EMBED_ENTRY_DEFINED\n");
    fprintf(outFile, "typedef struct Sob_EmbedEntry {\n");
    fprintf(outFile, "    const char* name;\n");
    fprintf(outFile, "    const unsigned char* data;\n");
    fprintf(outFile, "    unsigned long size;\n");
    fprintf(outFile, "} Sob_EmbedEntry;\n");
    fprintf(outFile, "#endif\n\n");

    fprintf(outFile, "static const Sob_EmbedEntry %s[] = {\n", manifestName);
    for (int i = 0; i < manifest->count; i++) {
        Sob_EmbedManifestEntry* entry = &manifest->entries[i];
        fprintf(outFile, "    { ");
        sob__emit_c_string(outFile, entry->name);
        fprintf(outFile, ", %s, %s },\n", entry->dataSymbol, entry->sizeExpr);
    }
    fprintf(outFile, "};\n");
    fprintf(outFile, "static const unsigned long %s_COUNT = %d;\n\n",
        manifestName, manifest->count);
}

static const char* sob__sanitize_name(Sob_Arena* arena, const char* name) {
    unsigned long long len = sob__strlen(name);
    char* out = (char*)sob_arena_alloc(arena, len + 1);
    if (!out) { return ""; }
    
    for (unsigned long long i = 0; i < len; i++) {
        char c = name[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
            (c >= '0' && c <= '9') || c == '_') {
            out[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;  /* uppercase */
        } else {
            out[i] = '_';
        }
    }
    out[len] = '\0';
    return out;
}

static int sob__embed_single_file(Sob_Arena* arena, const char* inputPath, 
                                   const char* varName, FILE* outFile,
                                   Sob_EmbedOpts opts) {
    FILE* inFile = fopen(inputPath, "rb");
    if (!inFile) {
        printf("[SOB] Error: Cannot open file: %s\n", inputPath);
        return -1;
    }
    
    if (fseek(inFile, 0, SEEK_END) != 0) {
        printf("[SOB] Error: Cannot seek file: %s\n", inputPath);
        fclose(inFile);
        return -1;
    }
    long size = ftell(inFile);
    if (size < 0) {
        printf("[SOB] Error: Cannot get file size: %s\n", inputPath);
        fclose(inFile);
        return -1;
    }
    if (fseek(inFile, 0, SEEK_SET) != 0) {
        printf("[SOB] Error: Cannot rewind file: %s\n", inputPath);
        fclose(inFile);
        return -1;
    }
    
    unsigned char* data = (unsigned char*)sob_arena_alloc(arena, (unsigned long long)size + 1);
    if (!data) {
        fclose(inFile);
        return -1;
    }
    if (fread(data, 1, (size_t)size, inFile) != (size_t)size) {
        printf("[SOB] Error: Failed to read file: %s\n", inputPath);
        fclose(inFile);
        return -1;
    }
    fclose(inFile);
    
    if (opts.alignment) {
#if defined(_MSC_VER)
        fprintf(outFile, "__declspec(align(%s)) ", opts.alignment);
#elif defined(__GNUC__) || defined(__clang__)
        fprintf(outFile, "__attribute__((aligned(%s))) ", opts.alignment);
#endif
    }
    fprintf(outFile, "static const unsigned char %s_DATA[] = {\n    ", varName);
    
    for (long i = 0; i < size; i++) {
        fprintf(outFile, "0x%02X", data[i]);
        if (i < size - 1 || opts.nullTerminate) {
            fprintf(outFile, ",");
        }
        if ((i + 1) % 16 == 0 && i < size - 1) {
            fprintf(outFile, "\n    ");
        } else if (i < size - 1) {
            fprintf(outFile, " ");
        }
    }
    
    if (opts.nullTerminate) {
        fprintf(outFile, " 0x00");
    }
    
    fprintf(outFile, "\n};\n");
    fprintf(outFile, "static const unsigned long %s_SIZE = %ld;\n\n", 
            varName, opts.nullTerminate ? size + 1 : size);
    
    return 0;
}

typedef struct Sob_EmbedFileContext {
    Sob_Arena* arena;
    FILE* outFile;
    const char* varName;
    Sob_EmbedOpts opts;
    Sob_EmbedManifest* manifest;
    int hadError;
} Sob_EmbedFileContext;

static void sob__embed_file_cb(void* userData, const char* fileName, const char* fullPath) {
    Sob_EmbedFileContext* ctx = (Sob_EmbedFileContext*)userData;
    if (!ctx || !fileName || !fullPath) {
        return;
    }

    const char* sanitized = sob__sanitize_name(ctx->arena, fileName);
    unsigned long long varLen = sob__strlen(ctx->varName);
    unsigned long long sanLen = sob__strlen(sanitized);
    char* fullVar = (char*)sob_arena_alloc(ctx->arena, varLen + sanLen + 1);
    if (!fullVar) {
        return;
    }
    sob__memcpy(fullVar, ctx->varName, varLen);
    sob__memcpy(fullVar + varLen, sanitized, sanLen + 1);

    if (sob__embed_single_file(ctx->arena, fullPath, fullVar, ctx->outFile, ctx->opts) != 0) {
        ctx->hadError = 1;
        return;
    }

    if (ctx->opts.manifest) {
        const char* entryName = sob__strdup(ctx->arena, fileName);
        const char* dataSymbol = sob__flag_with_value(ctx->arena, fullVar, "_DATA");
        const char* sizeExpr = sob__embed_size_expr(ctx->arena, dataSymbol);
        if (dataSymbol && dataSymbol[0] && sizeExpr && sizeExpr[0]) {
            sob__embed_manifest_push(ctx->manifest, entryName, dataSymbol, sizeExpr);
        }
    }
}

SOBDEF void sob_embed_(Sob_Arena* arena, const char* input, const char* varName,
                        const char* output, Sob_EmbedOpts opts) {
    if (!arena || !input || !varName || !output) { return; }

    if (opts.compress) {
        printf("[SOB] Error: Compression not supported (compress=1)\n");
        return;
    }
    
    Sob_EmbedManifest manifest;
    sob__memset(&manifest, 0, sizeof(manifest));
    manifest.arena = arena;

    const char* manifestName = 0;
    if (opts.manifest) {
        manifestName = opts.manifestName;
        if (!manifestName || !*manifestName) {
            manifestName = varName;
        }
        if (!manifestName || !*manifestName) {
            manifestName = "EMBED_MANIFEST";
        }
    }

    const char* lastSlash = output;
    const char* p = output;
    while (*p) {
        if (*p == '/' || *p == '\\') { lastSlash = p; }
        p++;
    }
    if (lastSlash != output) {
        unsigned long long dirLen = (unsigned long long)(lastSlash - output);
        char* dir = (char*)sob_arena_alloc(arena, dirLen + 1);
        sob__memcpy(dir, output, dirLen);
        dir[dirLen] = '\0';
        sob_fs_mkdir_p(dir);
    }
    
    FILE* outFile = fopen(output, "w");
    if (!outFile) {
        printf("[SOB] Error: Cannot create output file: %s\n", output);
        return;
    }
    int hadError = 0;
    
    fprintf(outFile, "/* Auto-generated by sob.h - DO NOT EDIT */\n\n");
    
    if (sob_fs_is_dir(input)) {
        printf("[SOB] Embedding directory: %s -> %s\n", input, output);
        Sob_EmbedFileContext ctx;
        ctx.arena = arena;
        ctx.outFile = outFile;
        ctx.varName = varName;
        ctx.opts = opts;
        ctx.manifest = &manifest;
        ctx.hadError = 0;
        sob__for_each_file(arena, input, opts.extension, sob__embed_file_cb, &ctx);
        hadError = ctx.hadError;
    } else {
        printf("[SOB] Embedding file: %s -> %s\n", input, output);
        if (sob__embed_single_file(arena, input, varName, outFile, opts) != 0) {
            hadError = 1;
        }

        if (!hadError && opts.manifest) {
            const char* baseName = sob_path_basename(input);
            const char* entryName = sob__strdup(arena, baseName);
            const char* dataSymbol = sob__flag_with_value(arena, varName, "_DATA");
            const char* sizeExpr = sob__embed_size_expr(arena, dataSymbol);
            if (dataSymbol && dataSymbol[0] && sizeExpr && sizeExpr[0]) {
                sob__embed_manifest_push(&manifest, entryName, dataSymbol, sizeExpr);
            }
        }
    }

    if (!hadError && opts.manifest) {
        sob__embed_manifest_emit(outFile, &manifest, manifestName);
    }
    
    fclose(outFile);
    if (hadError) {
        sob_fs_remove(output);
    }
}

/* ========================================================================= */
/*                              GO REBUILD URSELF                            */
/* ========================================================================= */

SOBDEF U64 sob_fs_newest_mtime(const char* const* paths, S32 count) {
    U64 newest = 0;
    for (S32 i = 0; i < count; ++i) {
        U64 time = sob_fs_mtime(paths[i]);
        if (time > newest) {
            newest = time;
        }
    }
    return newest;
}

static char sob__bootstrapScratchDir[768];
static char sob__bootstrapRebuiltExe[1024];

#if SOB_WINDOWS

static char sob__bootstrapRebuiltObj[1024];
static char sob__bootstrapVsdevBatch[1024];
static char sob__bootstrapReplaceBatch[1024];

SOBDEF S32 sob_command_exists(const char* name) {
    char path[MAX_PATH];
    DWORD length = SearchPathA(0, name, 0, (DWORD)sizeof(path), path, 0);
    return (length > 0 && length < (DWORD)sizeof(path));
}

static int sob__msvc_environment_ready(void) {
    const char* vscmd = getenv("VSCMD_VER");
    return (vscmd && vscmd[0] != 0 && sob_command_exists("cl.exe"));
}

static void sob__trim_line(char* str) {
    if (!str) {
        return;
    }

    size_t length = strlen(str);
    while (length > 0) {
        char c = str[length - 1];
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t') {
            break;
        }
        str[--length] = 0;
    }
}

static int sob__win_full_path(const char* path, char* out, size_t outSize) {
    if (!path || !out || outSize == 0) {
        return 0;
    }

    DWORD length = GetFullPathNameA(path, (DWORD)outSize, out, 0);
    return (length > 0 && length < (DWORD)outSize);
}

static int sob__try_vsdev_from_install(const char* installPath, char* outPath, size_t outPathSize) {
    if (!installPath || !installPath[0] || !outPath || outPathSize == 0) {
        return 0;
    }

    int length = snprintf(outPath, outPathSize, "%s\\Common7\\Tools\\VsDevCmd.bat", installPath);
    return (length > 0 && length < (int)outPathSize && sob_fs_exists(outPath));
}

static int sob__find_vsdev_with_vswhere(char* outPath, size_t outPathSize) {
    const char* programFilesX86 = getenv("ProgramFiles(x86)");
    if (!programFilesX86 || !programFilesX86[0]) {
        return 0;
    }

    char vswherePath[2048];
    int vswhereLength = snprintf(vswherePath,
                                 sizeof(vswherePath),
                                 "%s\\Microsoft Visual Studio\\Installer\\vswhere.exe",
                                 programFilesX86);
    if (vswhereLength <= 0 || vswhereLength >= (int)sizeof(vswherePath) || !sob_fs_exists(vswherePath)) {
        return 0;
    }

    char command[4096];
    int commandLength = snprintf(command,
                                 sizeof(command),
                                 "\"%s\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath",
                                 vswherePath);
    if (commandLength <= 0 || commandLength >= (int)sizeof(command)) {
        return 0;
    }

    FILE* pipe = _popen(command, "r");
    if (!pipe) {
        return 0;
    }

    char installPath[2048] = {0};
    int found = 0;
    if (fgets(installPath, sizeof(installPath), pipe)) {
        sob__trim_line(installPath);
        found = sob__try_vsdev_from_install(installPath, outPath, outPathSize);
    }

    _pclose(pipe);
    return found;
}

static int sob__find_vsdev_fallback(char* outPath, size_t outPathSize) {
    const char* programFiles = getenv("ProgramFiles");
    const char* programFilesX86 = getenv("ProgramFiles(x86)");
    const char* editions[] = {"Community", "Professional", "Enterprise", "BuildTools"};
    const char* roots[2] = {programFiles, programFilesX86};

    for (int rootIndex = 0; rootIndex < (int)(sizeof(roots) / sizeof(roots[0])); ++rootIndex) {
        const char* root = roots[rootIndex];
        if (!root || !root[0]) {
            continue;
        }

        for (int editionIndex = 0; editionIndex < (int)(sizeof(editions) / sizeof(editions[0])); ++editionIndex) {
            int length = snprintf(outPath,
                                  outPathSize,
                                  "%s\\Microsoft Visual Studio\\2022\\%s\\Common7\\Tools\\VsDevCmd.bat",
                                  root,
                                  editions[editionIndex]);
            if (length > 0 && length < (int)outPathSize && sob_fs_exists(outPath)) {
                return 1;
            }
        }
    }

    if (programFilesX86 && programFilesX86[0]) {
        int length = snprintf(outPath,
                              outPathSize,
                              "%s\\Microsoft Visual Studio\\18\\BuildTools\\Common7\\Tools\\VsDevCmd.bat",
                              programFilesX86);
        if (length > 0 && length < (int)outPathSize && sob_fs_exists(outPath)) {
            return 1;
        }
    }

    return 0;
}

static int sob__find_vsdev_bat(char* outPath, size_t outPathSize) {
    if (sob__find_vsdev_with_vswhere(outPath, outPathSize)) {
        return 1;
    }
    return sob__find_vsdev_fallback(outPath, outPathSize);
}

static void sob__batch_write_quoted_arg(FILE* file, const char* arg) {
    fputc('"', file);
    if (arg) {
        for (const char* p = arg; *p; ++p) {
            if (*p == '%') {
                fputs("%%", file);
            } else if (*p == '"') {
                fputs("\\\"", file);
            } else {
                fputc(*p, file);
            }
        }
    }
    fputc('"', file);
}

static int sob__write_vsdev_batch(const char* vsdevPath, int argc, char** argv) {
    char exePath[2048];
    const char* launchPath = argv[0];
    if (sob__win_full_path(argv[0], exePath, sizeof(exePath))) {
        launchPath = exePath;
    }

    FILE* file = fopen(sob__bootstrapVsdevBatch, "wb");
    if (!file) {
        return 0;
    }

    fputs("@echo off\r\n", file);
    fputs("setlocal\r\n", file);
    fputs("set \"SOB_VSDEV_BOOTSTRAPPED=1\"\r\n", file);
    fputs("call ", file);
    sob__batch_write_quoted_arg(file, vsdevPath);
    fputs(" -arch=x64 -host_arch=x64\r\n", file);
    fputs("if errorlevel 1 exit /b %errorlevel%\r\n", file);
    sob__batch_write_quoted_arg(file, launchPath);
    for (int i = 1; i < argc; ++i) {
        fputc(' ', file);
        sob__batch_write_quoted_arg(file, argv[i]);
    }
    fputs("\r\nexit /b %errorlevel%\r\n", file);

    fclose(file);
    return 1;
}

static S32 sob__run_batch_wait(const char* batchPath) {
    Sob_Arena* arena = sob_arena_create();
    if (!arena) {
        return 1;
    }

    Sob_Cmd* cmd = sob_cmd_create(arena);
    if (!cmd) {
        sob_arena_destroy(arena);
        return 1;
    }

    sob_cmd_append(cmd, "cmd.exe");
    sob_cmd_append(cmd, "/s");
    sob_cmd_append(cmd, "/c");
    sob_cmd_append(cmd, batchPath);

    S32 result = sob_cmd_run_(cmd, (Sob_CmdOpts){0});
    sob_arena_destroy(arena);
    return result;
}

static int sob__win_bootstrap_msvc_environment(int argc, char** argv, S32* outExitCode) {
    if (sob__msvc_environment_ready()) {
        return 0;
    }

    if (getenv("SOB_VSDEV_BOOTSTRAPPED")) {
        fprintf(stderr,
                "Error: Visual Studio C++ environment was entered, but cl.exe is still not usable.\n");
        fprintf(stderr,
                "Could not find Visual Studio C++ tools. Install Build Tools or run from a VS Developer shell.\n");
        *outExitCode = 1;
        return 1;
    }

    char vsdevPath[2048];
    if (!sob__find_vsdev_bat(vsdevPath, sizeof(vsdevPath))) {
        fprintf(stderr, "Error: Could not find Visual Studio C++ tools. Install Build Tools or run from a VS Developer shell.\n");
        *outExitCode = 1;
        return 1;
    }

    if (sob_fs_mkdir_p(sob__bootstrapScratchDir) != 0 && !sob_fs_is_dir(sob__bootstrapScratchDir)) {
        fprintf(stderr, "Error: failed to create '%s'\n", sob__bootstrapScratchDir);
        *outExitCode = 1;
        return 1;
    }

    if (!sob__write_vsdev_batch(vsdevPath, argc, argv)) {
        fprintf(stderr, "Error: failed to write '%s'\n", sob__bootstrapVsdevBatch);
        *outExitCode = 1;
        return 1;
    }

    printf("==> Entering Visual Studio C++ environment...\n");
    fflush(stdout);
    *outExitCode = sob__run_batch_wait(sob__bootstrapVsdevBatch);
    return 1;
}

static int sob__write_replace_batch(const char* exePath) {
    char fullExePath[2048];
    const char* targetPath = exePath;
    if (sob__win_full_path(exePath, fullExePath, sizeof(fullExePath))) {
        targetPath = fullExePath;
    }

    FILE* file = fopen(sob__bootstrapReplaceBatch, "wb");
    if (!file) {
        return 0;
    }

    fputs("@echo off\r\n", file);
    fputs("setlocal\r\n", file);
    fputs("set tries=0\r\n", file);
    fputs(":retry\r\n", file);
    fputs("copy /Y ", file);
    sob__batch_write_quoted_arg(file, sob__bootstrapRebuiltExe);
    fputc(' ', file);
    sob__batch_write_quoted_arg(file, targetPath);
    fputs(" >nul\r\n", file);
    fputs("if not errorlevel 1 goto done\r\n", file);
    fputs("set /a tries+=1 >nul\r\n", file);
    fputs("if %tries% geq 30 exit /b 1\r\n", file);
    fputs("timeout /t 1 /nobreak >nul\r\n", file);
    fputs("goto retry\r\n", file);
    fputs(":done\r\n", file);
    fputs("del ", file);
    sob__batch_write_quoted_arg(file, sob__bootstrapRebuiltExe);
    fputs(" >nul 2>nul\r\n", file);
    fputs("exit /b 0\r\n", file);

    fclose(file);
    return 1;
}

static void sob__schedule_self_replacement(const char* exePath) {
    if (!sob__write_replace_batch(exePath)) {
        fprintf(stderr, "Warning: failed to write '%s'; '%s' was not replaced.\n",
                sob__bootstrapReplaceBatch,
                exePath);
        return;
    }

    char commandLine[2048];
    int length = snprintf(commandLine, sizeof(commandLine), "cmd.exe /s /c \"%s\"", sob__bootstrapReplaceBatch);
    if (length <= 0 || length >= (int)sizeof(commandLine)) {
        fprintf(stderr, "Warning: replacement command was too long; '%s' was not replaced.\n", exePath);
        return;
    }

    STARTUPINFOA startupInfo;
    PROCESS_INFORMATION processInfo;
    memset(&startupInfo, 0, sizeof(startupInfo));
    memset(&processInfo, 0, sizeof(processInfo));
    startupInfo.cb = sizeof(startupInfo);

    if (!CreateProcessA(0, commandLine, 0, 0, FALSE, 0, 0, 0, &startupInfo, &processInfo)) {
        fprintf(stderr, "Warning: failed to schedule replacement of '%s'.\n", exePath);
        return;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
}

static int sob__win_self_rebuild(int argc, char** argv,
                                 const char* const* inputs, S32 inputCount,
                                 S32* outExitCode) {
    if (getenv("SOB_SKIP_SELF_REBUILD")) {
        return 0;
    }

    const char* exePath = argv[0];
    U64 exeTime = sob_fs_mtime(exePath);
    U64 inputTime = sob_fs_newest_mtime(inputs, inputCount);
    if (inputTime == 0 || exeTime == 0 || inputTime <= exeTime) {
        return 0;
    }

    if (sob_fs_mkdir_p(sob__bootstrapScratchDir) != 0 && !sob_fs_is_dir(sob__bootstrapScratchDir)) {
        fprintf(stderr, "Error: failed to create '%s'\n", sob__bootstrapScratchDir);
        *outExitCode = 1;
        return 1;
    }

    printf("[SOB] Rebuilding %s...\n", exePath);
    Sob_Arena* arena = sob_arena_create();
    if (!arena) {
        *outExitCode = 1;
        return 1;
    }

    Sob_Cmd* buildCmd = sob_cmd_create(arena);
    if (!buildCmd) {
        sob_arena_destroy(arena);
        *outExitCode = 1;
        return 1;
    }

    char feArg[1100];
    char foArg[1100];
    snprintf(feArg, sizeof(feArg), "/Fe:%s", sob__bootstrapRebuiltExe);
    snprintf(foArg, sizeof(foArg), "/Fo:%s", sob__bootstrapRebuiltObj);

    sob_fs_remove(sob__bootstrapRebuiltExe);
    sob_cmd_append(buildCmd, "cl");
    sob_cmd_append(buildCmd, "/nologo");
    sob_cmd_append(buildCmd, "/W4");
    sob_cmd_append(buildCmd, "/wd4100");
    sob_cmd_append(buildCmd, "/wd4189");
    sob_cmd_append(buildCmd, "/wd4505");
    sob_cmd_append(buildCmd, "/wd4996");
    sob_cmd_append(buildCmd, feArg);
    sob_cmd_append(buildCmd, foArg);
    sob_cmd_append(buildCmd, inputs[0]);

    S32 buildResult = sob_cmd_run_(buildCmd, (Sob_CmdOpts){0});
    if (buildResult != 0) {
        sob_arena_destroy(arena);
        fprintf(stderr, "[SOB] Rebuild failed.\n");
        *outExitCode = buildResult;
        return 1;
    }

    Sob_Cmd* runCmd = sob_cmd_create(arena);
    if (!runCmd) {
        sob_arena_destroy(arena);
        *outExitCode = 1;
        return 1;
    }

    _putenv("SOB_SKIP_SELF_REBUILD=1");
    sob_cmd_append(runCmd, sob__bootstrapRebuiltExe);
    for (int i = 1; i < argc; ++i) {
        sob_cmd_append(runCmd, argv[i]);
    }

    printf("[SOB] Running rebuilt build driver...\n");
    S32 runResult = sob_cmd_run_(runCmd, (Sob_CmdOpts){0});
    sob_arena_destroy(arena);

    sob__schedule_self_replacement(exePath);
    *outExitCode = runResult;
    return 1;
}

#else /* !SOB_WINDOWS */

static int sob__posix_self_rebuild(int argc, char** argv,
                                   const char* const* inputs, S32 inputCount,
                                   S32* outExitCode) {
    if (getenv("SOB_SKIP_SELF_REBUILD")) {
        return 0;
    }

    const char* exePath = argv[0];
    U64 exeTime = sob_fs_mtime(exePath);
    U64 inputTime = sob_fs_newest_mtime(inputs, inputCount);
    if (inputTime == 0u || exeTime == 0u || inputTime <= exeTime) {
        return 0;
    }

    if (sob_fs_mkdir_p(sob__bootstrapScratchDir) != 0 && !sob_fs_is_dir(sob__bootstrapScratchDir)) {
        fprintf(stderr, "Error: failed to create '%s'\n", sob__bootstrapScratchDir);
        *outExitCode = 1;
        return 1;
    }

    printf("[SOB] Rebuilding %s...\n", exePath);
    Sob_Arena* arena = sob_arena_create();
    if (!arena) {
        *outExitCode = 1;
        return 1;
    }

    Sob_Cmd* buildCmd = sob_cmd_create(arena);
    if (!buildCmd) {
        sob_arena_destroy(arena);
        *outExitCode = 1;
        return 1;
    }

    const char* compiler = getenv("CC");
    if (!compiler || compiler[0] == 0) {
        compiler = "cc";
    }

    sob_fs_remove(sob__bootstrapRebuiltExe);
    sob_cmd_append(buildCmd, compiler);
    sob_cmd_append(buildCmd, inputs[0]);
    sob_cmd_append(buildCmd, "-o");
    sob_cmd_append(buildCmd, sob__bootstrapRebuiltExe);

    S32 buildResult = sob_cmd_run_(buildCmd, (Sob_CmdOpts){0});
    if (buildResult != 0) {
        sob_arena_destroy(arena);
        fprintf(stderr, "[SOB] Rebuild failed.\n");
        *outExitCode = buildResult;
        return 1;
    }

    if (rename(sob__bootstrapRebuiltExe, exePath) != 0) {
        sob_arena_destroy(arena);
        fprintf(stderr, "[SOB] Failed to replace '%s'.\n", exePath);
        *outExitCode = 1;
        return 1;
    }

    setenv("SOB_SKIP_SELF_REBUILD", "1", 1);
    printf("[SOB] Re-executing...\n");
    fflush(stdout);
    execvp(exePath, argv);

    sob_arena_destroy(arena);
    fprintf(stderr, "[SOB] Failed to re-execute '%s'.\n", exePath);
    *outExitCode = 1;
    return 1;
}

#endif /* SOB_WINDOWS */

SOBDEF S32 sob_bootstrap(int argc, char** argv,
                         const char* const* inputs, S32 inputCount,
                         const char* scratchDir, S32* outExitCode) {
    if (!argv || !argv[0] || !inputs || inputCount <= 0 || !inputs[0] || !scratchDir) {
        return 0;
    }

    snprintf(sob__bootstrapScratchDir, sizeof(sob__bootstrapScratchDir), "%s", scratchDir);
#if SOB_WINDOWS
    char nativeDir[768];
    snprintf(nativeDir, sizeof(nativeDir), "%s", scratchDir);
    for (char* p = nativeDir; *p; ++p) {
        if (*p == '/') {
            *p = '\\';
        }
    }
    snprintf(sob__bootstrapRebuiltExe, sizeof(sob__bootstrapRebuiltExe), "%s\\sob_rebuilt.exe", nativeDir);
    snprintf(sob__bootstrapRebuiltObj, sizeof(sob__bootstrapRebuiltObj), "%s\\sob_rebuilt.obj", nativeDir);
    snprintf(sob__bootstrapVsdevBatch, sizeof(sob__bootstrapVsdevBatch), "%s\\sob_vsdev_launch.bat", nativeDir);
    snprintf(sob__bootstrapReplaceBatch, sizeof(sob__bootstrapReplaceBatch), "%s\\sob_replace.bat", nativeDir);

    if (sob__win_bootstrap_msvc_environment(argc, argv, outExitCode)) {
        return 1;
    }
    return sob__win_self_rebuild(argc, argv, inputs, inputCount, outExitCode);
#else
    snprintf(sob__bootstrapRebuiltExe, sizeof(sob__bootstrapRebuiltExe), "%s/sob_rebuilt", scratchDir);
    return sob__posix_self_rebuild(argc, argv, inputs, inputCount, outExitCode);
#endif
}

#endif /* SOB_IMPLEMENTATION */
