enum GfxResourceKind {
    GfxResourceKind_Invalid = 0,
    GfxResourceKind_Buffer,
    GfxResourceKind_Texture,
    GfxResourceKind_Sampler,
};

struct GfxResourceTableEntry {
    GfxResourceKind kind;
    U32 index;
    U32 generation;
    U32 nextFree;
};

struct GfxResourceTable {
    Arena* arena;
    GfxResourceTableEntry* entries;
    U32 count;
    U32 liveCount;
    U32 capacity;
    U32 freeHead;
};

struct GfxTextureUploadValidation {
    U32 blockWidth;
    U32 blockHeight;
    U32 bytesPerBlock;
    U32 mipWidth;
    U32 mipHeight;
    U64 rowBytes;
    U64 sourceBytesPerImage;
    B32 supported;
    B32 inBounds;
    B32 rowLayout;
    B32 sizeValid;
};

struct GfxTextureDescValidation {
    B32 storageKindValid;
    B32 transientAttachmentOnly;
    B32 transientSingleMip;
    B32 formatUsageValid;
};

FORCE_INLINE B32 gfx_buffer_handle_equal(GfxBuffer a, GfxBuffer b) {
    return (a.index == b.index && a.generation == b.generation) ? 1 : 0;
}

FORCE_INLINE B32 gfx_texture_handle_equal(GfxTexture a, GfxTexture b) {
    return (a.index == b.index && a.generation == b.generation) ? 1 : 0;
}

FORCE_INLINE B32 gfx_pipeline_handle_equal(GfxPipeline a, GfxPipeline b) {
    return (a.index == b.index && a.generation == b.generation) ? 1 : 0;
}

FORCE_INLINE B32 gfx_sampler_handle_equal(GfxSampler a, GfxSampler b) {
    return (a.index == b.index && a.generation == b.generation) ? 1 : 0;
}

struct GfxFormatInfo {
    U32 blockWidth;
    U32 blockHeight;
    U32 bytesPerBlock;
};

static GfxFormatInfo gfx_format_info(GfxFormat format) {
    GfxFormatInfo result = {};
    switch (format) {
        case GfxFormat_R8_UNorm: {
            result = {1u, 1u, 1u};
        } break;
        case GfxFormat_BGRA8_UNorm:
        case GfxFormat_RGBA8_UNorm:
        case GfxFormat_D32_Float: {
            result = {1u, 1u, 4u};
        } break;
        case GfxFormat_RGBA16_Float: {
            result = {1u, 1u, 8u};
        } break;
        case GfxFormat_BC1_RGBA_UNorm:
        case GfxFormat_BC4_R_UNorm: {
            result = {4u, 4u, 8u};
        } break;
        case GfxFormat_BC3_RGBA_UNorm:
        case GfxFormat_BC5_RG_UNorm:
        case GfxFormat_BC7_RGBA_UNorm: {
            result = {4u, 4u, 16u};
        } break;
        default: {
        } break;
    }
    return result;
}

static B32 gfx_format_is_block_compressed(GfxFormat format) {
    GfxFormatInfo info = gfx_format_info(format);
    return (info.blockWidth > 1u || info.blockHeight > 1u) ? 1 : 0;
}

static B32 gfx_texture_is_transient_storage_kind(GfxTextureStorageKind storageKind) {
    return (storageKind == GfxTextureStorageKind_Transient) ? 1 : 0;
}

static GfxTextureDescValidation gfx_validate_texture_desc_storage(const GfxTextureDesc* desc) {
    GfxTextureDescValidation result = {};
    if (!desc) {
        return result;
    }

    result.storageKindValid = (desc->storageKind == GfxTextureStorageKind_Device ||
                               desc->storageKind == GfxTextureStorageKind_Transient) ? 1 : 0;
    result.transientAttachmentOnly = 1;
    result.transientSingleMip = 1;
    result.formatUsageValid = 1;

    if (desc->storageKind == GfxTextureStorageKind_Transient) {
        U32 attachmentFlags = GfxTextureUsageFlags_ColorTarget | GfxTextureUsageFlags_DepthTarget;
        U32 forbiddenFlags = GfxTextureUsageFlags_Sampled |
                             GfxTextureUsageFlags_Storage |
                             GfxTextureUsageFlags_CopyDst;
        result.transientAttachmentOnly = ((desc->usageFlags & attachmentFlags) != 0u &&
                                          (desc->usageFlags & forbiddenFlags) == 0u) ? 1 : 0;
        result.transientSingleMip = (desc->mipCount == 0u || desc->mipCount == 1u) ? 1 : 0;
    }

    if (gfx_format_is_block_compressed(desc->format)) {
        U32 forbiddenBlockFlags = GfxTextureUsageFlags_ColorTarget |
                                  GfxTextureUsageFlags_DepthTarget |
                                  GfxTextureUsageFlags_Storage;
        result.formatUsageValid = ((desc->usageFlags & forbiddenBlockFlags) == 0u) ? 1 : 0;
    }

    return result;
}

static B32 gfx_validate_texture_upload_region(GfxFormat format,
                                              U32 textureWidth,
                                              U32 textureHeight,
                                              U32 textureMipCount,
                                              const GfxTextureUploadRegion* region,
                                              GfxTextureUploadValidation* outValidation) {
    if (outValidation) {
        *outValidation = {};
    }
    if (!region || !outValidation || textureWidth == 0u || textureHeight == 0u) {
        return 0;
    }

    GfxTextureUploadValidation result = {};
    GfxFormatInfo info = gfx_format_info(format);
    result.blockWidth = info.blockWidth;
    result.blockHeight = info.blockHeight;
    result.bytesPerBlock = info.bytesPerBlock;

    U32 mipCount = textureMipCount ? textureMipCount : 1u;
    result.mipWidth = textureWidth;
    result.mipHeight = textureHeight;
    U32 mipLimit = (region->mip < mipCount) ? region->mip : (mipCount - 1u);
    for (U32 mipIndex = 0u; mipIndex < mipLimit; ++mipIndex) {
        result.mipWidth = (result.mipWidth > 1u) ? (result.mipWidth >> 1u) : 1u;
        result.mipHeight = (result.mipHeight > 1u) ? (result.mipHeight >> 1u) : 1u;
    }

    result.supported = result.bytesPerBlock != 0u &&
                       region->layer == 0u &&
                       region->layerCount == 1u &&
                       region->z == 0u &&
                       region->depth == 1u;

    // Block-compressed regions must be block-aligned; partial blocks are only
    // legal against the mip edge.
    B32 blockAligned = 1;
    if (result.supported && (info.blockWidth > 1u || info.blockHeight > 1u)) {
        blockAligned = (region->x % info.blockWidth) == 0u &&
                       (region->y % info.blockHeight) == 0u &&
                       ((region->width % info.blockWidth) == 0u ||
                        (region->x + region->width) == result.mipWidth) &&
                       ((region->height % info.blockHeight) == 0u ||
                        (region->y + region->height) == result.mipHeight);
    }

    result.inBounds = region->mip < mipCount &&
                      region->width != 0u &&
                      region->height != 0u &&
                      region->x <= result.mipWidth &&
                      region->y <= result.mipHeight &&
                      region->width <= (result.mipWidth - region->x) &&
                      region->height <= (result.mipHeight - region->y) &&
                      blockAligned;

    // Row layout is measured in block rows: bytesPerRow covers one row of
    // blocks; rowsPerImage counts block rows (texel rows for linear formats).
    U64 blocksWide = (info.blockWidth != 0u) ?
                     (((U64)region->width + info.blockWidth - 1u) / info.blockWidth) : 0u;
    U64 blockRows = (info.blockHeight != 0u) ?
                    (((U64)region->height + info.blockHeight - 1u) / info.blockHeight) : 0u;
    if (result.bytesPerBlock != 0u &&
        blocksWide <= ((U64)-1) / (U64)result.bytesPerBlock) {
        result.rowBytes = blocksWide * (U64)result.bytesPerBlock;
    }

    result.rowLayout = result.rowBytes != 0u &&
                       region->bytesPerRow >= result.rowBytes &&
                       (region->bytesPerRow % GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT) == 0u &&
                       (U64)region->rowsPerImage >= blockRows;

    if (region->rowsPerImage != 0u &&
        region->bytesPerRow <= ((U64)-1) / (U64)region->rowsPerImage) {
        result.sourceBytesPerImage = region->bytesPerRow * (U64)region->rowsPerImage;
    }
    result.sizeValid = result.sourceBytesPerImage != 0u;

    *outValidation = result;
    return (result.supported && result.inBounds && result.rowLayout && result.sizeValid) ? 1 : 0;
}

static B32 gfx_resource_table_init(GfxResourceTable* table, Arena* arena, U32 capacity) {
    if (!table || !arena || capacity <= 1u) {
        return 0;
    }

    MEMSET(table, 0, sizeof(*table));
    table->arena = arena;
    table->entries = ARENA_PUSH_ARRAY(arena, GfxResourceTableEntry, capacity);
    if (!table->entries) {
        MEMSET(table, 0, sizeof(*table));
        return 0;
    }

    MEMSET(table->entries, 0, sizeof(GfxResourceTableEntry) * capacity);
    table->capacity = capacity;
    table->count = 1u;
    return 1;
}

static GfxResourceTableEntry* gfx_resource_table_get(GfxResourceTable* table, GfxResourceId resourceId) {
    if (!table ||
        !table->entries ||
        resourceId.index == 0u ||
        resourceId.index >= table->count ||
        resourceId.index >= table->capacity) {
        return 0;
    }

    GfxResourceTableEntry* entry = table->entries + resourceId.index;
    if (entry->kind == GfxResourceKind_Invalid) {
        return 0;
    }
    return entry;
}

static GfxResourceId gfx_resource_table_alloc(GfxResourceTable* table, GfxResourceKind kind, U32 index, U32 generation) {
    if (!table ||
        !table->entries ||
        kind == GfxResourceKind_Invalid ||
        generation == 0u) {
        return {};
    }

    U32 resourceIndex = 0u;
    if (table->freeHead != 0u) {
        resourceIndex = table->freeHead;
        GfxResourceTableEntry* freeEntry = table->entries + resourceIndex;
        table->freeHead = freeEntry->nextFree;
    } else {
        if (table->count >= table->capacity) {
            ASSERT_DEBUG(table->count < table->capacity);
            LOG_ERROR("gfx", "Resource table is full");
            return {};
        }
        resourceIndex = table->count;
        table->count += 1u;
    }

    GfxResourceTableEntry* entry = table->entries + resourceIndex;
    entry->kind = kind;
    entry->index = index;
    entry->generation = generation;
    entry->nextFree = 0u;
    table->liveCount += 1u;

    GfxResourceId result = {resourceIndex};
    return result;
}

static B32 gfx_resource_table_release(GfxResourceTable* table, GfxResourceId resourceId) {
    GfxResourceTableEntry* entry = gfx_resource_table_get(table, resourceId);
    if (!entry) {
        return 0;
    }

    MEMSET(entry, 0, sizeof(*entry));
    entry->nextFree = table->freeHead;
    table->freeHead = resourceId.index;
    if (table->liveCount > 0u) {
        table->liveCount -= 1u;
    }
    return 1;
}
