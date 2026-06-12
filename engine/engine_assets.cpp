//
// Created by André Leite on 12/06/2026.
//
// The asset system: project desc tables -> file watches -> artifact
// cache (build/publish/destroy) -> the world mesh/material tables and
// the host audio buffer table. Cooked formats live in
// engine_assets.hpp; the world pass consumes what publishes here.
//

// ////////////////////////
// Cooked asset decode (mesh + texture artifacts)

// Artifact type ids are the cooked-format magics — one fact, one place.
#define ENG_ARTIFACT_TYPE_MODEL ASSET_MODEL_MAGIC
#define ENG_ARTIFACT_TYPE_TEXTURE ASSET_TEXTURE_MAGIC
#define ENG_ARTIFACT_TYPE_AUDIO ASSET_AUDIO_MAGIC

// ArtifactValue u64[4] slot meanings, per stage (the destroy procs key
// on u64[3] to tell the stages apart):
//   build (all types):  [0] blob Arena*  [1] blob U8*  [2] blob size  [3] 0
//   model published:    [0] model Arena* [1] EngWorldModelResources*
//                       [2] assetIndex   [3] mark
//   texture published:  [0] texture index [1] texture generation
//                       [2] GfxResourceId index [3] mark
//   audio published:    [0] buffer index|generation [1] sound slot
//                       [2] 0 [3] mark
#define ENG_ARTIFACT_PUBLISHED_MARK 1ull

// assetIndex names the project asset slot; textureLocal is the
// model-local texture index for texture requests (model requests
// ignore it).
struct EngAssetRequest {
    ContentHash hash;
    U32 assetIndex;
    U32 textureLocal;
};

static B32 eng_asset_build_blob_(ArtifactBuildContext* buildCtx, U32 expectedMagic, U64 minimumSize,
                                 ArtifactValue* outValue, U64* outBytes) {
    const EngAssetRequest* request = (const EngAssetRequest*)buildCtx->requestData;
    if (!request || buildCtx->requestDataSize < sizeof(EngAssetRequest)) {
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

static B32 eng_model_artifact_build_(ArtifactBuildContext* buildCtx, ArtifactValue* outValue, U64* outBytes) {
    return eng_asset_build_blob_(buildCtx, ASSET_MODEL_MAGIC, sizeof(AssetModelHeader), outValue, outBytes);
}

static B32 eng_texture_artifact_build_(ArtifactBuildContext* buildCtx, ArtifactValue* outValue, U64* outBytes) {
    return eng_asset_build_blob_(buildCtx, ASSET_TEXTURE_MAGIC, sizeof(AssetTextureHeader), outValue, outBytes);
}

static B32 eng_model_artifact_publish_(ArtifactPublishContext* publishCtx, ArtifactValue buildValue,
                                       ArtifactValue* outValue, U64* outBytes) {
    EngAssetBridge* bridge = (EngAssetBridge*)publishCtx->typeUserData;
    Arena* arena = (Arena*)buildValue.u64[0];
    const U8* blob = (const U8*)buildValue.u64[1];
    U64 blobSize = buildValue.u64[2];
    const EngAssetRequest* request = (const EngAssetRequest*)publishCtx->requestData;
    if (!bridge || !bridge->device || !bridge->state || !blob ||
        !request || publishCtx->requestDataSize < sizeof(EngAssetRequest) ||
        request->assetIndex >= ENG_WORLD_MAX_MODELS) {
        arena_release(arena);
        return 0;
    }
    EngWorldState* world = &bridge->state->world;

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
        header->textureCount > ENG_WORLD_MODEL_MAX_TEXTURES ||
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
    EngWorldModelResources* resources = modelArena ? ARENA_PUSH_STRUCT(modelArena, EngWorldModelResources) : 0;
    EngWorldMeshHandle* sectionMeshes = resources
        ? ARENA_PUSH_ARRAY(modelArena, EngWorldMeshHandle, header->sectionCount) : 0;
    EngWorldModelMaterialRef* materialRefs = resources
        ? ARENA_PUSH_ARRAY(modelArena, EngWorldModelMaterialRef, header->materialCount) : 0;
    EngWorldModelInstanceRef* instanceRefs = resources
        ? ARENA_PUSH_ARRAY(modelArena, EngWorldModelInstanceRef, header->instanceCount) : 0;
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
        Vec3F32 center = vec3_make(source->boundsCenter[0], source->boundsCenter[1], source->boundsCenter[2]);
        Vec3F32 extents = vec3_make(source->boundsExtents[0], source->boundsExtents[1], source->boundsExtents[2]);
        // baseVertex 0: indices were rebased to pool-absolute above.
        sectionMeshes[section] = eng_world_register_mesh_(world, source->firstIndex, source->indexCount,
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
        if (!eng_world_material_alloc(world, &materialRefs[material].worldSlot)) {
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
        eng_world_material_set(world, materialRefs[material].worldSlot, &record);
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
            eng_world_release_mesh_(bridge->device, world, sectionMeshes[section]);
        }
        for (U32 material = 0u; material < allocatedMaterials; ++material) {
            eng_world_material_release(world, materialRefs[material].worldSlot);
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
                 textureLocal < resources->textureCount && textureLocal < ENG_WORLD_MODEL_MAX_TEXTURES;
                 ++textureLocal) {
                StringU8 path = str8_concat(scratch.arena, exeDir, str8("/../"));
                path = str8_concat(scratch.arena, path,
                                   asset_model_texture_path(scratch.arena,
                                                            str8(eng_project_()->models[request->assetIndex].path),
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
    outValue->u64[3] = ENG_ARTIFACT_PUBLISHED_MARK;
    *outBytes = vertexBytes + indexBytes;
    LOG_INFO("asset", "Model published: {} sections {} instances {} materials {} textures",
             resources->sectionCount, resources->instanceCount, resources->materialCount,
             resources->textureCount);
    return 1;
}

static B32 eng_texture_artifact_publish_(ArtifactPublishContext* publishCtx, ArtifactValue buildValue,
                                         ArtifactValue* outValue, U64* outBytes) {
    EngAssetBridge* bridge = (EngAssetBridge*)publishCtx->typeUserData;
    Arena* arena = (Arena*)buildValue.u64[0];
    const U8* blob = (const U8*)buildValue.u64[1];
    U64 blobSize = buildValue.u64[2];
    if (!bridge || !bridge->device || !bridge->state || !blob) {
        arena_release(arena);
        return 0;
    }
    EngWorldState* world = &bridge->state->world;

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

    if (world->assetTextureCount < ENG_WORLD_MAX_ASSET_TEXTURES) {
        world->assetTextures[world->assetTextureCount] = texture;
        world->assetTextureCount += 1u;
    }

    // Bind every model material that references this model-local texture.
    const EngAssetRequest* request = (const EngAssetRequest*)publishCtx->requestData;
    U32 boundCount = 0u;
    if (request && publishCtx->requestDataSize >= sizeof(EngAssetRequest) &&
        request->assetIndex < ENG_WORLD_MAX_MODELS) {
        EngWorldModelResources* resources = world->models[request->assetIndex];
        for (U32 at = 0u; resources && at < resources->materialCount; ++at) {
            const EngWorldModelMaterialRef* ref = resources->materials + at;
            if (ref->textureLocal != request->textureLocal ||
                ref->worldSlot >= ENG_WORLD_MAX_MATERIALS ||
                !FLAGS_HAS(world->materialUsedMask, 1ull << ref->worldSlot)) {
                continue;
            }
            ShdWorldMaterialRecord* material = &world->materialRecords[ref->worldSlot];
            material->textureIndex = textureId.index;
            material->samplerIndex = world->worldSamplerId.index;
            material->flags |= ENG_WORLD_MATERIAL_FLAG_TEXTURED;
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
    outValue->u64[3] = ENG_ARTIFACT_PUBLISHED_MARK;
    *outBytes = uploadedBytes;
    LOG_INFO("asset", "Texture published: {}x{} mips {}", publishedWidth, publishedHeight, publishedMips);
    return 1;
}

static void eng_model_artifact_destroy_(void* typeUserData, ArtifactValue value) {
    EngAssetBridge* bridge = (EngAssetBridge*)typeUserData;
    if (!bridge || !bridge->device || !bridge->state) {
        return;
    }
    if (value.u64[3] != ENG_ARTIFACT_PUBLISHED_MARK) {
        Arena* arena = (Arena*)value.u64[0];
        arena_release(arena);
        return;
    }
    EngWorldState* world = &bridge->state->world;
    Arena* modelArena = (Arena*)value.u64[0];
    EngWorldModelResources* resources = (EngWorldModelResources*)value.u64[1];
    U32 assetIndex = (U32)value.u64[2];
    for (U32 section = 0u; section < resources->sectionCount; ++section) {
        eng_world_release_mesh_(bridge->device, world, resources->sections[section]);
    }
    for (U32 material = 0u; material < resources->materialCount; ++material) {
        eng_world_material_release(world, resources->materials[material].worldSlot);
    }
    gfx_destroy_buffer(bridge->device, resources->vertexBuffer);
    gfx_destroy_buffer(bridge->device, resources->indexBuffer);
    // A supersede has already pointed models[] at the replacement; only a
    // terminal destroy (eviction, shutdown) still owns the slot.
    if (assetIndex < ENG_WORLD_MAX_MODELS && world->models[assetIndex] == resources) {
        world->models[assetIndex] = 0;
    }
    arena_release(modelArena);
    LOG_INFO("asset", "Model destroyed (asset {})", assetIndex);
}

static void eng_texture_artifact_destroy_(void* typeUserData, ArtifactValue value) {
    EngAssetBridge* bridge = (EngAssetBridge*)typeUserData;
    if (!bridge || !bridge->device) {
        return;
    }
    if (value.u64[3] != ENG_ARTIFACT_PUBLISHED_MARK) {
        Arena* arena = (Arena*)value.u64[0];
        arena_release(arena);
        return;
    }
    GfxTexture texture = {};
    texture.index = (U32)value.u64[0];
    texture.generation = (U32)value.u64[1];
    U32 textureId = (U32)value.u64[2];
    EngWorldState* world = &bridge->state->world;
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
    for (U32 materialIndex = 0u; materialIndex < ENG_WORLD_MAX_MATERIALS; ++materialIndex) {
        ShdWorldMaterialRecord* material = &world->materialRecords[materialIndex];
        if (FLAGS_HAS(material->flags, ENG_WORLD_MATERIAL_FLAG_TEXTURED) &&
            material->textureIndex == textureId) {
            material->flags &= ~ENG_WORLD_MATERIAL_FLAG_TEXTURED;
            material->textureIndex = 0u;
            material->samplerIndex = 0u;
            world->materialsDirty = 1;
            LOG_INFO("asset", "Texture destroyed; material {} unbound", materialIndex);
        }
    }
    gfx_destroy_texture(bridge->device, texture);
}

static B32 eng_audio_artifact_build_(ArtifactBuildContext* buildCtx, ArtifactValue* outValue, U64* outBytes) {
    return eng_asset_build_blob_(buildCtx, ASSET_AUDIO_MAGIC, sizeof(AssetAudioHeader), outValue, outBytes);
}

static B32 eng_audio_artifact_publish_(ArtifactPublishContext* publishCtx, ArtifactValue buildValue,
                                       ArtifactValue* outValue, U64* outBytes) {
    EngAssetBridge* bridge = (EngAssetBridge*)publishCtx->typeUserData;
    Arena* arena = (Arena*)buildValue.u64[0];
    const U8* blob = (const U8*)buildValue.u64[1];
    U64 blobSize = buildValue.u64[2];
    const EngAssetRequest* request = (const EngAssetRequest*)publishCtx->requestData;
    if (!bridge || !bridge->audioSystem || !bridge->state || !blob ||
        !request || publishCtx->requestDataSize < sizeof(EngAssetRequest) ||
        request->assetIndex >= ENG_AUDIO_MAX_SOUNDS) {
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

    EngAudio* audio = &bridge->state->audio;
    audio->sounds[request->assetIndex] = handle;
    // A (re)publish bumps the slot generation; what to do about it (e.g.
    // restarting a looping bed whose voice the buffer bump killed) is
    // project policy, watched from its pre_frame.
    audio->soundGenerations[request->assetIndex] += 1u;

    // The blob (and header) die here; log from locals only (the U6 class).
    U32 frameCount = header->frameCount;
    arena_release(arena);

    outValue->u64[0] = ((U64)handle.index << 32u) | (U64)handle.generation;
    outValue->u64[1] = request->assetIndex;
    outValue->u64[2] = 0ull;
    outValue->u64[3] = ENG_ARTIFACT_PUBLISHED_MARK;
    *outBytes = sampleBytes;
    LOG_INFO("asset", "Sound published: sound {} frames {} (slot {} gen {})",
             request->assetIndex, frameCount, handle.index, handle.generation);
    return 1;
}

static void eng_audio_artifact_destroy_(void* typeUserData, ArtifactValue value) {
    EngAssetBridge* bridge = (EngAssetBridge*)typeUserData;
    if (!bridge || !bridge->state) {
        return;
    }
    if (value.u64[3] != ENG_ARTIFACT_PUBLISHED_MARK) {
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
    EngAudio* audio = &bridge->state->audio;
    if (soundIndex < ENG_AUDIO_MAX_SOUNDS &&
        audio->sounds[soundIndex].index == handle.index &&
        audio->sounds[soundIndex].generation == handle.generation) {
        AudioBufferHandle zero = {};
        audio->sounds[soundIndex] = zero;
    }
    audio_buffer_destroy(bridge->audioSystem, handle);
    LOG_INFO("asset", "Sound destroyed (sound {} slot {} gen {})", soundIndex, handle.index, handle.generation);
}

static B32 eng_assets_register_types_(EngContext* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    EngState* state = ctx->engine;
    if (!state->resources.artifactCache || !ctx->host->gfxDevice) {
        return 1;
    }

    U32 caps = eng_project_()->capabilities;
    state->assetBridge.device = ctx->host->gfxDevice;
    state->assetBridge.audioSystem = ctx->host->audioSystem;
    state->assetBridge.state = state;

    if (caps & ENG_CAP_WORLD3D) {
        ArtifactTypeDesc modelType = {};
        modelType.typeId = ENG_ARTIFACT_TYPE_MODEL;
        modelType.name = str8("model");
        modelType.buildProc = eng_model_artifact_build_;
        modelType.publishProc = eng_model_artifact_publish_;
        modelType.destroyProc = eng_model_artifact_destroy_;
        modelType.userData = &state->assetBridge;
        if (!artifact_register_type(state->resources.artifactCache, &modelType)) {
            return 0;
        }

        ArtifactTypeDesc textureType = {};
        textureType.typeId = ENG_ARTIFACT_TYPE_TEXTURE;
        textureType.name = str8("texture");
        textureType.buildProc = eng_texture_artifact_build_;
        textureType.publishProc = eng_texture_artifact_publish_;
        textureType.destroyProc = eng_texture_artifact_destroy_;
        textureType.userData = &state->assetBridge;
        if (!artifact_register_type(state->resources.artifactCache, &textureType)) {
            return 0;
        }
    }

    if (caps & ENG_CAP_AUDIO) {
        ArtifactTypeDesc audioType = {};
        audioType.typeId = ENG_ARTIFACT_TYPE_AUDIO;
        audioType.name = str8("audio");
        audioType.buildProc = eng_audio_artifact_build_;
        audioType.publishProc = eng_audio_artifact_publish_;
        audioType.destroyProc = eng_audio_artifact_destroy_;
        audioType.userData = &state->assetBridge;
        if (!artifact_register_type(state->resources.artifactCache, &audioType)) {
            return 0;
        }
    }
    return 1;
}

// Resolves every project asset through the artifact cache and records
// whether the set is settled (all artifacts Ready for the current file
// generations) in world->assetsSettled, which gates the per-frame
// resource polls.
static void eng_assets_try_load_models_(EngContext* ctx) {
    EngState* state = ctx->engine;
    EngWorldState* world = &state->world;

    if (!state->resources.fileStream || !state->resources.artifactCache || !world->gpuResourcesCreated) {
        return;
    }

    const EngProject* project = eng_project_();
    B32 settled = 1;
    for (U32 assetIndex = 0u; assetIndex < project->modelCount; ++assetIndex) {
        const EngModelDesc* asset = project->models + assetIndex;

        FileView modelView = file_view(state->resources.fileStream, world->assetModelFiles[assetIndex]);
        if (modelView.status == FileStatus_Ready && !content_hash_is_zero(modelView.hash)) {
            EngAssetRequest request = {};
            request.hash = modelView.hash;
            request.assetIndex = assetIndex;
            ArtifactResult result = artifact_get(state->resources.artifactCache, ENG_ARTIFACT_TYPE_MODEL,
                                                 eng_artifact_key_from_label(asset->label),
                                                 modelView.generation, &request, sizeof(request),
                                                 ArtifactGetFlags_None, 0u);
            if (result.status != ArtifactStatus_Ready ||
                result.value.u64[3] != ENG_ARTIFACT_PUBLISHED_MARK) {
                settled = 0;
            }
        } else {
            settled = 0;
        }

        // The published model declares how many sibling textures exist.
        const EngWorldModelResources* resources = world->models[assetIndex];
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
            EngAssetRequest request = {};
            request.hash = textureView.hash;
            request.assetIndex = assetIndex;
            request.textureLocal = textureLocal;
            Temp scratch = get_scratch(0, 0);
            if (!scratch.arena) {
                settled = 0;
                continue;
            }
            StringU8 label = str8_fmt(scratch.arena, "{}#tex{}", str8(asset->label), textureLocal);
            ArtifactResult result = artifact_get(state->resources.artifactCache, ENG_ARTIFACT_TYPE_TEXTURE,
                                                 artifact_key_from_bytes(label.data, label.size),
                                                 textureView.generation, &request, sizeof(request),
                                                 ArtifactGetFlags_None, 0u);
            temp_end(&scratch);
            if (result.status != ArtifactStatus_Ready ||
                result.value.u64[3] != ENG_ARTIFACT_PUBLISHED_MARK) {
                settled = 0;
            }
        }
    }
    world->assetsSettled = settled;
}

// Same shape as the model poll: resolve every sound through the artifact
// cache and record whether the set is settled for the current generations.
static void eng_assets_try_load_sounds_(EngContext* ctx) {
    EngState* state = ctx->engine;
    EngAudio* audio = &state->audio;

    if (!state->resources.fileStream || !state->resources.artifactCache ||
        !ctx->host->audioSystem) {
        return;
    }

    const EngProject* project = eng_project_();
    B32 settled = 1;
    for (U32 soundIndex = 0u; soundIndex < project->soundCount; ++soundIndex) {
        FileView soundView = file_view(state->resources.fileStream, audio->soundFiles[soundIndex]);
        if (soundView.status != FileStatus_Ready || content_hash_is_zero(soundView.hash)) {
            settled = 0;
            continue;
        }
        EngAssetRequest request = {};
        request.hash = soundView.hash;
        request.assetIndex = soundIndex;
        ArtifactResult result = artifact_get(state->resources.artifactCache, ENG_ARTIFACT_TYPE_AUDIO,
                                             eng_artifact_key_from_label(project->sounds[soundIndex].label),
                                             soundView.generation, &request, sizeof(request),
                                             ArtifactGetFlags_None, 0u);
        if (result.status != ArtifactStatus_Ready ||
            result.value.u64[3] != ENG_ARTIFACT_PUBLISHED_MARK) {
            settled = 0;
        }
    }
    audio->settled = settled;
}


static void eng_assets_watch_files_(EngContext* ctx) {
    EngState* state = ctx->engine;
    if (!state->resources.fileStream) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    const EngProject* project = eng_project_();
    U32 caps = project->capabilities;
    ASSERT_ALWAYS(project->modelCount <= ENG_WORLD_MAX_MODELS);
    ASSERT_ALWAYS(project->soundCount <= ENG_AUDIO_MAX_SOUNDS);
    // Declaring assets without the matching capability is a project bug.
    ASSERT_ALWAYS((caps & ENG_CAP_WORLD3D) || project->modelCount == 0u);
    ASSERT_ALWAYS((caps & ENG_CAP_AUDIO) || project->soundCount == 0u);

    StringU8 exeDir = OS_get_executable_directory(scratch.arena);
    if (caps & ENG_CAP_WORLD3D) {
        for (U32 assetIndex = 0u; assetIndex < project->modelCount; ++assetIndex) {
            const EngModelDesc* asset = project->models + assetIndex;
            StringU8 modelPath = str8_concat(scratch.arena, exeDir, str8("/../"));
            modelPath = str8_concat(scratch.arena, modelPath, str8(asset->path));
            state->world.assetModelFiles[assetIndex] = file_watch(state->resources.fileStream, modelPath, 0u);
            // Texture watches are model-driven: the publish proc watches exactly
            // the textureCount the cooked model declares (a boot-time guess would
            // leave permanently-failing watches screaming in the stats).
        }
    }

    if (caps & ENG_CAP_AUDIO) {
        for (U32 soundIndex = 0u; soundIndex < project->soundCount; ++soundIndex) {
            StringU8 soundPath = str8_concat(scratch.arena, exeDir, str8("/../"));
            soundPath = str8_concat(scratch.arena, soundPath, str8(project->sounds[soundIndex].path));
            state->audio.soundFiles[soundIndex] = file_watch(state->resources.fileStream, soundPath, 0u);
        }
    }
}
