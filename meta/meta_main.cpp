//
// Created by André Leite on 10/12/2024.
//

#include "nstl/base/base_include.hpp"
#include "nstl/base/base_spmd.hpp"
#include "nstl/os/os_include.hpp"

#include "nstl/os/os_include.cpp"
#include "nstl/base/base_include.cpp"

#include "lexer.hpp"
#include "parser.hpp"
#include "generator.hpp"

#include "lexer.cpp"
#include "parser.cpp"
#include "generator.cpp"

#define METAGEN_VERSION "0.1.0"

#define METAGEN_DEFAULT_INPUT_EXT ".metadef"
#define METAGEN_DEFAULT_OUTPUT_SUFFIX ".generated.hpp"

struct MetagenConfig {
    StringU8 inputDir;
    StringU8 outputDir;
    StringU8 inputExtension;
    StringU8 outputSuffix;
    B32 verbose;
    B32 dryRun;
    U32 threadCount;
};

struct MetaFile {
    StringU8 path;
    StringU8 contents;
    MetaFile* next;
};

struct FileCollector {
    Arena* arena;
    MetaFile* head;
    MetaFile* tail;
    U32 count;
    OS_Handle mutex;
    StringU8 extension;
};

static void collect_meta_file(const char* path, B32 isDirectory, void* userData) {
    FileCollector* collector = (FileCollector*)userData;
    
    if (isDirectory) {
        return;
    }
    
    StringU8 pathStr = str8(path);
    if (!str8_ends_with(pathStr, collector->extension)) {
        return;
    }
    
    OS_mutex_lock(collector->mutex);
    DEFER_REF(OS_mutex_unlock(collector->mutex));
    
    MetaFile* file = ARENA_PUSH_STRUCT(collector->arena, MetaFile);
    MEMSET(file, 0, sizeof(MetaFile));
    file->path = str8_cpy(collector->arena, path);
    
    SLIST_APPEND(collector->head, collector->tail, file);
    collector->count++;
}

static FileCollector* discover_files(Arena* arena, StringU8 inputDir, StringU8 extension) {
    FileCollector* collector = ARENA_PUSH_STRUCT(arena, FileCollector);
    MEMSET(collector, 0, sizeof(FileCollector));
    collector->arena = arena;
    collector->mutex = OS_mutex_create();
    collector->extension = extension;
    
    StringU8 normalizedDir = str8_path_normalize(inputDir);
    
    char* dirPath = ARENA_PUSH_ARRAY(arena, char, normalizedDir.size + 1);
    MEMCPY(dirPath, normalizedDir.data, normalizedDir.size);
    dirPath[normalizedDir.size] = '\0';
    
    OS_dir_iterate(dirPath, collect_meta_file, collector, 1 /* recursive */);
    
    OS_mutex_destroy(collector->mutex);
    return collector;
}

struct ProcessResult {
    StringU8 inputPath;
    StringU8 outputPath;
    StringU8 generatedCode;
    B32 success;
    StringU8 errorMessage;
};

static ProcessResult process_file(Arena* arena, MetaFile* file, StringU8 outputDir, 
                                   StringU8 inputExtension, StringU8 outputSuffix) {
    ProcessResult result = {};
    result.inputPath = file->path;
    
    char* pathCStr = ARENA_PUSH_ARRAY(arena, char, file->path.size + 1);
    MEMCPY(pathCStr, file->path.data, file->path.size);
    pathCStr[file->path.size] = '\0';
    
    OS_Handle fh = OS_file_open(pathCStr, OS_FileOpenMode_Read);
    if (!fh.handle) {
        result.success = 0;
        result.errorMessage = str8_concat(arena, str8("Failed to open file: "), file->path);
        return result;
    }
    DEFER_REF(OS_file_close(fh));
    
    U64 fileSize = OS_file_size(fh);
    U8* contents = ARENA_PUSH_ARRAY(arena, U8, fileSize + 1);
    OS_file_read(fh, fileSize, contents);
    contents[fileSize] = '\0';
    
    StringU8 source = str8(contents, fileSize);
    
    StringU8 basename = str8_path_basename(file->path);
    StringU8 stem = str8_strip_suffix(basename, inputExtension);
    StringU8 outputFilename = str8_concat(arena, stem, outputSuffix);
    result.outputPath = str8_path_join(arena, outputDir, outputFilename);
    
    Lexer lexer;
    lexer_init(&lexer, arena, source, file->path);
    
    Parser parser;
    parser_init(&parser, arena, &lexer);
    
    ASTFile* ast = parser_parse_file(&parser);
    if (ast->hasError) {
        result.success = 0;
        result.errorMessage = ast->errorMessage;
        return result;
    }
    
    Generator gen;
    generator_init(&gen, arena);
    generator_generate_file(&gen, ast);
    
    result.generatedCode = generator_get_output(&gen);
    result.success = 1;
    return result;
}

struct SPMDProcessContext {
    MetaFile** files;
    U32 fileCount;
    ProcessResult* results;
    Arena* resultsArena;
    StringU8* outputDirs;
    StringU8 inputExtension;
    StringU8 outputSuffix;
    OS_Handle resultMutex;
    U32 successCount;
    U32 failCount;
    B32 verbose;
};

static void spmd_process_files_kernel(void* arg) {
    SPMDProcessContext* ctx = (SPMDProcessContext*)arg;
    
    RangeU64 myRange = SPMD_SPLIT_RANGE(ctx->fileCount);
    
    for (U64 i = myRange.min; i < myRange.max; ++i) {
        Temp scratch = get_scratch(0, 0);
        DEFER_REF(temp_end(&scratch));
        
        StringU8 outputDir = ctx->outputDirs[i];
        
        ProcessResult result = process_file(scratch.arena, ctx->files[i], outputDir,
                                            ctx->inputExtension, ctx->outputSuffix);
        
        OS_mutex_lock(ctx->resultMutex);
        ctx->results[i].success = result.success;
        ctx->results[i].inputPath = str8_cpy(ctx->resultsArena, result.inputPath);
        ctx->results[i].outputPath = str8_cpy(ctx->resultsArena, result.outputPath);
        ctx->results[i].generatedCode = str8_cpy(ctx->resultsArena, result.generatedCode);
        if (!result.success) {
            ctx->results[i].errorMessage = str8_cpy(ctx->resultsArena, result.errorMessage);
            ctx->failCount++;
        } else {
            ctx->successCount++;
        }
        OS_mutex_unlock(ctx->resultMutex);
    }
}

static void print_usage() {
    printf("metagen v%s - Meta Pre-compiler for C++\n\n", METAGEN_VERSION);
    printf("Usage: metagen [options] <input_dir>\n\n");
    printf("Options:\n");
    printf("  -o, --output <dir>     Output directory (default: same as input file location)\n");
    printf("  -e, --ext <extension>  Input file extension (default: %s)\n", METAGEN_DEFAULT_INPUT_EXT);
    printf("  -s, --suffix <suffix>  Output file suffix (default: %s)\n", METAGEN_DEFAULT_OUTPUT_SUFFIX);
    printf("  -v, --verbose          Verbose output\n");
    printf("  -n, --dry-run          Parse and generate but don't write files\n");
    printf("  -j, --jobs <n>         Number of parallel jobs (default: CPU cores)\n");
    printf("  -h, --help             Show this help\n");
    printf("\nExample:\n");
    printf("  metagen -o build/generated nstl/\n");
    printf("  metagen -e .def -s .gen.hpp src/\n");
}

static MetagenConfig parse_args(int argc, char** argv, Arena* arena) {
    MetagenConfig config = {};
    config.verbose = 0;
    config.dryRun = 0;
    config.threadCount = OS_get_system_info()->logicalCores;
    config.inputExtension = str8(METAGEN_DEFAULT_INPUT_EXT);
    config.outputSuffix = str8(METAGEN_DEFAULT_OUTPUT_SUFFIX);
    
    for (int i = 1; i < argc; ++i) {
        StringU8 arg = str8(argv[i]);
        
        if (str8_equal(arg, str8("-h")) || str8_equal(arg, str8("--help"))) {
            print_usage();
            exit(0);
        } else if (str8_equal(arg, str8("-v")) || str8_equal(arg, str8("--verbose"))) {
            config.verbose = 1;
        } else if (str8_equal(arg, str8("-n")) || str8_equal(arg, str8("--dry-run"))) {
            config.dryRun = 1;
        } else if (str8_equal(arg, str8("-o")) || str8_equal(arg, str8("--output"))) {
            if (i + 1 < argc) {
                config.outputDir = str8(argv[++i]);
            } else {
                fprintf(stderr, "Error: -o requires an argument\n");
                exit(1);
            }
        } else if (str8_equal(arg, str8("-e")) || str8_equal(arg, str8("--ext"))) {
            if (i + 1 < argc) {
                config.inputExtension = str8(argv[++i]);
            } else {
                fprintf(stderr, "Error: -e requires an argument\n");
                exit(1);
            }
        } else if (str8_equal(arg, str8("-s")) || str8_equal(arg, str8("--suffix"))) {
            if (i + 1 < argc) {
                config.outputSuffix = str8(argv[++i]);
            } else {
                fprintf(stderr, "Error: -s requires an argument\n");
                exit(1);
            }
        } else if (str8_equal(arg, str8("-j")) || str8_equal(arg, str8("--jobs"))) {
            if (i + 1 < argc) {
                config.threadCount = (U32)atoi(argv[++i]);
            } else {
                fprintf(stderr, "Error: -j requires an argument\n");
                exit(1);
            }
        } else if (arg.data[0] != '-') {
            config.inputDir = arg;
        } else {
            fprintf(stderr, "Error: Unknown option: %.*s\n", (int)arg.size, arg.data);
            exit(1);
        }
    }
    
    if (str8_is_nil(config.inputDir) || str8_is_empty(config.inputDir)) {
        fprintf(stderr, "Error: No input directory specified\n\n");
        print_usage();
        exit(1);
    }
    
    return config;
}

int main(int argc, char** argv) {
    OS_init();
    
    thread_context_init();
    DEFER(thread_context_release());
    
    Arena* arena = arena_alloc();
    
    MetagenConfig config = parse_args(argc, argv, arena);
    
    if (config.verbose) {
        printf("metagen v%s\n", METAGEN_VERSION);
        printf("Input directory: %.*s\n", (int)config.inputDir.size, config.inputDir.data);
        if (!str8_is_nil(config.outputDir)) {
            printf("Output directory: %.*s\n", (int)config.outputDir.size, config.outputDir.data);
        }
        printf("Input extension: %.*s\n", (int)config.inputExtension.size, config.inputExtension.data);
        printf("Output suffix: %.*s\n", (int)config.outputSuffix.size, config.outputSuffix.data);
        printf("Thread count: %u\n", config.threadCount);
    }
    
    FileCollector* collector = discover_files(arena, config.inputDir, config.inputExtension);
    
    if (collector->count == 0) {
        if (config.verbose) {
            printf("No %.*s files found\n", (int)config.inputExtension.size, config.inputExtension.data);
        }
        return 0;
    }
    
    if (config.verbose) {
        printf("Found %u %.*s files\n", collector->count, 
               (int)config.inputExtension.size, config.inputExtension.data);
    }
    
    MetaFile** files = ARENA_PUSH_ARRAY(arena, MetaFile*, collector->count);
    U32 idx = 0;
    for (MetaFile* f = collector->head; f; f = f->next) {
        files[idx++] = f;
    }
    
    StringU8* outputDirs = ARENA_PUSH_ARRAY(arena, StringU8, collector->count);
    for (U32 i = 0; i < collector->count; ++i) {
        if (str8_is_nil(config.outputDir) || str8_is_empty(config.outputDir)) {
            outputDirs[i] = str8_path_directory(files[i]->path);
        } else {
            outputDirs[i] = config.outputDir;
        }
    }
    
    ProcessResult* results = ARENA_PUSH_ARRAY(arena, ProcessResult, collector->count);
    MEMSET(results, 0, sizeof(ProcessResult) * collector->count);
    
    U32 successCount = 0;
    U32 failCount = 0;
    
    B32 useParallel = (collector->count > 1) && (config.threadCount > 1);
    
    if (useParallel) {
        SPMDProcessContext ctx = {};
        ctx.files = files;
        ctx.fileCount = collector->count;
        ctx.results = results;
        ctx.resultsArena = arena;
        ctx.outputDirs = outputDirs;
        ctx.inputExtension = config.inputExtension;
        ctx.outputSuffix = config.outputSuffix;
        ctx.resultMutex = OS_mutex_create();
        ctx.successCount = 0;
        ctx.failCount = 0;
        ctx.verbose = config.verbose;
        
        U32 laneCount = MIN(config.threadCount, collector->count);
        
        SPMDGroup* group = spmd_run(arena,
            .laneCount = laneCount,
            .kernel = spmd_process_files_kernel,
            .kernelParameters = &ctx
        );
        
        spmd_group_destroy(group);
        OS_mutex_destroy(ctx.resultMutex);
        successCount = ctx.successCount;
        failCount = ctx.failCount;
    } else {
        for (U32 i = 0; i < collector->count; ++i) {
            Temp scratch = get_scratch(&arena, 1);
            DEFER_REF(temp_end(&scratch));
            
            results[i] = process_file(scratch.arena, files[i], outputDirs[i],
                                      config.inputExtension, config.outputSuffix);
            
            results[i].inputPath = str8_cpy(arena, results[i].inputPath);
            results[i].outputPath = str8_cpy(arena, results[i].outputPath);
            results[i].generatedCode = str8_cpy(arena, results[i].generatedCode);
            if (!results[i].success) {
                results[i].errorMessage = str8_cpy(arena, results[i].errorMessage);
                failCount++;
            } else {
                successCount++;
            }
        }
    }
    
    for (U32 i = 0; i < collector->count; ++i) {
        ProcessResult* result = &results[i];
        
        if (result->success) {
            if (config.verbose) {
                printf("  [OK] %.*s -> %.*s\n",
                    (int)result->inputPath.size, result->inputPath.data,
                    (int)result->outputPath.size, result->outputPath.data);
            }
            
            if (!config.dryRun) {
                Temp scratch = get_scratch(&arena, 1);
                DEFER_REF(temp_end(&scratch));
                
                char* outPathCStr = ARENA_PUSH_ARRAY(scratch.arena, char, result->outputPath.size + 1);
                MEMCPY(outPathCStr, result->outputPath.data, result->outputPath.size);
                outPathCStr[result->outputPath.size] = '\0';
                
                OS_Handle out = OS_file_open(outPathCStr, OS_FileOpenMode_Create);
                if (!out.handle) {
                    fprintf(stderr, "Error: Failed to create output file: %s\n", outPathCStr);
                    failCount++;
                    successCount--;
                } else {
                    OS_file_write(out, result->generatedCode.size, result->generatedCode.data);
                    OS_file_close(out);
                }
            }
        } else {
            fprintf(stderr, "  [FAIL] %.*s: %.*s\n",
                (int)result->inputPath.size, result->inputPath.data,
                (int)result->errorMessage.size, result->errorMessage.data);
        }
    }
    
    printf("Processed %u files: %u succeeded, %u failed\n", collector->count, successCount, failCount);
    
    return (failCount > 0) ? 1 : 0;
}
