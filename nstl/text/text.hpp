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

UTILITIES_SHARED_API B32 text_context_create(const TextContextDesc* desc, TextContext** outText);
UTILITIES_SHARED_API void text_context_destroy(TextContext* text);

UTILITIES_SHARED_API TextFont text_font_load_memory(TextContext* text, const TextFontDesc* desc);
UTILITIES_SHARED_API TextDrawData text_prepare_draw(TextContext* text, Arena* frameArena, const TextDrawDesc* desc);
