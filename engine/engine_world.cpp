//
// Created by André Leite on 11/06/2026.
//
// World renderer: mesh/material tables, extraction lane writers, GPU
// visibility passes, and the forward pass. The asset system
// (engine_assets.cpp) publishes into the tables here.
//

#include "engine_world_kernels.hpp"

enum EngWorldShaderSlot {
    EngWorldShaderSlot_Vertex = 0,
    EngWorldShaderSlot_Fragment,
    EngWorldShaderSlot_Reset,
    EngWorldShaderSlot_Cull,
    EngWorldShaderSlot_Prefix,
    EngWorldShaderSlot_Scatter,
    EngWorldShaderSlot_Args,
};

// World pass slot -> manifest id; paths come off the runtime table.
static const EngShaderId ENG_WORLD_SHADERS[ENG_WORLD_SHADER_COUNT] = {
    EngShader_WorldVertex,
    EngShader_WorldFragment,
    EngShader_WorldReset,
    EngShader_WorldCull,
    EngShader_WorldPrefix,
    EngShader_WorldScatter,
    EngShader_WorldArgs,
};

// "<exe>/../<runtime path>" — shared by the world and 2D passes.
static StringU8 eng_shader_runtime_path_(Arena* arena, StringU8 exeDir, EngShaderId shader) {
    StringU8 path = str8_concat(arena, exeDir, str8("/../"));
    return str8_concat(arena, path, str8(ENG_SHADER_RUNTIME_PATHS[shader]));
}

// Which cooked models and sounds exist is project policy (the EngProject
// desc tables); the machinery here — watch -> artifact -> publish into
// the mesh tables / the host audio buffer table — is the engine's.
// Sibling texture paths derive via asset_model_texture_path (the rule
// lives with the cooked format in engine_assets.hpp).

static void eng_world_resource_cache_reset_(EngContext* ctx) {
    EngWorldState* world = &ctx->engine->world;
    for (U32 shaderIndex = 0u; shaderIndex < ENG_WORLD_SHADER_COUNT; ++shaderIndex) {
        world->shaderFiles[shaderIndex] = FILE_HANDLE_ZERO;
        world->shaderHashes[shaderIndex] = CONTENT_HASH_ZERO;
    }
}

static void eng_world_watch_files_(EngContext* ctx) {
    EngState* state = ctx->engine;
    if (!state->resources.fileStream) {
        return;
    }
    if (!(eng_project_()->capabilities & ENG_CAP_WORLD3D)) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    StringU8 exeDir = OS_get_executable_directory(scratch.arena);
    for (U32 shaderIndex = 0u; shaderIndex < ENG_WORLD_SHADER_COUNT; ++shaderIndex) {
        StringU8 worldPath = eng_shader_runtime_path_(scratch.arena, exeDir, ENG_WORLD_SHADERS[shaderIndex]);
        state->world.shaderFiles[shaderIndex] = file_watch(state->resources.fileStream, worldPath, 0u);
    }
}

// ////////////////////////
// World renderer (U5)

#define ENG_WORLD_CULL_GROUP_SIZE 64u
#define ENG_WORLD_MATERIAL_FLAG_ALPHA_TEST 1u
#define ENG_WORLD_MATERIAL_FLAG_TEXTURED 2u
// Sentinel for ShdWorldForwardRootData.directFirstRenderable: draw goes
// through cellOffsets/visibleBuffer instead of a direct base index.
#define ENG_WORLD_DIRECT_NONE 0xFFFFFFFFu

enum EngWorldFailLog {
    EngWorldFailLog_FrameAlloc = (1u << 0u),
    EngWorldFailLog_MappedBuffers = (1u << 1u),
    EngWorldFailLog_TransparentBuild = (1u << 2u),
    EngWorldFailLog_RootTemp = (1u << 3u),
    EngWorldFailLog_DrawAlloc = (1u << 4u),
};

static void eng_world_fail_once_(EngWorldState* world, U32 bit, const char* message) {
    if (FLAGS_HAS(world->failLogMask, bit)) {
        return;
    }
    LOG_ERROR("gfx", "World frame dropped: {}", str8(message));
    world->failLogMask |= bit;
}

static const U32 ENG_WORLD_COMPUTE_GROUP_SIZES[5] = {
    ENG_WORLD_CULL_GROUP_SIZE,
    ENG_WORLD_CULL_GROUP_SIZE,
    1u,
    ENG_WORLD_CULL_GROUP_SIZE,
    ENG_WORLD_CULL_GROUP_SIZE,
};

static EngWorldMeshHandle eng_world_register_mesh_(EngWorldState* world, U32 firstIndex, U32 indexCount,
                                                   U32 baseVertex, GfxBuffer vertexBuffer,
                                                   GfxResourceId vertexBufferId, GfxBuffer indexBuffer,
                                                   B32 ownsBuffers, Vec3F32 center, Vec3F32 extents) {
    // World tables are immutable between begin and execute; publishes run in
    // the artifact tick before the frame opens.
    ASSERT_DEBUG(!world->frameOpen);
    EngWorldMeshHandle handle = {};
    void* item = 0;
    U32 slot = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&world->meshes, &item, &slot, &generation)) {
        return handle;
    }
    EngWorldMesh* mesh = (EngWorldMesh*)item;
    mesh->indexCount = indexCount;
    mesh->firstIndex = firstIndex;
    mesh->baseVertex = baseVertex;
    mesh->vertexBuffer = vertexBuffer;
    mesh->vertexBufferId = vertexBufferId;
    mesh->vertexByteOffset = 0u;
    mesh->indexBuffer = indexBuffer;
    mesh->indexByteOffset = 0u;
    mesh->ownsBuffers = ownsBuffers;
    mesh->boundsCenter = center;
    mesh->boundsExtents = extents;
    mesh->boundsRadius = vec3_length(extents);
    handle.index = slot;
    handle.generation = generation;
    world->meshCount += 1u;
    world->meshRecordsDirty = 1;
    return handle;
}

static void eng_world_release_mesh_(GfxDevice* device, EngWorldState* world, EngWorldMeshHandle handle) {
    ASSERT_DEBUG(!world->frameOpen);
    void* item = 0;
    if (!slot_map_release(&world->meshes, handle.index, handle.generation, &item) || !item) {
        return;
    }
    EngWorldMesh* mesh = (EngWorldMesh*)item;
    if (mesh->ownsBuffers) {
        gfx_destroy_buffer(device, mesh->vertexBuffer);
        gfx_destroy_buffer(device, mesh->indexBuffer);
    }
    MEMSET(mesh, 0, sizeof(*mesh));
    world->meshCount -= 1u;
    world->meshRecordsDirty = 1;
}

// ////////////////////////
// Material table: fixed slots behind a used mask. Slot 0 is the builtin
// "missing" material (magenta) so unset or released references fail loudly.

static B32 eng_world_material_alloc(EngWorldState* world, U32* outIndex) {
    for (U32 index = ENG_WORLD_MATERIAL_MISSING + 1u; index < ENG_WORLD_MAX_MATERIALS; ++index) {
        if (FLAGS_HAS(world->materialUsedMask, 1ull << index)) {
            continue;
        }
        world->materialUsedMask |= 1ull << index;
        MEMSET(&world->materialRecords[index], 0, sizeof(world->materialRecords[index]));
        world->materialsDirty = 1;
        *outIndex = index;
        return 1;
    }
    LOG_ERROR("gfx", "World material table full ({} slots)", ENG_WORLD_MAX_MATERIALS);
    return 0;
}

static void eng_world_material_set(EngWorldState* world, U32 index, const ShdWorldMaterialRecord* record) {
    if (index >= ENG_WORLD_MAX_MATERIALS || !FLAGS_HAS(world->materialUsedMask, 1ull << index)) {
        return;
    }
    world->materialRecords[index] = *record;
    world->materialsDirty = 1;
}

static void eng_world_material_release(EngWorldState* world, U32 index) {
    if (index <= ENG_WORLD_MATERIAL_MISSING || index >= ENG_WORLD_MAX_MATERIALS) {
        return;
    }
    world->materialUsedMask &= ~(1ull << index);
    // In-flight references to the freed slot show the missing color until
    // the slot is reused.
    world->materialRecords[index] = world->materialRecords[ENG_WORLD_MATERIAL_MISSING];
    world->materialsDirty = 1;
}

static B32 eng_world_try_create_resources_(EngContext* ctx) {
    EngState* state = ctx->engine;
    EngWorldState* world = &state->world;
    if (world->gpuResourcesCreated || ctx->host->gfxDevice == 0 || state->resources.arena == 0) {
        return world->gpuResourcesCreated;
    }

    GfxDevice* device = ctx->host->gfxDevice;
    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    if (world->meshes.items == 0 &&
        !slot_map_init(&world->meshes, state->resources.arena, sizeof(EngWorldMesh), ENG_WORLD_MAX_MESHES)) {
        return 0;
    }

    EngWorldMeshBuilder builder = {};
    builder.vertexCapacity = 4096u;
    builder.indexCapacity = 16384u;
    builder.vertices = ARENA_PUSH_ARRAY(scratch.arena, ShdWorldVertexRecord, builder.vertexCapacity);
    builder.indices = ARENA_PUSH_ARRAY(scratch.arena, U32, builder.indexCapacity);
    if (!builder.vertices || !builder.indices) {
        return 0;
    }

    U32 cubeFirstIndex = builder.indexCount;
    eng_world_build_cube_(&builder);
    U32 cubeIndexCount = builder.indexCount - cubeFirstIndex;

    U32 sphereFirstIndex = builder.indexCount;
    eng_world_build_sphere_(&builder, 12u, 18u);
    U32 sphereIndexCount = builder.indexCount - sphereFirstIndex;

    U32 planeFirstIndex = builder.indexCount;
    eng_world_build_plane_(&builder);
    U32 planeIndexCount = builder.indexCount - planeFirstIndex;

    GfxBufferDesc vertexDesc = {};
    vertexDesc.name = "world vertices";
    vertexDesc.size = sizeof(ShdWorldVertexRecord) * builder.vertexCount;
    vertexDesc.usageFlags = GfxBufferUsageFlags_Storage;
    vertexDesc.memoryKind = GfxMemoryKind_Upload;
    vertexDesc.initialData = builder.vertices;
    world->vertexBuffer = gfx_create_buffer(device, &vertexDesc);
    world->vertexBufferId = gfx_register_buffer(device, world->vertexBuffer);

    GfxBufferDesc indexDesc = {};
    indexDesc.name = "world indices";
    indexDesc.size = sizeof(U32) * builder.indexCount;
    indexDesc.usageFlags = GfxBufferUsageFlags_Index;
    indexDesc.memoryKind = GfxMemoryKind_Upload;
    indexDesc.initialData = builder.indices;
    world->indexBuffer = gfx_create_buffer(device, &indexDesc);

    // Builder indices are pool-absolute, so builtin records carry baseVertex 0;
    // cooked meshes bring mesh-relative indices in their own buffers.
    world->meshCount = 0u;
    world->builtinMeshes[0] = eng_world_register_mesh_(world, cubeFirstIndex, cubeIndexCount, 0u,
                                                       world->vertexBuffer, world->vertexBufferId, world->indexBuffer, 0,
                                                       vec3_make(0.0f, 0.0f, 0.0f), vec3_make(0.5f, 0.5f, 0.5f));
    world->builtinMeshes[1] = eng_world_register_mesh_(world, sphereFirstIndex, sphereIndexCount, 0u,
                                                       world->vertexBuffer, world->vertexBufferId, world->indexBuffer, 0,
                                                       vec3_make(0.0f, 0.0f, 0.0f), vec3_make(0.5f, 0.5f, 0.5f));
    world->builtinMeshes[2] = eng_world_register_mesh_(world, planeFirstIndex, planeIndexCount, 0u,
                                                       world->vertexBuffer, world->vertexBufferId, world->indexBuffer, 0,
                                                       vec3_make(0.0f, 0.0f, 0.0f), vec3_make(0.5f, 0.02f, 0.5f));

    GfxBufferDesc meshRecordDesc = {};
    meshRecordDesc.name = "world mesh records";
    meshRecordDesc.size = sizeof(ShdWorldMeshRecord) * ENG_WORLD_MAX_MESHES;
    meshRecordDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    meshRecordDesc.memoryKind = GfxMemoryKind_Device;
    world->meshRecordBuffer = gfx_create_buffer(device, &meshRecordDesc);
    world->meshRecordBufferId = gfx_register_buffer(device, world->meshRecordBuffer);
    world->meshRecordsDirty = 1;

    // Materials are scene/asset-allocated; the table starts with only the
    // builtin missing material in slot 0.
    MEMSET(world->materialRecords, 0, sizeof(world->materialRecords));
    ShdWorldMaterialRecord* missing = &world->materialRecords[ENG_WORLD_MATERIAL_MISSING];
    missing->baseColor[0] = 1.0f;
    missing->baseColor[1] = 0.0f;
    missing->baseColor[2] = 1.0f;
    missing->baseColor[3] = 1.0f;
    world->materialUsedMask = 1u << ENG_WORLD_MATERIAL_MISSING;
    world->materialsDirty = 1;

    GfxSamplerDesc worldSamplerDesc = {};
    worldSamplerDesc.name = "world sampler";
    worldSamplerDesc.minFilter = GfxFilter_Linear;
    worldSamplerDesc.magFilter = GfxFilter_Linear;
    worldSamplerDesc.addressU = GfxAddressMode_Repeat;
    worldSamplerDesc.addressV = GfxAddressMode_Repeat;
    world->worldSampler = gfx_create_sampler(device, &worldSamplerDesc);
    world->worldSamplerId = gfx_register_sampler(device, world->worldSampler);

    GfxBufferDesc materialDesc = {};
    materialDesc.name = "world materials";
    materialDesc.size = sizeof(world->materialRecords);
    materialDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    materialDesc.memoryKind = GfxMemoryKind_Device;
    world->materialBuffer = gfx_create_buffer(device, &materialDesc);
    world->materialBufferId = gfx_register_buffer(device, world->materialBuffer);

    for (U32 bufferIndex = 0u; bufferIndex < ENG_WORLD_FRAME_BUFFER_COUNT; ++bufferIndex) {
        GfxBufferDesc frameDesc = {};
        frameDesc.name = "world frame record";
        frameDesc.size = sizeof(ShdWorldFrameRecord);
        frameDesc.usageFlags = GfxBufferUsageFlags_Storage;
        frameDesc.memoryKind = GfxMemoryKind_Upload;
        world->frameRecordBuffers[bufferIndex] = gfx_create_buffer(device, &frameDesc);
        world->frameRecordBufferIds[bufferIndex] = gfx_register_buffer(device, world->frameRecordBuffers[bufferIndex]);

        GfxBufferDesc renderableDesc = {};
        renderableDesc.name = "world renderables";
        renderableDesc.size = sizeof(ShdWorldRenderableRecord) * ENG_WORLD_MAX_RENDERABLES;
        // Per-frame data is written by the CPU straight into the mapping;
        // the [2] round-robin is the frames-in-flight protection.
        renderableDesc.usageFlags = GfxBufferUsageFlags_Storage;
        renderableDesc.memoryKind = GfxMemoryKind_Upload;
        world->renderableBuffers[bufferIndex] = gfx_create_buffer(device, &renderableDesc);
        world->renderableBufferIds[bufferIndex] = gfx_register_buffer(device, world->renderableBuffers[bufferIndex]);
    }

    GfxBufferDesc flagsDesc = {};
    flagsDesc.name = "world visibility flags";
    flagsDesc.size = sizeof(U32) * ENG_WORLD_MAX_RENDERABLES;
    flagsDesc.usageFlags = GfxBufferUsageFlags_Storage;
    flagsDesc.memoryKind = GfxMemoryKind_Device;
    world->flagsBuffer = gfx_create_buffer(device, &flagsDesc);
    world->flagsBufferId = gfx_register_buffer(device, world->flagsBuffer);

    U32 maxCells = ENG_WORLD_BIN_COUNT * ENG_WORLD_MAX_MESHES;
    GfxBufferDesc cellDesc = {};
    cellDesc.name = "world cell counts";
    cellDesc.size = sizeof(U32) * maxCells;
    cellDesc.usageFlags = GfxBufferUsageFlags_Storage;
    cellDesc.memoryKind = GfxMemoryKind_Device;
    world->cellCountBuffer = gfx_create_buffer(device, &cellDesc);
    world->cellCountBufferId = gfx_register_buffer(device, world->cellCountBuffer);
    cellDesc.name = "world cell offsets";
    world->cellOffsetBuffer = gfx_create_buffer(device, &cellDesc);
    world->cellOffsetBufferId = gfx_register_buffer(device, world->cellOffsetBuffer);
    cellDesc.name = "world cell cursors";
    world->cellCursorBuffer = gfx_create_buffer(device, &cellDesc);
    world->cellCursorBufferId = gfx_register_buffer(device, world->cellCursorBuffer);

    GfxBufferDesc visibleDesc = {};
    visibleDesc.name = "world visible list";
    visibleDesc.size = sizeof(U32) * ENG_WORLD_MAX_RENDERABLES;
    visibleDesc.usageFlags = GfxBufferUsageFlags_Storage;
    visibleDesc.memoryKind = GfxMemoryKind_Device;
    world->visibleBuffer = gfx_create_buffer(device, &visibleDesc);
    world->visibleBufferId = gfx_register_buffer(device, world->visibleBuffer);

    GfxBufferDesc argsDesc = {};
    argsDesc.name = "world indirect args";
    argsDesc.size = sizeof(GfxDrawIndexedIndirectArgs) * maxCells;
    argsDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_Indirect;
    argsDesc.memoryKind = GfxMemoryKind_Device;
    world->argsBuffer = gfx_create_buffer(device, &argsDesc);
    world->argsBufferId = gfx_register_buffer(device, world->argsBuffer);

    B32 created = world->vertexBuffer.generation != 0u &&
                  world->indexBuffer.generation != 0u &&
                  world->meshRecordBuffer.generation != 0u &&
                  world->materialBuffer.generation != 0u &&
                  world->flagsBuffer.generation != 0u &&
                  world->cellCountBuffer.generation != 0u &&
                  world->cellOffsetBuffer.generation != 0u &&
                  world->cellCursorBuffer.generation != 0u &&
                  world->visibleBuffer.generation != 0u &&
                  world->argsBuffer.generation != 0u;
    for (U32 bufferIndex = 0u; bufferIndex < ENG_WORLD_FRAME_BUFFER_COUNT; ++bufferIndex) {
        created = created &&
                  world->frameRecordBuffers[bufferIndex].generation != 0u &&
                  world->renderableBuffers[bufferIndex].generation != 0u;
    }
    if (!created) {
        LOG_ERROR("gfx", "Failed to create world GPU resources");
        return 0;
    }

    world->gpuResourcesCreated = 1;
    LOG_INFO("gfx", "World renderer resources ready (meshes {}, vertices {}, indices {})",
             world->meshCount, builder.vertexCount, builder.indexCount);
    return 1;
}

static B32 eng_world_create_graphics_pipeline_(EngContext* ctx, ContentHash vertexHash, ContentHash fragmentHash,
                                               B32 transparent, GfxPipeline* outPipeline) {
    *outPipeline = {};
    ContentView vertexView = content_view_hash(ctx->engine->resources.contentStore, vertexHash);
    ContentView fragmentView = content_view_hash(ctx->engine->resources.contentStore, fragmentHash);
    if (!vertexView.valid || vertexView.size == 0u || !fragmentView.valid || fragmentView.size == 0u) {
        return 0;
    }

    GfxFormat colorFormats[1] = {GfxFormat_BGRA8_UNorm};
    GfxColorBlendState blendStates[1] = {};
    blendStates[0].writeFlags = GfxColorWriteFlags_RGBA;
    if (transparent) {
        blendStates[0].blendEnabled = 1;
        blendStates[0].srcColorFactor = GfxBlendFactor_SrcAlpha;
        blendStates[0].dstColorFactor = GfxBlendFactor_OneMinusSrcAlpha;
        blendStates[0].colorOp = GfxBlendOp_Add;
        blendStates[0].srcAlphaFactor = GfxBlendFactor_One;
        blendStates[0].dstAlphaFactor = GfxBlendFactor_OneMinusSrcAlpha;
        blendStates[0].alphaOp = GfxBlendOp_Add;
    }

    GfxGraphicsPipelineDesc desc = {};
    desc.name = transparent ? "world transparent" : "world opaque";
#if defined(PLATFORM_OS_WINDOWS)
    desc.vertexShader.format = GfxShaderFormat_SPIRV;
    desc.fragmentShader.format = GfxShaderFormat_SPIRV;
#else
    desc.vertexShader.format = GfxShaderFormat_MSL_Source;
    desc.fragmentShader.format = GfxShaderFormat_MSL_Source;
#endif
    desc.vertexShader.entry = ENG_SHADER_ENTRY_NAMES[EngShader_WorldVertex];
    desc.vertexShader.data = vertexView.data;
    desc.vertexShader.size = vertexView.size;
    desc.fragmentShader.entry = ENG_SHADER_ENTRY_NAMES[EngShader_WorldFragment];
    desc.fragmentShader.data = fragmentView.data;
    desc.fragmentShader.size = fragmentView.size;
    desc.topology = GfxPrimitiveTopology_TriangleList;
    // Closed transparent meshes cull their interiors: one blended layer per
    // surface instead of front+back double tint in index order. Outward
    // triangles project COUNTER-clockwise under the y-up projection (the
    // old CW declaration was calibrated against the upside-down image the
    // pre-fix Y-flipped projection produced).
    desc.raster.cullMode = transparent ? GfxCullMode_Back : GfxCullMode_None;
    desc.raster.frontFace = GfxFrontFace_CCW;
    desc.depth.depthTestEnabled = 1;
    desc.depth.depthWriteEnabled = transparent ? 0 : 1;
    desc.depth.compareOp = GfxCompareOp_Less;
    desc.colorFormats = colorFormats;
    desc.colorFormatCount = 1u;
    desc.blendStates = blendStates;
    desc.blendStateCount = 1u;
    desc.depthFormat = GfxFormat_D32_Float;

    GfxPipeline pipeline = gfx_create_graphics_pipeline(ctx->host->gfxDevice, &desc);
    if (pipeline.generation == 0u) {
        return 0;
    }
    *outPipeline = pipeline;
    return 1;
}

static void eng_world_try_update_pipelines_(EngContext* ctx) {
    EngState* state = ctx->engine;
    EngWorldState* world = &state->world;
    if (state->resources.fileStream == 0 || state->resources.contentStore == 0 || ctx->host->gfxDevice == 0) {
        return;
    }

    FileView views[ENG_WORLD_SHADER_COUNT];
    B32 allReady = 1;
    B32 anyChanged = 0;
    for (U32 shaderIndex = 0u; shaderIndex < ENG_WORLD_SHADER_COUNT; ++shaderIndex) {
        views[shaderIndex] = file_view(state->resources.fileStream, world->shaderFiles[shaderIndex]);
        if (views[shaderIndex].status != FileStatus_Ready || content_hash_is_zero(views[shaderIndex].hash)) {
            allReady = 0;
            break;
        }
        if (!content_hash_equal(world->shaderHashes[shaderIndex], views[shaderIndex].hash)) {
            anyChanged = 1;
        }
    }
    if (!allReady || (!anyChanged && world->opaquePipeline.generation != 0u)) {
        return;
    }

    GfxPipeline newOpaque = {};
    GfxPipeline newTransparent = {};
    if (!eng_world_create_graphics_pipeline_(ctx, views[EngWorldShaderSlot_Vertex].hash,
                                             views[EngWorldShaderSlot_Fragment].hash, 0, &newOpaque) ||
        !eng_world_create_graphics_pipeline_(ctx, views[EngWorldShaderSlot_Vertex].hash,
                                             views[EngWorldShaderSlot_Fragment].hash, 1, &newTransparent)) {
        gfx_destroy_pipeline(ctx->host->gfxDevice, newOpaque);
        return;
    }

    GfxPipeline newCompute[5] = {};
    B32 computeOk = 1;
    for (U32 passIndex = 0u; passIndex < 5u; ++passIndex) {
        ContentView view = content_view_hash(ctx->engine->resources.contentStore,
                                             views[EngWorldShaderSlot_Reset + passIndex].hash);
        if (!view.valid || view.size == 0u) {
            computeOk = 0;
            break;
        }
        const char* entryName = ENG_SHADER_ENTRY_NAMES[ENG_WORLD_SHADERS[EngWorldShaderSlot_Reset + passIndex]];
        GfxComputePipelineDesc desc = {};
        desc.name = entryName;
#if defined(PLATFORM_OS_WINDOWS)
        desc.shader.format = GfxShaderFormat_SPIRV;
#else
        desc.shader.format = GfxShaderFormat_MSL_Source;
#endif
        desc.shader.entry = entryName;
        desc.shader.data = view.data;
        desc.shader.size = view.size;
        desc.threadsPerThreadgroupX = ENG_WORLD_COMPUTE_GROUP_SIZES[passIndex];
        desc.threadsPerThreadgroupY = 1u;
        desc.threadsPerThreadgroupZ = 1u;
        newCompute[passIndex] = gfx_create_compute_pipeline(ctx->host->gfxDevice, &desc);
        if (newCompute[passIndex].generation == 0u) {
            computeOk = 0;
            break;
        }
    }
    if (!computeOk) {
        gfx_destroy_pipeline(ctx->host->gfxDevice, newOpaque);
        gfx_destroy_pipeline(ctx->host->gfxDevice, newTransparent);
        for (U32 passIndex = 0u; passIndex < 5u; ++passIndex) {
            gfx_destroy_pipeline(ctx->host->gfxDevice, newCompute[passIndex]);
        }
        return;
    }

    gfx_destroy_pipeline(ctx->host->gfxDevice, world->opaquePipeline);
    gfx_destroy_pipeline(ctx->host->gfxDevice, world->transparentPipeline);
    for (U32 passIndex = 0u; passIndex < 5u; ++passIndex) {
        gfx_destroy_pipeline(ctx->host->gfxDevice, world->computePipelines[passIndex]);
        world->computePipelines[passIndex] = newCompute[passIndex];
    }
    world->opaquePipeline = newOpaque;
    world->transparentPipeline = newTransparent;
    for (U32 shaderIndex = 0u; shaderIndex < ENG_WORLD_SHADER_COUNT; ++shaderIndex) {
        world->shaderHashes[shaderIndex] = views[shaderIndex].hash;
    }
    LOG_INFO("gfx", "World pipelines ready");
}

static void eng_world_ensure_depth_(EngContext* ctx) {
    EngState* state = ctx->engine;
    EngWorldState* world = &state->world;
    U32 width = ctx->host->windowWidth;
    U32 height = ctx->host->windowHeight;
    if (width == 0u || height == 0u) {
        return;
    }
    if (world->depthTexture.generation != 0u && world->depthWidth == width && world->depthHeight == height) {
        return;
    }
    if (world->depthTexture.generation != 0u) {
        gfx_destroy_texture(ctx->host->gfxDevice, world->depthTexture);
    }
    GfxTextureDesc depthDesc = {};
    depthDesc.name = "world depth";
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.mipCount = 1u;
    depthDesc.format = GfxFormat_D32_Float;
    depthDesc.usageFlags = GfxTextureUsageFlags_DepthTarget;
    depthDesc.storageKind = GfxTextureStorageKind_Transient;
    world->depthTexture = gfx_create_texture(ctx->host->gfxDevice, &depthDesc);
    world->depthWidth = width;
    world->depthHeight = height;
}

static void eng_world_begin_frame_(EngContext* ctx) {
    EngState* state = ctx->engine;
    EngWorldState* world = &state->world;
    Arena* arena = ctx->host->frameArena;

    // Lane policy is the project's (set per frame in pre_frame);
    // spmd_dispatch lanes must not exceed worker count (debug-asserted).
    U32 laneCount = MAX(world->requestedLaneCount, 1u);
    laneCount = MIN(laneCount, ENG_WORLD_MAX_LANES);
    if (!state->jobSystem || state->workerCount <= 1u) {
        laneCount = 1u;
    } else {
        laneCount = MIN(laneCount, state->workerCount);
    }
    world->laneCount = laneCount;
    world->laneWriters = ARENA_PUSH_ARRAY(arena, EngWorldLaneWriter, laneCount);
    ShdWorldRenderableRecord* records = ARENA_PUSH_ARRAY(arena, ShdWorldRenderableRecord, ENG_WORLD_MAX_RENDERABLES);
    ShdWorldRenderableRecord* transparents = ARENA_PUSH_ARRAY(arena, ShdWorldRenderableRecord, ENG_WORLD_MAX_TRANSPARENTS);
    world->frameOpen = (world->laneWriters != 0 && records != 0 && transparents != 0);
    if (!world->frameOpen) {
        eng_world_fail_once_(world, EngWorldFailLog_FrameAlloc, "frame arena exhausted in begin");
        return;
    }

    U32 cap = ENG_WORLD_MAX_RENDERABLES / laneCount;
    U32 transparentCap = ENG_WORLD_MAX_TRANSPARENTS / laneCount;
    for (U32 lane = 0u; lane < laneCount; ++lane) {
        EngWorldLaneWriter* writer = world->laneWriters + lane;
        writer->records = records + (U64)lane * cap;
        writer->count = 0u;
        writer->cap = cap;
        writer->transparents = transparents + (U64)lane * transparentCap;
        writer->transparentCount = 0u;
        writer->transparentCap = transparentCap;
        writer->dropped = 0u;
    }
}

static void eng_world_set_camera(EngContext* ctx, Vec3F32 eye, Vec3F32 target, F32 fovYRadians,
                                 F32 zNear, F32 zFar) {
    EngState* state = ctx->engine;
    EngWorldState* world = &state->world;
    F32 aspect = (ctx->host->windowHeight != 0u)
        ? ((F32)ctx->host->windowWidth / (F32)ctx->host->windowHeight)
        : 1.0f;
    Mat4x4F32 viewProj = eng_world_camera_view_proj_(eye, target, vec3_make(0.0f, 1.0f, 0.0f),
                                                     fovYRadians, aspect, zNear, zFar);

    MEMSET(&world->frameRecord, 0, sizeof(world->frameRecord));
    MEMCPY(world->frameRecord.viewProj, &viewProj, sizeof(world->frameRecord.viewProj));
    eng_world_frustum_planes_(&viewProj, world->frameRecord.frustumPlanes);
    world->frameRecord.cameraPos[0] = eye.x;
    world->frameRecord.cameraPos[1] = eye.y;
    world->frameRecord.cameraPos[2] = eye.z;
    world->frameRecord.time = (F32)state->simTimeSeconds;

    world->cameraForward = vec3_normalize(vec3_make(target.x - eye.x, target.y - eye.y,
                                                    target.z - eye.z));
}

static void eng_world_writer_push_(EngWorldState* world, EngWorldLaneWriter* writer,
                                   EngWorldMeshHandle meshHandle, U32 materialIndex,
                                   EngWorldBin bin, const Mat4x4F32* transform) {
    EngWorldMesh* mesh = (EngWorldMesh*)slot_map_get(&world->meshes, meshHandle.index, meshHandle.generation);
    if (!mesh) {
        return;
    }

    ShdWorldRenderableRecord record = {};
    MEMCPY(record.transform, transform, sizeof(record.transform));

    // Row-vector convention: world = local . M, translation in storage row 3.
    Vec3F32 local = mesh->boundsCenter;
    Vec3F32 worldCenter;
    worldCenter.x = local.x * transform->v[0][0] + local.y * transform->v[1][0] + local.z * transform->v[2][0] + transform->v[3][0];
    worldCenter.y = local.x * transform->v[0][1] + local.y * transform->v[1][1] + local.z * transform->v[2][1] + transform->v[3][1];
    worldCenter.z = local.x * transform->v[0][2] + local.y * transform->v[1][2] + local.z * transform->v[2][2] + transform->v[3][2];
    F32 scaleX = SQRT_F32(transform->v[0][0] * transform->v[0][0] + transform->v[0][1] * transform->v[0][1] + transform->v[0][2] * transform->v[0][2]);
    F32 scaleY = SQRT_F32(transform->v[1][0] * transform->v[1][0] + transform->v[1][1] * transform->v[1][1] + transform->v[1][2] * transform->v[1][2]);
    F32 scaleZ = SQRT_F32(transform->v[2][0] * transform->v[2][0] + transform->v[2][1] * transform->v[2][1] + transform->v[2][2] * transform->v[2][2]);
    F32 maxScale = MAX(scaleX, MAX(scaleY, scaleZ));

    record.boundsCenter[0] = worldCenter.x;
    record.boundsCenter[1] = worldCenter.y;
    record.boundsCenter[2] = worldCenter.z;
    record.boundsRadius = mesh->boundsRadius * maxScale;
    record.boundsExtents[0] = mesh->boundsExtents.x * maxScale;
    record.boundsExtents[1] = mesh->boundsExtents.y * maxScale;
    record.boundsExtents[2] = mesh->boundsExtents.z * maxScale;
    record.materialIndex = materialIndex;
    // Transparents are CPU-direct and own no GPU cell; their cellIndex is
    // just the mesh slot for run grouping in the sorted tail.
    record.cellIndex = (bin == EngWorldBin_Transparent)
        ? meshHandle.index
        : (U32)bin * ENG_WORLD_MAX_MESHES + meshHandle.index;
    record.flags = 0u;

    if (bin == EngWorldBin_Transparent) {
        if (writer->transparentCount >= writer->transparentCap) {
            writer->dropped += 1u;
            return;
        }
        writer->transparents[writer->transparentCount] = record;
        writer->transparentCount += 1u;
        return;
    }

    if (writer->count >= writer->cap) {
        writer->dropped += 1u;
        return;
    }
    writer->records[writer->count] = record;
    writer->count += 1u;
}

static void eng_world_push(EngContext* ctx, EngWorldMeshHandle meshHandle, U32 materialIndex,
                           EngWorldBin bin, const Mat4x4F32* transform) {
    EngWorldState* world = &ctx->engine->world;
    if (!world->frameOpen) {
        return;
    }
    eng_world_writer_push_(world, world->laneWriters, meshHandle, materialIndex, bin, transform);
}

// Merges every lane's transparent slice, frustum-culls on the CPU (the GPU
// never sees transparents), sorts back-to-front by the view depth of the
// nearest bounding point (depth - radius, so a shell draws after what it
// contains), and writes the tail straight into tailOut (the mapped
// renderable buffer). outTailCells gets each tail record's cellIndex in CPU
// memory so the run walk never reads GPU memory back. Cap overflow drops
// the nearest records and counts as dropped; culled records are just
// invisible. Returns 0 only on allocation failure.
static B32 eng_world_cull_sort_transparents_(EngWorldState* world, Arena* frameArena,
                                             U32 total, U32 maxUpload,
                                             ShdWorldRenderableRecord* tailOut,
                                             U32** outTailCells,
                                             U32* outUploadCount, U32* outDropped) {
    *outTailCells = 0;
    *outUploadCount = 0u;
    *outDropped = 0u;
    F32* depths = ARENA_PUSH_ARRAY(frameArena, F32, total);
    const ShdWorldRenderableRecord** sources = ARENA_PUSH_ARRAY(frameArena, const ShdWorldRenderableRecord*, total);
    if (!depths || !sources) {
        return 0;
    }

    const F32* planes = world->frameRecord.frustumPlanes;
    const F32* eye = world->frameRecord.cameraPos;
    Vec3F32 forward = world->cameraForward;
    U32 visible = 0u;
    for (U32 lane = 0u; lane < world->laneCount; ++lane) {
        const EngWorldLaneWriter* writer = world->laneWriters + lane;
        for (U32 at = 0u; at < writer->transparentCount; ++at) {
            const ShdWorldRenderableRecord* record = writer->transparents + at;
            F32 radius = record->boundsRadius;
            if (!eng_world_sphere_visible_(planes, record->boundsCenter, radius)) {
                continue;
            }
            depths[visible] = eng_world_transparent_depth_(record->boundsCenter, radius, eye, forward);
            sources[visible] = record;
            visible += 1u;
        }
    }
    if (visible == 0u) {
        return 1;
    }

    U32 uploadCount = MIN(visible, maxUpload);
    *outDropped = visible - uploadCount;
    if (uploadCount == 0u) {
        return 1;
    }
    U32* tailCells = ARENA_PUSH_ARRAY(frameArena, U32, uploadCount);
    if (!tailCells) {
        return 0;
    }
    if (visible == 1u) {
        tailOut[0] = *sources[0];
        tailCells[0] = sources[0]->cellIndex;
        *outTailCells = tailCells;
        *outUploadCount = 1u;
        return 1;
    }

    U32* order = ARENA_PUSH_ARRAY(frameArena, U32, visible);
    U32* scratch = ARENA_PUSH_ARRAY(frameArena, U32, visible);
    if (!order || !scratch) {
        return 0;
    }
    // Back-to-front: descending view depth; ascending radix, reversed
    // copy-out.
    eng_world_order_ascending_(depths, order, scratch, visible);
    for (U32 at = 0u; at < uploadCount; ++at) {
        const ShdWorldRenderableRecord* source = sources[order[visible - 1u - at]];
        tailOut[at] = *source;
        tailCells[at] = source->cellIndex;
    }
    *outTailCells = tailCells;
    *outUploadCount = uploadCount;
    return 1;
}

static void eng_world_execute_(EngContext* ctx, EngRendererFrame* rendererFrame) {
    EngState* state = ctx->engine;
    EngWorldState* world = &state->world;
    GfxFrame* frame = rendererFrame->frame;

    if (!world->frameOpen) {
        world->lastRenderableCount = 0u;
        return;
    }
    world->frameOpen = 0;

    U32 opaqueTotal = 0u;
    U32 transparentTotal = 0u;
    U32 dropped = 0u;
    for (U32 lane = 0u; lane < world->laneCount; ++lane) {
        const EngWorldLaneWriter* writer = world->laneWriters + lane;
        opaqueTotal += writer->count;
        transparentTotal += writer->transparentCount;
        dropped += writer->dropped;
    }

    world->lastRenderableCount = 0u;
    world->lastDroppedCount = dropped;
    world->lastTransparentDraws = 0u;
    if (!world->gpuResourcesCreated ||
        world->opaquePipeline.generation == 0u ||
        world->depthTexture.generation == 0u ||
        (opaqueTotal == 0u && transparentTotal == 0u)) {
        return;
    }

    // Per-frame GPU data is written straight into the mapped Upload buffers;
    // the frame-parity round robin protects frames in flight.
    U32 frameBufferIndex = (U32)(state->frameCounter & (ENG_WORLD_FRAME_BUFFER_COUNT - 1u));
    GfxDevice* device = ctx->host->gfxDevice;
    ShdWorldRenderableRecord* mappedRenderables =
        (ShdWorldRenderableRecord*)gfx_buffer_contents(device, world->renderableBuffers[frameBufferIndex]);
    ShdWorldFrameRecord* mappedFrameRecord =
        (ShdWorldFrameRecord*)gfx_buffer_contents(device, world->frameRecordBuffers[frameBufferIndex]);
    if (!mappedRenderables || !mappedFrameRecord) {
        eng_world_fail_once_(world, EngWorldFailLog_MappedBuffers, "per-frame buffers are not mapped");
        return;
    }

    // Transparents never enter the GPU cull set: CPU frustum cull + sort,
    // written as a contiguous tail after the opaque records, drawn directly.
    U32* transparentCells = 0;
    U32 transparentUpload = 0u;
    if (transparentTotal != 0u) {
        PROF_SCOPE("world transparents");
        U32 transparentDropped = 0u;
        if (!eng_world_cull_sort_transparents_(world, ctx->host->frameArena, transparentTotal,
                                               ENG_WORLD_MAX_RENDERABLES - opaqueTotal,
                                               mappedRenderables + opaqueTotal,
                                               &transparentCells, &transparentUpload, &transparentDropped)) {
            eng_world_fail_once_(world, EngWorldFailLog_TransparentBuild, "transparent cull/sort allocation failed");
            return;
        }
        dropped += transparentDropped;
        world->lastDroppedCount = dropped;
    }
    U32 renderableTotal = opaqueTotal + transparentUpload;
    if (renderableTotal == 0u) {
        return;
    }
    world->lastRenderableCount = renderableTotal;

    world->frameRecord.renderableCount = opaqueTotal;
    *mappedFrameRecord = world->frameRecord;
    U32 uploadOffset = 0u;
    for (U32 lane = 0u; lane < world->laneCount; ++lane) {
        const EngWorldLaneWriter* writer = world->laneWriters + lane;
        if (writer->count == 0u) {
            continue;
        }
        MEMCPY(mappedRenderables + uploadOffset, writer->records,
               sizeof(ShdWorldRenderableRecord) * writer->count);
        uploadOffset += writer->count;
    }

    if (world->meshRecordsDirty) {
        ShdWorldMeshRecord meshRecords[ENG_WORLD_MAX_MESHES] = {};
        for (U32 meshSlot = 0u; meshSlot < ENG_WORLD_MAX_MESHES; ++meshSlot) {
            if (!slot_map_is_occupied(&world->meshes, meshSlot)) {
                continue;
            }
            EngWorldMesh* mesh = (EngWorldMesh*)slot_map_item_at(&world->meshes, meshSlot);
            meshRecords[meshSlot].indexCount = mesh->indexCount;
            meshRecords[meshSlot].firstIndex = mesh->firstIndex;
            meshRecords[meshSlot].baseVertex = mesh->baseVertex;
        }
        if (gfx_upload_buffer(frame, world->meshRecordBuffer, 0u, meshRecords, sizeof(meshRecords))) {
            world->meshRecordsDirty = 0;
        }
    }
    if (world->materialsDirty) {
        if (gfx_upload_buffer(frame, world->materialBuffer, 0u, world->materialRecords,
                              sizeof(world->materialRecords))) {
            world->materialsDirty = 0;
        }
    }

    U32 cellCount = ENG_WORLD_CELL_COUNT;

    GfxTemp rootTemp = gfx_allocate_temp(frame, sizeof(ShdWorldCullRootData), 16u);
    if (!rootTemp.cpu) {
        eng_world_fail_once_(world, EngWorldFailLog_RootTemp, "cull root temp allocation failed");
        world->lastRenderableCount = 0u;
        return;
    }
    ShdWorldCullRootData* cullRoot = (ShdWorldCullRootData*)rootTemp.cpu;
    MEMSET(cullRoot, 0, sizeof(*cullRoot));
    cullRoot->frameBuffer = world->frameRecordBufferIds[frameBufferIndex].index;
    cullRoot->renderableBuffer = world->renderableBufferIds[frameBufferIndex].index;
    cullRoot->flagsBuffer = world->flagsBufferId.index;
    cullRoot->cellCountBuffer = world->cellCountBufferId.index;
    cullRoot->cellOffsetBuffer = world->cellOffsetBufferId.index;
    cullRoot->cellCursorBuffer = world->cellCursorBufferId.index;
    cullRoot->visibleBuffer = world->visibleBufferId.index;
    cullRoot->argsBuffer = world->argsBufferId.index;
    cullRoot->meshBuffer = world->meshRecordBufferId.index;
    cullRoot->renderableCount = opaqueTotal;
    cullRoot->cellCount = cellCount;
    cullRoot->meshCount = ENG_WORLD_MAX_MESHES;

    static const char* passNames[5] = {
        "world reset", "world cull", "world prefix", "world scatter", "world args",
    };
    // groupsX floors at 1 so an all-transparent frame still dispatches legally;
    // the kernels early-out on renderableCount.
    U32 renderableGroups = MAX(1u, (opaqueTotal + ENG_WORLD_CULL_GROUP_SIZE - 1u) / ENG_WORLD_CULL_GROUP_SIZE);
    U32 groupCounts[5] = {
        (cellCount + ENG_WORLD_CULL_GROUP_SIZE - 1u) / ENG_WORLD_CULL_GROUP_SIZE,
        renderableGroups,
        1u,
        renderableGroups,
        (cellCount + ENG_WORLD_CULL_GROUP_SIZE - 1u) / ENG_WORLD_CULL_GROUP_SIZE,
    };

    for (U32 passIndex = 0u; passIndex < 5u; ++passIndex) {
        GfxResourceUse uses[9] = {};
        U32 useCount = 0u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->renderableBuffers[frameBufferIndex];
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->frameRecordBuffers[frameBufferIndex];
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead | GfxResourceAccessFlags_ShaderWrite;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->flagsBuffer;
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead | GfxResourceAccessFlags_ShaderWrite;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->cellCountBuffer;
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead | GfxResourceAccessFlags_ShaderWrite;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->cellOffsetBuffer;
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead | GfxResourceAccessFlags_ShaderWrite;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->cellCursorBuffer;
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead | GfxResourceAccessFlags_ShaderWrite;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->visibleBuffer;
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderWrite;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->argsBuffer;
        useCount += 1u;
        uses[useCount].kind = GfxResourceUseKind_Buffer;
        uses[useCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
        uses[useCount].shaderStages = GfxShaderStageFlags_Compute;
        uses[useCount].buffer = world->meshRecordBuffer;
        useCount += 1u;

        GfxComputePassDesc passDesc = {};
        passDesc.name = passNames[passIndex];
        passDesc.resourceUses = uses;
        passDesc.resourceUseCount = useCount;

        GfxDispatch dispatch = {};
        dispatch.pipeline = world->computePipelines[passIndex];
        dispatch.rootDataOffset = (U32)rootTemp.gpu.offset;
        dispatch.rootDataSize = (U32)rootTemp.gpu.size;
        dispatch.groupsX = groupCounts[passIndex];
        dispatch.groupsY = 1u;
        dispatch.groupsZ = 1u;
        gfx_compute_pass(rendererFrame->commands, &passDesc, &dispatch, 1u);
    }

    GfxColorTarget colorTarget = {};
    colorTarget.texture = gfx_get_backbuffer(frame);
    colorTarget.loadOp = GfxLoadOp_Clear;
    colorTarget.storeOp = GfxStoreOp_Store;
    colorTarget.clearColor[0] = 0.06f;
    colorTarget.clearColor[1] = 0.08f;
    colorTarget.clearColor[2] = 0.10f;
    colorTarget.clearColor[3] = 1.0f;

    GfxDepthTarget depthTarget = {};
    depthTarget.texture = world->depthTexture;
    depthTarget.loadOp = GfxLoadOp_Clear;
    depthTarget.storeOp = GfxStoreOp_DontCare;
    depthTarget.clearDepth = 1.0f;

    U32 maxDrawUses = 7u + ENG_WORLD_MAX_MODELS + world->assetTextureCount;
    GfxResourceUse* drawUses = ARENA_PUSH_ARRAY(ctx->host->frameArena, GfxResourceUse, maxDrawUses);
    if (!drawUses) {
        eng_world_fail_once_(world, EngWorldFailLog_DrawAlloc, "forward pass allocation failed");
        world->lastRenderableCount = 0u;
        return;
    }
    MEMSET(drawUses, 0, sizeof(GfxResourceUse) * maxDrawUses);
    U32 drawUseCount = 0u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_IndirectRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
    drawUses[drawUseCount].buffer = world->argsBuffer;
    drawUseCount += 1u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
    drawUses[drawUseCount].buffer = world->visibleBuffer;
    drawUseCount += 1u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
    drawUses[drawUseCount].buffer = world->cellOffsetBuffer;
    drawUseCount += 1u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
    drawUses[drawUseCount].buffer = world->renderableBuffers[frameBufferIndex];
    drawUseCount += 1u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
    drawUses[drawUseCount].buffer = world->frameRecordBuffers[frameBufferIndex];
    drawUseCount += 1u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
    drawUses[drawUseCount].buffer = world->vertexBuffer;
    drawUseCount += 1u;
    drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
    drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
    drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Fragment;
    drawUses[drawUseCount].buffer = world->materialBuffer;
    drawUseCount += 1u;
    // Residency contract (U6): every buffer the resource table reaches must
    // appear in the pass uses. Model sections share one vertex buffer per
    // model — declare per model, not per slot.
    for (U32 assetIndex = 0u; assetIndex < ENG_WORLD_MAX_MODELS; ++assetIndex) {
        const EngWorldModelResources* model = world->models[assetIndex];
        if (!model) {
            continue;
        }
        drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
        drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
        drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
        drawUses[drawUseCount].buffer = model->vertexBuffer;
        drawUseCount += 1u;
    }
    for (U32 at = 0u; at < world->assetTextureCount; ++at) {
        drawUses[drawUseCount].kind = GfxResourceUseKind_Texture;
        drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
        drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Fragment;
        drawUses[drawUseCount].texture = world->assetTextures[at];
        drawUseCount += 1u;
    }

    GfxRenderPassDesc passDesc = {};
    passDesc.name = "world forward";
    passDesc.colorTargets = &colorTarget;
    passDesc.colorTargetCount = 1u;
    passDesc.depthTarget = &depthTarget;
    passDesc.resourceUses = drawUses;
    passDesc.resourceUseCount = drawUseCount;

    GfxDraw* draws = ARENA_PUSH_ARRAY(ctx->host->frameArena, GfxDraw, cellCount + transparentUpload);
    if (!draws) {
        eng_world_fail_once_(world, EngWorldFailLog_DrawAlloc, "forward pass allocation failed");
        world->lastRenderableCount = 0u;
        return;
    }
    // One temp block holds every draw's root record at a fixed stride.
    GfxTemp rootBlock = gfx_allocate_temp(frame, sizeof(ShdWorldForwardRootData) * (cellCount + transparentUpload), 16u);
    if (!rootBlock.cpu) {
        eng_world_fail_once_(world, EngWorldFailLog_RootTemp, "draw root temp allocation failed");
        world->lastRenderableCount = 0u;
        return;
    }
    U32 drawCount = 0u;
    for (U32 cell = 0u; cell < cellCount; ++cell) {
        U32 meshSlot = cell % ENG_WORLD_MAX_MESHES;
        if (!slot_map_is_occupied(&world->meshes, meshSlot)) {
            continue;
        }
        EngWorldMesh* mesh = (EngWorldMesh*)slot_map_item_at(&world->meshes, meshSlot);

        ShdWorldForwardRootData* rootData = (ShdWorldForwardRootData*)rootBlock.cpu + drawCount;
        MEMSET(rootData, 0, sizeof(*rootData));
        rootData->frameBuffer = world->frameRecordBufferIds[frameBufferIndex].index;
        rootData->renderableBuffer = world->renderableBufferIds[frameBufferIndex].index;
        rootData->visibleBuffer = world->visibleBufferId.index;
        rootData->cellOffsetBuffer = world->cellOffsetBufferId.index;
        rootData->materialBuffer = world->materialBufferId.index;
        rootData->vertexBuffer = mesh->vertexBufferId.index;
        rootData->vertexByteOffset = mesh->vertexByteOffset;
        rootData->cellIndex = cell;
        rootData->directFirstRenderable = ENG_WORLD_DIRECT_NONE;

        GfxDraw* draw = draws + drawCount;
        *draw = {};
        draw->pipeline = world->opaquePipeline;
        draw->indexBuffer = mesh->indexBuffer;
        draw->indexByteOffset = mesh->indexByteOffset;
        draw->indirectBuffer = world->argsBuffer;
        draw->indirectByteOffset = cell * (U32)sizeof(GfxDrawIndexedIndirectArgs);
        draw->indexType = GfxIndexType_U32;
        draw->rootDataOffset = (U32)(rootBlock.gpu.offset + drawCount * sizeof(ShdWorldForwardRootData));
        draw->rootDataSize = (U32)sizeof(ShdWorldForwardRootData);
        drawCount += 1u;
    }

    // Transparent tail: one direct instanced draw per contiguous same-mesh
    // run of the back-to-front order; the vertex shader indexes the tail via
    // directFirstRenderable, no visibility indirection.
    U32 transparentDraws = 0u;
    for (U32 runStart = 0u; runStart < transparentUpload;) {
        U32 cellIndex = transparentCells[runStart];
        U32 runEnd = runStart + 1u;
        while (runEnd < transparentUpload && transparentCells[runEnd] == cellIndex) {
            runEnd += 1u;
        }
        U32 meshSlot = cellIndex % ENG_WORLD_MAX_MESHES;
        if (!slot_map_is_occupied(&world->meshes, meshSlot)) {
            runStart = runEnd;
            continue;
        }
        EngWorldMesh* mesh = (EngWorldMesh*)slot_map_item_at(&world->meshes, meshSlot);

        ShdWorldForwardRootData* rootData = (ShdWorldForwardRootData*)rootBlock.cpu + drawCount;
        MEMSET(rootData, 0, sizeof(*rootData));
        rootData->frameBuffer = world->frameRecordBufferIds[frameBufferIndex].index;
        rootData->renderableBuffer = world->renderableBufferIds[frameBufferIndex].index;
        rootData->materialBuffer = world->materialBufferId.index;
        rootData->vertexBuffer = mesh->vertexBufferId.index;
        rootData->vertexByteOffset = mesh->vertexByteOffset;
        rootData->cellIndex = cellIndex;
        rootData->directFirstRenderable = opaqueTotal + runStart;

        GfxDraw* draw = draws + drawCount;
        *draw = {};
        draw->pipeline = world->transparentPipeline;
        draw->indexBuffer = mesh->indexBuffer;
        draw->indexByteOffset = mesh->indexByteOffset + mesh->firstIndex * (U32)sizeof(U32);
        draw->indexCount = mesh->indexCount;
        draw->baseVertex = (S32)mesh->baseVertex;
        draw->instanceCount = runEnd - runStart;
        draw->indexType = GfxIndexType_U32;
        draw->rootDataOffset = (U32)(rootBlock.gpu.offset + drawCount * sizeof(ShdWorldForwardRootData));
        draw->rootDataSize = (U32)sizeof(ShdWorldForwardRootData);
        drawCount += 1u;
        transparentDraws += 1u;
        runStart = runEnd;
    }
    world->lastTransparentDraws = transparentDraws;

    GfxDrawArea area = {};
    area.viewport.width = (F32)ctx->host->windowWidth;
    area.viewport.height = (F32)ctx->host->windowHeight;
    area.viewport.maxDepth = 1.0f;
    area.scissor.width = ctx->host->windowWidth;
    area.scissor.height = ctx->host->windowHeight;
    area.draws = draws;
    area.drawCount = drawCount;

    gfx_render_pass(rendererFrame->commands, &passDesc, &area, 1u);
}

static void eng_world_shutdown_(EngContext* ctx) {
    EngState* state = ctx->engine;
    GfxDevice* device = ctx->host ? ctx->host->gfxDevice : 0;
    if (device == 0) {
        return;
    }

    EngWorldState* world = &state->world;
    gfx_destroy_buffer(device, world->vertexBuffer);
    gfx_destroy_buffer(device, world->indexBuffer);
    gfx_destroy_buffer(device, world->meshRecordBuffer);
    gfx_destroy_buffer(device, world->materialBuffer);
    for (U32 bufferIndex = 0u; bufferIndex < ENG_WORLD_FRAME_BUFFER_COUNT; ++bufferIndex) {
        gfx_destroy_buffer(device, world->frameRecordBuffers[bufferIndex]);
        gfx_destroy_buffer(device, world->renderableBuffers[bufferIndex]);
    }
    gfx_destroy_buffer(device, world->flagsBuffer);
    gfx_destroy_buffer(device, world->cellCountBuffer);
    gfx_destroy_buffer(device, world->cellOffsetBuffer);
    gfx_destroy_buffer(device, world->cellCursorBuffer);
    gfx_destroy_buffer(device, world->visibleBuffer);
    gfx_destroy_buffer(device, world->argsBuffer);
    gfx_destroy_sampler(device, world->worldSampler);
    gfx_destroy_texture(device, world->depthTexture);
    gfx_destroy_pipeline(device, world->opaquePipeline);
    gfx_destroy_pipeline(device, world->transparentPipeline);
    for (U32 passIndex = 0u; passIndex < 5u; ++passIndex) {
        gfx_destroy_pipeline(device, world->computePipelines[passIndex]);
    }
    MEMSET(world, 0, sizeof(*world));
}

