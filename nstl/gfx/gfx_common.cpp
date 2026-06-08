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
    U32 bytesPerPixel;
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

static U32 gfx_format_bytes_per_pixel(GfxFormat format) {
    switch (format) {
        case GfxFormat_R8_UNorm: {
            return 1u;
        }
        case GfxFormat_BGRA8_UNorm:
        case GfxFormat_RGBA8_UNorm: {
            return 4u;
        }
        case GfxFormat_RGBA16_Float: {
            return 8u;
        }
        case GfxFormat_D32_Float: {
            return 4u;
        }
        default: {
            return 0u;
        }
    }
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

    if (desc->storageKind == GfxTextureStorageKind_Transient) {
        U32 attachmentFlags = GfxTextureUsageFlags_ColorTarget | GfxTextureUsageFlags_DepthTarget;
        U32 forbiddenFlags = GfxTextureUsageFlags_Sampled |
                             GfxTextureUsageFlags_Storage |
                             GfxTextureUsageFlags_CopyDst;
        result.transientAttachmentOnly = ((desc->usageFlags & attachmentFlags) != 0u &&
                                          (desc->usageFlags & forbiddenFlags) == 0u) ? 1 : 0;
        result.transientSingleMip = (desc->mipCount == 0u || desc->mipCount == 1u) ? 1 : 0;
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
    result.bytesPerPixel = gfx_format_bytes_per_pixel(format);

    U32 mipCount = textureMipCount ? textureMipCount : 1u;
    result.mipWidth = textureWidth;
    result.mipHeight = textureHeight;
    U32 mipLimit = (region->mip < mipCount) ? region->mip : (mipCount - 1u);
    for (U32 mipIndex = 0u; mipIndex < mipLimit; ++mipIndex) {
        result.mipWidth = (result.mipWidth > 1u) ? (result.mipWidth >> 1u) : 1u;
        result.mipHeight = (result.mipHeight > 1u) ? (result.mipHeight >> 1u) : 1u;
    }

    result.supported = result.bytesPerPixel != 0u &&
                       region->layer == 0u &&
                       region->layerCount == 1u &&
                       region->z == 0u &&
                       region->depth == 1u;

    result.inBounds = region->mip < mipCount &&
                      region->width != 0u &&
                      region->height != 0u &&
                      region->x <= result.mipWidth &&
                      region->y <= result.mipHeight &&
                      region->width <= (result.mipWidth - region->x) &&
                      region->height <= (result.mipHeight - region->y);

    if (result.bytesPerPixel != 0u &&
        (U64)region->width <= ((U64)-1) / (U64)result.bytesPerPixel) {
        result.rowBytes = (U64)region->width * (U64)result.bytesPerPixel;
    }

    result.rowLayout = result.rowBytes != 0u &&
                       region->bytesPerRow >= result.rowBytes &&
                       (region->bytesPerRow % GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT) == 0u &&
                       region->rowsPerImage >= region->height;

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
