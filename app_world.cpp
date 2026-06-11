//
// Created by André Leite on 11/06/2026.
//
// World renderer: mesh/material tables, cooked-asset decode, extraction
// lane writers, GPU visibility passes, and the forward pass.
//

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

// Demo asset policy: which cooked assets exist. Module rodata; hot state
// stores handles and dynamically allocated material indices.
struct AppWorldDemoAssetDesc {
    const char* meshPath;
    const char* texturePath;
    const char* meshLabel;
    const char* textureLabel;
};

static const AppWorldDemoAssetDesc APP_WORLD_DEMO_ASSETS[APP_WORLD_DEMO_ASSET_COUNT] = {
    {"app/assets/cooked/Duck.umsh", "app/assets/cooked/Duck.utex",
     "assets/Duck.umsh", "assets/Duck.utex"},
    {"app/assets/cooked/Avocado.umsh", "app/assets/cooked/Avocado.utex",
     "assets/Avocado.umsh", "assets/Avocado.utex"},
};


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
        StringU8 meshPath = str8_concat(scratch.arena, exeDir, str8("/../"));
        meshPath = str8_concat(scratch.arena, meshPath, str8(asset->meshPath));
        StringU8 texturePath = str8_concat(scratch.arena, exeDir, str8("/../"));
        texturePath = str8_concat(scratch.arena, texturePath, str8(asset->texturePath));
        state->world.assetMeshFiles[assetIndex] = file_watch(state->resources.fileStream, meshPath, 0u);
        state->world.assetTextureFiles[assetIndex] = file_watch(state->resources.fileStream, texturePath, 0u);
    }

    for (U32 shaderIndex = 0u; shaderIndex < APP_WORLD_SHADER_COUNT; ++shaderIndex) {
        StringU8 worldPath = str8_concat(scratch.arena, exeDir, str8("/../"));
        worldPath = str8_concat(scratch.arena, worldPath, str8(APP_WORLD_SHADER_PATHS[shaderIndex]));
        state->world.shaderFiles[shaderIndex] = file_watch(state->resources.fileStream, worldPath, 0u);
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
    return APP_WORLD_BIN_COUNT * APP_WORLD_MAX_MESHES;
}

static Vec3F32 app_world_vec3_(F32 x, F32 y, F32 z) {
    Vec3F32 result;
    result.x = x;
    result.y = y;
    result.z = z;
    return result;
}

struct AppWorldMeshBuilder {
    ShdWorldVertexRecord* vertices;
    U32* indices;
    U32 vertexCount;
    U32 indexCount;
    U32 vertexCapacity;
    U32 indexCapacity;
};

static void app_world_builder_vertex_(AppWorldMeshBuilder* builder, F32 px, F32 py, F32 pz,
                                      F32 nx, F32 ny, F32 nz, F32 u, F32 v) {
    if (builder->vertexCount >= builder->vertexCapacity) {
        return;
    }
    ShdWorldVertexRecord* vertex = builder->vertices + builder->vertexCount;
    builder->vertexCount += 1u;
    vertex->position[0] = px;
    vertex->position[1] = py;
    vertex->position[2] = pz;
    vertex->normal[0] = nx;
    vertex->normal[1] = ny;
    vertex->normal[2] = nz;
    vertex->uv[0] = u;
    vertex->uv[1] = v;
}

static void app_world_builder_index_(AppWorldMeshBuilder* builder, U32 a, U32 b, U32 c) {
    if (builder->indexCount + 3u > builder->indexCapacity) {
        return;
    }
    builder->indices[builder->indexCount + 0u] = a;
    builder->indices[builder->indexCount + 1u] = b;
    builder->indices[builder->indexCount + 2u] = c;
    builder->indexCount += 3u;
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
        if (FLAGS_HAS(world->materialUsedMask, 1u << index)) {
            continue;
        }
        world->materialUsedMask |= 1u << index;
        MEMSET(&world->materialRecords[index], 0, sizeof(world->materialRecords[index]));
        world->materialsDirty = 1;
        *outIndex = index;
        return 1;
    }
    LOG_ERROR("gfx", "World material table full ({} slots)", APP_WORLD_MAX_MATERIALS);
    return 0;
}

static void app_world_material_set(AppWorldState* world, U32 index, const ShdWorldMaterialRecord* record) {
    if (index >= APP_WORLD_MAX_MATERIALS || !FLAGS_HAS(world->materialUsedMask, 1u << index)) {
        return;
    }
    world->materialRecords[index] = *record;
    world->materialsDirty = 1;
}

static void app_world_material_release(AppWorldState* world, U32 index) {
    if (index <= APP_WORLD_MATERIAL_MISSING || index >= APP_WORLD_MAX_MATERIALS) {
        return;
    }
    world->materialUsedMask &= ~(1u << index);
    // In-flight references to the freed slot show the missing color until
    // the slot is reused.
    world->materialRecords[index] = world->materialRecords[APP_WORLD_MATERIAL_MISSING];
    world->materialsDirty = 1;
}

static void app_world_build_cube_(AppWorldMeshBuilder* builder) {
    static const F32 faces[6][3] = {
        {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
    };
    for (U32 face = 0u; face < 6u; ++face) {
        F32 nx = faces[face][0];
        F32 ny = faces[face][1];
        F32 nz = faces[face][2];
        F32 ux = ny;
        F32 uy = nz;
        F32 uz = nx;
        F32 vx = ny * uz - nz * uy;
        F32 vy = nz * ux - nx * uz;
        F32 vz = nx * uy - ny * ux;
        U32 base = builder->vertexCount;
        for (U32 corner = 0u; corner < 4u; ++corner) {
            F32 s = (corner == 1u || corner == 2u) ? 0.5f : -0.5f;
            F32 t = (corner >= 2u) ? 0.5f : -0.5f;
            app_world_builder_vertex_(builder,
                                      nx * 0.5f + ux * s + vx * t,
                                      ny * 0.5f + uy * s + vy * t,
                                      nz * 0.5f + uz * s + vz * t,
                                      nx, ny, nz,
                                      (corner == 1u || corner == 2u) ? 1.0f : 0.0f,
                                      (corner >= 2u) ? 1.0f : 0.0f);
        }
        app_world_builder_index_(builder, base + 0u, base + 1u, base + 2u);
        app_world_builder_index_(builder, base + 0u, base + 2u, base + 3u);
    }
}

static void app_world_build_sphere_(AppWorldMeshBuilder* builder, U32 rings, U32 sectors) {
    U32 base = builder->vertexCount;
    for (U32 ring = 0u; ring <= rings; ++ring) {
        F32 v = (F32)ring / (F32)rings;
        F32 phi = v * 3.14159265f;
        F32 y = COS_F32(phi);
        F32 r = SIN_F32(phi);
        for (U32 sector = 0u; sector <= sectors; ++sector) {
            F32 u = (F32)sector / (F32)sectors;
            F32 theta = u * 2.0f * 3.14159265f;
            F32 x = r * COS_F32(theta);
            F32 z = r * SIN_F32(theta);
            app_world_builder_vertex_(builder, x * 0.5f, y * 0.5f, z * 0.5f, x, y, z, u, v);
        }
    }
    for (U32 ring = 0u; ring < rings; ++ring) {
        for (U32 sector = 0u; sector < sectors; ++sector) {
            U32 a = base + ring * (sectors + 1u) + sector;
            U32 b = a + sectors + 1u;
            // Wound to match the cube/plane convention (right-handed cross
            // points outward); the transparent pipeline culls on it.
            app_world_builder_index_(builder, a, a + 1u, b);
            app_world_builder_index_(builder, a + 1u, b + 1u, b);
        }
    }
}

static void app_world_build_plane_(AppWorldMeshBuilder* builder) {
    U32 base = builder->vertexCount;
    app_world_builder_vertex_(builder, -0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    app_world_builder_vertex_(builder, 0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f);
    app_world_builder_vertex_(builder, 0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f);
    app_world_builder_vertex_(builder, -0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f);
    app_world_builder_index_(builder, base + 0u, base + 2u, base + 1u);
    app_world_builder_index_(builder, base + 0u, base + 3u, base + 2u);
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

#define APP_ARTIFACT_TYPE_MESH 0x4D455348u
#define APP_ARTIFACT_TYPE_TEXTURE 0x54455855u
#define APP_ARTIFACT_PUBLISHED_MARK 1ull

// materialIndex rides the request so the texture publish knows its target
// material; mesh requests ignore it.
struct AppAssetRequest {
    ContentHash hash;
    U32 materialIndex;
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

static B32 app_mesh_artifact_build_(ArtifactBuildContext* buildCtx, ArtifactValue* outValue, U64* outBytes) {
    return app_asset_build_blob_(buildCtx, ASSET_MESH_MAGIC, sizeof(AssetMeshHeader), outValue, outBytes);
}

static B32 app_texture_artifact_build_(ArtifactBuildContext* buildCtx, ArtifactValue* outValue, U64* outBytes) {
    return app_asset_build_blob_(buildCtx, ASSET_TEXTURE_MAGIC, sizeof(AssetTextureHeader), outValue, outBytes);
}

static B32 app_mesh_artifact_publish_(ArtifactPublishContext* publishCtx, ArtifactValue buildValue,
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

    const AssetMeshHeader* header = (const AssetMeshHeader*)blob;
    U64 vertexBytes = (U64)header->vertexCount * sizeof(ShdWorldVertexRecord);
    U64 indexBytes = (U64)header->indexCount * sizeof(U32);
    if (header->version != ASSET_MESH_VERSION ||
        sizeof(AssetMeshHeader) + vertexBytes + indexBytes > blobSize) {
        arena_release(arena);
        return 0;
    }

    GfxBufferDesc vertexDesc = {};
    vertexDesc.name = "asset mesh vertices";
    vertexDesc.size = vertexBytes;
    vertexDesc.usageFlags = GfxBufferUsageFlags_Storage;
    vertexDesc.memoryKind = GfxMemoryKind_Upload;
    vertexDesc.initialData = blob + sizeof(AssetMeshHeader);
    GfxBuffer vertexBuffer = gfx_create_buffer(bridge->device, &vertexDesc);

    GfxBufferDesc indexDesc = {};
    indexDesc.name = "asset mesh indices";
    indexDesc.size = indexBytes;
    indexDesc.usageFlags = GfxBufferUsageFlags_Index;
    indexDesc.memoryKind = GfxMemoryKind_Upload;
    indexDesc.initialData = blob + sizeof(AssetMeshHeader) + vertexBytes;
    GfxBuffer indexBuffer = gfx_create_buffer(bridge->device, &indexDesc);
    GfxResourceId vertexBufferId = gfx_register_buffer(bridge->device, vertexBuffer);

    U32 vertexCount = header->vertexCount;
    U32 indexCount = header->indexCount;
    Vec3F32 center = app_world_vec3_(header->boundsCenter[0], header->boundsCenter[1], header->boundsCenter[2]);
    Vec3F32 extents = app_world_vec3_(header->boundsExtents[0], header->boundsExtents[1], header->boundsExtents[2]);
    arena_release(arena);

    if (vertexBuffer.generation == 0u || indexBuffer.generation == 0u || vertexBufferId.index == 0u) {
        gfx_destroy_buffer(bridge->device, vertexBuffer);
        gfx_destroy_buffer(bridge->device, indexBuffer);
        return 0;
    }

    AppWorldMeshHandle handle = app_world_register_mesh_(world, 0u, indexCount, 0u,
                                                         vertexBuffer, vertexBufferId, indexBuffer, 1,
                                                         center, extents);
    if (handle.generation == 0u) {
        gfx_destroy_buffer(bridge->device, vertexBuffer);
        gfx_destroy_buffer(bridge->device, indexBuffer);
        return 0;
    }

    outValue->u64[0] = handle.index;
    outValue->u64[1] = handle.generation;
    outValue->u64[2] = 0ull;
    outValue->u64[3] = APP_ARTIFACT_PUBLISHED_MARK;
    *outBytes = vertexBytes + indexBytes;
    LOG_INFO("asset", "Mesh published: {} vertices {} indices", vertexCount, indexCount);
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
        GfxTextureUploadRegion region = {};
        region.mip = mipIndex;
        region.layerCount = 1u;
        region.width = mipWidth;
        region.height = mipHeight;
        region.depth = 1u;
        U64 packedRow = (U64)((mipWidth + 3u) / 4u) * blockBytes;
        region.bytesPerRow = ((packedRow + GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT - 1u) /
                              GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT) *
                             GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT;
        region.rowsPerImage = (mipHeight + 3u) / 4u;
        if (!gfx_upload_texture(frame, texture, &region, blob + header->mipOffsets[mipIndex])) {
            gfx_destroy_texture(bridge->device, texture);
            arena_release(arena);
            return 0;
        }
        uploadedBytes += header->mipSizes[mipIndex];
        mipWidth = MAX(mipWidth / 2u, 1u);
        mipHeight = MAX(mipHeight / 2u, 1u);
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

    const AppAssetRequest* request = (const AppAssetRequest*)publishCtx->requestData;
    U32 materialIndex = (request && publishCtx->requestDataSize >= sizeof(AppAssetRequest))
        ? request->materialIndex : APP_WORLD_MATERIAL_MISSING;
    if (materialIndex > APP_WORLD_MATERIAL_MISSING && materialIndex < APP_WORLD_MAX_MATERIALS &&
        FLAGS_HAS(world->materialUsedMask, 1u << materialIndex)) {
        ShdWorldMaterialRecord* material = &world->materialRecords[materialIndex];
        material->textureIndex = textureId.index;
        material->samplerIndex = world->worldSamplerId.index;
        material->flags |= APP_WORLD_MATERIAL_FLAG_TEXTURED;
        world->materialsDirty = 1;
    } else {
        LOG_ERROR("asset", "Texture publish targets unallocated material {}; left unbound", materialIndex);
    }

    outValue->u64[0] = texture.index;
    outValue->u64[1] = texture.generation;
    outValue->u64[2] = textureId.index;
    outValue->u64[3] = APP_ARTIFACT_PUBLISHED_MARK;
    *outBytes = uploadedBytes;
    LOG_INFO("asset", "Texture published: {}x{} mips {}", publishedWidth, publishedHeight, publishedMips);
    return 1;
}

static void app_mesh_artifact_destroy_(void* typeUserData, ArtifactValue value) {
    AppWorldArtifactBridge* bridge = (AppWorldArtifactBridge*)typeUserData;
    if (!bridge || !bridge->device || !bridge->state) {
        return;
    }
    if (value.u64[3] != APP_ARTIFACT_PUBLISHED_MARK) {
        Arena* arena = (Arena*)value.u64[0];
        arena_release(arena);
        return;
    }
    AppWorldMeshHandle handle = {};
    handle.index = (U32)value.u64[0];
    handle.generation = (U32)value.u64[1];
    app_world_release_mesh_(bridge->device, &bridge->state->world, handle);
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

static B32 app_world_register_artifact_types_(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    AppCoreState* state = ctx->core;
    if (!state->resources.artifactCache || !ctx->host->gfxDevice) {
        return 1;
    }

    state->world.artifactBridge.device = ctx->host->gfxDevice;
    state->world.artifactBridge.state = state;

    ArtifactTypeDesc meshType = {};
    meshType.typeId = APP_ARTIFACT_TYPE_MESH;
    meshType.name = str8("mesh");
    meshType.buildProc = app_mesh_artifact_build_;
    meshType.publishProc = app_mesh_artifact_publish_;
    meshType.destroyProc = app_mesh_artifact_destroy_;
    meshType.userData = &state->world.artifactBridge;
    if (!artifact_register_type(state->resources.artifactCache, &meshType)) {
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

        if (world->assetMaterials[assetIndex] == APP_WORLD_MATERIAL_MISSING) {
            U32 materialIndex = 0u;
            if (!app_world_material_alloc(world, &materialIndex)) {
                settled = 0;
                continue;
            }
            ShdWorldMaterialRecord record = {};
            record.baseColor[0] = 1.0f;
            record.baseColor[1] = 1.0f;
            record.baseColor[2] = 1.0f;
            record.baseColor[3] = 1.0f;
            app_world_material_set(world, materialIndex, &record);
            world->assetMaterials[assetIndex] = materialIndex;
        }

        FileView meshView = file_view(state->resources.fileStream, world->assetMeshFiles[assetIndex]);
        if (meshView.status == FileStatus_Ready && !content_hash_is_zero(meshView.hash)) {
            AppAssetRequest request = {};
            request.hash = meshView.hash;
            request.materialIndex = world->assetMaterials[assetIndex];
            ArtifactResult result = artifact_get(state->resources.artifactCache, APP_ARTIFACT_TYPE_MESH,
                                                 app_artifact_key_from_label(asset->meshLabel),
                                                 meshView.generation, &request, sizeof(request),
                                                 ArtifactGetFlags_None, 0u);
            if (result.status == ArtifactStatus_Ready &&
                result.value.u64[3] == APP_ARTIFACT_PUBLISHED_MARK) {
                world->assetMeshes[assetIndex].index = (U32)result.value.u64[0];
                world->assetMeshes[assetIndex].generation = (U32)result.value.u64[1];
            } else {
                settled = 0;
            }
        } else {
            settled = 0;
        }

        FileView textureView = file_view(state->resources.fileStream, world->assetTextureFiles[assetIndex]);
        if (textureView.status == FileStatus_Ready && !content_hash_is_zero(textureView.hash)) {
            AppAssetRequest request = {};
            request.hash = textureView.hash;
            request.materialIndex = world->assetMaterials[assetIndex];
            ArtifactResult result = artifact_get(state->resources.artifactCache, APP_ARTIFACT_TYPE_TEXTURE,
                                                 app_artifact_key_from_label(asset->textureLabel),
                                                 textureView.generation, &request, sizeof(request),
                                                 ArtifactGetFlags_None, 0u);
            if (result.status != ArtifactStatus_Ready ||
                result.value.u64[3] != APP_ARTIFACT_PUBLISHED_MARK) {
                settled = 0;
            }
        } else {
            settled = 0;
        }
    }
    world->assetsSettled = settled;
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
    // triangles project CLOCKWISE under the row-vector view.proj, so front
    // is CW here or back-face culling would remove the surfaces instead.
    desc.raster.cullMode = transparent ? GfxCullMode_Back : GfxCullMode_None;
    desc.raster.frontFace = GfxFrontFace_CW;
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

static void app_world_frustum_planes_(const Mat4x4F32* m, F32* outPlanes) {
    // Gribb-Hartmann from column-major viewProj; rowI[j] = v[j][i].
    for (U32 planeIndex = 0u; planeIndex < 6u; ++planeIndex) {
        F32 plane[4];
        U32 row = planeIndex / 2u;
        B32 add = (planeIndex & 1u) == 0u;
        for (U32 component = 0u; component < 4u; ++component) {
            F32 row3 = m->v[component][3];
            F32 rowN = m->v[component][row];
            plane[component] = add ? (row3 + rowN) : (row3 - rowN);
        }
        if (planeIndex == 4u) {
            // Near plane for [0,1] clip depth is row2 itself.
            for (U32 component = 0u; component < 4u; ++component) {
                plane[component] = m->v[component][2];
            }
        }
        F32 lengthSq = plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2];
        F32 inverseLength = (lengthSq > 0.0f) ? (1.0f / SQRT_F32(lengthSq)) : 0.0f;
        for (U32 component = 0u; component < 4u; ++component) {
            outPlanes[planeIndex * 4u + component] = plane[component] * inverseLength;
        }
    }
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
    Mat4x4F32 view = mat4_look_at(eye, target, app_world_vec3_(0.0f, 1.0f, 0.0f));
    Mat4x4F32 projection = mat4_perspective(fovYRadians, aspect, zNear, zFar);
    Mat4x4F32 viewProj = view * projection;

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
    record.cellIndex = (U32)bin * APP_WORLD_MAX_MESHES + meshHandle.index;
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
            B32 inside = 1;
            for (U32 plane = 0u; plane < 6u; ++plane) {
                F32 distance = planes[plane * 4u + 0u] * record->boundsCenter[0] +
                               planes[plane * 4u + 1u] * record->boundsCenter[1] +
                               planes[plane * 4u + 2u] * record->boundsCenter[2] +
                               planes[plane * 4u + 3u];
                if (distance < -radius) {
                    inside = 0;
                    break;
                }
            }
            if (!inside) {
                continue;
            }
            depths[visible] = (record->boundsCenter[0] - eye[0]) * forward.x +
                              (record->boundsCenter[1] - eye[1]) * forward.y +
                              (record->boundsCenter[2] - eye[2]) * forward.z -
                              radius;
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
    for (U32 at = 0u; at < visible; ++at) {
        order[at] = at;
    }
    // Back-to-front: descending view depth; radix over flipped F32 bits,
    // two 16-bit passes, then reversed copy-out.
    for (U32 pass = 0u; pass < 2u; ++pass) {
        U32 shift = pass * 16u;
        U32 histogram[65536];
        MEMSET(histogram, 0, sizeof(histogram));
        for (U32 at = 0u; at < visible; ++at) {
            union { F32 f; U32 u; } bits;
            bits.f = depths[order[at]];
            U32 key = ((bits.u >> 31u) != 0u) ? ~bits.u : (bits.u | 0x80000000u);
            histogram[(key >> shift) & 0xFFFFu] += 1u;
        }
        U32 running = 0u;
        for (U32 bucket = 0u; bucket < 65536u; ++bucket) {
            U32 bucketCount = histogram[bucket];
            histogram[bucket] = running;
            running += bucketCount;
        }
        for (U32 at = 0u; at < visible; ++at) {
            union { F32 f; U32 u; } bits;
            bits.f = depths[order[at]];
            U32 key = ((bits.u >> 31u) != 0u) ? ~bits.u : (bits.u | 0x80000000u);
            scratch[histogram[(key >> shift) & 0xFFFFu]++] = order[at];
        }
        U32* swap = order;
        order = scratch;
        scratch = swap;
    }
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

    U32 maxDrawUses = 7u + APP_WORLD_MAX_MESHES + world->assetTextureCount;
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
    for (U32 meshSlot = 0u; meshSlot < APP_WORLD_MAX_MESHES; ++meshSlot) {
        if (!slot_map_is_occupied(&world->meshes, meshSlot)) {
            continue;
        }
        AppWorldMesh* slotMesh = (AppWorldMesh*)slot_map_item_at(&world->meshes, meshSlot);
        if (!slotMesh->ownsBuffers) {
            continue;
        }
        drawUses[drawUseCount].kind = GfxResourceUseKind_Buffer;
        drawUses[drawUseCount].accessFlags = GfxResourceAccessFlags_ShaderRead;
        drawUses[drawUseCount].shaderStages = GfxShaderStageFlags_Vertex;
        drawUses[drawUseCount].buffer = slotMesh->vertexBuffer;
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
        U32 bin = cell / APP_WORLD_MAX_MESHES;
        U32 meshSlot = cell % APP_WORLD_MAX_MESHES;
        if (bin == (U32)AppWorldBin_Transparent) {
            continue;
        }
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

