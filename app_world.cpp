//
// Created by André Leite on 11/06/2026.
//
// World renderer: mesh/material tables, cooked-asset decode, extraction
// lane writers, GPU visibility passes, and the forward pass.
//

#include "app_world_kernels.hpp"

enum AppWorldShaderSlot {
    AppWorldShaderSlot_Vertex = 0,
    AppWorldShaderSlot_Fragment,
    AppWorldShaderSlot_Reset,
    AppWorldShaderSlot_Cull,
    AppWorldShaderSlot_Prefix,
    AppWorldShaderSlot_Scatter,
    AppWorldShaderSlot_Args,
};

static const char* APP_WORLD_SHADER_PATHS[APP_WORLD_SHADER_COUNT] = {
    APP_SHADER_WORLD_VERTEX_RUNTIME_PATH,
    APP_SHADER_WORLD_FRAGMENT_RUNTIME_PATH,
    APP_SHADER_WORLD_RESET_RUNTIME_PATH,
    APP_SHADER_WORLD_CULL_RUNTIME_PATH,
    APP_SHADER_WORLD_PREFIX_RUNTIME_PATH,
    APP_SHADER_WORLD_SCATTER_RUNTIME_PATH,
    APP_SHADER_WORLD_ARGS_RUNTIME_PATH,
};

// Demo asset policy: which cooked models exist. Module rodata; hot state
// stores the published model resources. A model's textures cook as
// "<stem>_tex<N>.utex" siblings; the runtime derives those paths.
struct AppWorldDemoAssetDesc {
    const char* modelPath;
    const char* modelLabel;
};

static const AppWorldDemoAssetDesc APP_WORLD_DEMO_ASSETS[APP_WORLD_DEMO_ASSET_COUNT] = {
    {"app/assets/cooked/Duck.umdl", "assets/Duck.umdl"},
    {"app/assets/cooked/Avocado.umdl", "assets/Avocado.umdl"},
    {"app/assets/cooked/Lantern.umdl", "assets/Lantern.umdl"},
    {"app/assets/cooked/Buggy.umdl", "assets/Buggy.umdl"},
};

// Cooked sounds ride the same path: watch -> artifact -> publish into the
// host audio buffer table. Indexed by AppSound.
struct AppAudioSoundDesc {
    const char* soundPath;
    const char* soundLabel;
};

static const AppAudioSoundDesc APP_AUDIO_SOUND_DESCS[AppSound_Count] = {
    {"app/assets/cooked/jump.uaud", "assets/jump.uaud"},
    {"app/assets/cooked/land.uaud", "assets/land.uaud"},
    {"app/assets/cooked/click.uaud", "assets/click.uaud"},
    {"app/assets/cooked/ambience.uaud", "assets/ambience.uaud"},
};

// "<dir>/<stem>.umdl" -> "<dir>/<stem>_tex<N>.utex"
static StringU8 app_world_model_texture_path_(Arena* arena, const char* modelPath, U32 textureLocal) {
    StringU8 path = str8(modelPath);
    StringU8 base = path;
    base.size = (path.size > 5u) ? path.size - 5u : 0u;
    return str8_fmt(arena, "{}_tex{}.utex", base, textureLocal);
}


static void app_world_resource_cache_reset_(APP_Context* ctx) {
    AppWorldState* world = &ctx->core->world;
    for (U32 shaderIndex = 0u; shaderIndex < APP_WORLD_SHADER_COUNT; ++shaderIndex) {
        world->shaderFiles[shaderIndex] = FILE_HANDLE_ZERO;
        world->shaderHashes[shaderIndex] = CONTENT_HASH_ZERO;
    }
}

static void app_world_watch_files_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    if (!state->resources.fileStream) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    StringU8 exeDir = OS_get_executable_directory(scratch.arena);
    for (U32 assetIndex = 0u; assetIndex < APP_WORLD_DEMO_ASSET_COUNT; ++assetIndex) {
        const AppWorldDemoAssetDesc* asset = APP_WORLD_DEMO_ASSETS + assetIndex;
        StringU8 modelPath = str8_concat(scratch.arena, exeDir, str8("/../"));
        modelPath = str8_concat(scratch.arena, modelPath, str8(asset->modelPath));
        state->world.assetModelFiles[assetIndex] = file_watch(state->resources.fileStream, modelPath, 0u);
        // Texture watches are model-driven: the publish proc watches exactly
        // the textureCount the cooked model declares (a boot-time guess would
        // leave permanently-failing watches screaming in the stats).
    }

    for (U32 shaderIndex = 0u; shaderIndex < APP_WORLD_SHADER_COUNT; ++shaderIndex) {
        StringU8 worldPath = str8_concat(scratch.arena, exeDir, str8("/../"));
        worldPath = str8_concat(scratch.arena, worldPath, str8(APP_WORLD_SHADER_PATHS[shaderIndex]));
        state->world.shaderFiles[shaderIndex] = file_watch(state->resources.fileStream, worldPath, 0u);
    }

    for (U32 soundIndex = 0u; soundIndex < AppSound_Count; ++soundIndex) {
        StringU8 soundPath = str8_concat(scratch.arena, exeDir, str8("/../"));
        soundPath = str8_concat(scratch.arena, soundPath, str8(APP_AUDIO_SOUND_DESCS[soundIndex].soundPath));
        state->audio.soundFiles[soundIndex] = file_watch(state->resources.fileStream, soundPath, 0u);
    }
}

// ////////////////////////
// World renderer (U5)

#define APP_WORLD_CULL_GROUP_SIZE 64u
#define APP_WORLD_MATERIAL_FLAG_ALPHA_TEST 1u
#define APP_WORLD_MATERIAL_FLAG_TEXTURED 2u
// Sentinel for ShdWorldForwardRootData.directFirstRenderable: draw goes
// through cellOffsets/visibleBuffer instead of a direct base index.
#define APP_WORLD_DIRECT_NONE 0xFFFFFFFFu

enum AppWorldFailLog {
    AppWorldFailLog_FrameAlloc = (1u << 0u),
    AppWorldFailLog_MappedBuffers = (1u << 1u),
    AppWorldFailLog_TransparentBuild = (1u << 2u),
    AppWorldFailLog_RootTemp = (1u << 3u),
    AppWorldFailLog_DrawAlloc = (1u << 4u),
};

static void app_world_fail_once_(AppWorldState* world, U32 bit, const char* message) {
    if (FLAGS_HAS(world->failLogMask, bit)) {
        return;
    }
    LOG_ERROR("gfx", "World frame dropped: {}", str8(message));
    world->failLogMask |= bit;
}

static const char* APP_WORLD_COMPUTE_ENTRIES[5] = {
    APP_SHADER_WORLD_RESET_ENTRY,
    APP_SHADER_WORLD_CULL_ENTRY,
    APP_SHADER_WORLD_PREFIX_ENTRY,
    APP_SHADER_WORLD_SCATTER_ENTRY,
    APP_SHADER_WORLD_ARGS_ENTRY,
};

static const U32 APP_WORLD_COMPUTE_GROUP_SIZES[5] = {
    APP_WORLD_CULL_GROUP_SIZE,
    APP_WORLD_CULL_GROUP_SIZE,
    1u,
    APP_WORLD_CULL_GROUP_SIZE,
    APP_WORLD_CULL_GROUP_SIZE,
};

static U32 app_world_cell_count_(const AppWorldState* world) {
    (void)world;
    return APP_WORLD_CELL_COUNT;
}

static Vec3F32 app_world_vec3_(F32 x, F32 y, F32 z) {
    Vec3F32 result;
    result.x = x;
    result.y = y;
    result.z = z;
    return result;
}

static AppWorldMeshHandle app_world_register_mesh_(AppWorldState* world, U32 firstIndex, U32 indexCount,
                                                   U32 baseVertex, GfxBuffer vertexBuffer,
                                                   GfxResourceId vertexBufferId, GfxBuffer indexBuffer,
                                                   B32 ownsBuffers, Vec3F32 center, Vec3F32 extents) {
    // World tables are immutable between begin and execute; publishes run in
    // the artifact tick before the frame opens.
    ASSERT_DEBUG(!world->frameOpen);
    AppWorldMeshHandle handle = {};
    void* item = 0;
    U32 slot = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&world->meshes, &item, &slot, &generation)) {
        return handle;
    }
    AppWorldMesh* mesh = (AppWorldMesh*)item;
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

static void app_world_release_mesh_(GfxDevice* device, AppWorldState* world, AppWorldMeshHandle handle) {
    ASSERT_DEBUG(!world->frameOpen);
    void* item = 0;
    if (!slot_map_release(&world->meshes, handle.index, handle.generation, &item) || !item) {
        return;
    }
    AppWorldMesh* mesh = (AppWorldMesh*)item;
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

static B32 app_world_material_alloc(AppWorldState* world, U32* outIndex) {
    for (U32 index = APP_WORLD_MATERIAL_MISSING + 1u; index < APP_WORLD_MAX_MATERIALS; ++index) {
        if (FLAGS_HAS(world->materialUsedMask, 1ull << index)) {
            continue;
        }
        world->materialUsedMask |= 1ull << index;
        MEMSET(&world->materialRecords[index], 0, sizeof(world->materialRecords[index]));
        world->materialsDirty = 1;
        *outIndex = index;
        return 1;
    }
    LOG_ERROR("gfx", "World material table full ({} slots)", APP_WORLD_MAX_MATERIALS);
    return 0;
}

static void app_world_material_set(AppWorldState* world, U32 index, const ShdWorldMaterialRecord* record) {
    if (index >= APP_WORLD_MAX_MATERIALS || !FLAGS_HAS(world->materialUsedMask, 1ull << index)) {
        return;
    }
    world->materialRecords[index] = *record;
    world->materialsDirty = 1;
}

static void app_world_material_release(AppWorldState* world, U32 index) {
    if (index <= APP_WORLD_MATERIAL_MISSING || index >= APP_WORLD_MAX_MATERIALS) {
        return;
    }
    world->materialUsedMask &= ~(1ull << index);
    // In-flight references to the freed slot show the missing color until
    // the slot is reused.
    world->materialRecords[index] = world->materialRecords[APP_WORLD_MATERIAL_MISSING];
    world->materialsDirty = 1;
}

static B32 app_world_try_create_resources_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
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
        !slot_map_init(&world->meshes, state->resources.arena, sizeof(AppWorldMesh), APP_WORLD_MAX_MESHES)) {
        return 0;
    }

    AppWorldMeshBuilder builder = {};
    builder.vertexCapacity = 4096u;
    builder.indexCapacity = 16384u;
    builder.vertices = ARENA_PUSH_ARRAY(scratch.arena, ShdWorldVertexRecord, builder.vertexCapacity);
    builder.indices = ARENA_PUSH_ARRAY(scratch.arena, U32, builder.indexCapacity);
    if (!builder.vertices || !builder.indices) {
        return 0;
    }

    U32 cubeFirstIndex = builder.indexCount;
    app_world_build_cube_(&builder);
    U32 cubeIndexCount = builder.indexCount - cubeFirstIndex;

    U32 sphereFirstIndex = builder.indexCount;
    app_world_build_sphere_(&builder, 12u, 18u);
    U32 sphereIndexCount = builder.indexCount - sphereFirstIndex;

    U32 planeFirstIndex = builder.indexCount;
    app_world_build_plane_(&builder);
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
    world->builtinMeshes[0] = app_world_register_mesh_(world, cubeFirstIndex, cubeIndexCount, 0u,
                                                       world->vertexBuffer, world->vertexBufferId, world->indexBuffer, 0,
                                                       app_world_vec3_(0.0f, 0.0f, 0.0f), app_world_vec3_(0.5f, 0.5f, 0.5f));
    world->builtinMeshes[1] = app_world_register_mesh_(world, sphereFirstIndex, sphereIndexCount, 0u,
                                                       world->vertexBuffer, world->vertexBufferId, world->indexBuffer, 0,
                                                       app_world_vec3_(0.0f, 0.0f, 0.0f), app_world_vec3_(0.5f, 0.5f, 0.5f));
    world->builtinMeshes[2] = app_world_register_mesh_(world, planeFirstIndex, planeIndexCount, 0u,
                                                       world->vertexBuffer, world->vertexBufferId, world->indexBuffer, 0,
                                                       app_world_vec3_(0.0f, 0.0f, 0.0f), app_world_vec3_(0.5f, 0.02f, 0.5f));

    GfxBufferDesc meshRecordDesc = {};
    meshRecordDesc.name = "world mesh records";
    meshRecordDesc.size = sizeof(ShdWorldMeshRecord) * APP_WORLD_MAX_MESHES;
    meshRecordDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    meshRecordDesc.memoryKind = GfxMemoryKind_Device;
    world->meshRecordBuffer = gfx_create_buffer(device, &meshRecordDesc);
    world->meshRecordBufferId = gfx_register_buffer(device, world->meshRecordBuffer);
    world->meshRecordsDirty = 1;

    // Materials are scene/asset-allocated; the table starts with only the
    // builtin missing material in slot 0.
    MEMSET(world->materialRecords, 0, sizeof(world->materialRecords));
    ShdWorldMaterialRecord* missing = &world->materialRecords[APP_WORLD_MATERIAL_MISSING];
    missing->baseColor[0] = 1.0f;
    missing->baseColor[1] = 0.0f;
    missing->baseColor[2] = 1.0f;
    missing->baseColor[3] = 1.0f;
    world->materialUsedMask = 1u << APP_WORLD_MATERIAL_MISSING;
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

    for (U32 bufferIndex = 0u; bufferIndex < APP_WORLD_FRAME_BUFFER_COUNT; ++bufferIndex) {
        GfxBufferDesc frameDesc = {};
        frameDesc.name = "world frame record";
        frameDesc.size = sizeof(ShdWorldFrameRecord);
        frameDesc.usageFlags = GfxBufferUsageFlags_Storage;
        frameDesc.memoryKind = GfxMemoryKind_Upload;
        world->frameRecordBuffers[bufferIndex] = gfx_create_buffer(device, &frameDesc);
        world->frameRecordBufferIds[bufferIndex] = gfx_register_buffer(device, world->frameRecordBuffers[bufferIndex]);

        GfxBufferDesc renderableDesc = {};
        renderableDesc.name = "world renderables";
        renderableDesc.size = sizeof(ShdWorldRenderableRecord) * APP_WORLD_MAX_RENDERABLES;
        // Per-frame data is written by the CPU straight into the mapping;
        // the [2] round-robin is the frames-in-flight protection.
        renderableDesc.usageFlags = GfxBufferUsageFlags_Storage;
        renderableDesc.memoryKind = GfxMemoryKind_Upload;
        world->renderableBuffers[bufferIndex] = gfx_create_buffer(device, &renderableDesc);
        world->renderableBufferIds[bufferIndex] = gfx_register_buffer(device, world->renderableBuffers[bufferIndex]);
    }

    GfxBufferDesc flagsDesc = {};
    flagsDesc.name = "world visibility flags";
    flagsDesc.size = sizeof(U32) * APP_WORLD_MAX_RENDERABLES;
    flagsDesc.usageFlags = GfxBufferUsageFlags_Storage;
    flagsDesc.memoryKind = GfxMemoryKind_Device;
    world->flagsBuffer = gfx_create_buffer(device, &flagsDesc);
    world->flagsBufferId = gfx_register_buffer(device, world->flagsBuffer);

    U32 maxCells = APP_WORLD_BIN_COUNT * APP_WORLD_MAX_MESHES;
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
    visibleDesc.size = sizeof(U32) * APP_WORLD_MAX_RENDERABLES;
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
    for (U32 bufferIndex = 0u; bufferIndex < APP_WORLD_FRAME_BUFFER_COUNT; ++bufferIndex) {
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

// ////////////////////////
// Cooked asset decode (mesh + texture artifacts)

#define APP_ARTIFACT_TYPE_MODEL 0x4C444D55u
#define APP_ARTIFACT_TYPE_TEXTURE 0x54455855u
#define APP_ARTIFACT_TYPE_AUDIO 0x44554155u
#define APP_ARTIFACT_PUBLISHED_MARK 1ull

// assetIndex names the demo-asset slot; textureLocal is the model-local
// texture index for texture requests (model requests ignore it).
struct AppAssetRequest {
    ContentHash hash;
    U32 assetIndex;
    U32 textureLocal;
};

static B32 app_asset_build_blob_(ArtifactBuildContext* buildCtx, U32 expectedMagic, U64 minimumSize,
                                 ArtifactValue* outValue, U64* outBytes) {
    const AppAssetRequest* request = (const AppAssetRequest*)buildCtx->requestData;
    if (!request || buildCtx->requestDataSize < sizeof(AppAssetRequest)) {
        return 0;
    }
    ContentView view = content_view_hash(buildCtx->content, request->hash);
    if (!view.valid || view.size < minimumSize) {
        return 0;
    }
    if (*(const U32*)view.data != expectedMagic) {
        return 0;
    }
    Arena* arena = arena_alloc(.arenaSize = view.size + KB(64), .committedSize = KB(64));
    if (!arena) {
        return 0;
    }
    U8* blob = ARENA_PUSH_ARRAY(arena, U8, view.size);
    if (!blob) {
        arena_release(arena);
        return 0;
    }
    MEMCPY(blob, view.data, view.size);
    outValue->u64[0] = (U64)arena;
    outValue->u64[1] = (U64)blob;
    outValue->u64[2] = view.size;
    outValue->u64[3] = 0ull;
    *outBytes = view.size;
    return 1;
}

static B32 app_model_artifact_build_(ArtifactBuildContext* buildCtx, ArtifactValue* outValue, U64* outBytes) {
    return app_asset_build_blob_(buildCtx, ASSET_MODEL_MAGIC, sizeof(AssetModelHeader), outValue, outBytes);
}

static B32 app_texture_artifact_build_(ArtifactBuildContext* buildCtx, ArtifactValue* outValue, U64* outBytes) {
    return app_asset_build_blob_(buildCtx, ASSET_TEXTURE_MAGIC, sizeof(AssetTextureHeader), outValue, outBytes);
}

static B32 app_model_artifact_publish_(ArtifactPublishContext* publishCtx, ArtifactValue buildValue,
                                       ArtifactValue* outValue, U64* outBytes) {
    AppWorldArtifactBridge* bridge = (AppWorldArtifactBridge*)publishCtx->typeUserData;
    Arena* arena = (Arena*)buildValue.u64[0];
    const U8* blob = (const U8*)buildValue.u64[1];
    U64 blobSize = buildValue.u64[2];
    const AppAssetRequest* request = (const AppAssetRequest*)publishCtx->requestData;
    if (!bridge || !bridge->device || !bridge->state || !blob ||
        !request || publishCtx->requestDataSize < sizeof(AppAssetRequest) ||
        request->assetIndex >= APP_WORLD_DEMO_ASSET_COUNT) {
        arena_release(arena);
        return 0;
    }
    AppWorldState* world = &bridge->state->world;

    const AssetModelHeader* header = (const AssetModelHeader*)blob;
    U64 sectionBytes = (U64)header->sectionCount * sizeof(AssetModelSection);
    U64 instanceBytes = (U64)header->instanceCount * sizeof(AssetModelInstance);
    U64 materialBytes = (U64)header->materialCount * sizeof(AssetModelMaterial);
    U64 vertexBytes = (U64)header->vertexCount * sizeof(ShdWorldVertexRecord);
    U64 indexBytes = (U64)header->indexCount * sizeof(U32);
    U64 expectedSize = sizeof(AssetModelHeader) + sectionBytes + instanceBytes + materialBytes +
                       vertexBytes + indexBytes;
    if (header->version != ASSET_MODEL_VERSION ||
        header->sectionCount == 0u || header->instanceCount == 0u || header->materialCount == 0u ||
        header->textureCount > APP_WORLD_MODEL_MAX_TEXTURES ||
        header->vertexCount == 0u || header->indexCount == 0u ||
        expectedSize > blobSize) {
        LOG_ERROR("asset", "Model blob rejected (asset {})", request->assetIndex);
        arena_release(arena);
        return 0;
    }
    const AssetModelSection* sections = (const AssetModelSection*)(blob + sizeof(AssetModelHeader));
    const AssetModelInstance* instances = (const AssetModelInstance*)((const U8*)sections + sectionBytes);
    const AssetModelMaterial* materials = (const AssetModelMaterial*)((const U8*)instances + instanceBytes);
    const U8* vertexData = (const U8*)materials + materialBytes;
    const U8* indexData = vertexData + vertexBytes;

    // Cooked sections carry mesh-relative indices plus a baseVertex, but the
    // world shaders pull vertices by SV_VertexID, whose baseVertex semantics
    // differ per compiler/target (slang's SPIR-V subtracts BaseVertex, Metal
    // keeps it included). Rebase to pool-absolute indices at publish — same
    // convention as the builtin pool — so no draw depends on baseVertex.
    Arena* rebaseArena = arena_alloc(.arenaSize = indexBytes + KB(64), .committedSize = KB(64));
    U32* poolIndices = rebaseArena ? ARENA_PUSH_ARRAY(rebaseArena, U32, header->indexCount) : 0;
    if (!poolIndices) {
        if (rebaseArena) {
            arena_release(rebaseArena);
        }
        arena_release(arena);
        return 0;
    }
    {
        const U32* localIndices = (const U32*)indexData;
        for (U32 s = 0u; s < header->sectionCount; ++s) {
            U32 firstIndex = sections[s].firstIndex;
            U32 endIndex = firstIndex + sections[s].indexCount;
            U32 baseVertex = sections[s].baseVertex;
            for (U32 i = firstIndex; i < endIndex; ++i) {
                poolIndices[i] = localIndices[i] + baseVertex;
            }
        }
    }

    GfxBufferDesc vertexDesc = {};
    vertexDesc.name = "model vertices";
    vertexDesc.size = vertexBytes;
    vertexDesc.usageFlags = GfxBufferUsageFlags_Storage;
    vertexDesc.memoryKind = GfxMemoryKind_Upload;
    vertexDesc.initialData = vertexData;
    GfxBuffer vertexBuffer = gfx_create_buffer(bridge->device, &vertexDesc);

    GfxBufferDesc indexDesc = {};
    indexDesc.name = "model indices";
    indexDesc.size = indexBytes;
    indexDesc.usageFlags = GfxBufferUsageFlags_Index;
    indexDesc.memoryKind = GfxMemoryKind_Upload;
    indexDesc.initialData = poolIndices;
    GfxBuffer indexBuffer = gfx_create_buffer(bridge->device, &indexDesc);
    arena_release(rebaseArena);
    GfxResourceId vertexBufferId = gfx_register_buffer(bridge->device, vertexBuffer);
    if (vertexBuffer.generation == 0u || indexBuffer.generation == 0u || vertexBufferId.index == 0u) {
        gfx_destroy_buffer(bridge->device, vertexBuffer);
        gfx_destroy_buffer(bridge->device, indexBuffer);
        arena_release(arena);
        return 0;
    }

    Arena* modelArena = arena_alloc(.arenaSize = MB(4), .committedSize = KB(64));
    AppWorldModelResources* resources = modelArena ? ARENA_PUSH_STRUCT(modelArena, AppWorldModelResources) : 0;
    AppWorldMeshHandle* sectionMeshes = resources
        ? ARENA_PUSH_ARRAY(modelArena, AppWorldMeshHandle, header->sectionCount) : 0;
    AppWorldModelMaterialRef* materialRefs = resources
        ? ARENA_PUSH_ARRAY(modelArena, AppWorldModelMaterialRef, header->materialCount) : 0;
    AppWorldModelInstanceRef* instanceRefs = resources
        ? ARENA_PUSH_ARRAY(modelArena, AppWorldModelInstanceRef, header->instanceCount) : 0;
    if (!resources || !sectionMeshes || !materialRefs || !instanceRefs) {
        gfx_destroy_buffer(bridge->device, vertexBuffer);
        gfx_destroy_buffer(bridge->device, indexBuffer);
        if (modelArena) {
            arena_release(modelArena);
        }
        arena_release(arena);
        return 0;
    }
    MEMSET(resources, 0, sizeof(*resources));

    B32 failed = 0;
    U32 registeredSections = 0u;
    for (U32 section = 0u; section < header->sectionCount; ++section) {
        const AssetModelSection* source = sections + section;
        Vec3F32 center = app_world_vec3_(source->boundsCenter[0], source->boundsCenter[1], source->boundsCenter[2]);
        Vec3F32 extents = app_world_vec3_(source->boundsExtents[0], source->boundsExtents[1], source->boundsExtents[2]);
        // baseVertex 0: indices were rebased to pool-absolute above.
        sectionMeshes[section] = app_world_register_mesh_(world, source->firstIndex, source->indexCount,
                                                          0u, vertexBuffer, vertexBufferId,
                                                          indexBuffer, 0, center, extents);
        if (sectionMeshes[section].generation == 0u) {
            failed = 1;
            break;
        }
        registeredSections += 1u;
    }

    U32 allocatedMaterials = 0u;
    for (U32 material = 0u; !failed && material < header->materialCount; ++material) {
        if (!app_world_material_alloc(world, &materialRefs[material].worldSlot)) {
            failed = 1;
            break;
        }
        allocatedMaterials += 1u;
        materialRefs[material].textureLocal = materials[material].textureIndex;
        ShdWorldMaterialRecord record = {};
        record.baseColor[0] = materials[material].baseColor[0];
        record.baseColor[1] = materials[material].baseColor[1];
        record.baseColor[2] = materials[material].baseColor[2];
        record.baseColor[3] = materials[material].baseColor[3];
        app_world_material_set(world, materialRefs[material].worldSlot, &record);
    }

    for (U32 instance = 0u; !failed && instance < header->instanceCount; ++instance) {
        const AssetModelInstance* source = instances + instance;
        if (source->sectionIndex >= header->sectionCount || source->materialIndex >= header->materialCount) {
            failed = 1;
            break;
        }
        instanceRefs[instance].mesh = sectionMeshes[source->sectionIndex];
        instanceRefs[instance].materialSlot = materialRefs[source->materialIndex].worldSlot;
        MEMCPY(&instanceRefs[instance].transform, source->transform, sizeof(instanceRefs[instance].transform));
    }

    if (failed) {
        for (U32 section = 0u; section < registeredSections; ++section) {
            app_world_release_mesh_(bridge->device, world, sectionMeshes[section]);
        }
        for (U32 material = 0u; material < allocatedMaterials; ++material) {
            app_world_material_release(world, materialRefs[material].worldSlot);
        }
        gfx_destroy_buffer(bridge->device, vertexBuffer);
        gfx_destroy_buffer(bridge->device, indexBuffer);
        arena_release(modelArena);
        arena_release(arena);
        LOG_ERROR("asset", "Model publish failed (asset {})", request->assetIndex);
        return 0;
    }

    resources->vertexBuffer = vertexBuffer;
    resources->indexBuffer = indexBuffer;
    resources->vertexBufferId = vertexBufferId;
    resources->sectionCount = header->sectionCount;
    resources->sections = sectionMeshes;
    resources->materialCount = header->materialCount;
    resources->materials = materialRefs;
    resources->instanceCount = header->instanceCount;
    resources->instances = instanceRefs;
    resources->textureCount = header->textureCount;
    resources->boundsCenter[0] = header->boundsCenter[0];
    resources->boundsCenter[1] = header->boundsCenter[1];
    resources->boundsCenter[2] = header->boundsCenter[2];
    resources->boundsRadius = header->boundsRadius;
    world->models[request->assetIndex] = resources;

    // Watch the sibling texture files this model declares; file_watch
    // dedupes by path so republish is idempotent.
    if (bridge->state->resources.fileStream && resources->textureCount != 0u) {
        Temp scratch = get_scratch(0, 0);
        if (scratch.arena) {
            StringU8 exeDir = OS_get_executable_directory(scratch.arena);
            for (U32 textureLocal = 0u;
                 textureLocal < resources->textureCount && textureLocal < APP_WORLD_MODEL_MAX_TEXTURES;
                 ++textureLocal) {
                StringU8 path = str8_concat(scratch.arena, exeDir, str8("/../"));
                path = str8_concat(scratch.arena, path,
                                   app_world_model_texture_path_(scratch.arena,
                                                                 APP_WORLD_DEMO_ASSETS[request->assetIndex].modelPath,
                                                                 textureLocal));
                world->assetModelTextureFiles[request->assetIndex][textureLocal] =
                    file_watch(bridge->state->resources.fileStream, path, 0u);
            }
            temp_end(&scratch);
        }
    }

    // The blob (and header) die here; log from the resources copy.
    arena_release(arena);

    outValue->u64[0] = (U64)modelArena;
    outValue->u64[1] = (U64)resources;
    outValue->u64[2] = request->assetIndex;
    outValue->u64[3] = APP_ARTIFACT_PUBLISHED_MARK;
    *outBytes = vertexBytes + indexBytes;
    LOG_INFO("asset", "Model published: {} sections {} instances {} materials {} textures",
             resources->sectionCount, resources->instanceCount, resources->materialCount,
             resources->textureCount);
    return 1;
}

static B32 app_texture_artifact_publish_(ArtifactPublishContext* publishCtx, ArtifactValue buildValue,
                                         ArtifactValue* outValue, U64* outBytes) {
    AppWorldArtifactBridge* bridge = (AppWorldArtifactBridge*)publishCtx->typeUserData;
    Arena* arena = (Arena*)buildValue.u64[0];
    const U8* blob = (const U8*)buildValue.u64[1];
    U64 blobSize = buildValue.u64[2];
    if (!bridge || !bridge->device || !bridge->state || !blob) {
        arena_release(arena);
        return 0;
    }
    AppWorldState* world = &bridge->state->world;

    const AssetTextureHeader* header = (const AssetTextureHeader*)blob;
    if (header->version != ASSET_TEXTURE_VERSION ||
        header->mipCount == 0u || header->mipCount > ASSET_TEXTURE_MAX_MIPS) {
        arena_release(arena);
        return 0;
    }

    // The renderer parks the open frame on the bridge around the artifact
    // tick; publishes record their uploads into it.
    GfxFrame* frame = bridge->frame;
    if (!frame) {
        arena_release(arena);
        return 0;
    }

    GfxTextureDesc textureDesc = {};
    textureDesc.name = "asset texture";
    textureDesc.width = header->width;
    textureDesc.height = header->height;
    textureDesc.mipCount = header->mipCount;
    textureDesc.format = (header->format == ASSET_TEXTURE_FORMAT_BC3) ? GfxFormat_BC3_RGBA_UNorm
                                                                      : GfxFormat_BC1_RGBA_UNorm;
    textureDesc.usageFlags = GfxTextureUsageFlags_Sampled | GfxTextureUsageFlags_CopyDst;
    GfxTexture texture = gfx_create_texture(bridge->device, &textureDesc);
    if (texture.generation == 0u) {
        arena_release(arena);
        return 0;
    }

    // The blob carries the full mip chain; the whole chain uploads as one
    // atomic batch (mipCount <= ASSET_TEXTURE_MAX_MIPS was validated above).
    GfxTextureUploadRegion regions[ASSET_TEXTURE_MAX_MIPS] = {};
    U32 blockBytes = (header->format == ASSET_TEXTURE_FORMAT_BC3) ? 16u : 8u;
    U64 uploadedBytes = 0u;
    U32 mipWidth = header->width;
    U32 mipHeight = header->height;
    for (U32 mipIndex = 0u; mipIndex < header->mipCount; ++mipIndex) {
        if (header->mipOffsets[mipIndex] + header->mipSizes[mipIndex] > blobSize) {
            gfx_destroy_texture(bridge->device, texture);
            arena_release(arena);
            return 0;
        }
        GfxTextureUploadRegion* region = regions + mipIndex;
        region->src = blob + header->mipOffsets[mipIndex];
        region->mip = mipIndex;
        region->layerCount = 1u;
        region->width = mipWidth;
        region->height = mipHeight;
        region->depth = 1u;
        U64 packedRow = (U64)((mipWidth + 3u) / 4u) * blockBytes;
        region->bytesPerRow = ((packedRow + GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT - 1u) /
                               GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT) *
                              GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT;
        region->rowsPerImage = (mipHeight + 3u) / 4u;
        uploadedBytes += header->mipSizes[mipIndex];
        mipWidth = MAX(mipWidth / 2u, 1u);
        mipHeight = MAX(mipHeight / 2u, 1u);
    }
    if (!gfx_upload_texture(frame, texture, regions, header->mipCount)) {
        gfx_destroy_texture(bridge->device, texture);
        arena_release(arena);
        return 0;
    }
    U32 publishedWidth = header->width;
    U32 publishedHeight = header->height;
    U32 publishedMips = header->mipCount;
    arena_release(arena);

    GfxResourceId textureId = gfx_register_texture(bridge->device, texture);
    if (textureId.index == 0u) {
        gfx_destroy_texture(bridge->device, texture);
        return 0;
    }

    if (world->assetTextureCount < APP_WORLD_MAX_ASSET_TEXTURES) {
        world->assetTextures[world->assetTextureCount] = texture;
        world->assetTextureCount += 1u;
    }

    // Bind every model material that references this model-local texture.
    const AppAssetRequest* request = (const AppAssetRequest*)publishCtx->requestData;
    U32 boundCount = 0u;
    if (request && publishCtx->requestDataSize >= sizeof(AppAssetRequest) &&
        request->assetIndex < APP_WORLD_DEMO_ASSET_COUNT) {
        AppWorldModelResources* resources = world->models[request->assetIndex];
        for (U32 at = 0u; resources && at < resources->materialCount; ++at) {
            const AppWorldModelMaterialRef* ref = resources->materials + at;
            if (ref->textureLocal != request->textureLocal ||
                ref->worldSlot >= APP_WORLD_MAX_MATERIALS ||
                !FLAGS_HAS(world->materialUsedMask, 1ull << ref->worldSlot)) {
                continue;
            }
            ShdWorldMaterialRecord* material = &world->materialRecords[ref->worldSlot];
            material->textureIndex = textureId.index;
            material->samplerIndex = world->worldSamplerId.index;
            material->flags |= APP_WORLD_MATERIAL_FLAG_TEXTURED;
            world->materialsDirty = 1;
            boundCount += 1u;
        }
    }
    if (boundCount == 0u) {
        LOG_ERROR("asset", "Texture publish bound no materials (asset {} tex {})",
                  request ? request->assetIndex : 0xFFFFFFFFu,
                  request ? request->textureLocal : 0xFFFFFFFFu);
    }

    outValue->u64[0] = texture.index;
    outValue->u64[1] = texture.generation;
    outValue->u64[2] = textureId.index;
    outValue->u64[3] = APP_ARTIFACT_PUBLISHED_MARK;
    *outBytes = uploadedBytes;
    LOG_INFO("asset", "Texture published: {}x{} mips {}", publishedWidth, publishedHeight, publishedMips);
    return 1;
}

static void app_model_artifact_destroy_(void* typeUserData, ArtifactValue value) {
    AppWorldArtifactBridge* bridge = (AppWorldArtifactBridge*)typeUserData;
    if (!bridge || !bridge->device || !bridge->state) {
        return;
    }
    if (value.u64[3] != APP_ARTIFACT_PUBLISHED_MARK) {
        Arena* arena = (Arena*)value.u64[0];
        arena_release(arena);
        return;
    }
    AppWorldState* world = &bridge->state->world;
    Arena* modelArena = (Arena*)value.u64[0];
    AppWorldModelResources* resources = (AppWorldModelResources*)value.u64[1];
    U32 assetIndex = (U32)value.u64[2];
    for (U32 section = 0u; section < resources->sectionCount; ++section) {
        app_world_release_mesh_(bridge->device, world, resources->sections[section]);
    }
    for (U32 material = 0u; material < resources->materialCount; ++material) {
        app_world_material_release(world, resources->materials[material].worldSlot);
    }
    gfx_destroy_buffer(bridge->device, resources->vertexBuffer);
    gfx_destroy_buffer(bridge->device, resources->indexBuffer);
    // A supersede has already pointed models[] at the replacement; only a
    // terminal destroy (eviction, shutdown) still owns the slot.
    if (assetIndex < APP_WORLD_DEMO_ASSET_COUNT && world->models[assetIndex] == resources) {
        world->models[assetIndex] = 0;
    }
    arena_release(modelArena);
    LOG_INFO("asset", "Model destroyed (asset {})", assetIndex);
}

static void app_texture_artifact_destroy_(void* typeUserData, ArtifactValue value) {
    AppWorldArtifactBridge* bridge = (AppWorldArtifactBridge*)typeUserData;
    if (!bridge || !bridge->device) {
        return;
    }
    if (value.u64[3] != APP_ARTIFACT_PUBLISHED_MARK) {
        Arena* arena = (Arena*)value.u64[0];
        arena_release(arena);
        return;
    }
    GfxTexture texture = {};
    texture.index = (U32)value.u64[0];
    texture.generation = (U32)value.u64[1];
    U32 textureId = (U32)value.u64[2];
    AppWorldState* world = &bridge->state->world;
    for (U32 at = 0u; at < world->assetTextureCount; ++at) {
        if (world->assetTextures[at].index == texture.index &&
            world->assetTextures[at].generation == texture.generation) {
            world->assetTextures[at] = world->assetTextures[world->assetTextureCount - 1u];
            world->assetTextureCount -= 1u;
            break;
        }
    }
    // Unbind any material still pointing at this texture's bindless id; a
    // supersede has already rebound to the replacement and matches nothing.
    for (U32 materialIndex = 0u; materialIndex < APP_WORLD_MAX_MATERIALS; ++materialIndex) {
        ShdWorldMaterialRecord* material = &world->materialRecords[materialIndex];
        if (FLAGS_HAS(material->flags, APP_WORLD_MATERIAL_FLAG_TEXTURED) &&
            material->textureIndex == textureId) {
            material->flags &= ~APP_WORLD_MATERIAL_FLAG_TEXTURED;
            material->textureIndex = 0u;
            material->samplerIndex = 0u;
            world->materialsDirty = 1;
            LOG_INFO("asset", "Texture destroyed; material {} unbound", materialIndex);
        }
    }
    gfx_destroy_texture(bridge->device, texture);
}

static B32 app_audio_artifact_build_(ArtifactBuildContext* buildCtx, ArtifactValue* outValue, U64* outBytes) {
    return app_asset_build_blob_(buildCtx, ASSET_AUDIO_MAGIC, sizeof(AssetAudioHeader), outValue, outBytes);
}

static B32 app_audio_artifact_publish_(ArtifactPublishContext* publishCtx, ArtifactValue buildValue,
                                       ArtifactValue* outValue, U64* outBytes) {
    AppWorldArtifactBridge* bridge = (AppWorldArtifactBridge*)publishCtx->typeUserData;
    Arena* arena = (Arena*)buildValue.u64[0];
    const U8* blob = (const U8*)buildValue.u64[1];
    U64 blobSize = buildValue.u64[2];
    const AppAssetRequest* request = (const AppAssetRequest*)publishCtx->requestData;
    if (!bridge || !bridge->audioSystem || !bridge->state || !blob ||
        !request || publishCtx->requestDataSize < sizeof(AppAssetRequest) ||
        request->assetIndex >= AppSound_Count) {
        arena_release(arena);
        return 0;
    }

    const AssetAudioHeader* header = (const AssetAudioHeader*)blob;
    U64 sampleBytes = (U64)header->frameCount * header->channelCount * sizeof(F32);
    if (header->version != ASSET_AUDIO_VERSION ||
        header->sampleRate != ASSET_AUDIO_SAMPLE_RATE ||
        header->channelCount != ASSET_AUDIO_CHANNELS ||
        header->frameCount == 0u ||
        sizeof(AssetAudioHeader) + sampleBytes > blobSize) {
        LOG_ERROR("asset", "Audio blob rejected (sound {})", request->assetIndex);
        arena_release(arena);
        return 0;
    }

    AudioBufferDesc desc = {};
    desc.frameCount = header->frameCount;
    desc.loopBegin = header->loopBegin;
    desc.loopEnd = header->loopEnd;
    desc.samples = (const F32*)(blob + sizeof(AssetAudioHeader));
    AudioBufferHandle handle = audio_buffer_create(bridge->audioSystem, &desc);
    if (handle.generation == 0u) {
        arena_release(arena);
        return 0;
    }

    AppAudioState* audio = &bridge->state->audio;
    audio->sounds[request->assetIndex] = handle;
    // Republishing the ambience bed killed its looping voice through the
    // generation bump; restart it so the toggle stays truthful.
    if (request->assetIndex == AppSound_Ambience && audio->ambienceOn) {
        audio_play(bridge->audioSystem, handle, APP_SOUND_GAIN_AMBIENCE, 1);
    }

    // The blob (and header) die here; log from locals only (the U6 class).
    U32 frameCount = header->frameCount;
    arena_release(arena);

    outValue->u64[0] = ((U64)handle.index << 32u) | (U64)handle.generation;
    outValue->u64[1] = request->assetIndex;
    outValue->u64[2] = 0ull;
    outValue->u64[3] = APP_ARTIFACT_PUBLISHED_MARK;
    *outBytes = sampleBytes;
    LOG_INFO("asset", "Sound published: sound {} frames {} (slot {} gen {})",
             request->assetIndex, frameCount, handle.index, handle.generation);
    return 1;
}

static void app_audio_artifact_destroy_(void* typeUserData, ArtifactValue value) {
    AppWorldArtifactBridge* bridge = (AppWorldArtifactBridge*)typeUserData;
    if (!bridge || !bridge->state) {
        return;
    }
    if (value.u64[3] != APP_ARTIFACT_PUBLISHED_MARK) {
        Arena* arena = (Arena*)value.u64[0];
        arena_release(arena);
        return;
    }
    AudioBufferHandle handle = {};
    handle.index = (U32)(value.u64[0] >> 32u);
    handle.generation = (U32)value.u64[0];
    U32 soundIndex = (U32)value.u64[1];
    // A supersede has already rebound sounds[]; only a terminal destroy
    // still owns the slot.
    AppAudioState* audio = &bridge->state->audio;
    if (soundIndex < AppSound_Count &&
        audio->sounds[soundIndex].index == handle.index &&
        audio->sounds[soundIndex].generation == handle.generation) {
        AudioBufferHandle zero = {};
        audio->sounds[soundIndex] = zero;
    }
    audio_buffer_destroy(bridge->audioSystem, handle);
    LOG_INFO("asset", "Sound destroyed (sound {} slot {} gen {})", soundIndex, handle.index, handle.generation);
}

static B32 app_world_register_artifact_types_(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    AppCoreState* state = ctx->core;
    if (!state->resources.artifactCache || !ctx->host->gfxDevice) {
        return 1;
    }

    state->world.artifactBridge.device = ctx->host->gfxDevice;
    state->world.artifactBridge.audioSystem = ctx->host->audioSystem;
    state->world.artifactBridge.state = state;

    ArtifactTypeDesc modelType = {};
    modelType.typeId = APP_ARTIFACT_TYPE_MODEL;
    modelType.name = str8("model");
    modelType.buildProc = app_model_artifact_build_;
    modelType.publishProc = app_model_artifact_publish_;
    modelType.destroyProc = app_model_artifact_destroy_;
    modelType.userData = &state->world.artifactBridge;
    if (!artifact_register_type(state->resources.artifactCache, &modelType)) {
        return 0;
    }

    ArtifactTypeDesc textureType = {};
    textureType.typeId = APP_ARTIFACT_TYPE_TEXTURE;
    textureType.name = str8("texture");
    textureType.buildProc = app_texture_artifact_build_;
    textureType.publishProc = app_texture_artifact_publish_;
    textureType.destroyProc = app_texture_artifact_destroy_;
    textureType.userData = &state->world.artifactBridge;
    if (!artifact_register_type(state->resources.artifactCache, &textureType)) {
        return 0;
    }

    ArtifactTypeDesc audioType = {};
    audioType.typeId = APP_ARTIFACT_TYPE_AUDIO;
    audioType.name = str8("audio");
    audioType.buildProc = app_audio_artifact_build_;
    audioType.publishProc = app_audio_artifact_publish_;
    audioType.destroyProc = app_audio_artifact_destroy_;
    audioType.userData = &state->world.artifactBridge;
    if (!artifact_register_type(state->resources.artifactCache, &audioType)) {
        return 0;
    }
    return 1;
}

// Resolves every demo asset through the artifact cache and records whether
// the set is settled (all artifacts Ready for the current file generations)
// in world->assetsSettled, which gates the per-frame resource polls.
static void app_world_try_load_assets_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;

    if (!state->resources.fileStream || !state->resources.artifactCache || !world->gpuResourcesCreated) {
        return;
    }

    B32 settled = 1;
    for (U32 assetIndex = 0u; assetIndex < APP_WORLD_DEMO_ASSET_COUNT; ++assetIndex) {
        const AppWorldDemoAssetDesc* asset = APP_WORLD_DEMO_ASSETS + assetIndex;

        FileView modelView = file_view(state->resources.fileStream, world->assetModelFiles[assetIndex]);
        if (modelView.status == FileStatus_Ready && !content_hash_is_zero(modelView.hash)) {
            AppAssetRequest request = {};
            request.hash = modelView.hash;
            request.assetIndex = assetIndex;
            ArtifactResult result = artifact_get(state->resources.artifactCache, APP_ARTIFACT_TYPE_MODEL,
                                                 app_artifact_key_from_label(asset->modelLabel),
                                                 modelView.generation, &request, sizeof(request),
                                                 ArtifactGetFlags_None, 0u);
            if (result.status != ArtifactStatus_Ready ||
                result.value.u64[3] != APP_ARTIFACT_PUBLISHED_MARK) {
                settled = 0;
            }
        } else {
            settled = 0;
        }

        // The published model declares how many sibling textures exist.
        const AppWorldModelResources* resources = world->models[assetIndex];
        if (!resources) {
            settled = 0;
            continue;
        }
        for (U32 textureLocal = 0u; textureLocal < resources->textureCount; ++textureLocal) {
            if (world->assetModelTextureFiles[assetIndex][textureLocal].generation == 0u) {
                settled = 0;
                continue;
            }
            FileView textureView = file_view(state->resources.fileStream,
                                             world->assetModelTextureFiles[assetIndex][textureLocal]);
            if (textureView.status != FileStatus_Ready || content_hash_is_zero(textureView.hash)) {
                settled = 0;
                continue;
            }
            AppAssetRequest request = {};
            request.hash = textureView.hash;
            request.assetIndex = assetIndex;
            request.textureLocal = textureLocal;
            Temp scratch = get_scratch(0, 0);
            if (!scratch.arena) {
                settled = 0;
                continue;
            }
            StringU8 label = str8_fmt(scratch.arena, "{}#tex{}", str8(asset->modelLabel), textureLocal);
            ArtifactResult result = artifact_get(state->resources.artifactCache, APP_ARTIFACT_TYPE_TEXTURE,
                                                 artifact_key_from_bytes(label.data, label.size),
                                                 textureView.generation, &request, sizeof(request),
                                                 ArtifactGetFlags_None, 0u);
            temp_end(&scratch);
            if (result.status != ArtifactStatus_Ready ||
                result.value.u64[3] != APP_ARTIFACT_PUBLISHED_MARK) {
                settled = 0;
            }
        }
    }
    world->assetsSettled = settled;
}

// Same shape as the model poll: resolve every sound through the artifact
// cache and record whether the set is settled for the current generations.
static void app_audio_try_load_sounds_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    AppAudioState* audio = &state->audio;

    if (!state->resources.fileStream || !state->resources.artifactCache ||
        !ctx->host->audioSystem) {
        return;
    }

    B32 settled = 1;
    for (U32 soundIndex = 0u; soundIndex < AppSound_Count; ++soundIndex) {
        FileView soundView = file_view(state->resources.fileStream, audio->soundFiles[soundIndex]);
        if (soundView.status != FileStatus_Ready || content_hash_is_zero(soundView.hash)) {
            settled = 0;
            continue;
        }
        AppAssetRequest request = {};
        request.hash = soundView.hash;
        request.assetIndex = soundIndex;
        ArtifactResult result = artifact_get(state->resources.artifactCache, APP_ARTIFACT_TYPE_AUDIO,
                                             app_artifact_key_from_label(APP_AUDIO_SOUND_DESCS[soundIndex].soundLabel),
                                             soundView.generation, &request, sizeof(request),
                                             ArtifactGetFlags_None, 0u);
        if (result.status != ArtifactStatus_Ready ||
            result.value.u64[3] != APP_ARTIFACT_PUBLISHED_MARK) {
            settled = 0;
        }
    }
    audio->settled = settled;
}

static B32 app_world_create_graphics_pipeline_(APP_Context* ctx, ContentHash vertexHash, ContentHash fragmentHash,
                                               B32 transparent, GfxPipeline* outPipeline) {
    *outPipeline = {};
    ContentView vertexView = content_view_hash(ctx->core->resources.contentStore, vertexHash);
    ContentView fragmentView = content_view_hash(ctx->core->resources.contentStore, fragmentHash);
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
    desc.vertexShader.entry = APP_SHADER_WORLD_VERTEX_ENTRY;
    desc.vertexShader.data = vertexView.data;
    desc.vertexShader.size = vertexView.size;
    desc.fragmentShader.entry = APP_SHADER_WORLD_FRAGMENT_ENTRY;
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

static void app_world_try_update_pipelines_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
    if (state->resources.fileStream == 0 || state->resources.contentStore == 0 || ctx->host->gfxDevice == 0) {
        return;
    }

    FileView views[APP_WORLD_SHADER_COUNT];
    B32 allReady = 1;
    B32 anyChanged = 0;
    for (U32 shaderIndex = 0u; shaderIndex < APP_WORLD_SHADER_COUNT; ++shaderIndex) {
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
    if (!app_world_create_graphics_pipeline_(ctx, views[AppWorldShaderSlot_Vertex].hash,
                                             views[AppWorldShaderSlot_Fragment].hash, 0, &newOpaque) ||
        !app_world_create_graphics_pipeline_(ctx, views[AppWorldShaderSlot_Vertex].hash,
                                             views[AppWorldShaderSlot_Fragment].hash, 1, &newTransparent)) {
        gfx_destroy_pipeline(ctx->host->gfxDevice, newOpaque);
        return;
    }

    GfxPipeline newCompute[5] = {};
    B32 computeOk = 1;
    for (U32 passIndex = 0u; passIndex < 5u; ++passIndex) {
        ContentView view = content_view_hash(ctx->core->resources.contentStore,
                                             views[AppWorldShaderSlot_Reset + passIndex].hash);
        if (!view.valid || view.size == 0u) {
            computeOk = 0;
            break;
        }
        GfxComputePipelineDesc desc = {};
        desc.name = APP_WORLD_COMPUTE_ENTRIES[passIndex];
#if defined(PLATFORM_OS_WINDOWS)
        desc.shader.format = GfxShaderFormat_SPIRV;
#else
        desc.shader.format = GfxShaderFormat_MSL_Source;
#endif
        desc.shader.entry = APP_WORLD_COMPUTE_ENTRIES[passIndex];
        desc.shader.data = view.data;
        desc.shader.size = view.size;
        desc.threadsPerThreadgroupX = APP_WORLD_COMPUTE_GROUP_SIZES[passIndex];
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
    for (U32 shaderIndex = 0u; shaderIndex < APP_WORLD_SHADER_COUNT; ++shaderIndex) {
        world->shaderHashes[shaderIndex] = views[shaderIndex].hash;
    }
    LOG_INFO("gfx", "World pipelines ready");
}

static void app_world_ensure_depth_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
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

static void app_world_begin_frame_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
    Arena* arena = ctx->host->frameArena;

    // spmd_dispatch lanes must not exceed worker count (debug-asserted).
    U32 laneCount = 1u;
    if (state->demo.threadedExtract && state->jobSystem && state->workerCount > 1u) {
        laneCount = MIN(state->workerCount, MIN(state->demo.maxLanes, APP_WORLD_MAX_LANES));
        laneCount = MAX(laneCount, 1u);
    }
    world->laneCount = laneCount;
    world->laneWriters = ARENA_PUSH_ARRAY(arena, AppWorldLaneWriter, laneCount);
    ShdWorldRenderableRecord* records = ARENA_PUSH_ARRAY(arena, ShdWorldRenderableRecord, APP_WORLD_MAX_RENDERABLES);
    ShdWorldRenderableRecord* transparents = ARENA_PUSH_ARRAY(arena, ShdWorldRenderableRecord, APP_WORLD_MAX_TRANSPARENTS);
    world->frameOpen = (world->laneWriters != 0 && records != 0 && transparents != 0);
    if (!world->frameOpen) {
        app_world_fail_once_(world, AppWorldFailLog_FrameAlloc, "frame arena exhausted in begin");
        return;
    }

    U32 cap = APP_WORLD_MAX_RENDERABLES / laneCount;
    U32 transparentCap = APP_WORLD_MAX_TRANSPARENTS / laneCount;
    for (U32 lane = 0u; lane < laneCount; ++lane) {
        AppWorldLaneWriter* writer = world->laneWriters + lane;
        writer->records = records + (U64)lane * cap;
        writer->count = 0u;
        writer->cap = cap;
        writer->transparents = transparents + (U64)lane * transparentCap;
        writer->transparentCount = 0u;
        writer->transparentCap = transparentCap;
        writer->dropped = 0u;
    }
}

static void app_world_set_camera(APP_Context* ctx, Vec3F32 eye, Vec3F32 target, F32 fovYRadians,
                                 F32 zNear, F32 zFar) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
    F32 aspect = (ctx->host->windowHeight != 0u)
        ? ((F32)ctx->host->windowWidth / (F32)ctx->host->windowHeight)
        : 1.0f;
    Mat4x4F32 viewProj = app_world_camera_view_proj_(eye, target, app_world_vec3_(0.0f, 1.0f, 0.0f),
                                                     fovYRadians, aspect, zNear, zFar);

    MEMSET(&world->frameRecord, 0, sizeof(world->frameRecord));
    MEMCPY(world->frameRecord.viewProj, &viewProj, sizeof(world->frameRecord.viewProj));
    app_world_frustum_planes_(&viewProj, world->frameRecord.frustumPlanes);
    world->frameRecord.cameraPos[0] = eye.x;
    world->frameRecord.cameraPos[1] = eye.y;
    world->frameRecord.cameraPos[2] = eye.z;
    world->frameRecord.time = (F32)state->simTimeSeconds;

    Vec3F32 forward = app_world_vec3_(target.x - eye.x, target.y - eye.y, target.z - eye.z);
    F32 length = vec3_length(forward);
    if (length > 0.0f) {
        forward.x /= length;
        forward.y /= length;
        forward.z /= length;
    }
    world->cameraForward = forward;
}

static void app_world_writer_push_(AppWorldState* world, AppWorldLaneWriter* writer,
                                   AppWorldMeshHandle meshHandle, U32 materialIndex,
                                   AppWorldBin bin, const Mat4x4F32* transform) {
    AppWorldMesh* mesh = (AppWorldMesh*)slot_map_get(&world->meshes, meshHandle.index, meshHandle.generation);
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
    record.cellIndex = (bin == AppWorldBin_Transparent)
        ? meshHandle.index
        : (U32)bin * APP_WORLD_MAX_MESHES + meshHandle.index;
    record.flags = 0u;

    if (bin == AppWorldBin_Transparent) {
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

static void app_world_push(APP_Context* ctx, AppWorldMeshHandle meshHandle, U32 materialIndex,
                           AppWorldBin bin, const Mat4x4F32* transform) {
    AppWorldState* world = &ctx->core->world;
    if (!world->frameOpen) {
        return;
    }
    app_world_writer_push_(world, world->laneWriters, meshHandle, materialIndex, bin, transform);
}

// Merges every lane's transparent slice, frustum-culls on the CPU (the GPU
// never sees transparents), sorts back-to-front by the view depth of the
// nearest bounding point (depth - radius, so a shell draws after what it
// contains), and writes the tail straight into tailOut (the mapped
// renderable buffer). outTailCells gets each tail record's cellIndex in CPU
// memory so the run walk never reads GPU memory back. Cap overflow drops
// the nearest records and counts as dropped; culled records are just
// invisible. Returns 0 only on allocation failure.
static B32 app_world_cull_sort_transparents_(AppWorldState* world, Arena* frameArena,
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
        const AppWorldLaneWriter* writer = world->laneWriters + lane;
        for (U32 at = 0u; at < writer->transparentCount; ++at) {
            const ShdWorldRenderableRecord* record = writer->transparents + at;
            F32 radius = record->boundsRadius;
            if (!app_world_sphere_visible_(planes, record->boundsCenter, radius)) {
                continue;
            }
            depths[visible] = app_world_transparent_depth_(record->boundsCenter, radius, eye, forward);
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
    app_world_order_ascending_(depths, order, scratch, visible);
    for (U32 at = 0u; at < uploadCount; ++at) {
        const ShdWorldRenderableRecord* source = sources[order[visible - 1u - at]];
        tailOut[at] = *source;
        tailCells[at] = source->cellIndex;
    }
    *outTailCells = tailCells;
    *outUploadCount = uploadCount;
    return 1;
}

static void app_world_execute_(APP_Context* ctx, AppRendererFrame* rendererFrame) {
    AppCoreState* state = ctx->core;
    AppWorldState* world = &state->world;
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
        const AppWorldLaneWriter* writer = world->laneWriters + lane;
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
    U32 frameBufferIndex = (U32)(state->frameCounter & (APP_WORLD_FRAME_BUFFER_COUNT - 1u));
    GfxDevice* device = ctx->host->gfxDevice;
    ShdWorldRenderableRecord* mappedRenderables =
        (ShdWorldRenderableRecord*)gfx_buffer_contents(device, world->renderableBuffers[frameBufferIndex]);
    ShdWorldFrameRecord* mappedFrameRecord =
        (ShdWorldFrameRecord*)gfx_buffer_contents(device, world->frameRecordBuffers[frameBufferIndex]);
    if (!mappedRenderables || !mappedFrameRecord) {
        app_world_fail_once_(world, AppWorldFailLog_MappedBuffers, "per-frame buffers are not mapped");
        return;
    }

    // Transparents never enter the GPU cull set: CPU frustum cull + sort,
    // written as a contiguous tail after the opaque records, drawn directly.
    U32* transparentCells = 0;
    U32 transparentUpload = 0u;
    if (transparentTotal != 0u) {
        PROF_SCOPE("world transparents");
        U32 transparentDropped = 0u;
        if (!app_world_cull_sort_transparents_(world, ctx->host->frameArena, transparentTotal,
                                               APP_WORLD_MAX_RENDERABLES - opaqueTotal,
                                               mappedRenderables + opaqueTotal,
                                               &transparentCells, &transparentUpload, &transparentDropped)) {
            app_world_fail_once_(world, AppWorldFailLog_TransparentBuild, "transparent cull/sort allocation failed");
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
        const AppWorldLaneWriter* writer = world->laneWriters + lane;
        if (writer->count == 0u) {
            continue;
        }
        MEMCPY(mappedRenderables + uploadOffset, writer->records,
               sizeof(ShdWorldRenderableRecord) * writer->count);
        uploadOffset += writer->count;
    }

    if (world->meshRecordsDirty) {
        ShdWorldMeshRecord meshRecords[APP_WORLD_MAX_MESHES] = {};
        for (U32 meshSlot = 0u; meshSlot < APP_WORLD_MAX_MESHES; ++meshSlot) {
            if (!slot_map_is_occupied(&world->meshes, meshSlot)) {
                continue;
            }
            AppWorldMesh* mesh = (AppWorldMesh*)slot_map_item_at(&world->meshes, meshSlot);
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

    U32 cellCount = app_world_cell_count_(world);

    GfxTemp rootTemp = gfx_allocate_temp(frame, sizeof(ShdWorldCullRootData), 16u);
    if (!rootTemp.cpu) {
        app_world_fail_once_(world, AppWorldFailLog_RootTemp, "cull root temp allocation failed");
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
    cullRoot->meshCount = APP_WORLD_MAX_MESHES;

    static const char* passNames[5] = {
        "world reset", "world cull", "world prefix", "world scatter", "world args",
    };
    // groupsX floors at 1 so an all-transparent frame still dispatches legally;
    // the kernels early-out on renderableCount.
    U32 renderableGroups = MAX(1u, (opaqueTotal + APP_WORLD_CULL_GROUP_SIZE - 1u) / APP_WORLD_CULL_GROUP_SIZE);
    U32 groupCounts[5] = {
        (cellCount + APP_WORLD_CULL_GROUP_SIZE - 1u) / APP_WORLD_CULL_GROUP_SIZE,
        renderableGroups,
        1u,
        renderableGroups,
        (cellCount + APP_WORLD_CULL_GROUP_SIZE - 1u) / APP_WORLD_CULL_GROUP_SIZE,
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

    U32 maxDrawUses = 7u + APP_WORLD_DEMO_ASSET_COUNT + world->assetTextureCount;
    GfxResourceUse* drawUses = ARENA_PUSH_ARRAY(ctx->host->frameArena, GfxResourceUse, maxDrawUses);
    if (!drawUses) {
        app_world_fail_once_(world, AppWorldFailLog_DrawAlloc, "forward pass allocation failed");
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
    for (U32 assetIndex = 0u; assetIndex < APP_WORLD_DEMO_ASSET_COUNT; ++assetIndex) {
        const AppWorldModelResources* model = world->models[assetIndex];
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
        app_world_fail_once_(world, AppWorldFailLog_DrawAlloc, "forward pass allocation failed");
        world->lastRenderableCount = 0u;
        return;
    }
    // One temp block holds every draw's root record at a fixed stride.
    GfxTemp rootBlock = gfx_allocate_temp(frame, sizeof(ShdWorldForwardRootData) * (cellCount + transparentUpload), 16u);
    if (!rootBlock.cpu) {
        app_world_fail_once_(world, AppWorldFailLog_RootTemp, "draw root temp allocation failed");
        world->lastRenderableCount = 0u;
        return;
    }
    U32 drawCount = 0u;
    for (U32 cell = 0u; cell < cellCount; ++cell) {
        U32 meshSlot = cell % APP_WORLD_MAX_MESHES;
        if (!slot_map_is_occupied(&world->meshes, meshSlot)) {
            continue;
        }
        AppWorldMesh* mesh = (AppWorldMesh*)slot_map_item_at(&world->meshes, meshSlot);

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
        rootData->directFirstRenderable = APP_WORLD_DIRECT_NONE;

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
        U32 meshSlot = cellIndex % APP_WORLD_MAX_MESHES;
        if (!slot_map_is_occupied(&world->meshes, meshSlot)) {
            runStart = runEnd;
            continue;
        }
        AppWorldMesh* mesh = (AppWorldMesh*)slot_map_item_at(&world->meshes, meshSlot);

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

static void app_world_shutdown_(APP_Context* ctx) {
    AppCoreState* state = ctx->core;
    GfxDevice* device = ctx->host ? ctx->host->gfxDevice : 0;
    if (device == 0) {
        return;
    }

    AppWorldState* world = &state->world;
    gfx_destroy_buffer(device, world->vertexBuffer);
    gfx_destroy_buffer(device, world->indexBuffer);
    gfx_destroy_buffer(device, world->meshRecordBuffer);
    gfx_destroy_buffer(device, world->materialBuffer);
    for (U32 bufferIndex = 0u; bufferIndex < APP_WORLD_FRAME_BUFFER_COUNT; ++bufferIndex) {
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

