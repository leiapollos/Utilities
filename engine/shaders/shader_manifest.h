#pragma once

//
// Single source of truth for shader entry points: one ENG_SHADER_LIST
// row each. Everything else derives — entry strings, output paths
// ("<entry>.<backend ext>" by rule), the EngShaderId enum, the runtime
// path table, and sob's compile list. Adding an entry point is one row
// here plus the slang code.
//

#define ENG_SHADER_MANIFEST_SOURCE "engine/shaders/shader_manifest.h"
#define ENG_SHADER_OUTPUT_DIR "build/shaders"

#define ENG_SHADER_SLANG_DRAW2D_SOURCE "engine/shaders/draw2d.slang"
#define ENG_SHADER_SLANG_WORLD_SOURCE "engine/shaders/world.slang"
#define ENG_SHADER_SLANG_WORLD_CULL_SOURCE "engine/shaders/world_cull.slang"
#define ENG_SHADER_SLANG_ABI_SOURCE "engine/shaders/gfx_shader_abi.slang"
#define ENG_SHADER_SLANG_RECORDS_SOURCE "engine/shaders/shader_records.generated.hpp"

// Compile inputs for rebuild detection (sob + the dev hot rebuild); the
// ABI and generated records are include deps, not compile roots.
#define ENG_SHADER_SOURCE_LIST(X) \
    X(Draw2dSlang, ENG_SHADER_SLANG_DRAW2D_SOURCE) \
    X(WorldSlang, ENG_SHADER_SLANG_WORLD_SOURCE) \
    X(WorldCullSlang, ENG_SHADER_SLANG_WORLD_CULL_SOURCE) \
    X(GfxShaderAbiSlang, ENG_SHADER_SLANG_ABI_SOURCE) \
    X(ShaderRecordsSlang, ENG_SHADER_SLANG_RECORDS_SOURCE)

// X(name, source, entry, stage, kind)
#define ENG_SHADER_LIST(X) \
    X(Draw2dVertex,   ENG_SHADER_SLANG_DRAW2D_SOURCE,     draw2d_vertex,   vertex,   graphics) \
    X(Draw2dFragment, ENG_SHADER_SLANG_DRAW2D_SOURCE,     draw2d_fragment, fragment, graphics) \
    X(WorldVertex,    ENG_SHADER_SLANG_WORLD_SOURCE,      world_vertex,    vertex,   graphics) \
    X(WorldFragment,  ENG_SHADER_SLANG_WORLD_SOURCE,      world_fragment,  fragment, graphics) \
    X(WorldReset,     ENG_SHADER_SLANG_WORLD_CULL_SOURCE, world_reset,     compute,  compute) \
    X(WorldCull,      ENG_SHADER_SLANG_WORLD_CULL_SOURCE, world_cull,      compute,  compute) \
    X(WorldPrefix,    ENG_SHADER_SLANG_WORLD_CULL_SOURCE, world_prefix,    compute,  compute) \
    X(WorldScatter,   ENG_SHADER_SLANG_WORLD_CULL_SOURCE, world_scatter,   compute,  compute) \
    X(WorldArgs,      ENG_SHADER_SLANG_WORLD_CULL_SOURCE, world_args,      compute,  compute)

#if defined(PLATFORM_OS_WINDOWS) || defined(SOB_WINDOWS)
#define ENG_SHADER_OUTPUT_EXT ".spv"
#elif defined(PLATFORM_OS_MACOS) || defined(SOB_MACOS)
#define ENG_SHADER_OUTPUT_EXT ".metal"
#else
#error No shader backend configured for this platform.
#endif

#define ENG_SHADER_OUTPUT_PATH(entry) ENG_SHADER_OUTPUT_DIR "/" #entry ENG_SHADER_OUTPUT_EXT

// Runtime-only projections (sob expands its own build items).
#if defined(PLATFORM_OS_WINDOWS) || defined(PLATFORM_OS_MACOS)
#define ENG_SHADER_ID_ENTRY_(name, source, entry, stage, kind) EngShader_##name,
enum EngShaderId {
    ENG_SHADER_LIST(ENG_SHADER_ID_ENTRY_)
    EngShader_Count,
};
#undef ENG_SHADER_ID_ENTRY_

#define ENG_SHADER_PATH_ENTRY_(name, source, entry, stage, kind) ENG_SHADER_OUTPUT_PATH(entry),
static const char* ENG_SHADER_RUNTIME_PATHS[EngShader_Count] = {
    ENG_SHADER_LIST(ENG_SHADER_PATH_ENTRY_)
};
#undef ENG_SHADER_PATH_ENTRY_

#define ENG_SHADER_NAME_ENTRY_(name, source, entry, stage, kind) #entry,
static const char* ENG_SHADER_ENTRY_NAMES[EngShader_Count] = {
    ENG_SHADER_LIST(ENG_SHADER_NAME_ENTRY_)
};
#undef ENG_SHADER_NAME_ENTRY_
#endif
