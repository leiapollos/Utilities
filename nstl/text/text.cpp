#define TEXT_DEFAULT_ATLAS_WIDTH 2048u
#define TEXT_DEFAULT_ATLAS_HEIGHT 2048u
#define TEXT_DEFAULT_MAX_FONTS 8u
#define TEXT_DEFAULT_MAX_GLYPHS 16384u
#define TEXT_SHAPE_CONTEXT_MEMORY_SIZE MB(4)
#define TEXT_ATLAS_UPLOAD_PITCH_ALIGNMENT 256u

#define TEXT_RUN_CACHE_SLOTS 2048u
#define TEXT_RUN_CACHE_SLOT_QUADS 48u
#define TEXT_RUN_CACHE_PROBE 8u
#define TEXT_RUN_CACHE_EXPIRE_FRAMES 120u
#define TEXT_RUN_CACHE_VALIDATE 0

struct TextFontSlot {
    U32 generation;
    U32 index;
    StringU8 debugName;
    U8* fontBytes;
    U64 fontByteSize;
    U32 faceIndex;
    U32 unitsPerEm;
    U32 currentPixelSize;
    FT_Face ftFace;
    kbts_font kbFont;
    B32 kbFontValid;
};

struct TextRunEntry {
    U64 key;
    U32 quadCount;
    U32 lastUsedFrame;
    F32 width;
    F32 height;
};

struct TextGlyphCacheEntry {
    B32 occupied;
    U32 fontIndex;
    U32 glyphId;
    U32 pixelSize;
    S32 bitmapLeft;
    S32 bitmapTop;
    U32 page;
    U32 atlasX;
    U32 atlasY;
    U32 width;
    U32 height;
    B32 hasBitmap;
    B32 atlasOverflow;
};

struct TextContext {
    Arena* arena;
    FT_Library ftLibrary;
    kbts_shape_context* shapeContext;
    void* shapeContextMemory;
    U32 shapeContextMemorySize;

    TextFontSlot* fonts;
    U32 maxFonts;
    U32 fontCount;
    U32 nextFontGeneration;

    TextGlyphCacheEntry* glyphs;
    U32 maxGlyphs;
    U32 glyphCount;

    U64* glyphSlotKeys;
    U32* glyphSlotValues;
    U32 glyphSlotCapacity;

    U8* atlasPixels;
    U32 atlasWidth;
    U32 atlasHeight;
    U32 atlasPitch;
    U32 shelfX;
    U32 shelfY;
    U32 shelfHeight;

    F32 whiteU;
    F32 whiteV;

    TextRunEntry* runEntries;
    TextQuad* runQuads;
    U32 runFrameIndex;
    U64 runHits;
    U64 runMisses;
    U64 runBypasses;
    U64 runEvictions;

    B32 loggedAtlasOverflow;
    B32 loggedGlyphOverflow;
    B32 loggedShapeMemory;
};

static U64 text_glyph_key(U32 fontIndex, U32 glyphId, U32 pixelSize) {
    return ((U64)fontIndex << 48u) | ((U64)pixelSize << 32u) | (U64)glyphId;
}

static TextDrawData text_draw_data_nil(void) {
    TextDrawData result = {};
    return result;
}

static void text_kb_arena_allocator(void* data, kbts_allocator_op* op) {
    Arena* arena = (Arena*)data;
    if (!arena || !op) {
        return;
    }

    if (op->Kind == KBTS_ALLOCATOR_OP_KIND_ALLOCATE) {
        op->Allocate.Pointer = arena_push(arena, op->Allocate.Size, 16u);
    } else if (op->Kind == KBTS_ALLOCATOR_OP_KIND_FREE) {
        op->Free.Pointer = 0;
    }
}

static TextFontSlot* text_font_slot_from_handle(TextContext* text, TextFont font) {
    if (!text ||
        !text->fonts ||
        font.index == 0u ||
        font.index > text->maxFonts) {
        return 0;
    }

    TextFontSlot* slot = text->fonts + font.index;
    if (slot->generation == 0u || slot->generation != font.generation) {
        return 0;
    }
    return slot;
}

static B32 text_font_set_pixel_size(TextFontSlot* font, U32 pixelSize) {
    if (font->currentPixelSize == pixelSize) {
        return 1;
    }
    if (FT_Set_Pixel_Sizes(font->ftFace, 0u, pixelSize) != 0) {
        font->currentPixelSize = 0u;
        return 0;
    }
    font->currentPixelSize = pixelSize;
    return 1;
}

static U64 text_run_hash(const U8* data, U64 size, U64 seed) {
    U64 hash = seed;
    for (U64 at = 0u; at < size; ++at) {
        hash ^= (U64)data[at];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

static U64 text_run_key(StringU8 bytes, U32 fontIndex, U32 fontGeneration, U32 pixelSize) {
    U64 key = text_run_hash(bytes.data, bytes.size, 0xcbf29ce484222325ull);
    U32 salt[3] = {fontIndex, fontGeneration, pixelSize};
    key = text_run_hash((const U8*)salt, sizeof(salt), key);
    return key ? key : 1ull;
}

static TextRunEntry* text_run_cache_find(TextContext* text, U64 key) {
    if (!text->runEntries) {
        return 0;
    }
    for (U32 probe = 0u; probe < TEXT_RUN_CACHE_PROBE; ++probe) {
        TextRunEntry* entry = text->runEntries + ((key + probe) & (TEXT_RUN_CACHE_SLOTS - 1u));
        if (entry->key == key) {
            return entry;
        }
        if (entry->key == 0ull) {
            return 0;
        }
    }
    return 0;
}

static void text_run_cache_insert(TextContext* text, U64 key, const TextQuad* quads, U32 quadCount,
                                  F32 width, F32 height) {
    if (!text->runEntries) {
        return;
    }
    TextRunEntry* victim = 0;
    for (U32 probe = 0u; probe < TEXT_RUN_CACHE_PROBE; ++probe) {
        TextRunEntry* entry = text->runEntries + ((key + probe) & (TEXT_RUN_CACHE_SLOTS - 1u));
        if (entry->key == key || entry->key == 0ull) {
            victim = entry;
            break;
        }
        if (!victim || entry->lastUsedFrame < victim->lastUsedFrame) {
            victim = entry;
        }
    }
    if (victim->key != 0ull && victim->key != key) {
        if (victim->lastUsedFrame + TEXT_RUN_CACHE_EXPIRE_FRAMES > text->runFrameIndex) {
            text->runBypasses += 1ull;
            return;
        }
        text->runEvictions += 1ull;
    }
    victim->key = key;
    victim->quadCount = quadCount;
    victim->lastUsedFrame = text->runFrameIndex;
    victim->width = width;
    victim->height = height;
    U64 slotIndex = (U64)(victim - text->runEntries);
    MEMCPY(text->runQuads + slotIndex * TEXT_RUN_CACHE_SLOT_QUADS, quads, quadCount * sizeof(TextQuad));
}

static TextRunView text_run_view_from_entry_(TextContext* text, TextRunEntry* entry, U64 key) {
    TextRunView view = {};
    U64 slotIndex = (U64)(entry - text->runEntries);
    view.quads = text->runQuads + slotIndex * TEXT_RUN_CACHE_SLOT_QUADS;
    view.quadCount = entry->quadCount;
    view.width = entry->width;
    view.height = entry->height;
    view.slot = (U32)slotIndex;
    view.key = key;
    return view;
}

static B32 text_atlas_alloc(TextContext* text, U32 width, U32 height, U32* outX, U32* outY) {
    ASSERT_ALWAYS(outX != 0);
    ASSERT_ALWAYS(outY != 0);

    *outX = 0u;
    *outY = 0u;
    if (!text || width == 0u || height == 0u) {
        return 0;
    }

    U32 paddedWidth = width + 2u;
    U32 paddedHeight = height + 2u;
    if (paddedWidth > text->atlasWidth || paddedHeight > text->atlasHeight) {
        return 0;
    }

    if (text->shelfX + paddedWidth > text->atlasWidth) {
        text->shelfX = 0u;
        text->shelfY += text->shelfHeight;
        text->shelfHeight = 0u;
    }

    if (text->shelfY + paddedHeight > text->atlasHeight) {
        return 0;
    }

    *outX = text->shelfX + 1u;
    *outY = text->shelfY + 1u;
    text->shelfX += paddedWidth;
    text->shelfHeight = MAX(text->shelfHeight, paddedHeight);
    return 1;
}

static TextGlyphCacheEntry* text_find_glyph(TextContext* text, U32 fontIndex, U32 glyphId, U32 pixelSize) {
    if (!text || !text->glyphs || !text->glyphSlotKeys) {
        return 0;
    }

    U64 key = text_glyph_key(fontIndex, glyphId, pixelSize);
    U32 mask = text->glyphSlotCapacity - 1u;
    U32 slot = (U32)((key * 0x9E3779B97F4A7C15ull) >> 32u) & mask;
    for (U32 probe = 0u; probe < text->glyphSlotCapacity; ++probe) {
        U64 slotKey = text->glyphSlotKeys[slot];
        if (slotKey == 0u) {
            return 0;
        }
        if (slotKey == key) {
            return text->glyphs + (text->glyphSlotValues[slot] - 1u);
        }
        slot = (slot + 1u) & mask;
    }
    return 0;
}

static void text_insert_glyph_slot(TextContext* text, U32 fontIndex, U32 glyphId, U32 pixelSize, U32 entryIndex) {
    U64 key = text_glyph_key(fontIndex, glyphId, pixelSize);
    U32 mask = text->glyphSlotCapacity - 1u;
    U32 slot = (U32)((key * 0x9E3779B97F4A7C15ull) >> 32u) & mask;
    for (U32 probe = 0u; probe < text->glyphSlotCapacity; ++probe) {
        if (text->glyphSlotKeys[slot] == 0u) {
            text->glyphSlotKeys[slot] = key;
            text->glyphSlotValues[slot] = entryIndex + 1u;
            return;
        }
        slot = (slot + 1u) & mask;
    }
}

static void text_copy_ft_bitmap_to_atlas(TextContext* text, U32 atlasX, U32 atlasY, const FT_Bitmap* bitmap) {
    ASSERT_ALWAYS(text != 0);
    ASSERT_ALWAYS(bitmap != 0);

    if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY ||
        bitmap->buffer == 0 ||
        bitmap->width == 0u ||
        bitmap->rows == 0u) {
        return;
    }

    for (U32 row = 0u; row < bitmap->rows; ++row) {
        const U8* src = 0;
        if (bitmap->pitch >= 0) {
            src = bitmap->buffer + (U64)row * (U64)bitmap->pitch;
        } else {
            src = bitmap->buffer + (U64)(bitmap->rows - 1u - row) * (U64)(-bitmap->pitch);
        }

        U8* dst = text->atlasPixels + (U64)(atlasY + row) * (U64)text->atlasPitch + atlasX;
        MEMCPY(dst, src, bitmap->width);
    }
}

static TextGlyphCacheEntry* text_get_or_cache_glyph(TextContext* text,
                                                    TextFontSlot* font,
                                                    U32 glyphId,
                                                    U32 pixelSize,
                                                    TextAtlasUpload* uploads,
                                                    U32 uploadCapacity,
                                                    U32* uploadCount,
                                                    B32* outAtlasOverflow) {
    ASSERT_ALWAYS(uploadCount != 0);
    ASSERT_ALWAYS(outAtlasOverflow != 0);

    TextGlyphCacheEntry* entry = text_find_glyph(text, font->index, glyphId, pixelSize);
    if (entry) {
        if (entry->atlasOverflow) {
            *outAtlasOverflow = 1;
        }
        return entry;
    }

    if (text->glyphCount >= text->maxGlyphs) {
        if (!text->loggedGlyphOverflow) {
            LOG_WARNING("text", "Glyph cache full; increase TextContextDesc.maxGlyphs");
            text->loggedGlyphOverflow = 1;
        }
        return 0;
    }

    U32 entryIndex = text->glyphCount;
    entry = text->glyphs + entryIndex;
    text->glyphCount += 1u;
    *entry = {};
    entry->occupied = 1;
    entry->fontIndex = font->index;
    entry->glyphId = glyphId;
    entry->pixelSize = pixelSize;
    text_insert_glyph_slot(text, font->index, glyphId, pixelSize, entryIndex);

    if (!text_font_set_pixel_size(font, pixelSize)) {
        return entry;
    }

    FT_Error loadError = FT_Load_Glyph(font->ftFace, glyphId, FT_LOAD_DEFAULT);
    if (loadError != 0) {
        return entry;
    }

    FT_GlyphSlot glyph = font->ftFace->glyph;
    FT_Error renderError = FT_Render_Glyph(glyph, FT_RENDER_MODE_NORMAL);
    if (renderError != 0) {
        return entry;
    }

    entry->bitmapLeft = glyph->bitmap_left;
    entry->bitmapTop = glyph->bitmap_top;
    entry->width = glyph->bitmap.width;
    entry->height = glyph->bitmap.rows;

    if (entry->width == 0u || entry->height == 0u) {
        return entry;
    }

    U32 atlasX = 0u;
    U32 atlasY = 0u;
    if (!text_atlas_alloc(text, entry->width, entry->height, &atlasX, &atlasY)) {
        entry->atlasOverflow = 1;
        *outAtlasOverflow = 1;
        if (!text->loggedAtlasOverflow) {
            LOG_WARNING("text", "Text atlas full; increase TextContextDesc atlas dimensions");
            text->loggedAtlasOverflow = 1;
        }
        return entry;
    }

    entry->atlasX = atlasX;
    entry->atlasY = atlasY;
    entry->hasBitmap = 1;
    text_copy_ft_bitmap_to_atlas(text, atlasX, atlasY, &glyph->bitmap);

    if (uploads && *uploadCount < uploadCapacity) {
        TextAtlasUpload* upload = uploads + *uploadCount;
        *upload = {};
        upload->page = 0u;
        upload->x = atlasX;
        upload->y = atlasY;
        upload->width = entry->width;
        upload->height = entry->height;
        upload->pixels = text->atlasPixels + (U64)atlasY * (U64)text->atlasPitch + atlasX;
        upload->pitch = text->atlasPitch;
        *uploadCount += 1u;
    }
    return entry;
}

static void text_shape_line(TextContext* text,
                            TextFontSlot* font,
                            const U8* line,
                            U32 lineSize,
                            F32 originX,
                            F32 baselineY,
                            U32 pixelSize,
                            U32 rgba8,
                            TextQuad* quads,
                            U32 quadCapacity,
                            U32* quadCount,
                            TextAtlasUpload* uploads,
                            U32 uploadCapacity,
                            U32* uploadCount,
                            B32* outAtlasOverflow,
                            F32* outAdvanceX) {
    ASSERT_ALWAYS(text != 0);
    ASSERT_ALWAYS(font != 0);
    ASSERT_ALWAYS(quadCount != 0);
    ASSERT_ALWAYS(uploadCount != 0);
    ASSERT_ALWAYS(outAtlasOverflow != 0);
    ASSERT_ALWAYS(outAdvanceX != 0);

    *outAdvanceX = 0.0f;
    kbts_font* pushedFont = kbts_ShapePushFont(text->shapeContext, &font->kbFont);
    if (!pushedFont) {
        if (!text->loggedShapeMemory) {
            LOG_WARNING("text", "kb_text_shape could not push font");
            text->loggedShapeMemory = 1;
        }
        return;
    }

    {
        PROF_SCOPE("kbts shape");
        kbts_ShapeBegin(text->shapeContext, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);
        kbts_ShapeUtf8(text->shapeContext,
                       (const char*)line,
                       (int)lineSize,
                       KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
        kbts_ShapeEnd(text->shapeContext);
    }

    if (kbts_ShapeError(text->shapeContext) != 0) {
        if (!text->loggedShapeMemory) {
            LOG_WARNING("text", "kb_text_shape failed while shaping");
            text->loggedShapeMemory = 1;
        }
        kbts_ShapePopFont(text->shapeContext);
        return;
    }

    F32 scale = (F32)pixelSize / (F32)font->unitsPerEm;
    F32 penX = 0.0f;
    F32 penY = 0.0f;

    kbts_run run = {};
    while (kbts_ShapeRun(text->shapeContext, &run)) {
        kbts_glyph* shapedGlyph = 0;
        while (kbts_GlyphIteratorNext(&run.Glyphs, &shapedGlyph)) {
            if (!shapedGlyph) {
                continue;
            }

            F32 glyphOffsetX = (F32)shapedGlyph->OffsetX * scale;
            F32 glyphOffsetY = (F32)shapedGlyph->OffsetY * scale;
            TextGlyphCacheEntry* glyph = text_get_or_cache_glyph(text,
                                                                  font,
                                                                  shapedGlyph->Id,
                                                                  pixelSize,
                                                                  uploads,
                                                                  uploadCapacity,
                                                                  uploadCount,
                                                                  outAtlasOverflow);
            if (glyph && glyph->hasBitmap && *quadCount < quadCapacity) {
                TextQuad* quad = quads + *quadCount;
                F32 minX = originX + penX + glyphOffsetX + (F32)glyph->bitmapLeft;
                F32 minY = baselineY - penY - glyphOffsetY - (F32)glyph->bitmapTop;
                F32 maxX = minX + (F32)glyph->width;
                F32 maxY = minY + (F32)glyph->height;

                quad->minX = minX;
                quad->minY = minY;
                quad->maxX = maxX;
                quad->maxY = maxY;
                quad->minU = (F32)glyph->atlasX / (F32)text->atlasWidth;
                quad->minV = (F32)glyph->atlasY / (F32)text->atlasHeight;
                quad->maxU = (F32)(glyph->atlasX + glyph->width) / (F32)text->atlasWidth;
                quad->maxV = (F32)(glyph->atlasY + glyph->height) / (F32)text->atlasHeight;
                quad->rgba8 = rgba8;
                *quadCount += 1u;
            }

            penX += (F32)shapedGlyph->AdvanceX * scale;
            penY += (F32)shapedGlyph->AdvanceY * scale;
            if (penX > *outAdvanceX) {
                *outAdvanceX = penX;
            }
        }
    }

    kbts_ShapePopFont(text->shapeContext);
}

B32 text_context_create(const TextContextDesc* desc, TextContext** outText) {
    if (outText != 0) {
        *outText = 0;
    }
    if (!desc || !desc->arena || !outText) {
        return 0;
    }

    U32 atlasWidth = desc->atlasWidth ? desc->atlasWidth : TEXT_DEFAULT_ATLAS_WIDTH;
    U32 atlasHeight = desc->atlasHeight ? desc->atlasHeight : TEXT_DEFAULT_ATLAS_HEIGHT;
    U32 maxFonts = desc->maxFonts ? desc->maxFonts : TEXT_DEFAULT_MAX_FONTS;
    U32 maxGlyphs = desc->maxGlyphs ? desc->maxGlyphs : TEXT_DEFAULT_MAX_GLYPHS;
    if (atlasWidth == 0u || atlasHeight == 0u || maxFonts == 0u || maxGlyphs == 0u) {
        return 0;
    }

    TextContext* text = ARENA_PUSH_STRUCT(desc->arena, TextContext);
    if (!text) {
        return 0;
    }
    *text = {};
    text->arena = desc->arena;
    text->atlasWidth = atlasWidth;
    text->atlasHeight = atlasHeight;
    text->atlasPitch = (U32)align_pow2(atlasWidth, TEXT_ATLAS_UPLOAD_PITCH_ALIGNMENT);
    text->maxFonts = maxFonts;
    text->maxGlyphs = maxGlyphs;
    text->nextFontGeneration = 1u;

    U32 glyphSlotCapacity = 1u;
    while (glyphSlotCapacity < maxGlyphs * 2u) {
        glyphSlotCapacity <<= 1u;
    }
    text->glyphSlotCapacity = glyphSlotCapacity;

    text->fonts = ARENA_PUSH_ARRAY(desc->arena, TextFontSlot, maxFonts + 1u);
    text->glyphs = ARENA_PUSH_ARRAY(desc->arena, TextGlyphCacheEntry, maxGlyphs);
    text->glyphSlotKeys = ARENA_PUSH_ARRAY(desc->arena, U64, glyphSlotCapacity);
    text->glyphSlotValues = ARENA_PUSH_ARRAY(desc->arena, U32, glyphSlotCapacity);
    text->atlasPixels = ARENA_PUSH_ARRAY(desc->arena, U8, (U64)text->atlasPitch * (U64)atlasHeight);
    text->runEntries = ARENA_PUSH_ARRAY(desc->arena, TextRunEntry, TEXT_RUN_CACHE_SLOTS);
    text->runQuads = ARENA_PUSH_ARRAY(desc->arena, TextQuad,
                                      (U64)TEXT_RUN_CACHE_SLOTS * TEXT_RUN_CACHE_SLOT_QUADS);
    text->shapeContextMemorySize = (U32)(kbts_SizeOfShapeContext() + TEXT_SHAPE_CONTEXT_MEMORY_SIZE);
    text->shapeContextMemory = arena_push(desc->arena, text->shapeContextMemorySize, 16u);
    if (!text->fonts || !text->glyphs || !text->glyphSlotKeys || !text->glyphSlotValues ||
        !text->atlasPixels || !text->runEntries || !text->runQuads || !text->shapeContextMemory) {
        return 0;
    }

    MEMSET(text->fonts, 0, sizeof(TextFontSlot) * (maxFonts + 1u));
    MEMSET(text->glyphs, 0, sizeof(TextGlyphCacheEntry) * maxGlyphs);
    MEMSET(text->glyphSlotKeys, 0, sizeof(U64) * glyphSlotCapacity);
    MEMSET(text->glyphSlotValues, 0, sizeof(U32) * glyphSlotCapacity);
    MEMSET(text->atlasPixels, 0, (U64)text->atlasPitch * (U64)atlasHeight);
    MEMSET(text->runEntries, 0, sizeof(TextRunEntry) * TEXT_RUN_CACHE_SLOTS);
    MEMSET(text->shapeContextMemory, 0, text->shapeContextMemorySize);

    U32 whiteX = 0u;
    U32 whiteY = 0u;
    if (text_atlas_alloc(text, 2u, 2u, &whiteX, &whiteY)) {
        for (U32 row = 0u; row < 2u; ++row) {
            U8* dst = text->atlasPixels + (U64)(whiteY + row) * (U64)text->atlasPitch + whiteX;
            dst[0] = 0xFFu;
            dst[1] = 0xFFu;
        }
        text->whiteU = ((F32)whiteX + 1.0f) / (F32)atlasWidth;
        text->whiteV = ((F32)whiteY + 1.0f) / (F32)atlasHeight;
    }

    text->shapeContext = kbts_PlaceShapeContextFixedMemory2(text->shapeContextMemory,
                                                            (int)text->shapeContextMemorySize,
                                                            KBTS_SHAPE_CONTEXT_FLAG_NONE);
    if (!text->shapeContext) {
        return 0;
    }

    FT_Error ftError = FT_Init_FreeType(&text->ftLibrary);
    if (ftError != 0) {
        LOG_ERROR("text", "Failed to initialize FreeType");
        return 0;
    }

    *outText = text;
    return 1;
}

void text_context_destroy(TextContext* text) {
    if (!text) {
        return;
    }

    for (U32 fontIndex = 1u; fontIndex <= text->fontCount && fontIndex <= text->maxFonts; ++fontIndex) {
        TextFontSlot* font = text->fonts + fontIndex;
        if (font->ftFace) {
            FT_Done_Face(font->ftFace);
            font->ftFace = 0;
        }
        if (font->kbFontValid) {
            kbts_FreeFont(&font->kbFont);
            font->kbFontValid = 0;
        }
    }

    if (text->shapeContext) {
        kbts_DestroyShapeContext(text->shapeContext);
        text->shapeContext = 0;
    }
    if (text->ftLibrary) {
        FT_Done_FreeType(text->ftLibrary);
        text->ftLibrary = 0;
    }
}

TextFont text_font_load_memory(TextContext* text, const TextFontDesc* desc) {
    TextFont result = {};
    if (!text ||
        !desc ||
        !desc->data ||
        desc->size == 0u ||
        desc->size > (U64)0x7fffffffu ||
        text->fontCount >= text->maxFonts) {
        return result;
    }

    U8* fontBytes = ARENA_PUSH_ARRAY(text->arena, U8, desc->size);
    if (!fontBytes) {
        return result;
    }
    MEMCPY(fontBytes, desc->data, desc->size);

    kbts_font kbFont = kbts_FontFromMemory(fontBytes,
                                           (int)desc->size,
                                           (int)desc->faceIndex,
                                           text_kb_arena_allocator,
                                           text->arena);
    if (!kbts_FontIsValid(&kbFont)) {
        LOG_ERROR("text", "kb_text_shape failed to parse font");
        return result;
    }

    FT_Face ftFace = 0;
    FT_Error ftError = FT_New_Memory_Face(text->ftLibrary,
                                          fontBytes,
                                          (FT_Long)desc->size,
                                          (FT_Long)desc->faceIndex,
                                          &ftFace);
    if (ftError != 0 || !ftFace) {
        kbts_FreeFont(&kbFont);
        LOG_ERROR("text", "FreeType failed to parse font");
        return result;
    }

    U32 slotIndex = text->fontCount + 1u;
    TextFontSlot* slot = text->fonts + slotIndex;
    *slot = {};
    slot->generation = text->nextFontGeneration++;
    if (slot->generation == 0u) {
        slot->generation = text->nextFontGeneration++;
    }
    slot->index = slotIndex;
    slot->debugName = str8_cpy(text->arena, desc->debugName);
    slot->fontBytes = fontBytes;
    slot->fontByteSize = desc->size;
    slot->faceIndex = desc->faceIndex;
    slot->unitsPerEm = ftFace->units_per_EM ? (U32)ftFace->units_per_EM : 1000u;
    slot->ftFace = ftFace;
    slot->kbFont = kbFont;
    slot->kbFontValid = 1;
    text->fontCount = slotIndex;

    result.index = slot->index;
    result.generation = slot->generation;
    return result;
}

static TextDrawData text_shape_run_origin(TextContext* text, Arena* frameArena, TextFontSlot* font,
                                          StringU8 textBytes, U32 pixelSize, U32 rgba8) {
    TextDrawData result = text_draw_data_nil();
    if (!text_font_set_pixel_size(font, pixelSize)) {
        return result;
    }

    F32 ascender = (F32)(font->ftFace->size->metrics.ascender >> 6);
    F32 lineHeight = (F32)(font->ftFace->size->metrics.height >> 6);
    if (lineHeight <= 0.0f) {
        lineHeight = (F32)pixelSize * 1.25f;
    }

    U32 capacity = (U32)textBytes.size + 1u;
    TextQuad* quads = ARENA_PUSH_ARRAY(frameArena, TextQuad, capacity);
    TextAtlasUpload* uploads = ARENA_PUSH_ARRAY(frameArena, TextAtlasUpload, capacity);
    if (!quads || !uploads) {
        return result;
    }

    U32 quadCount = 0u;
    U32 uploadCount = 0u;
    U32 lineCount = 0u;
    F32 maxLineWidth = 0.0f;

    U64 lineStart = 0u;
    for (U64 at = 0u; at <= textBytes.size; ++at) {
        B32 atEnd = (at == textBytes.size) ? 1 : 0;
        B32 atNewline = (!atEnd && textBytes.data[at] == (U8)'\n') ? 1 : 0;
        if (!atEnd && !atNewline) {
            continue;
        }

        U64 lineEnd = at;
        if (lineEnd > lineStart && textBytes.data[lineEnd - 1u] == (U8)'\r') {
            lineEnd -= 1u;
        }

        U64 lineSize64 = lineEnd - lineStart;
        F32 lineWidth = 0.0f;
        if (lineSize64 > 0u && lineSize64 <= (U64)0x7fffffffu) {
            text_shape_line(text,
                            font,
                            textBytes.data + lineStart,
                            (U32)lineSize64,
                            0.0f,
                            ascender + (F32)lineCount * lineHeight,
                            pixelSize,
                            rgba8,
                            quads,
                            capacity,
                            &quadCount,
                            uploads,
                            capacity,
                            &uploadCount,
                            &result.atlasOverflow,
                            &lineWidth);
        }

        maxLineWidth = MAX(maxLineWidth, lineWidth);
        lineCount += 1u;
        lineStart = at + 1u;
    }

    result.quads = quads;
    result.quadCount = quadCount;
    result.uploads = uploads;
    result.uploadCount = uploadCount;
    result.width = maxLineWidth;
    result.height = (F32)lineCount * lineHeight;
    return result;
}

TextRunView text_prepare_run(TextContext* text, Arena* frameArena, const TextRunDesc* desc) {
    PROF_FUNCTION();
    TextRunView view = {};
    view.slot = TEXT_RUN_NO_SLOT;
    if (!text || !frameArena || !desc || desc->text.size == 0u || desc->pixelSize <= 0.0f) {
        return view;
    }

    TextFontSlot* font = text_font_slot_from_handle(text, desc->font);
    if (!font || !font->ftFace || !font->kbFontValid || !desc->text.data) {
        return view;
    }

    U32 pixelSize = (U32)(desc->pixelSize + 0.5f);
    if (pixelSize == 0u || pixelSize > 512u || desc->text.size > (U64)0x7fffffffu) {
        return view;
    }

    U64 runKey = text_run_key(desc->text, font->index, font->generation, pixelSize);
    TextRunEntry* cached = text_run_cache_find(text, runKey);
    if (cached) {
        cached->lastUsedFrame = text->runFrameIndex;
        text->runHits += 1ull;
#if TEXT_RUN_CACHE_VALIDATE
        {
            TextDrawData fresh = text_shape_run_origin(text, frameArena, font, desc->text, pixelSize,
                                                       0xFFFFFFFFu);
            ASSERT_ALWAYS(fresh.quadCount == cached->quadCount);
            ASSERT_ALWAYS(fresh.width == cached->width);
            ASSERT_ALWAYS(fresh.height == cached->height);
            U64 slotIndex = (U64)(cached - text->runEntries);
            const TextQuad* src = text->runQuads + slotIndex * TEXT_RUN_CACHE_SLOT_QUADS;
            for (U32 at = 0u; at < fresh.quadCount; ++at) {
                ASSERT_ALWAYS(fresh.quads[at].minX == src[at].minX);
                ASSERT_ALWAYS(fresh.quads[at].minY == src[at].minY);
                ASSERT_ALWAYS(fresh.quads[at].maxX == src[at].maxX);
                ASSERT_ALWAYS(fresh.quads[at].maxY == src[at].maxY);
                ASSERT_ALWAYS(fresh.quads[at].minU == src[at].minU);
                ASSERT_ALWAYS(fresh.quads[at].minV == src[at].minV);
                ASSERT_ALWAYS(fresh.quads[at].maxU == src[at].maxU);
                ASSERT_ALWAYS(fresh.quads[at].maxV == src[at].maxV);
            }
        }
#endif
        return text_run_view_from_entry_(text, cached, runKey);
    }
    text->runMisses += 1ull;

    TextDrawData shaped = text_shape_run_origin(text, frameArena, font, desc->text, pixelSize, 0xFFFFFFFFu);
    view.uploads = shaped.uploads;
    view.uploadCount = shaped.uploadCount;
    if (shaped.quads && !shaped.atlasOverflow && shaped.quadCount <= TEXT_RUN_CACHE_SLOT_QUADS) {
        text_run_cache_insert(text, runKey, shaped.quads, shaped.quadCount, shaped.width, shaped.height);
        TextRunEntry* inserted = text_run_cache_find(text, runKey);
        if (inserted) {
            TextRunView stored = text_run_view_from_entry_(text, inserted, runKey);
            stored.uploads = shaped.uploads;
            stored.uploadCount = shaped.uploadCount;
            return stored;
        }
    }
    view.quads = shaped.quads;
    view.quadCount = shaped.quadCount;
    view.width = shaped.width;
    view.height = shaped.height;
    return view;
}

B32 text_run_resolve(TextContext* text, U32 slot, U64 key, TextRunView* outView) {
    if (!text || !outView || slot >= TEXT_RUN_CACHE_SLOTS || key == 0ull || !text->runEntries) {
        return 0;
    }
    TextRunEntry* entry = text->runEntries + slot;
    if (entry->key != key) {
        return 0;
    }
    entry->lastUsedFrame = text->runFrameIndex;
    text->runHits += 1ull;
    *outView = text_run_view_from_entry_(text, entry, key);
    return 1;
}

TextDrawData text_prepare_draw(TextContext* text, Arena* frameArena, const TextDrawDesc* desc) {
    TextDrawData result = text_draw_data_nil();
    if (!desc || !frameArena) {
        return result;
    }
    TextRunDesc runDesc = {};
    runDesc.font = desc->font;
    runDesc.text = desc->text;
    runDesc.pixelSize = desc->pixelSize;
    TextRunView view = text_prepare_run(text, frameArena, &runDesc);
    if (!view.quads && view.quadCount == 0u) {
        return result;
    }
    TextQuad* out = ARENA_PUSH_ARRAY(frameArena, TextQuad, view.quadCount ? view.quadCount : 1u);
    if (!out) {
        return result;
    }
    for (U32 at = 0u; at < view.quadCount; ++at) {
        TextQuad quad = view.quads[at];
        quad.minX += desc->x;
        quad.maxX += desc->x;
        quad.minY += desc->y;
        quad.maxY += desc->y;
        quad.rgba8 = desc->rgba8;
        out[at] = quad;
    }
    result.quads = out;
    result.quadCount = view.quadCount;
    result.uploads = view.uploads;
    result.uploadCount = view.uploadCount;
    result.width = view.width;
    result.height = view.height;
    return result;
}

void text_frame_advance(TextContext* text) {
    if (!text) {
        return;
    }
    text->runFrameIndex += 1u;
}

void text_white_uv(TextContext* text, F32* outU, F32* outV) {
    if (outU) {
        *outU = text ? text->whiteU : 0.0f;
    }
    if (outV) {
        *outV = text ? text->whiteV : 0.0f;
    }
}

TextAtlasUpload text_atlas_full_upload(TextContext* text) {
    TextAtlasUpload result = {};
    if (!text || !text->atlasPixels) {
        return result;
    }
    result.page = 0u;
    result.x = 0u;
    result.y = 0u;
    result.width = text->atlasWidth;
    result.height = text->atlasHeight;
    result.pixels = text->atlasPixels;
    result.pitch = text->atlasPitch;
    return result;
}

TextStats text_stats(TextContext* text) {
    TextStats result = {};
    if (!text) {
        return result;
    }
    result.fontCount = text->fontCount;
    result.maxFonts = text->maxFonts;
    result.glyphCount = text->glyphCount;
    result.maxGlyphs = text->maxGlyphs;
    result.atlasWidth = text->atlasWidth;
    result.atlasHeight = text->atlasHeight;
    result.shelfX = text->shelfX;
    result.shelfY = text->shelfY;
    result.shelfHeight = text->shelfHeight;
    result.runHits = text->runHits;
    result.runMisses = text->runMisses;
    result.runBypasses = text->runBypasses;
    result.runEvictions = text->runEvictions;
    return result;
}
