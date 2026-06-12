#define SOB_IMPLEMENTATION
#include "third_party/sob/sob.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if SOB_MACOS
#include <unistd.h>
#endif

#include "engine/shaders/shader_manifest.h"

#define BUILD_DIR "build"
#define BUILD_TOOLS_DIR "build/tools"
#define BUILD_HOT_DIR "build/hot"
#define META_DIR "meta"

#define HOST_EXE_BASENAME "utilities_host"

// Active project: products live under projects/<name>/ with a
// <name>_main.cpp TU root. sob records the active name at module build
// time so the host rebuilds the same one.
static const char* g_project = "demo";
static char g_projectMainPath[512];

static int project_exists(const char* name) {
    snprintf(g_projectMainPath, sizeof(g_projectMainPath), "projects/%s/%s_main.cpp", name, name);
    return sob_fs_exists(g_projectMainPath);
}

static void write_module_project_record(void) {
    FILE* file = fopen("build/module_project.txt", "wb");
    if (file) {
        fprintf(file, "%s\n", g_project);
        fclose(file);
    }
}
#define HOST_IMPORT_LIB_PATH BUILD_DIR "/" HOST_EXE_BASENAME ".lib"
#if SOB_WINDOWS
#define HOST_OUTPUT_PATH BUILD_DIR "/" HOST_EXE_BASENAME ".exe"
#define HOST_RUN_PATH BUILD_DIR "\\" HOST_EXE_BASENAME ".exe"
#else
#define HOST_OUTPUT_PATH BUILD_DIR "/" HOST_EXE_BASENAME
#define HOST_RUN_PATH HOST_OUTPUT_PATH
#endif

#define METAGEN_EXE_BASENAME "metagen"
#if SOB_WINDOWS
#define METAGEN_OUTPUT_PATH BUILD_TOOLS_DIR "/" METAGEN_EXE_BASENAME ".exe"
#define METAGEN_RUN_PATH BUILD_DIR "\\tools\\" METAGEN_EXE_BASENAME ".exe"
#define METAGEN_INPUT_PATH "."
#else
#define METAGEN_OUTPUT_PATH BUILD_TOOLS_DIR "/" METAGEN_EXE_BASENAME
#define METAGEN_RUN_PATH "../" METAGEN_OUTPUT_PATH
#define METAGEN_INPUT_PATH ".."
#endif

#define SOB_STRINGIZE_(x) #x
#define SOB_STRINGIZE(x) SOB_STRINGIZE_(x)

#if SOB_WINDOWS
#define SOB_REBUILT_EXE_PATH "build\\tools\\sob_rebuilt.exe"
#define SOB_REBUILT_OBJ_PATH "build\\tools\\sob_rebuilt.obj"
#define SOB_VSDEV_BATCH_PATH "build\\tools\\sob_vsdev_launch.bat"
#define SOB_REPLACE_BATCH_PATH "build\\tools\\sob_replace.bat"
#define VULKAN_VENDOR_ROOT "third_party/vulkan"
#define VULKAN_VENDOR_INCLUDE_DIR VULKAN_VENDOR_ROOT "/include"
#define VULKAN_VENDOR_LOADER_DIR VULKAN_VENDOR_ROOT "/loader"
#define VULKAN_VENDOR_LOADER_GENERATED_DIR VULKAN_VENDOR_LOADER_DIR "/generated"
#define VULKAN_VENDOR_LIB_DIR VULKAN_VENDOR_ROOT "/lib/win64"
#define VULKAN_VENDOR_STATIC_LOADER_LIB VULKAN_VENDOR_LIB_DIR "/VKstatic.1.lib"
#define VULKAN_LOADER_OBJ_DIR BUILD_TOOLS_DIR "/vulkan_loader"
#endif

#if SOB_WINDOWS
#define SLANG_VENDOR_PATH "third_party/slang/bin/win64/slangc.exe"
#elif SOB_MACOS
#define SLANG_VENDOR_PATH "third_party/slang/bin/macos/slangc"
#else
#define SLANG_VENDOR_PATH "third_party/slang/bin/linux/slangc"
#endif

static U64 newest_sob_input_mtime(void) {
    U64 result = sob_fs_mtime("sob.c");
    U64 sobHeaderTime = sob_fs_mtime("third_party/sob/sob.h");
    if (sobHeaderTime > result) {
        result = sobHeaderTime;
    }
    U64 shaderManifestTime = sob_fs_mtime(ENG_SHADER_MANIFEST_SOURCE);
    if (shaderManifestTime > result) {
        result = shaderManifestTime;
    }
    return result;
}

typedef enum BuildMode {
    BuildMode_Debug,
    BuildMode_Asan,
    BuildMode_Release,
} BuildMode;

typedef enum BuildTarget {
    BuildTarget_Run,
    BuildTarget_All,
    BuildTarget_Dev,
    BuildTarget_Host,
    BuildTarget_Module,
    BuildTarget_Ship,
    BuildTarget_Metagen,
    BuildTarget_Shaders,
    BuildTarget_Cook,
    BuildTarget_Test,
    BuildTarget_Clean,
} BuildTarget;

static void print_usage(void) {
    printf("Usage: ./sob [target] [mode]\n");
    printf("\n");
    printf("Targets:\n");
    printf("  run      Build host + hot module and start the app (default)\n");
    printf("Usage: ./sob [target] [mode] [project]   (project defaults to demo)\n");
    printf("  dev      Build host + hot module\n");
    printf("  all      Alias for dev\n");
    printf("  host     Build host executable only\n");
    printf("  module   Build hot-reload module only\n");
    printf("  ship     Build one executable with app statically linked\n");
    printf("  metagen  Build metagen and regenerate metadata\n");
    printf("  shaders  Build reloadable shader artifacts\n");
    printf("  cook     Build the asset cooker and cook projects/demo/assets/src\n");
    printf("  test     Build and run the CPU seam tests\n");
    printf("  clean    Remove build artifacts\n");
    printf("\n");
    printf("Modes:\n");
    printf("  debug    Debug build (default)\n");
    printf("  asan     Debug build with Address Sanitizer\n");
    printf("  release  Release build\n");
}

static int parse_mode(const char* value, BuildMode* outMode) {
    if (!value || !outMode) {
        return 0;
    }

    if (strcmp(value, "debug") == 0) {
        *outMode = BuildMode_Debug;
        return 1;
    }
    if (strcmp(value, "asan") == 0) {
        *outMode = BuildMode_Asan;
        return 1;
    }
    if (strcmp(value, "release") == 0) {
        *outMode = BuildMode_Release;
        return 1;
    }

    return 0;
}

static int parse_target(const char* value, BuildTarget* outTarget) {
    if (!value || !outTarget) {
        return 0;
    }

    if (strcmp(value, "run") == 0) {
        *outTarget = BuildTarget_Run;
        return 1;
    }
    if (strcmp(value, "all") == 0) {
        *outTarget = BuildTarget_All;
        return 1;
    }
    if (strcmp(value, "dev") == 0) {
        *outTarget = BuildTarget_Dev;
        return 1;
    }
    if (strcmp(value, "host") == 0) {
        *outTarget = BuildTarget_Host;
        return 1;
    }
    if (strcmp(value, "module") == 0) {
        *outTarget = BuildTarget_Module;
        return 1;
    }
    if (strcmp(value, "ship") == 0) {
        *outTarget = BuildTarget_Ship;
        return 1;
    }
    if (strcmp(value, "metagen") == 0) {
        *outTarget = BuildTarget_Metagen;
        return 1;
    }
    if (strcmp(value, "cook") == 0) {
        *outTarget = BuildTarget_Cook;
        return 1;
    }
    if (strcmp(value, "test") == 0) {
        *outTarget = BuildTarget_Test;
        return 1;
    }
    if (strcmp(value, "shaders") == 0) {
        *outTarget = BuildTarget_Shaders;
        return 1;
    }
    if (strcmp(value, "clean") == 0) {
        *outTarget = BuildTarget_Clean;
        return 1;
    }

    return 0;
}

static const char* build_mode_name(BuildMode mode) {
    return (mode == BuildMode_Release) ? "release" : ((mode == BuildMode_Asan) ? "asan" : "debug");
}

static const char* build_target_name(BuildTarget target) {
    if (target == BuildTarget_Run) {
        return "run";
    }
    if (target == BuildTarget_All || target == BuildTarget_Dev) {
        return "dev";
    }
    if (target == BuildTarget_Host) {
        return "host";
    }
    if (target == BuildTarget_Module) {
        return "module";
    }
    if (target == BuildTarget_Ship) {
        return "ship";
    }
    if (target == BuildTarget_Metagen) {
        return "metagen";
    }
    if (target == BuildTarget_Cook) {
        return "cook";
    }
    if (target == BuildTarget_Test) {
        return "test";
    }
    if (target == BuildTarget_Shaders) {
        return "shaders";
    }
    return "clean";
}

#if SOB_WINDOWS
static int windows_command_exists(const char* name) {
    char path[MAX_PATH];
    DWORD length = SearchPathA(0, name, 0, (DWORD)sizeof(path), path, 0);
    return (length > 0 && length < (DWORD)sizeof(path));
}

static int windows_msvc_environment_ready(void) {
    const char* vscmd = getenv("VSCMD_VER");
    return (vscmd && vscmd[0] != 0 && windows_command_exists("cl.exe"));
}

static void windows_trim_line(char* str) {
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

static int windows_get_full_path(const char* path, char* out, size_t outSize) {
    if (!path || !out || outSize == 0) {
        return 0;
    }

    DWORD length = GetFullPathNameA(path, (DWORD)outSize, out, 0);
    return (length > 0 && length < (DWORD)outSize);
}

static int windows_try_vsdev_from_install(const char* installPath, char* outPath, size_t outPathSize) {
    if (!installPath || !installPath[0] || !outPath || outPathSize == 0) {
        return 0;
    }

    int length = snprintf(outPath, outPathSize, "%s\\Common7\\Tools\\VsDevCmd.bat", installPath);
    return (length > 0 && length < (int)outPathSize && sob_fs_exists(outPath));
}

static int windows_find_vsdev_with_vswhere(char* outPath, size_t outPathSize) {
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
        windows_trim_line(installPath);
        found = windows_try_vsdev_from_install(installPath, outPath, outPathSize);
    }

    _pclose(pipe);
    return found;
}

static int windows_find_vsdev_fallback(char* outPath, size_t outPathSize) {
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

static int windows_find_vsdev_bat(char* outPath, size_t outPathSize) {
    if (windows_find_vsdev_with_vswhere(outPath, outPathSize)) {
        return 1;
    }
    return windows_find_vsdev_fallback(outPath, outPathSize);
}

static void windows_batch_write_quoted_arg(FILE* file, const char* arg) {
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

static int windows_write_vsdev_batch(const char* vsdevPath, int argc, char** argv) {
    char exePath[2048];
    const char* launchPath = argv[0];
    if (windows_get_full_path(argv[0], exePath, sizeof(exePath))) {
        launchPath = exePath;
    }

    FILE* file = fopen(SOB_VSDEV_BATCH_PATH, "wb");
    if (!file) {
        return 0;
    }

    fputs("@echo off\r\n", file);
    fputs("setlocal\r\n", file);
    fputs("set \"SOB_VSDEV_BOOTSTRAPPED=1\"\r\n", file);
    fputs("call ", file);
    windows_batch_write_quoted_arg(file, vsdevPath);
    fputs(" -arch=x64 -host_arch=x64\r\n", file);
    fputs("if errorlevel 1 exit /b %errorlevel%\r\n", file);
    windows_batch_write_quoted_arg(file, launchPath);
    for (int i = 1; i < argc; ++i) {
        fputc(' ', file);
        windows_batch_write_quoted_arg(file, argv[i]);
    }
    fputs("\r\nexit /b %errorlevel%\r\n", file);

    fclose(file);
    return 1;
}

static S32 windows_run_batch_wait(const char* batchPath) {
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

    S32 result = sob_cmd_run(cmd);
    sob_arena_destroy(arena);
    return result;
}

static int windows_bootstrap_msvc_environment_if_needed(int argc, char** argv, S32* outExitCode) {
    if (windows_msvc_environment_ready()) {
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
    if (!windows_find_vsdev_bat(vsdevPath, sizeof(vsdevPath))) {
        fprintf(stderr, "Error: Could not find Visual Studio C++ tools. Install Build Tools or run from a VS Developer shell.\n");
        *outExitCode = 1;
        return 1;
    }

    if (sob_fs_mkdir_p(BUILD_TOOLS_DIR) != 0 && !sob_fs_is_dir(BUILD_TOOLS_DIR)) {
        fprintf(stderr, "Error: failed to create '%s'\n", BUILD_TOOLS_DIR);
        *outExitCode = 1;
        return 1;
    }

    if (!windows_write_vsdev_batch(vsdevPath, argc, argv)) {
        fprintf(stderr, "Error: failed to write '%s'\n", SOB_VSDEV_BATCH_PATH);
        *outExitCode = 1;
        return 1;
    }

    printf("==> Entering Visual Studio C++ environment...\n");
    fflush(stdout);
    *outExitCode = windows_run_batch_wait(SOB_VSDEV_BATCH_PATH);
    return 1;
}

static int windows_write_replace_batch(const char* exePath) {
    char fullExePath[2048];
    const char* targetPath = exePath;
    if (windows_get_full_path(exePath, fullExePath, sizeof(fullExePath))) {
        targetPath = fullExePath;
    }

    FILE* file = fopen(SOB_REPLACE_BATCH_PATH, "wb");
    if (!file) {
        return 0;
    }

    fputs("@echo off\r\n", file);
    fputs("setlocal\r\n", file);
    fputs("set tries=0\r\n", file);
    fputs(":retry\r\n", file);
    fputs("copy /Y ", file);
    windows_batch_write_quoted_arg(file, SOB_REBUILT_EXE_PATH);
    fputc(' ', file);
    windows_batch_write_quoted_arg(file, targetPath);
    fputs(" >nul\r\n", file);
    fputs("if not errorlevel 1 goto done\r\n", file);
    fputs("set /a tries+=1 >nul\r\n", file);
    fputs("if %tries% geq 30 exit /b 1\r\n", file);
    fputs("timeout /t 1 /nobreak >nul\r\n", file);
    fputs("goto retry\r\n", file);
    fputs(":done\r\n", file);
    fputs("del ", file);
    windows_batch_write_quoted_arg(file, SOB_REBUILT_EXE_PATH);
    fputs(" >nul 2>nul\r\n", file);
    fputs("exit /b 0\r\n", file);

    fclose(file);
    return 1;
}

static void windows_schedule_sob_replacement(const char* exePath) {
    if (!windows_write_replace_batch(exePath)) {
        fprintf(stderr, "Warning: failed to write '%s'; '%s' was not replaced.\n",
                SOB_REPLACE_BATCH_PATH,
                exePath);
        return;
    }

    char commandLine[2048];
    int length = snprintf(commandLine, sizeof(commandLine), "cmd.exe /s /c \"%s\"", SOB_REPLACE_BATCH_PATH);
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

static int windows_self_rebuild_if_needed(int argc, char** argv, S32* outExitCode) {
    if (getenv("SOB_SKIP_SELF_REBUILD")) {
        return 0;
    }

    const char* exePath = argv[0];
    U64 exeTime = sob_fs_mtime(exePath);
    U64 inputTime = newest_sob_input_mtime();
    if (inputTime == 0 || exeTime == 0 || inputTime <= exeTime) {
        return 0;
    }

    if (sob_fs_mkdir_p(BUILD_TOOLS_DIR) != 0 && !sob_fs_is_dir(BUILD_TOOLS_DIR)) {
        fprintf(stderr, "Error: failed to create '%s'\n", BUILD_TOOLS_DIR);
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

    sob_fs_remove(SOB_REBUILT_EXE_PATH);
    sob_cmd_append(buildCmd, "cl");
    sob_cmd_append(buildCmd, "/nologo");
    sob_cmd_append(buildCmd, "/W4");
    sob_cmd_append(buildCmd, "/wd4100");
    sob_cmd_append(buildCmd, "/wd4189");
    sob_cmd_append(buildCmd, "/wd4505");
    sob_cmd_append(buildCmd, "/wd4996");
    sob_cmd_append(buildCmd, "/Fe:" SOB_REBUILT_EXE_PATH);
    sob_cmd_append(buildCmd, "/Fo:" SOB_REBUILT_OBJ_PATH);
    sob_cmd_append(buildCmd, "sob.c");

    S32 buildResult = sob_cmd_run(buildCmd);
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
    sob_cmd_append(runCmd, SOB_REBUILT_EXE_PATH);
    for (int i = 1; i < argc; ++i) {
        sob_cmd_append(runCmd, argv[i]);
    }

    printf("[SOB] Running rebuilt build driver...\n");
    S32 runResult = sob_cmd_run(runCmd);
    sob_arena_destroy(arena);

    windows_schedule_sob_replacement(exePath);
    *outExitCode = runResult;
    return 1;
}
#endif

#if !SOB_WINDOWS
#define SOB_REBUILT_EXE_PATH BUILD_TOOLS_DIR "/sob_rebuilt"
static int nonwindows_self_rebuild_if_needed(int argc, char** argv, S32* outExitCode) {
    if (getenv("SOB_SKIP_SELF_REBUILD")) {
        return 0;
    }

    const char* exePath = argv[0];
    U64 exeTime = sob_fs_mtime(exePath);
    U64 inputTime = newest_sob_input_mtime();
    if (inputTime == 0u || exeTime == 0u || inputTime <= exeTime) {
        return 0;
    }

    if (sob_fs_mkdir_p(BUILD_TOOLS_DIR) != 0 && !sob_fs_is_dir(BUILD_TOOLS_DIR)) {
        fprintf(stderr, "Error: failed to create '%s'\n", BUILD_TOOLS_DIR);
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

    sob_fs_remove(SOB_REBUILT_EXE_PATH);
    sob_cmd_append(buildCmd, compiler);
    sob_cmd_append(buildCmd, "sob.c");
    sob_cmd_append(buildCmd, "-o");
    sob_cmd_append(buildCmd, SOB_REBUILT_EXE_PATH);

    S32 buildResult = sob_cmd_run(buildCmd);
    if (buildResult != 0) {
        sob_arena_destroy(arena);
        fprintf(stderr, "[SOB] Rebuild failed.\n");
        *outExitCode = buildResult;
        return 1;
    }

    if (rename(SOB_REBUILT_EXE_PATH, exePath) != 0) {
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
#endif

static void configure_compiler_for_mode(Sob_BuildContext* ctx, BuildMode mode) {
    Sob_CompilerConfig config = {0};
    config.kind = Sob_CompilerKind_Auto;
    config.warningsAsErrors = 0;

    if (mode == BuildMode_Debug) {
        config.optLevel = Sob_OptLevel_Debug;
        config.sanitizers = Sob_Sanitizer_None;
        config.enableLTO = 0;
    } else if (mode == BuildMode_Asan) {
        config.optLevel = Sob_OptLevel_Debug;
        config.sanitizers = Sob_Sanitizer_Address;
        config.enableLTO = 0;
    } else {
        config.optLevel = Sob_OptLevel_Release;
        config.sanitizers = Sob_Sanitizer_None;
        config.enableLTO = 1;
    }

    sob_build_set_compiler(ctx, config);
}

static void apply_common_warning_flags(Sob_Target* target) {
#if SOB_WINDOWS
    const char* flags[] = {
        "/Zc:preprocessor",
        "/wd4100", /* unreferenced formal parameter */
        "/wd4189", /* local variable initialized but not referenced */
        "/wd4201", /* nameless struct/union */
        "/wd4324", /* structure was padded due to alignment specifier */
        "/wd4505", /* unreferenced local function removed */
    };
#else
    const char* flags[] = {
        "-Wpedantic",
        "-Wno-unused-parameter",
        "-Wno-unused-variable",
        "-Wno-unused-function",
        "-Wno-gnu-anonymous-struct",
        "-Wno-nested-anon-types",
        "-Wno-gnu-zero-variadic-macro-arguments",
        "-Wno-initializer-overrides",
        "-Wno-c++20-designator",
        "-Wno-c99-designator",
    };
#endif

    for (S32 i = 0; i < (S32)(sizeof(flags) / sizeof(flags[0])); ++i) {
        sob_target_add_cflags(target, flags[i]);
    }
}

static void apply_metagen_warning_flags(Sob_Target* target) {
#if SOB_WINDOWS
    const char* flags[] = {
        "/wd4100", /* unreferenced formal parameter */
        "/wd4189", /* local variable initialized but not referenced */
        "/wd4201", /* nameless struct/union */
        "/wd4505", /* unreferenced local function removed */
    };
#else
    const char* flags[] = {
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Wno-unused-parameter",
        "-Wno-unused-variable",
        "-Wno-unused-function",
        "-Wno-gnu-anonymous-struct",
        "-Wno-nested-anon-types",
        "-Wno-c++20-designator",
        "-Wno-c99-designator",
    };
#endif

    for (S32 i = 0; i < (S32)(sizeof(flags) / sizeof(flags[0])); ++i) {
        sob_target_add_cflags(target, flags[i]);
    }
}

static void apply_cpp_runtime_flags(Sob_Target* target) {
#if SOB_WINDOWS
    sob_target_add_cflags(target, "/GR-");
#else
    sob_target_add_cflags(target, "-fno-exceptions");
    sob_target_add_cflags(target, "-fno-rtti");
#endif
}

static void apply_third_party_warning_flags(Sob_Target* target) {
#if SOB_WINDOWS
    const char* flags[] = {
        "/wd4996",
    };
#else
    const char* flags[] = {
        "-Wno-deprecated-declarations",
        "-Wno-deprecated-volatile",
    };
#endif

    for (S32 i = 0; i < (S32)(sizeof(flags) / sizeof(flags[0])); ++i) {
        sob_target_add_cflags(target, flags[i]);
    }
}

static void apply_mode_target_flags(Sob_Target* target, BuildMode mode) {
    if (mode == BuildMode_Debug) {
        sob_target_define(target, "DEBUG", .value = "1");
#if SOB_WINDOWS
        sob_target_add_cflags(target, "/Od");
#else
        sob_target_add_cflags(target, "-O0");
        sob_target_add_cflags(target, "-fno-omit-frame-pointer");
        sob_target_add_cflags(target, "-g0");
#endif
    } else if (mode == BuildMode_Asan) {
        sob_target_define(target, "DEBUG", .value = "1");
#if SOB_WINDOWS
        sob_target_add_cflags(target, "/Od");
#else
        sob_target_add_cflags(target, "-fno-omit-frame-pointer");
        sob_target_add_cflags(target, "-gline-tables-only");
#endif
    } else {
        sob_target_define(target, "NDEBUG", .value = "1");
    }
}

static void configure_common_includes(Sob_Target* target) {
    sob_target_add_include(target, ".");
}

#if SOB_WINDOWS
typedef struct WindowsVulkanLoaderBuildItem {
    const char* src;
    const char* obj;
} WindowsVulkanLoaderBuildItem;

static const WindowsVulkanLoaderBuildItem windowsVulkanLoaderBuildItems[] = {
    {VULKAN_VENDOR_LOADER_DIR "/allocation.c", VULKAN_LOADER_OBJ_DIR "/allocation.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/cJSON.c", VULKAN_LOADER_OBJ_DIR "/cJSON.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/debug_utils.c", VULKAN_LOADER_OBJ_DIR "/debug_utils.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/extension_manual.c", VULKAN_LOADER_OBJ_DIR "/extension_manual.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/loader_environment.c", VULKAN_LOADER_OBJ_DIR "/loader_environment.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/gpa_helper.c", VULKAN_LOADER_OBJ_DIR "/gpa_helper.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/loader.c", VULKAN_LOADER_OBJ_DIR "/loader.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/log.c", VULKAN_LOADER_OBJ_DIR "/log.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/settings.c", VULKAN_LOADER_OBJ_DIR "/settings.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/terminator.c", VULKAN_LOADER_OBJ_DIR "/terminator.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/trampoline.c", VULKAN_LOADER_OBJ_DIR "/trampoline.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/unknown_function_handling.c", VULKAN_LOADER_OBJ_DIR "/unknown_function_handling.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/wsi.c", VULKAN_LOADER_OBJ_DIR "/wsi.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/loader_windows.c", VULKAN_LOADER_OBJ_DIR "/loader_windows.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/dirent_on_windows.c", VULKAN_LOADER_OBJ_DIR "/dirent_on_windows.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/dev_ext_trampoline.c", VULKAN_LOADER_OBJ_DIR "/dev_ext_trampoline.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/phys_dev_ext.c", VULKAN_LOADER_OBJ_DIR "/phys_dev_ext.obj"},
    {VULKAN_VENDOR_LOADER_DIR "/unknown_ext_chain.c", VULKAN_LOADER_OBJ_DIR "/unknown_ext_chain.obj"},
};

static const char* windowsVulkanLoaderHeaderInputs[] = {
    VULKAN_VENDOR_INCLUDE_DIR "/vulkan/vulkan.h",
    VULKAN_VENDOR_INCLUDE_DIR "/vulkan/vk_icd.h",
    VULKAN_VENDOR_INCLUDE_DIR "/vulkan/vk_layer.h",
    VULKAN_VENDOR_INCLUDE_DIR "/vulkan/vulkan_core.h",
    VULKAN_VENDOR_INCLUDE_DIR "/vulkan/vulkan_win32.h",
    VULKAN_VENDOR_INCLUDE_DIR "/vk_video/vulkan_video_codecs_common.h",
    VULKAN_VENDOR_LOADER_DIR "/adapters.h",
    VULKAN_VENDOR_LOADER_DIR "/allocation.h",
    VULKAN_VENDOR_LOADER_DIR "/cJSON.h",
    VULKAN_VENDOR_LOADER_DIR "/debug_utils.h",
    VULKAN_VENDOR_LOADER_DIR "/dirent_on_windows.h",
    VULKAN_VENDOR_LOADER_DIR "/extension_manual.h",
    VULKAN_VENDOR_LOADER_DIR "/gpa_helper.h",
    VULKAN_VENDOR_LOADER_DIR "/loader.h",
    VULKAN_VENDOR_LOADER_DIR "/loader_common.h",
    VULKAN_VENDOR_LOADER_DIR "/loader_environment.h",
    VULKAN_VENDOR_LOADER_DIR "/loader_windows.h",
    VULKAN_VENDOR_LOADER_DIR "/log.h",
    VULKAN_VENDOR_LOADER_DIR "/settings.h",
    VULKAN_VENDOR_LOADER_DIR "/stack_allocation.h",
    VULKAN_VENDOR_LOADER_DIR "/unknown_function_handling.h",
    VULKAN_VENDOR_LOADER_DIR "/vk_loader_layer.h",
    VULKAN_VENDOR_LOADER_DIR "/vk_loader_platform.h",
    VULKAN_VENDOR_LOADER_DIR "/wsi.h",
    VULKAN_VENDOR_LOADER_GENERATED_DIR "/vk_layer_dispatch_table.h",
    VULKAN_VENDOR_LOADER_GENERATED_DIR "/vk_loader_extensions.c",
    VULKAN_VENDOR_LOADER_GENERATED_DIR "/vk_loader_extensions.h",
    VULKAN_VENDOR_LOADER_GENERATED_DIR "/vk_object_types.h",
};

static int windows_require_vulkan_vendor_file(const char* path, const char* label) {
    if (sob_fs_exists(path)) {
        return 1;
    }

    fprintf(stderr, "Error: missing vendored Vulkan %s: '%s'\n", label, path);
    return 0;
}

static U64 newest_windows_vulkan_loader_input_mtime(void) {
    U64 newest = 0;
    for (S32 i = 0; i < (S32)(sizeof(windowsVulkanLoaderBuildItems) /
                              sizeof(windowsVulkanLoaderBuildItems[0])); ++i) {
        U64 time = sob_fs_mtime(windowsVulkanLoaderBuildItems[i].src);
        if (time == 0) {
            return 0;
        }
        if (time > newest) {
            newest = time;
        }
    }

    for (S32 i = 0; i < (S32)(sizeof(windowsVulkanLoaderHeaderInputs) /
                              sizeof(windowsVulkanLoaderHeaderInputs[0])); ++i) {
        U64 time = sob_fs_mtime(windowsVulkanLoaderHeaderInputs[i]);
        if (time == 0) {
            return 0;
        }
        if (time > newest) {
            newest = time;
        }
    }

    return newest;
}

static int build_windows_vulkan_static_loader(Sob_Arena* arena) {
    if (!arena) {
        return 0;
    }

    if (!windows_command_exists("cl.exe") || !windows_command_exists("lib.exe")) {
        fprintf(stderr, "Error: cl.exe/lib.exe are not usable. Run through ./sob.exe from a VS-enabled environment.\n");
        return 0;
    }

    if (!windows_require_vulkan_vendor_file(VULKAN_VENDOR_INCLUDE_DIR "/vulkan/vulkan.h", "header") ||
        !windows_require_vulkan_vendor_file(VULKAN_VENDOR_INCLUDE_DIR "/vk_video/vulkan_video_codecs_common.h", "video header") ||
        !windows_require_vulkan_vendor_file(VULKAN_VENDOR_LOADER_GENERATED_DIR "/vk_loader_extensions.h", "generated loader header")) {
        return 0;
    }

    U64 inputTime = newest_windows_vulkan_loader_input_mtime();
    if (inputTime == 0) {
        fprintf(stderr, "Error: vendored Vulkan loader source set is incomplete under '%s'.\n",
                VULKAN_VENDOR_LOADER_DIR);
        return 0;
    }

    U64 libTime = sob_fs_mtime(VULKAN_VENDOR_STATIC_LOADER_LIB);
    if (libTime != 0 && libTime >= inputTime) {
        return 1;
    }

    if (sob_fs_mkdir_p(VULKAN_VENDOR_LIB_DIR) != 0 && !sob_fs_is_dir(VULKAN_VENDOR_LIB_DIR)) {
        fprintf(stderr, "Error: failed to create '%s'\n", VULKAN_VENDOR_LIB_DIR);
        return 0;
    }
    if (sob_fs_mkdir_p(VULKAN_LOADER_OBJ_DIR) != 0 && !sob_fs_is_dir(VULKAN_LOADER_OBJ_DIR)) {
        fprintf(stderr, "Error: failed to create '%s'\n", VULKAN_LOADER_OBJ_DIR);
        return 0;
    }

    printf("==> Building vendored Vulkan static loader...\n");
    for (S32 i = 0; i < (S32)(sizeof(windowsVulkanLoaderBuildItems) /
                              sizeof(windowsVulkanLoaderBuildItems[0])); ++i) {
        const WindowsVulkanLoaderBuildItem* item = &windowsVulkanLoaderBuildItems[i];
        U64 objTime = sob_fs_mtime(item->obj);
        U64 srcTime = sob_fs_mtime(item->src);
        if (objTime != 0 && objTime >= inputTime && objTime >= srcTime) {
            continue;
        }

        Sob_Cmd* cmd = sob_cmd_create(arena);
        if (!cmd) {
            return 0;
        }

        char objFlag[2048];
        int objFlagLength = snprintf(objFlag, sizeof(objFlag), "/Fo:%s", item->obj);
        if (objFlagLength <= 0 || objFlagLength >= (int)sizeof(objFlag)) {
            fprintf(stderr, "Error: Vulkan loader object path is too long.\n");
            return 0;
        }

        sob_cmd_append(cmd, "cl");
        sob_cmd_append(cmd, "/nologo");
        sob_cmd_append(cmd, "/c");
        sob_cmd_append(cmd, "/O2");
        sob_cmd_append(cmd, "/Ob2");
        sob_cmd_append(cmd, "/W4");
        sob_cmd_append(cmd, "/wd4100");
        sob_cmd_append(cmd, "/wd4127");
        sob_cmd_append(cmd, "/wd4152");
        sob_cmd_append(cmd, "/wd4201");
        sob_cmd_append(cmd, "/wd4204");
        sob_cmd_append(cmd, "/wd4244");
        sob_cmd_append(cmd, "/wd4267");
        sob_cmd_append(cmd, "/wd4701");
        sob_cmd_append(cmd, "/wd4702");
        sob_cmd_append(cmd, "/wd4996");
        sob_cmd_append(cmd, "/I" VULKAN_VENDOR_INCLUDE_DIR);
        sob_cmd_append(cmd, "/I" VULKAN_VENDOR_LOADER_DIR);
        sob_cmd_append(cmd, "/I" VULKAN_VENDOR_LOADER_GENERATED_DIR);
        sob_cmd_append(cmd, "/DWIN32_LEAN_AND_MEAN");
        sob_cmd_append(cmd, "/DNOMINMAX");
        sob_cmd_append(cmd, "/DVK_USE_PLATFORM_WIN32_KHR");
        sob_cmd_append(cmd, "/DVK_ENABLE_BETA_EXTENSIONS");
        sob_cmd_append(cmd, "/DVULKAN_LOADER_STATIC_LINKED");
        sob_cmd_append(cmd, "/D_CRT_SECURE_NO_WARNINGS");
        sob_cmd_append(cmd, "/D_CRT_NONSTDC_NO_WARNINGS");
        sob_cmd_append(cmd, objFlag);
        sob_cmd_append(cmd, item->src);

        S32 result = sob_cmd_run(cmd);
        if (result != 0) {
            return 0;
        }
    }

    Sob_Cmd* libCmd = sob_cmd_create(arena);
    if (!libCmd) {
        return 0;
    }

    sob_cmd_append(libCmd, "lib");
    sob_cmd_append(libCmd, "/NOLOGO");
    sob_cmd_append(libCmd, "/OUT:" VULKAN_VENDOR_STATIC_LOADER_LIB);
    for (S32 i = 0; i < (S32)(sizeof(windowsVulkanLoaderBuildItems) /
                              sizeof(windowsVulkanLoaderBuildItems[0])); ++i) {
        sob_cmd_append(libCmd, windowsVulkanLoaderBuildItems[i].obj);
    }

    S32 libResult = sob_cmd_run(libCmd);
    return libResult == 0;
}

static int configure_windows_vulkan_vendor(Sob_Arena* arena, Sob_Target* target) {
    if (!build_windows_vulkan_static_loader(arena)) {
        return 0;
    }

    sob_target_add_include(target, VULKAN_VENDOR_INCLUDE_DIR);
    sob_target_add_ldflags(target, "/link");
    sob_target_link(target, VULKAN_VENDOR_STATIC_LOADER_LIB, .kind = Sob_LibKind_Static);
    return 1;
}
#endif

static const char* find_slangc_path(void) {
    const char* envPath = getenv("SLANGC");
    if (envPath && envPath[0] != 0) {
        return envPath;
    }
    if (sob_fs_exists(SLANG_VENDOR_PATH)) {
        return SLANG_VENDOR_PATH;
    }
    return "slangc";
}

static U64 newest_shader_input_mtime(void) {
    U64 result = sob_fs_mtime(ENG_SHADER_MANIFEST_SOURCE);

#define SHADER_INPUT_MTIME(name, source) \
    do { \
        U64 sourceTime = sob_fs_mtime(source); \
        if (sourceTime > result) { \
            result = sourceTime; \
        } \
    } while (0);
    ENG_SHADER_SOURCE_LIST(SHADER_INPUT_MTIME)
#undef SHADER_INPUT_MTIME

    return result;
}

static int verify_shader_inputs_exist(void) {
    int result = 1;
#define SHADER_INPUT_EXISTS(name, source) \
    do { \
        if (!sob_fs_exists(source)) { \
            fprintf(stderr, "Error: shader source missing: '%s'\n", source); \
            result = 0; \
        } \
    } while (0);
    ENG_SHADER_SOURCE_LIST(SHADER_INPUT_EXISTS)
#undef SHADER_INPUT_EXISTS
    return result;
}

static int build_slang_shaders(Sob_Arena* arena) {
    if (!arena) {
        return 1;
    }

    if (!verify_shader_inputs_exist()) {
        return 1;
    }

    if (sob_fs_mkdir_p(ENG_SHADER_OUTPUT_DIR) != 0 &&
        !sob_fs_is_dir(ENG_SHADER_OUTPUT_DIR)) {
        fprintf(stderr, "Error: failed to create '%s'\n", ENG_SHADER_OUTPUT_DIR);
        return 1;
    }

    struct ShaderBuildItem {
        const char* src;
        const char* dst;
        const char* entry;
        const char* stage;
        const char* stageKind;
    };
#define SHADER_BUILD_ITEM(name, source, output, entry, stage, stageKind) \
    {source, output, entry, SOB_STRINGIZE(stage), SOB_STRINGIZE(stageKind)},
    static const struct ShaderBuildItem shaders[] = {
#if SOB_WINDOWS
        ENG_SHADER_VULKAN_OUTPUT_LIST(SHADER_BUILD_ITEM)
#elif SOB_MACOS
        ENG_SHADER_METAL_OUTPUT_LIST(SHADER_BUILD_ITEM)
#else
#error No shader build backend configured for this platform.
#endif
    };
#undef SHADER_BUILD_ITEM

    const char* slangcPath = find_slangc_path();
    U64 inputTime = newest_shader_input_mtime();
    for (S32 i = 0; i < (S32)(sizeof(shaders) / sizeof(shaders[0])); ++i) {
        U64 dstTime = sob_fs_mtime(shaders[i].dst);
        if (dstTime != 0u && dstTime >= inputTime) {
            continue;
        }

        char tempPath[1024];
        snprintf(tempPath, sizeof(tempPath), "%s.tmp", shaders[i].dst);
        sob_fs_remove(tempPath);

        Sob_Cmd* cmd = sob_cmd_create(arena);
        if (!cmd) {
            return 1;
        }

        sob_cmd_append(cmd, slangcPath);
        sob_cmd_append(cmd, shaders[i].src);
        sob_cmd_append(cmd, "-I");
        sob_cmd_append(cmd, "app/shaders");
#if SOB_WINDOWS
        sob_cmd_append(cmd, "-DGFX_SHADER_TARGET_VULKAN=1");
        sob_cmd_append(cmd, "-target");
        sob_cmd_append(cmd, "spirv");
        sob_cmd_append(cmd, "-profile");
        sob_cmd_append(cmd, "glsl_450");
        sob_cmd_append(cmd, "-fvk-use-entrypoint-name");
#elif SOB_MACOS
        sob_cmd_append(cmd, "-DGFX_SHADER_TARGET_METAL=1");
        sob_cmd_append(cmd, "-target");
        sob_cmd_append(cmd, "metal");
        sob_cmd_append(cmd, "-profile");
        sob_cmd_append(cmd, "sm_6_0");
#else
#error No shader compiler target configured for this platform.
#endif
        if (strcmp(shaders[i].stageKind, "compute") == 0) {
            sob_cmd_append(cmd, "-DGFX_SHADER_STAGE_COMPUTE=1");
        } else {
            sob_cmd_append(cmd, "-DGFX_SHADER_STAGE_GRAPHICS=1");
        }
        sob_cmd_append(cmd, "-warnings-disable");
        sob_cmd_append(cmd, "39029");
        sob_cmd_append(cmd, "-entry");
        sob_cmd_append(cmd, shaders[i].entry);
        sob_cmd_append(cmd, "-stage");
        sob_cmd_append(cmd, shaders[i].stage);
        sob_cmd_append(cmd, "-o");
        sob_cmd_append(cmd, tempPath);

        printf("==> Compiling shader %s:%s -> %s\n", shaders[i].src, shaders[i].entry, shaders[i].dst);
        S32 result = sob_cmd_run(cmd);
        if (result != 0) {
            sob_fs_remove(tempPath);
            return result;
        }
        if (sob_fs_copy(tempPath, shaders[i].dst) != 0) {
            sob_fs_remove(tempPath);
            fprintf(stderr, "Error: failed to publish shader output '%s'\n", shaders[i].dst);
            return 1;
        }
        sob_fs_remove(tempPath);
    }

    return 0;
}

static Sob_Target* configure_vendor_lib(Sob_BuildContext* ctx) {
    Sob_Target* vendor = sob_target_create(ctx, "utilities_vendor", Sob_TargetKind_StaticLib,
                                           .outputDir = BUILD_DIR,
                                           .outputName = "utilities_vendor");
    if (!vendor) {
        return 0;
    }
    sob_target_add_source(vendor, "third_party/freetype_v0/freetype_v0.cpp");
    sob_target_add_source(vendor, "third_party/kb/kb_text_shape_impl.cpp");
    sob_target_add_include(vendor, "third_party/freetype_local/include");
    sob_target_add_include(vendor, "third_party/freetype/include");
    sob_target_set_standard(vendor, Sob_Standard_Cpp20);
    apply_cpp_runtime_flags(vendor);
    apply_common_warning_flags(vendor);
    apply_third_party_warning_flags(vendor);
    sob_target_define(vendor, "NDEBUG", .value = "1");
#if SOB_WINDOWS
    sob_target_add_cflags(vendor, "/O2");
#else
    sob_target_add_cflags(vendor, "-O2");
    sob_target_add_cflags(vendor, "-g0");
#endif
    return vendor;
}

static int configure_runtime_executable(Sob_Arena* arena, Sob_Target* target, BuildMode mode,
                                        Sob_Target* vendor) {
#if !SOB_WINDOWS
    (void)arena;
#endif

#if SOB_WINDOWS
    sob_target_add_source(target, "host/main.cpp");
#else
    sob_target_add_source(target, "host/main.mm");
#endif
    sob_target_link_target(target, vendor);
    configure_common_includes(target);
    sob_target_add_include(target, "third_party/freetype_local/include");
    sob_target_add_include(target, "third_party/freetype/include");
    sob_target_set_standard(target, Sob_Standard_Cpp20);
    apply_cpp_runtime_flags(target);
    apply_common_warning_flags(target);
    apply_third_party_warning_flags(target);
    apply_mode_target_flags(target, mode);

#if SOB_MACOS
    sob_target_link(target, "Cocoa", .kind = Sob_LibKind_Framework);
    sob_target_link(target, "QuartzCore", .kind = Sob_LibKind_Framework);
    sob_target_link(target, "Metal", .kind = Sob_LibKind_Framework);
    sob_target_link(target, "AudioToolbox", .kind = Sob_LibKind_Framework);
    sob_target_link(target, "CoreAudio", .kind = Sob_LibKind_Framework);
#elif SOB_WINDOWS
    sob_target_link(target, "user32");
    sob_target_link(target, "gdi32");
    sob_target_link(target, "shell32");
    sob_target_link(target, "advapi32");
    sob_target_link(target, "cfgmgr32");
    sob_target_link(target, "shlwapi");
    sob_target_link(target, "ole32");
    if (!configure_windows_vulkan_vendor(arena, target)) {
        return 0;
    }
#endif

    return 1;
}

static S32 run_metagen_command(void) {
    Sob_Arena* arena = sob_arena_create();
    if (!arena) {
        return -1;
    }

    Sob_Cmd* cmd = sob_cmd_create(arena);
    if (!cmd) {
        sob_arena_destroy(arena);
        return -1;
    }

    sob_cmd_append(cmd, METAGEN_RUN_PATH);
    sob_cmd_append(cmd, "-v");
    sob_cmd_append(cmd, METAGEN_INPUT_PATH);

#if SOB_WINDOWS
    S32 result = sob_cmd_run(cmd);
#else
    S32 result = sob_cmd_run(cmd, .cwd = META_DIR);
#endif
    sob_arena_destroy(arena);
    return result;
}

static int metadata_outputs_are_fresh(void) {
    static const char* inputs[] = {
        "nstl/os/graphics/os_graphics.metadef",
        "meta/meta_main.cpp",
        "meta/generator.cpp",
        "meta/generator.hpp",
        "meta/parser.cpp",
        "meta/parser.hpp",
        "meta/lexer.cpp",
        "meta/lexer.hpp",
    };

    U64 outputTime = sob_fs_mtime("nstl/os/graphics/os_graphics.generated.hpp");
    if (outputTime == 0u) {
        return 0;
    }

    for (S32 i = 0; i < (S32)(sizeof(inputs) / sizeof(inputs[0])); ++i) {
        U64 inputTime = sob_fs_mtime(inputs[i]);
        if (inputTime == 0u || inputTime > outputTime) {
            return 0;
        }
    }

    return 1;
}

static S32 build_and_run_metagen(BuildMode mode) {
    if (sob_fs_mkdir_p(BUILD_TOOLS_DIR) != 0 && !sob_fs_is_dir(BUILD_TOOLS_DIR)) {
        fprintf(stderr, "Error: failed to create '%s'\\n", BUILD_TOOLS_DIR);
        return 1;
    }

    Sob_Arena* arena = sob_arena_create();
    if (!arena) {
        fprintf(stderr, "Error: failed to create sob arena for metagen build\n");
        return 1;
    }

    Sob_BuildContext* ctx = sob_build_create(arena);
    if (!ctx) {
        fprintf(stderr, "Error: failed to create sob build context for metagen build\n");
        sob_arena_destroy(arena);
        return 1;
    }

    configure_compiler_for_mode(ctx, mode);

    Sob_Target* metagen = sob_target_create(ctx, "metagen", Sob_TargetKind_Executable,
                                            .outputDir = BUILD_TOOLS_DIR,
                                            .outputName = METAGEN_EXE_BASENAME);
    if (!metagen) {
        fprintf(stderr, "Error: failed to create metagen target\n");
        sob_arena_destroy(arena);
        return 1;
    }

    sob_target_add_source(metagen, "meta/meta_main.cpp");
    sob_target_add_include(metagen, "meta");
    sob_target_set_standard(metagen, Sob_Standard_Cpp20);
    apply_cpp_runtime_flags(metagen);
#if !SOB_WINDOWS
    sob_target_add_cflags(metagen, "-pthread");
    sob_target_add_ldflags(metagen, "-pthread");
#endif
    apply_metagen_warning_flags(metagen);
    apply_mode_target_flags(metagen, mode);

    printf("==> Building metagen (%s)...\n", build_mode_name(mode));
    S32 buildResult = sob_build_run(ctx);
    sob_arena_destroy(arena);
    if (buildResult != 0) {
        return buildResult;
    }

    printf("==> Running metagen...\n");
    return run_metagen_command();
}

#define COOKER_EXE_BASENAME "cooker"
#if SOB_WINDOWS
#define COOKER_RUN_PATH BUILD_DIR "\\tools\\" COOKER_EXE_BASENAME ".exe"
#else
#define COOKER_RUN_PATH BUILD_DIR "/tools/" COOKER_EXE_BASENAME
#endif

static const char* COOK_ASSET_SOURCES[] = {
    "projects/demo/assets/src/Duck.glb",
    "projects/demo/assets/src/Avocado.glb",
    "projects/demo/assets/src/Lantern.glb",
    "projects/demo/assets/src/Buggy.glb",
    "projects/demo/assets/src/jump.wav",
    "projects/demo/assets/src/land.wav",
    "projects/demo/assets/src/click.wav",
    "projects/demo/assets/src/ambience.wav",
};

static S32 build_and_run_cooker(BuildMode mode) {
    if (sob_fs_mkdir_p(BUILD_TOOLS_DIR) != 0 && !sob_fs_is_dir(BUILD_TOOLS_DIR)) {
        fprintf(stderr, "Error: failed to create '%s'\n", BUILD_TOOLS_DIR);
        return 1;
    }
    if (sob_fs_mkdir_p("projects/demo/assets/cooked") != 0 && !sob_fs_is_dir("projects/demo/assets/cooked")) {
        fprintf(stderr, "Error: failed to create 'projects/demo/assets/cooked'\n");
        return 1;
    }

    Sob_Arena* arena = sob_arena_create();
    if (!arena) {
        return 1;
    }
    Sob_BuildContext* ctx = sob_build_create(arena);
    if (!ctx) {
        sob_arena_destroy(arena);
        return 1;
    }

    configure_compiler_for_mode(ctx, mode);

    Sob_Target* cooker = sob_target_create(ctx, "cooker", Sob_TargetKind_Executable,
                                           .outputDir = BUILD_TOOLS_DIR,
                                           .outputName = COOKER_EXE_BASENAME);
    if (!cooker) {
        sob_arena_destroy(arena);
        return 1;
    }
    sob_target_add_source(cooker, "cooker/cooker_main.cpp");
    sob_target_add_include(cooker, "meta");
    sob_target_add_include(cooker, ".");
    sob_target_set_standard(cooker, Sob_Standard_Cpp20);
    apply_cpp_runtime_flags(cooker);
#if !SOB_WINDOWS
    sob_target_add_cflags(cooker, "-pthread");
    sob_target_add_ldflags(cooker, "-pthread");
#endif
    apply_metagen_warning_flags(cooker);
    apply_third_party_warning_flags(cooker);
    apply_mode_target_flags(cooker, mode);
#if !SOB_WINDOWS
    sob_target_add_cflags(cooker, "-O2");
#endif

    printf("==> Building cooker (%s)...\n", build_mode_name(mode));
    S32 buildResult = sob_build_run(ctx);
    sob_arena_destroy(arena);
    if (buildResult != 0) {
        return buildResult;
    }

    for (S32 i = 0; i < (S32)(sizeof(COOK_ASSET_SOURCES) / sizeof(COOK_ASSET_SOURCES[0])); ++i) {
        Sob_Arena* cmdArena = sob_arena_create();
        if (!cmdArena) {
            return 1;
        }
        Sob_Cmd* cmd = sob_cmd_create(cmdArena);
        if (!cmd) {
            sob_arena_destroy(cmdArena);
            return 1;
        }
        sob_cmd_append(cmd, COOKER_RUN_PATH);
        sob_cmd_append(cmd, COOK_ASSET_SOURCES[i]);
        sob_cmd_append(cmd, "projects/demo/assets/cooked");
        printf("==> Cooking %s...\n", COOK_ASSET_SOURCES[i]);
        S32 result = sob_cmd_run(cmd);
        sob_arena_destroy(cmdArena);
        if (result != 0) {
            return result;
        }
    }
    return 0;
}

#define TEST_EXE_BASENAME "utilities_tests"
#if SOB_WINDOWS
#define TEST_RUN_PATH BUILD_DIR "\\tools\\" TEST_EXE_BASENAME ".exe"
#else
#define TEST_RUN_PATH BUILD_DIR "/tools/" TEST_EXE_BASENAME
#endif

static S32 build_and_run_tests(BuildMode mode) {
    if (sob_fs_mkdir_p(BUILD_TOOLS_DIR) != 0 && !sob_fs_is_dir(BUILD_TOOLS_DIR)) {
        fprintf(stderr, "Error: failed to create '%s'\n", BUILD_TOOLS_DIR);
        return 1;
    }

    Sob_Arena* arena = sob_arena_create();
    if (!arena) {
        return 1;
    }
    Sob_BuildContext* ctx = sob_build_create(arena);
    if (!ctx) {
        sob_arena_destroy(arena);
        return 1;
    }

    configure_compiler_for_mode(ctx, mode);

    Sob_Target* vendor = configure_vendor_lib(ctx);
    if (!vendor) {
        sob_arena_destroy(arena);
        return 1;
    }

    Sob_Target* tests = sob_target_create(ctx, "tests", Sob_TargetKind_Executable,
                                          .outputDir = BUILD_TOOLS_DIR,
                                          .outputName = TEST_EXE_BASENAME);
    if (!tests) {
        sob_arena_destroy(arena);
        return 1;
    }
    sob_target_add_source(tests, "tests/test_main.cpp");
    sob_target_add_include(tests, ".");
    sob_target_add_include(tests, "third_party/freetype_local/include");
    sob_target_add_include(tests, "third_party/freetype/include");
    sob_target_link_target(tests, vendor);
    sob_target_set_standard(tests, Sob_Standard_Cpp20);
    apply_cpp_runtime_flags(tests);
#if !SOB_WINDOWS
    sob_target_add_cflags(tests, "-pthread");
    sob_target_add_ldflags(tests, "-pthread");
#endif
    // Tests compile engine code (nstl unity includes), so they need the engine
    // warning profile: /Zc:preprocessor for __VA_OPT__, /wd4324 for aligned types.
    apply_common_warning_flags(tests);
    apply_third_party_warning_flags(tests);
    apply_mode_target_flags(tests, mode);

    printf("==> Building tests (%s)...\n", build_mode_name(mode));
    S32 buildResult = sob_build_run(ctx);
    sob_arena_destroy(arena);
    if (buildResult != 0) {
        return buildResult;
    }

    Sob_Arena* cmdArena = sob_arena_create();
    if (!cmdArena) {
        return 1;
    }
    Sob_Cmd* cmd = sob_cmd_create(cmdArena);
    if (!cmd) {
        sob_arena_destroy(cmdArena);
        return 1;
    }
    sob_cmd_append(cmd, TEST_RUN_PATH);
    printf("==> Running tests...\n");
    S32 result = sob_cmd_run(cmd);
    sob_arena_destroy(cmdArena);
    return result;
}

static S32 build_project_targets(BuildTarget requestedTarget, BuildMode mode) {
    Sob_Arena* arena = sob_arena_create();
    if (!arena) {
        fprintf(stderr, "Error: failed to create sob arena for project build\n");
        return 1;
    }

    if (requestedTarget == BuildTarget_Cook) {
        sob_arena_destroy(arena);
        return build_and_run_cooker(mode);
    }
    if (requestedTarget == BuildTarget_Test) {
        sob_arena_destroy(arena);
        return build_and_run_tests(mode);
    }
    if (requestedTarget == BuildTarget_Shaders) {
        S32 shaderResult = build_slang_shaders(arena);
        sob_arena_destroy(arena);
        return shaderResult;
    }

    Sob_BuildContext* ctx = sob_build_create(arena);
    if (!ctx) {
        fprintf(stderr, "Error: failed to create sob build context for project build\n");
        sob_arena_destroy(arena);
        return 1;
    }

    configure_compiler_for_mode(ctx, mode);

    if (requestedTarget == BuildTarget_Ship) {
        S32 shaderResult = build_slang_shaders(arena);
        if (shaderResult != 0) {
            sob_arena_destroy(arena);
            return shaderResult;
        }
    }

    if (requestedTarget == BuildTarget_Module || requestedTarget == BuildTarget_All ||
        requestedTarget == BuildTarget_Dev) {
#if SOB_WINDOWS
        if (requestedTarget == BuildTarget_Module && !sob_fs_exists(HOST_IMPORT_LIB_PATH)) {
            fprintf(stderr, "Error: '%s' is missing. Build './sob host' before './sob module' on Windows.\n",
                    HOST_IMPORT_LIB_PATH);
            sob_arena_destroy(arena);
            return 1;
        }
#endif

        Sob_Target* moduleTarget = sob_target_create(ctx, "utilities_app", Sob_TargetKind_DynamicLib,
                                                     .outputDir = BUILD_HOT_DIR,
                                                     .outputName = "utilities_app");
        if (!moduleTarget) {
            fprintf(stderr, "Error: failed to create module target\n");
            sob_arena_destroy(arena);
            return 1;
        }

        snprintf(g_projectMainPath, sizeof(g_projectMainPath), "projects/%s/%s_main.cpp", g_project, g_project);
        sob_target_add_source(moduleTarget, g_projectMainPath);
        configure_common_includes(moduleTarget);
        sob_target_set_standard(moduleTarget, Sob_Standard_Cpp20);
        apply_cpp_runtime_flags(moduleTarget);
#if SOB_WINDOWS
        sob_target_define(moduleTarget, "UTILITIES_SHARED_IMPORT", .value = "1");
        sob_target_link(moduleTarget, HOST_IMPORT_LIB_PATH, .kind = Sob_LibKind_Static);
#endif
#if !SOB_WINDOWS
        sob_target_add_cflags(moduleTarget, "-fvisibility=hidden");
#endif
        apply_common_warning_flags(moduleTarget);
        apply_mode_target_flags(moduleTarget, mode);

#if SOB_MACOS
        sob_target_add_ldflags(moduleTarget, "-undefined");
        sob_target_add_ldflags(moduleTarget, "dynamic_lookup");
#endif
    }

    if (requestedTarget == BuildTarget_Host || requestedTarget == BuildTarget_All ||
        requestedTarget == BuildTarget_Dev) {
        Sob_Target* hostTarget = sob_target_create(ctx, "utilities_host", Sob_TargetKind_Executable,
                                                   .outputDir = BUILD_DIR,
                                                   .outputName = HOST_EXE_BASENAME);
        if (!hostTarget) {
            fprintf(stderr, "Error: failed to create host target\n");
            sob_arena_destroy(arena);
            return 1;
        }

        Sob_Target* vendorTarget = configure_vendor_lib(ctx);
        if (!vendorTarget || !configure_runtime_executable(arena, hostTarget, mode, vendorTarget)) {
            sob_arena_destroy(arena);
            return 1;
        }

#if SOB_MACOS
        sob_target_add_ldflags(hostTarget, "-Wl,-export_dynamic");
#elif SOB_WINDOWS
        sob_target_define(hostTarget, "UTILITIES_SHARED_EXPORT", .value = "1");
        sob_target_add_ldflags(hostTarget, "/IMPLIB:" HOST_IMPORT_LIB_PATH);
#endif
    }

    if (requestedTarget == BuildTarget_Ship) {
        Sob_Target* shipTarget = sob_target_create(ctx, "utilities_ship", Sob_TargetKind_Executable,
                                                   .outputDir = BUILD_DIR,
                                                   .outputName = "utilities_ship");
        if (!shipTarget) {
            fprintf(stderr, "Error: failed to create ship target\n");
            sob_arena_destroy(arena);
            return 1;
        }

        Sob_Target* shipVendor = configure_vendor_lib(ctx);
        if (!shipVendor || !configure_runtime_executable(arena, shipTarget, mode, shipVendor)) {
            sob_arena_destroy(arena);
            return 1;
        }
        snprintf(g_projectMainPath, sizeof(g_projectMainPath), "projects/%s/%s_main.cpp", g_project, g_project);
        sob_target_add_source(shipTarget, g_projectMainPath);
        sob_target_define(shipTarget, "UTILITIES_STATIC_APP", .value = "1");
    }

    printf("==> Building %s [%s] (%s)...\n", build_target_name(requestedTarget), g_project, build_mode_name(mode));

    S32 result = sob_build_run(ctx);
    sob_arena_destroy(arena);

    if (result == 0 && (requestedTarget == BuildTarget_Module || requestedTarget == BuildTarget_All ||
                        requestedTarget == BuildTarget_Dev || requestedTarget == BuildTarget_Ship)) {
        write_module_project_record();
    }

    return result;
}

static S32 build_dev_targets_parallel(BuildMode mode, const char* selfPath) {
    if (!selfPath || selfPath[0] == 0) {
        selfPath = "./sob";
    }

    Sob_Arena* arena = sob_arena_create();
    if (!arena) {
        fprintf(stderr, "Error: failed to create sob arena for parallel build\n");
        return 1;
    }

    Sob_Cmd* moduleCmd = sob_cmd_create(arena);
    Sob_Cmd* hostCmd = sob_cmd_create(arena);
    if (!moduleCmd || !hostCmd) {
        fprintf(stderr, "Error: failed to create parallel build commands\n");
        sob_arena_destroy(arena);
        return 1;
    }

    const char* modeName = build_mode_name(mode);
    sob_cmd_append(moduleCmd, selfPath);
    sob_cmd_append(moduleCmd, "module");
    sob_cmd_append(moduleCmd, modeName);
    sob_cmd_append(moduleCmd, g_project);

    sob_cmd_append(hostCmd, selfPath);
    sob_cmd_append(hostCmd, "host");
    sob_cmd_append(hostCmd, modeName);

    printf("==> Building dev (%s) in parallel...\n", modeName);
    fflush(stdout);

    Sob_Proc* moduleProc = sob_cmd_spawn(moduleCmd);
    Sob_Proc* hostProc = sob_cmd_spawn(hostCmd);
    if (!moduleProc || !hostProc) {
        fprintf(stderr, "Error: failed to spawn parallel build commands\n");
        if (moduleProc) {
            sob_proc_kill(moduleProc);
        }
        if (hostProc) {
            sob_proc_kill(hostProc);
        }
        sob_arena_destroy(arena);
        return 1;
    }

    S32 moduleResult = sob_proc_wait(moduleProc);
    S32 hostResult = sob_proc_wait(hostProc);
    sob_arena_destroy(arena);

    return (moduleResult == 0 && hostResult == 0) ? 0 : 1;
}

static S32 build_dev_targets(BuildMode mode, const char* selfPath) {
    S32 shaderResult = build_project_targets(BuildTarget_Shaders, mode);
    if (shaderResult != 0) {
        return shaderResult;
    }

#if SOB_WINDOWS
    (void)selfPath;
    printf("==> Building dev (%s): host then module...\n", build_mode_name(mode));
    fflush(stdout);

    S32 hostResult = build_project_targets(BuildTarget_Host, mode);
    if (hostResult != 0) {
        return hostResult;
    }

    return build_project_targets(BuildTarget_Module, mode);
#else
    return build_dev_targets_parallel(mode, selfPath);
#endif
}

static S32 run_app_command(void) {
    Sob_Arena* arena = sob_arena_create();
    if (!arena) {
        fprintf(stderr, "Error: failed to create sob arena for run command\n");
        return 1;
    }

    Sob_Cmd* cmd = sob_cmd_create(arena);
    if (!cmd) {
        fprintf(stderr, "Error: failed to create app run command\n");
        sob_arena_destroy(arena);
        return 1;
    }

    sob_cmd_append(cmd, HOST_RUN_PATH);

    printf("==> Running %s...\n", HOST_RUN_PATH);
    fflush(stdout);

    S32 result = sob_cmd_run(cmd);
    sob_arena_destroy(arena);
    return result;
}

static S32 handle_clean(void) {
    if (!sob_fs_exists(BUILD_DIR)) {
        printf("==> Build directory '%s' is already clean.\n", BUILD_DIR);
        return 0;
    }

    printf("==> Removing '%s'...\n", BUILD_DIR);
    if (sob_fs_remove_tree(BUILD_DIR) != 0) {
        fprintf(stderr, "Error: failed to clean '%s'\n", BUILD_DIR);
        return 1;
    }

    printf("==> Clean complete.\n");
    return 0;
}

int main(int argc, char** argv) {
#if SOB_WINDOWS
    S32 bootstrapExitCode = 0;
    if (windows_bootstrap_msvc_environment_if_needed(argc, argv, &bootstrapExitCode)) {
        return bootstrapExitCode;
    }
    if (windows_self_rebuild_if_needed(argc, argv, &bootstrapExitCode)) {
        return bootstrapExitCode;
    }
#else
    S32 bootstrapExitCode = 0;
    if (nonwindows_self_rebuild_if_needed(argc, argv, &bootstrapExitCode)) {
        return bootstrapExitCode;
    }
#endif

    BuildTarget target = BuildTarget_Run;
    BuildMode mode = BuildMode_Debug;

    if (argc >= 2) {
        BuildTarget parsedTarget = BuildTarget_Run;
        BuildMode parsedMode = BuildMode_Debug;

        if (parse_target(argv[1], &parsedTarget)) {
            target = parsedTarget;
            int argAt = 2;
            if (argc > argAt && parse_mode(argv[argAt], &parsedMode)) {
                mode = parsedMode;
                argAt += 1;
            }
            if (argc > argAt) {
                if (!project_exists(argv[argAt])) {
                    fprintf(stderr, "Error: unknown mode or project '%s' (no projects/%s/%s_main.cpp)\n",
                            argv[argAt], argv[argAt], argv[argAt]);
                    print_usage();
                    return 1;
                }
                g_project = argv[argAt];
                argAt += 1;
            }
            if (argc > argAt) {
                fprintf(stderr, "Error: too many arguments\n");
                print_usage();
                return 1;
            }
        } else if (parse_mode(argv[1], &parsedMode)) {
            target = BuildTarget_Run;
            mode = parsedMode;
            if (argc > 2 && project_exists(argv[2])) {
                g_project = argv[2];
            } else if (argc > 2) {
                fprintf(stderr, "Error: too many arguments\n");
                print_usage();
                return 1;
            }
        } else {
            fprintf(stderr, "Error: unknown target or mode '%s'\n", argv[1]);
            print_usage();
            return 1;
        }
    }

    if (target == BuildTarget_Clean) {
        return handle_clean();
    }

    if (target == BuildTarget_Metagen || !metadata_outputs_are_fresh()) {
        S32 metadataResult = build_and_run_metagen(mode);
        if (metadataResult != 0) {
            return metadataResult;
        }
    } else {
        printf("==> Metadata is up to date.\n");
        fflush(stdout);
    }

    if (target == BuildTarget_Metagen) {
        return 0;
    }

    if (target == BuildTarget_Run) {
        S32 buildResult = build_dev_targets(mode, argv[0]);
        if (buildResult != 0) {
            return buildResult;
        }

        return run_app_command();
    }

    if (target == BuildTarget_All || target == BuildTarget_Dev) {
        return build_dev_targets(mode, argv[0]);
    }

    return build_project_targets(target, mode);
}
