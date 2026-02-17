#define SOB_IMPLEMENTATION
#include "third_party/sob/sob.h"

#include <stdio.h>
#include <string.h>

#define BUILD_DIR "build"
#define BUILD_TOOLS_DIR "build/tools"
#define BUILD_HOT_DIR "build/hot"
#define META_DIR "meta"

#define METAGEN_EXE_BASENAME "metagen"
#if SOB_WINDOWS
#define METAGEN_OUTPUT_PATH BUILD_TOOLS_DIR "/" METAGEN_EXE_BASENAME ".exe"
#else
#define METAGEN_OUTPUT_PATH BUILD_TOOLS_DIR "/" METAGEN_EXE_BASENAME
#endif

#define VULKAN_LIB_DIR "third_party/vulkan/macos/lib"
#define VULKAN_ICD_DST BUILD_DIR "/MoltenVK_icd.json"

typedef enum BuildMode {
    BuildMode_Debug,
    BuildMode_Release,
} BuildMode;

typedef enum BuildTarget {
    BuildTarget_All,
    BuildTarget_Host,
    BuildTarget_Module,
    BuildTarget_Metagen,
    BuildTarget_Clean,
} BuildTarget;

static void print_usage(void) {
    printf("Usage: ./sob [target] [mode]\n");
    printf("\n");
    printf("Targets:\n");
    printf("  all      Build host + module (default)\n");
    printf("  host     Build host executable only\n");
    printf("  module   Build hot-reload module only\n");
    printf("  metagen  Build metagen and regenerate metadata\n");
    printf("  clean    Remove build artifacts\n");
    printf("\n");
    printf("Modes:\n");
    printf("  debug    Debug build (default)\n");
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

    if (strcmp(value, "all") == 0) {
        *outTarget = BuildTarget_All;
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
    if (strcmp(value, "metagen") == 0) {
        *outTarget = BuildTarget_Metagen;
        return 1;
    }
    if (strcmp(value, "clean") == 0) {
        *outTarget = BuildTarget_Clean;
        return 1;
    }

    return 0;
}

static const char* build_mode_name(BuildMode mode) {
    return (mode == BuildMode_Release) ? "release" : "debug";
}

static void configure_compiler_for_mode(Sob_BuildContext* ctx, BuildMode mode) {
    Sob_CompilerConfig config = {0};
    config.kind = Sob_CompilerKind_Auto;
    config.warningsAsErrors = 0;

    if (mode == BuildMode_Debug) {
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
    const char* flags[] = {
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Wconversion",
        "-Wsign-conversion",
        "-Wshadow",
        "-Wformat=2",
        "-Wnull-dereference",
        "-Wdouble-promotion",
        "-Wimplicit-fallthrough",
        "-Wno-unused-parameter",
        "-Wno-unused-variable",
        "-Wno-unused-function",
        "-Wno-gnu-anonymous-struct",
        "-Wno-nested-anon-types",
        "-Wno-gnu-zero-variadic-macro-arguments",
        "-Wno-initializer-overrides",
    };

    for (S32 i = 0; i < (S32)(sizeof(flags) / sizeof(flags[0])); ++i) {
        sob_target_add_cflags(target, flags[i]);
    }
}

static void apply_metagen_warning_flags(Sob_Target* target) {
    const char* flags[] = {
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Wno-unused-parameter",
        "-Wno-unused-variable",
        "-Wno-unused-function",
        "-Wno-gnu-anonymous-struct",
        "-Wno-nested-anon-types",
    };

    for (S32 i = 0; i < (S32)(sizeof(flags) / sizeof(flags[0])); ++i) {
        sob_target_add_cflags(target, flags[i]);
    }
}

static void apply_mode_target_flags(Sob_Target* target, BuildMode mode) {
    if (mode == BuildMode_Debug) {
        sob_target_define(target, "DEBUG", .value = "1");
        sob_target_add_cflags(target, "-fno-omit-frame-pointer");
    }
}

static void configure_common_includes(Sob_Target* target, int includeVulkan) {
    sob_target_add_include(target, ".");
    sob_target_add_include(target, "third_party/vulkan_vma");
    sob_target_add_include(target, "third_party/dear_imgui");
    sob_target_add_include(target, "third_party/cgltf");

    if (includeVulkan) {
        sob_target_add_include(target, "third_party/vulkan/include");
    }
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

    sob_cmd_append(cmd, "../" METAGEN_OUTPUT_PATH);
    sob_cmd_append(cmd, "-v");
    sob_cmd_append(cmd, "..");

    S32 result = sob_cmd_run(cmd, .cwd = META_DIR);
    sob_arena_destroy(arena);
    return result;
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
    sob_target_set_standard(metagen, Sob_Standard_Cpp17);
    sob_target_add_cflags(metagen, "-pthread");
    sob_target_add_ldflags(metagen, "-pthread");
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

#if SOB_MACOS
static S32 stage_host_runtime_files(void) {
    static const char* copyPairs[][2] = {
        {"third_party/vulkan/macos/lib/libvulkan.dylib", BUILD_DIR "/libvulkan.dylib"},
        {"third_party/vulkan/macos/lib/libvulkan.1.dylib", BUILD_DIR "/libvulkan.1.dylib"},
        {"third_party/vulkan/macos/lib/libMoltenVK.dylib", BUILD_DIR "/libMoltenVK.dylib"},
        {"third_party/vulkan/macos/lib/libdxcompiler.dylib", BUILD_DIR "/libdxcompiler.dylib"},
    };

    static const char* hardcodedIcdJson =
        "{\n"
        "    \"file_format_version\": \"1.0.0\",\n"
        "    \"ICD\": {\n"
        "        \"library_path\": \"./libMoltenVK.dylib\",\n"
        "        \"api_version\": \"1.4.0\",\n"
        "        \"is_portability_driver\": true\n"
        "    }\n"
        "}\n";

    if (sob_fs_mkdir_p(BUILD_DIR) != 0 && !sob_fs_is_dir(BUILD_DIR)) {
        fprintf(stderr, "Error: failed to create build output directory '%s'\n", BUILD_DIR);
        return 1;
    }

    for (S32 i = 0; i < (S32)(sizeof(copyPairs) / sizeof(copyPairs[0])); ++i) {
        if (sob_fs_copy(copyPairs[i][0], copyPairs[i][1]) != 0) {
            fprintf(stderr, "Error: failed to copy '%s' to '%s'\n", copyPairs[i][0], copyPairs[i][1]);
            return 1;
        }
    }

    if (sob_fs_write_text(VULKAN_ICD_DST, hardcodedIcdJson) != 0) {
        fprintf(stderr, "Error: failed to write '%s'\n", VULKAN_ICD_DST);
        return 1;
    }

    return 0;
}
#endif

static S32 build_project_targets(BuildTarget requestedTarget, BuildMode mode) {
    Sob_Arena* arena = sob_arena_create();
    if (!arena) {
        fprintf(stderr, "Error: failed to create sob arena for project build\n");
        return 1;
    }

    Sob_BuildContext* ctx = sob_build_create(arena);
    if (!ctx) {
        fprintf(stderr, "Error: failed to create sob build context for project build\n");
        sob_arena_destroy(arena);
        return 1;
    }

    configure_compiler_for_mode(ctx, mode);

    if (requestedTarget == BuildTarget_Module || requestedTarget == BuildTarget_All) {
        Sob_Target* moduleTarget = sob_target_create(ctx, "utilities_app", Sob_TargetKind_DynamicLib,
                                                     .outputDir = BUILD_HOT_DIR,
                                                     .outputName = "utilities_app");
        if (!moduleTarget) {
            fprintf(stderr, "Error: failed to create module target\n");
            sob_arena_destroy(arena);
            return 1;
        }

        sob_target_add_source(moduleTarget, "app.cpp");
        configure_common_includes(moduleTarget, 1);
        sob_target_set_standard(moduleTarget, Sob_Standard_Cpp17);
        apply_common_warning_flags(moduleTarget);
        apply_mode_target_flags(moduleTarget, mode);

#if SOB_MACOS
        sob_target_add_ldflags(moduleTarget, "-undefined");
        sob_target_add_ldflags(moduleTarget, "dynamic_lookup");
#endif
    }

    if (requestedTarget == BuildTarget_Host || requestedTarget == BuildTarget_All) {
        Sob_Target* imguiTarget = sob_target_create(ctx, "dear_imgui", Sob_TargetKind_StaticLib,
                                                    .outputDir = BUILD_DIR,
                                                    .outputName = "libdear_imgui");
        if (!imguiTarget) {
            fprintf(stderr, "Error: failed to create dear_imgui target\n");
            sob_arena_destroy(arena);
            return 1;
        }

        sob_target_add_source(imguiTarget, "third_party/dear_imgui/imgui_unity.cpp");
        sob_target_add_include(imguiTarget, "third_party/dear_imgui");
        sob_target_add_include(imguiTarget, "third_party/vulkan/include");
        sob_target_set_standard(imguiTarget, Sob_Standard_Cpp17);
        apply_common_warning_flags(imguiTarget);
        apply_mode_target_flags(imguiTarget, mode);

        Sob_Target* hostTarget = sob_target_create(ctx, "utilities_host", Sob_TargetKind_Executable,
                                                   .outputDir = BUILD_DIR,
                                                   .outputName = "utilities_host");
        if (!hostTarget) {
            fprintf(stderr, "Error: failed to create host target\n");
            sob_arena_destroy(arena);
            return 1;
        }

        sob_target_add_source(hostTarget, "main.mm");
        configure_common_includes(hostTarget, 0);
        sob_target_set_standard(hostTarget, Sob_Standard_Cpp17);
        sob_target_define(hostTarget, "UTILITIES_ICD_FILENAME", .value = "\"MoltenVK_icd.json\"");
        apply_common_warning_flags(hostTarget);
        apply_mode_target_flags(hostTarget, mode);
        sob_target_link_target(hostTarget, imguiTarget);

#if SOB_MACOS
        sob_target_link(hostTarget, "Cocoa", .kind = Sob_LibKind_Framework);
        sob_target_link(hostTarget, "QuartzCore", .kind = Sob_LibKind_Framework);
        sob_target_link(hostTarget, "Metal", .kind = Sob_LibKind_Framework);
        sob_target_link(hostTarget, "vulkan", .searchPath = VULKAN_LIB_DIR);
        sob_target_link(hostTarget, "dxcompiler", .searchPath = VULKAN_LIB_DIR);
        sob_target_add_ldflags(hostTarget, "-Wl,-export_dynamic");
        sob_target_add_ldflags(hostTarget, "-Wl,-rpath,@loader_path");
#endif
    }

    printf("==> Building %s (%s)...\n",
           (requestedTarget == BuildTarget_All) ? "all" :
           (requestedTarget == BuildTarget_Host) ? "host" : "module",
           build_mode_name(mode));

    S32 result = sob_build_run(ctx);
    sob_arena_destroy(arena);

#if SOB_MACOS
    if (result == 0 && (requestedTarget == BuildTarget_Host || requestedTarget == BuildTarget_All)) {
        result = stage_host_runtime_files();
    }
#endif

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
    SOB_GO_REBUILD_URSELF(argc, argv);

    BuildTarget target = BuildTarget_All;
    BuildMode mode = BuildMode_Debug;

    if (argc >= 2) {
        BuildTarget parsedTarget = BuildTarget_All;
        BuildMode parsedMode = BuildMode_Debug;

        if (parse_target(argv[1], &parsedTarget)) {
            target = parsedTarget;
            if (argc >= 3) {
                if (!parse_mode(argv[2], &parsedMode)) {
                    fprintf(stderr, "Error: unknown mode '%s'\n", argv[2]);
                    print_usage();
                    return 1;
                }
                mode = parsedMode;
            }
            if (argc > 3) {
                fprintf(stderr, "Error: too many arguments\n");
                print_usage();
                return 1;
            }
        } else if (parse_mode(argv[1], &parsedMode)) {
            target = BuildTarget_All;
            mode = parsedMode;
            if (argc > 2) {
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

#if !SOB_MACOS
    if (target == BuildTarget_Host || target == BuildTarget_Module || target == BuildTarget_All) {
        fprintf(stderr, "Error: '%s' build is currently supported only on macOS.\n",
                (target == BuildTarget_Host) ? "host" :
                (target == BuildTarget_Module) ? "module" : "all");
        fprintf(stderr, "Use './sob metagen' for metadata generation on this platform.\n");
        return 1;
    }
#endif

    S32 metadataResult = build_and_run_metagen(mode);
    if (metadataResult != 0) {
        return metadataResult;
    }

    if (target == BuildTarget_Metagen) {
        return 0;
    }

    return build_project_targets(target, mode);
}
