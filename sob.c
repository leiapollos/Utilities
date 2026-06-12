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

#if SOB_WINDOWS
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
    printf("  cook     Build the asset cooker and cook projects/<project>/assets/src\n");
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

typedef struct ToolBuild {
    Sob_Arena* arena;
    Sob_BuildContext* ctx;
    Sob_Target* target;
} ToolBuild;

// Arena + context + compiler + an executable target under build/tools in
// the shared tool shape: C++20, no exceptions/RTTI, pthread, mode flags.
// Sources, includes, warning profile, and links stay at the call site.
// Returns 0 with everything torn down on failure.
static int tool_build_begin(ToolBuild* tool, const char* name, const char* exeBasename, BuildMode mode) {
    tool->arena = 0;
    tool->ctx = 0;
    tool->target = 0;

    if (sob_fs_mkdir_p(BUILD_TOOLS_DIR) != 0 && !sob_fs_is_dir(BUILD_TOOLS_DIR)) {
        fprintf(stderr, "Error: failed to create '%s'\n", BUILD_TOOLS_DIR);
        return 0;
    }

    tool->arena = sob_arena_create();
    if (!tool->arena) {
        fprintf(stderr, "Error: failed to set up %s build\n", name);
        return 0;
    }
    tool->ctx = sob_build_create(tool->arena);
    if (tool->ctx) {
        configure_compiler_for_mode(tool->ctx, mode);
        tool->target = sob_target_create(tool->ctx, name, Sob_TargetKind_Executable,
                                         .outputDir = BUILD_TOOLS_DIR,
                                         .outputName = exeBasename);
    }
    if (!tool->target) {
        fprintf(stderr, "Error: failed to set up %s build\n", name);
        sob_arena_destroy(tool->arena);
        tool->arena = 0;
        return 0;
    }

    sob_target_set_standard(tool->target, Sob_Standard_Cpp20);
    apply_cpp_runtime_flags(tool->target);
#if !SOB_WINDOWS
    sob_target_add_cflags(tool->target, "-pthread");
    sob_target_add_ldflags(tool->target, "-pthread");
#endif
    apply_mode_target_flags(tool->target, mode);
    return 1;
}

static S32 tool_build_finish(ToolBuild* tool, const char* name, BuildMode mode) {
    printf("==> Building %s (%s)...\n", name, build_mode_name(mode));
    S32 result = sob_build_run(tool->ctx);
    sob_arena_destroy(tool->arena);
    tool->arena = 0;
    return result;
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

    if (!sob_command_exists("cl.exe") || !sob_command_exists("lib.exe")) {
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
    static const char* const inputs[] = {
        ENG_SHADER_MANIFEST_SOURCE,
#define SHADER_INPUT_PATH(name, source) source,
        ENG_SHADER_SOURCE_LIST(SHADER_INPUT_PATH)
#undef SHADER_INPUT_PATH
    };
    return sob_fs_newest_mtime(inputs, (S32)(sizeof(inputs) / sizeof(inputs[0])));
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
#define SHADER_BUILD_ITEM(name, source, entry, stage, kind) \
    {source, ENG_SHADER_OUTPUT_PATH(entry), #entry, #stage, #kind},
    static const struct ShaderBuildItem shaders[] = {
        ENG_SHADER_LIST(SHADER_BUILD_ITEM)
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
        sob_cmd_append(cmd, "engine/shaders");
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
        "engine/shaders/shader_records.metadef",
        "meta/meta_main.cpp",
        "meta/generator.cpp",
        "meta/generator.hpp",
        "meta/parser.cpp",
        "meta/parser.hpp",
        "meta/lexer.cpp",
        "meta/lexer.hpp",
    };

    U64 outputTime = sob_fs_mtime("nstl/os/graphics/os_graphics.generated.hpp");
    U64 shaderRecordsTime = sob_fs_mtime("engine/shaders/shader_records.generated.hpp");
    if (outputTime == 0u || shaderRecordsTime == 0u) {
        return 0;
    }
    if (shaderRecordsTime < outputTime) {
        outputTime = shaderRecordsTime;
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
    ToolBuild tool;
    if (!tool_build_begin(&tool, "metagen", METAGEN_EXE_BASENAME, mode)) {
        return 1;
    }
    sob_target_add_source(tool.target, "meta/meta_main.cpp");
    // meta/nstl is a deliberate, permanent snapshot: the tools must keep
    // building even while live nstl is mid-surgery. Keep metagen standalone
    // forever; never point it at live nstl, never merge the two.
    sob_target_add_include(tool.target, "meta");
    apply_metagen_warning_flags(tool.target);

    S32 buildResult = tool_build_finish(&tool, "metagen", mode);
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

// `sob cook [project]` discovers sources by scanning assets/src — the
// filesystem is the cook manifest. One rule per extension; anything
// else in src is reported and skipped.
static const char* COOK_EXT_RULES[][2] = {
    {".glb", ".umdl"},
    {".wav", ".uaud"},
};

typedef struct CookScan {
    Sob_Arena* arena;
    const char* cookedDir;
    S32 cookedCount;
    S32 skippedCount;
    S32 failedCount;
} CookScan;

static void cook_scan_file(void* userData, const char* fileName, const char* fullPath) {
    CookScan* scan = (CookScan*)userData;
    const char* ext = sob_path_ext(fileName);
    const char* outExt = 0;
    for (S32 i = 0; i < (S32)(sizeof(COOK_EXT_RULES) / sizeof(COOK_EXT_RULES[0])); ++i) {
        if (strcmp(ext, COOK_EXT_RULES[i][0]) == 0) {
            outExt = COOK_EXT_RULES[i][1];
            break;
        }
    }
    if (!outExt) {
        printf("==> Skipping %s (no cook rule for '%s')\n", fullPath, ext);
        return;
    }

    char dstPath[1024];
    S32 stemLength = (S32)(strlen(fileName) - strlen(ext));
    snprintf(dstPath, sizeof(dstPath), "%s/%.*s%s", scan->cookedDir, stemLength, fileName, outExt);

    // Skip when the cooked output is newer than the source. Cooker
    // format drift is the load-time magic/version checks' job: a format
    // change bumps ASSET_*_VERSION and stale files fail loudly.
    U64 srcTime = sob_fs_mtime(fullPath);
    U64 dstTime = sob_fs_mtime(dstPath);
    if (dstTime != 0u && dstTime >= srcTime) {
        scan->skippedCount++;
        return;
    }

    Sob_Cmd* cmd = sob_cmd_create(scan->arena);
    if (!cmd) {
        scan->failedCount++;
        return;
    }
    sob_cmd_append(cmd, COOKER_RUN_PATH);
    sob_cmd_append(cmd, fullPath);
    sob_cmd_append(cmd, scan->cookedDir);
    printf("==> Cooking %s...\n", fullPath);
    if (sob_cmd_run(cmd) != 0) {
        scan->failedCount++;
        return;
    }
    scan->cookedCount++;
}

static S32 build_and_run_cooker(BuildMode mode) {
    char srcDir[512];
    char cookedDir[512];
    snprintf(srcDir, sizeof(srcDir), "projects/%s/assets/src", g_project);
    snprintf(cookedDir, sizeof(cookedDir), "projects/%s/assets/cooked", g_project);

    if (!sob_fs_is_dir(srcDir)) {
        printf("==> No '%s' — nothing to cook.\n", srcDir);
        return 0;
    }
    if (sob_fs_mkdir_p(cookedDir) != 0 && !sob_fs_is_dir(cookedDir)) {
        fprintf(stderr, "Error: failed to create '%s'\n", cookedDir);
        return 1;
    }

    ToolBuild tool;
    if (!tool_build_begin(&tool, "cooker", COOKER_EXE_BASENAME, mode)) {
        return 1;
    }
    sob_target_add_source(tool.target, "cooker/cooker_main.cpp");
    // "meta" before "." so the vendored meta/nstl snapshot shadows live
    // nstl — same standalone-forever policy as metagen.
    sob_target_add_include(tool.target, "meta");
    sob_target_add_include(tool.target, ".");
    apply_metagen_warning_flags(tool.target);
    apply_third_party_warning_flags(tool.target);
#if !SOB_WINDOWS
    sob_target_add_cflags(tool.target, "-O2");
#endif

    S32 buildResult = tool_build_finish(&tool, "cooker", mode);
    if (buildResult != 0) {
        return buildResult;
    }

    Sob_Arena* scanArena = sob_arena_create();
    if (!scanArena) {
        return 1;
    }
    CookScan scan = {0};
    scan.arena = scanArena;
    scan.cookedDir = cookedDir;
    sob__for_each_file(scanArena, srcDir, 0, cook_scan_file, &scan);
    sob_arena_destroy(scanArena);

    printf("==> Cook done: %d cooked, %d skipped, %d failed\n",
           scan.cookedCount, scan.skippedCount, scan.failedCount);
    return scan.failedCount != 0 ? 1 : 0;
}

#define TEST_EXE_BASENAME "utilities_tests"
#if SOB_WINDOWS
#define TEST_RUN_PATH BUILD_DIR "\\tools\\" TEST_EXE_BASENAME ".exe"
#else
#define TEST_RUN_PATH BUILD_DIR "/tools/" TEST_EXE_BASENAME
#endif

static S32 build_and_run_tests(BuildMode mode) {
    ToolBuild tool;
    if (!tool_build_begin(&tool, "tests", TEST_EXE_BASENAME, mode)) {
        return 1;
    }
    Sob_Target* vendor = configure_vendor_lib(tool.ctx);
    if (!vendor) {
        sob_arena_destroy(tool.arena);
        return 1;
    }
    sob_target_add_source(tool.target, "tests/test_main.cpp");
    sob_target_add_include(tool.target, ".");
    sob_target_add_include(tool.target, "third_party/freetype_local/include");
    sob_target_add_include(tool.target, "third_party/freetype/include");
    sob_target_link_target(tool.target, vendor);
    // Tests compile engine code (nstl unity includes), so they need the engine
    // warning profile: /Zc:preprocessor for __VA_OPT__, /wd4324 for aligned types.
    apply_common_warning_flags(tool.target);
    apply_third_party_warning_flags(tool.target);

    S32 buildResult = tool_build_finish(&tool, "tests", mode);
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

// The host's hot-reload watch list, generated per module build by a
// compiler dep scan of the active project's TU. The host polls exactly
// these paths; nothing is hand-listed.
#define MODULE_INPUTS_MANIFEST_PATH "build/module_inputs.txt"
#define MODULE_INPUTS_DEP_PATH "build/module_inputs.dep"

static S32 write_module_inputs_manifest(BuildMode mode) {
    const char* modeDefine = (mode == BuildMode_Release) ? "NDEBUG=1" : "DEBUG=1";

#if SOB_WINDOWS
    {
        char command[2048];
        // /Zs: syntax-only; /showIncludes prints every include used.
        // /Zc:preprocessor matches the real build's language mode (__VA_OPT__).
        snprintf(command, sizeof(command),
                 "cl /nologo /Zs /showIncludes /Zc:preprocessor /I. /std:c++20 /D%s \"%s\" > \"%s\" 2>&1",
                 modeDefine, g_projectMainPath, MODULE_INPUTS_DEP_PATH);
        if (system(command) != 0) {
            fprintf(stderr, "Error: module dep scan failed\n");
            return 1;
        }
    }
#else
    {
        Sob_Arena* arena = sob_arena_create();
        if (!arena) {
            return 1;
        }
        Sob_Cmd* cmd = sob_cmd_create(arena);
        if (!cmd) {
            sob_arena_destroy(arena);
            return 1;
        }
        char define[64];
        snprintf(define, sizeof(define), "-D%s", modeDefine);
        sob_cmd_append(cmd, "clang++");
        sob_cmd_append(cmd, "-MM");
        sob_cmd_append(cmd, "-I.");
        sob_cmd_append(cmd, "-std=c++20");
        sob_cmd_append(cmd, define);
        sob_cmd_append(cmd, g_projectMainPath);
        sob_cmd_append(cmd, "-o");
        sob_cmd_append(cmd, MODULE_INPUTS_DEP_PATH);
        S32 result = sob_cmd_run(cmd);
        sob_arena_destroy(arena);
        if (result != 0) {
            fprintf(stderr, "Error: module dep scan failed\n");
            return 1;
        }
    }
#endif

    FILE* in = fopen(MODULE_INPUTS_DEP_PATH, "rb");
    if (!in) {
        fprintf(stderr, "Error: cannot read '%s'\n", MODULE_INPUTS_DEP_PATH);
        return 1;
    }
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);
    char* text = (char*)malloc((size_t)size + 1u);
    if (!text) {
        fclose(in);
        return 1;
    }
    size_t readBytes = fread(text, 1, (size_t)size, in);
    text[readBytes] = 0;
    fclose(in);

    FILE* out = fopen(MODULE_INPUTS_MANIFEST_PATH, "wb");
    if (!out) {
        free(text);
        fprintf(stderr, "Error: cannot write '%s'\n", MODULE_INPUTS_MANIFEST_PATH);
        return 1;
    }

    S32 lineCount = 0;
#if SOB_WINDOWS
    // Parse "Note: including file: <path>" lines; system headers come
    // back absolute, so keep only paths under the repo root.
    char cwd[1024];
    cwd[0] = 0;
    GetCurrentDirectoryA(sizeof(cwd), cwd);
    size_t cwdLength = strlen(cwd);
    char* line = text;
    while (line && *line) {
        char* end = strchr(line, '\n');
        if (end) { *end = 0; }
        const char* prefix = "Note: including file:";
        if (strncmp(line, prefix, strlen(prefix)) == 0) {
            char* path = line + strlen(prefix);
            while (*path == ' ') { path++; }
            size_t len = strlen(path);
            while (len && (path[len - 1] == '\r' || path[len - 1] == ' ')) { path[--len] = 0; }
            if (cwdLength != 0 && _strnicmp(path, cwd, cwdLength) == 0) {
                const char* relative = path + cwdLength;
                while (*relative == '\\' || *relative == '/') { relative++; }
                fprintf(out, "%s\n", relative);
                lineCount++;
            }
        }
        line = end ? end + 1 : 0;
    }
    fprintf(out, "%s\n", g_projectMainPath);
    lineCount++;
#else
    // Make-style .d: the first token is "<target>:", "\" tokens are line
    // continuations, everything else is a path (the TU included).
    char* at = text;
    while (*at) {
        while (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r') { at++; }
        if (!*at) { break; }
        char* token = at;
        while (*at && *at != ' ' && *at != '\t' && *at != '\n' && *at != '\r') { at++; }
        if (*at) { *at = 0; at++; }
        size_t tokenLength = strlen(token);
        if (tokenLength == 0 || strcmp(token, "\\") == 0) { continue; }
        if (token[tokenLength - 1] == ':') { continue; }
        fprintf(out, "%s\n", token);
        lineCount++;
    }
#endif
    fclose(out);
    free(text);
    printf("==> Module inputs manifest: %d files -> %s\n", lineCount, MODULE_INPUTS_MANIFEST_PATH);
    return 0;
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
    if (result == 0 && (requestedTarget == BuildTarget_Module || requestedTarget == BuildTarget_All ||
                        requestedTarget == BuildTarget_Dev)) {
        result = write_module_inputs_manifest(mode);
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

static S32 run_host_command(void) {
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
    static const char* const sobInputs[] = {
        "sob.c",
        "third_party/sob/sob.h",
        ENG_SHADER_MANIFEST_SOURCE,
    };
    S32 bootstrapExitCode = 0;
    if (sob_bootstrap(argc, argv, sobInputs, (S32)(sizeof(sobInputs) / sizeof(sobInputs[0])),
                      BUILD_TOOLS_DIR, &bootstrapExitCode)) {
        return bootstrapExitCode;
    }

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

        return run_host_command();
    }

    if (target == BuildTarget_All || target == BuildTarget_Dev) {
        return build_dev_targets(mode, argv[0]);
    }

    return build_project_targets(target, mode);
}
