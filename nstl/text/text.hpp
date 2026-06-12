#pragma once

struct TextContext;

struct TextFont {
    U32 index;
    U32 generation;
};

struct TextContextDesc {
    Arena* arena;
    U32 atlasWidth;
    U32 atlasHeight;
    U32 maxFonts;
    U32 maxGlyphs;
};

struct TextFontDesc {
    StringU8 debugName;
    const U8* data;
    U64 size;
    U32 faceIndex;
};

struct TextDrawDesc {
    TextFont font;
    StringU8 text;
    F32 x;
    F32 y;
    F32 pixelSize;
    U32 rgba8;
};

struct TextQuad {
    F32 minX;
    F32 minY;
    F32 maxX;
    F32 maxY;
    F32 minU;
    F32 minV;
    F32 maxU;
    F32 maxV;
    U32 rgba8;
};

struct TextAtlasUpload {
    U32 page;
    U32 x;
    U32 y;
    U32 width;
    U32 height;
    const U8* pixels;
    U32 pitch;
};

struct TextDrawData {
    TextQuad* quads;
    U32 quadCount;
    TextAtlasUpload* uploads;
    U32 uploadCount;
    F32 width;
    F32 height;
    B32 atlasOverflow;
};

#define TEXT_RUN_NO_SLOT 0xFFFFFFFFu

struct TextRunDesc {
    TextFont font;
    StringU8 text;
    F32 pixelSize;
};

struct TextRunView {
    const TextQuad* quads;
    U32 quadCount;
    F32 width;
    F32 height;
    U32 slot;
    U64 key;
    TextAtlasUpload* uploads;
    U32 uploadCount;
};

UTILITIES_SHARED_API B32 text_context_create(const TextContextDesc* desc, TextContext** outText);
UTILITIES_SHARED_API void text_context_destroy(TextContext* text);

UTILITIES_SHARED_API TextFont text_font_load_memory(TextContext* text, const TextFontDesc* desc);
UTILITIES_SHARED_API TextDrawData text_prepare_draw(TextContext* text, Arena* frameArena, const TextDrawDesc* desc);
UTILITIES_SHARED_API TextRunView text_prepare_run(TextContext* text, Arena* frameArena, const TextRunDesc* desc);
UTILITIES_SHARED_API B32 text_run_resolve(TextContext* text, U32 slot, U64 key, TextRunView* outView);
UTILITIES_SHARED_API void text_frame_advance(TextContext* text);

UTILITIES_SHARED_API void text_white_uv(TextContext* text, F32* outU, F32* outV);
UTILITIES_SHARED_API TextAtlasUpload text_atlas_full_upload(TextContext* text);

struct TextStats {
    U32 fontCount;
    U32 maxFonts;
    U32 glyphCount;
    U32 maxGlyphs;
    U32 atlasWidth;
    U32 atlasHeight;
    U32 shelfX;
    U32 shelfY;
    U32 shelfHeight;
    U64 runHits;
    U64 runMisses;
    U64 runBypasses;
    U64 runEvictions;
};

UTILITIES_SHARED_API TextStats text_stats(TextContext* text);
