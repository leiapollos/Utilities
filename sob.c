#define SOB_IMPLEMENTATION
#include "third_party/sob/sob.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if SOB_MACOS
#include <unistd.h>
#endif

#define BUILD_DIR "build"
#define BUILD_TOOLS_DIR "build/tools"
#define BUILD_HOT_DIR "build/hot"
#define META_DIR "meta"

#define HOST_EXE_BASENAME "utilities_host"
#if SOB_WINDOWS
#define HOST_OUTPUT_PATH BUILD_DIR "/" HOST_EXE_BASENAME ".exe"
#else
#define HOST_OUTPUT_PATH BUILD_DIR "/" HOST_EXE_BASENAME
#endif

#define METAGEN_EXE_BASENAME "metagen"
#if SOB_WINDOWS
#define METAGEN_OUTPUT_PATH BUILD_TOOLS_DIR "/" METAGEN_EXE_BASENAME ".exe"
#else
#define METAGEN_OUTPUT_PATH BUILD_TOOLS_DIR "/" METAGEN_EXE_BASENAME
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
    BuildTarget_Clean,
} BuildTarget;

static void print_usage(void) {
    printf("Usage: ./sob [target] [mode]\n");
    printf("\n");
    printf("Targets:\n");
    printf("  run      Build host + hot module and start the app (default)\n");
    printf("  dev      Build host + hot module\n");
    printf("  all      Alias for dev\n");
    printf("  host     Build host executable only\n");
    printf("  module   Build hot-reload module only\n");
    printf("  ship     Build one executable with app statically linked\n");
    printf("  metagen  Build metagen and regenerate metadata\n");
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
    return "clean";
}

static void configure_compiler_for_mode(Sob_BuildContext* ctx, BuildMode mode) {
    Sob_CompilerConfig config = {0};
    config.kind = Sob_CompilerKind_Auto;
    config.warningsAsErrors = 0;

    if (mode == BuildMode_Debug) {
        config.optLevel = Sob_OptLevel_ReleaseSmall;
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
        "-Wno-c++20-designator",
        "-Wno-c99-designator",
    };

    for (S32 i = 0; i < (S32)(sizeof(flags) / sizeof(flags[0])); ++i) {
        sob_target_add_cflags(target, flags[i]);
    }
}

static void apply_cpp_runtime_flags(Sob_Target* target) {
    sob_target_add_cflags(target, "-fno-exceptions");
    sob_target_add_cflags(target, "-fno-rtti");
}

static void apply_third_party_warning_flags(Sob_Target* target) {
    const char* flags[] = {
        "-Wno-deprecated-declarations",
    };

    for (S32 i = 0; i < (S32)(sizeof(flags) / sizeof(flags[0])); ++i) {
        sob_target_add_cflags(target, flags[i]);
    }
}

static void apply_mode_target_flags(Sob_Target* target, BuildMode mode) {
    if (mode == BuildMode_Debug) {
        sob_target_define(target, "DEBUG", .value = "1");
        sob_target_add_cflags(target, "-O0");
        sob_target_add_cflags(target, "-fno-omit-frame-pointer");
        sob_target_add_cflags(target, "-g0");
    } else if (mode == BuildMode_Asan) {
        sob_target_define(target, "DEBUG", .value = "1");
        sob_target_add_cflags(target, "-fno-omit-frame-pointer");
        sob_target_add_cflags(target, "-gline-tables-only");
    } else {
        sob_target_define(target, "NDEBUG", .value = "1");
    }
}

static void configure_common_includes(Sob_Target* target) {
    sob_target_add_include(target, ".");
}

static void configure_runtime_executable(Sob_Target* target, BuildMode mode) {
    sob_target_add_source(target, "main.mm");
    configure_common_includes(target);
    sob_target_set_standard(target, Sob_Standard_Cpp17);
    apply_cpp_runtime_flags(target);
    apply_common_warning_flags(target);
    apply_mode_target_flags(target, mode);

#if SOB_MACOS
    sob_target_link(target, "Cocoa", .kind = Sob_LibKind_Framework);
    sob_target_link(target, "QuartzCore", .kind = Sob_LibKind_Framework);
    sob_target_link(target, "Metal", .kind = Sob_LibKind_Framework);
#endif
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
    sob_target_set_standard(metagen, Sob_Standard_Cpp17);
    apply_cpp_runtime_flags(metagen);
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

    if (requestedTarget == BuildTarget_Module || requestedTarget == BuildTarget_All ||
        requestedTarget == BuildTarget_Dev) {
        Sob_Target* moduleTarget = sob_target_create(ctx, "utilities_app", Sob_TargetKind_DynamicLib,
                                                     .outputDir = BUILD_HOT_DIR,
                                                     .outputName = "utilities_app");
        if (!moduleTarget) {
            fprintf(stderr, "Error: failed to create module target\n");
            sob_arena_destroy(arena);
            return 1;
        }

        sob_target_add_source(moduleTarget, "app.cpp");
        configure_common_includes(moduleTarget);
        sob_target_set_standard(moduleTarget, Sob_Standard_Cpp17);
        apply_cpp_runtime_flags(moduleTarget);
        sob_target_add_cflags(moduleTarget, "-fvisibility=hidden");
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

        configure_runtime_executable(hostTarget, mode);

#if SOB_MACOS
        sob_target_add_ldflags(hostTarget, "-Wl,-export_dynamic");
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

        configure_runtime_executable(shipTarget, mode);
        sob_target_add_source(shipTarget, "app.cpp");
        sob_target_define(shipTarget, "UTILITIES_STATIC_APP", .value = "1");
    }

    printf("==> Building %s (%s)...\n", build_target_name(requestedTarget), build_mode_name(mode));

    S32 result = sob_build_run(ctx);
    sob_arena_destroy(arena);

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

    sob_cmd_append(cmd, HOST_OUTPUT_PATH);

    printf("==> Running %s...\n", HOST_OUTPUT_PATH);
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
    SOB_GO_REBUILD_URSELF(argc, argv);

    BuildTarget target = BuildTarget_Run;
    BuildMode mode = BuildMode_Debug;

    if (argc >= 2) {
        BuildTarget parsedTarget = BuildTarget_Run;
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
            target = BuildTarget_Run;
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
    if (target == BuildTarget_Run || target == BuildTarget_Host || target == BuildTarget_Module ||
        target == BuildTarget_All || target == BuildTarget_Dev || target == BuildTarget_Ship) {
        fprintf(stderr, "Error: '%s' build is currently supported only on macOS.\n",
                build_target_name(target));
        fprintf(stderr, "Use './sob metagen' for metadata generation on this platform.\n");
        return 1;
    }
#endif

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
        S32 buildResult = build_dev_targets_parallel(mode, argv[0]);
        if (buildResult != 0) {
            return buildResult;
        }

        return run_app_command();
    }

    if (target == BuildTarget_All || target == BuildTarget_Dev) {
        return build_dev_targets_parallel(mode, argv[0]);
    }

    return build_project_targets(target, mode);
}
