# Text Rendering V0 Research And Plan

Date: 2026-06-08

This replaces the larger text-rendering plan with a smaller first step: get text on screen with a clean path that can grow later.

## Decision

Use `FreeType` + `kb_text_shape.h` for V0.

No runtime fallback chain. No DirectWrite/CoreText/Android native backend in V0. No HarfBuzz backend in V0. Native APIs and HarfBuzz remain later compile-time backend candidates if `kb_text_shape.h` fails real correctness tests.

V0 goal:

- Draw static/debug text from one bundled font.
- Use one grayscale glyph atlas.
- Keep the API small and boring.
- Keep renderer/RHI ownership clean.

Out of scope for V0:

- UI widgets.
- Editable text.
- Selection/cursor/IME.
- Paragraph layout and wrapping.
- Font fallback.
- Color emoji.
- SDF/MSDF.
- Multi-atlas eviction.
- Shaped-run cache.

This follows `documentation/SEB_AALTONEN_2026_FINDINGS.md`: start with concrete data, keep the API flat, avoid hidden allocation/remapping in draw paths, use GPU-visible buffers/resource IDs directly, and do not build a second renderer around `nstl/gfx`.

## Ownership

Keep the split blunt:

- `TextContext` owns CPU-side text state:
  - copied font bytes,
  - FreeType and `kb_text_shape.h` backend state,
  - fixed glyph cache,
  - one CPU `R8` atlas image,
  - atlas dirty upload records generated while preparing text.
- Renderer/app owns GPU state:
  - atlas `GfxTexture`,
  - sampler,
  - text shader/pipeline,
  - quad buffers,
  - static quad index buffer,
  - root data,
  - render pass placement,
  - `gfx_upload_texture` and draw packets.
- `nstl/gfx` stays text-unaware:
  - add `GfxFormat_R8_UNorm`,
  - support R8 texture creation/upload/sampling in Metal and Vulkan,
  - no `gfx_draw_text` API.

`TextDrawData` is CPU-side output only. It is not a GPU ABI. The renderer may copy or convert it into whatever quad record the shader wants.

## V0 Pipeline

```text
UTF-8 -> shape -> rasterize missing glyphs -> pack atlas -> CPU quads -> renderer draw
```

V0 behavior:

- `text_prepare_draw` shapes one string with one `TextFont`.
- `\n` is a hard line break: reset X to the draw origin and advance Y by the font line height.
- No word wrapping.
- No paragraph object.
- No shaped-run cache. Shape each submitted string for now.
- Glyph bitmaps are cached after first rasterization.
- Atlas is one fixed-size page. No eviction and no repacking.
- Atlas overflow logs once and skips glyphs that cannot be packed.
- Nil `TextFont {0, 0}` returns zero quads.
- Invalid font load returns nil font.
- Invalid UTF-8 returns zero quads and logs in debug. Replacement-character policy can come later.

FreeType usage:

- Load faces from copied font bytes with `FT_New_Memory_Face`.
- Shape first, then rasterize shaped glyph IDs with `FT_Load_Glyph`.
- Do not map Unicode codepoints through `FT_Get_Char_Index` after shaping.
- Start with grayscale antialiasing and `FT_RENDER_MODE_NORMAL`.
- Avoid LCD/subpixel rendering in V0.

`kb_text_shape.h` usage:

- Use it for segmentation/shaping and glyph IDs/positions.
- Do not ask it to own rendering, atlas policy, or paragraph layout.
- In the dependency spike, confirm shaping unit conversion against FreeType metrics and `hb-shape` for a small corpus.

## Simple API

Keep the public text API about this small:

```c
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

B32 text_context_create(const TextContextDesc* desc, TextContext** outText);
void text_context_destroy(TextContext* text);

TextFont text_font_load_memory(TextContext* text, const TextFontDesc* desc);
TextDrawData text_prepare_draw(TextContext* text, Arena* frameArena, const TextDrawDesc* desc);
```

API rules:

- `text_font_load_memory` copies font bytes into `TextContext` ownership. The caller does not need to keep `desc->data` alive.
- `text_prepare_draw` allocates returned arrays from `frameArena`.
- `TextAtlasUpload::pixels` points into the persistent CPU atlas owned by `TextContext`.
- Upload records are generated only for glyphs newly rasterized during that call.
- The renderer should upload returned dirty rects before drawing returned quads.
- V0 has one atlas page, so no atlas page field is needed yet.

Do not add feature flags, language tags, color modes, font stacks, or backend polymorphism to the V0 API. Add them only when tests or real product use need them.

## V0 Internal Data

Use fixed capacities from `TextContextDesc`. No unbounded growth.

Text context data:

- `Arena* arena`.
- Backend state: one `FT_Library` and one `kb_text_shape.h` context.
- Flat `TextFontSlot` array of `maxFonts`.
- Fixed glyph cache of `maxGlyphs`.
- CPU atlas pixels: `atlasWidth * atlasHeight` bytes.
- Simple shelf packer:
  - `nextX`,
  - `nextY`,
  - `rowHeight`.

Glyph cache key:

- font slot/generation,
- glyph ID,
- pixel size rounded to a deterministic V0 bucket.

Glyph cache value:

- atlas rectangle,
- bitmap bearing,
- shaped/raster placement data needed to build quads.

V0 can use a simple open-addressed table or a linear table if `maxGlyphs` is small. The important rule is no heap allocation in `text_prepare_draw`.

Initial defaults:

```c
#define TEXT_DEFAULT_ATLAS_WIDTH 1024u
#define TEXT_DEFAULT_ATLAS_HEIGHT 1024u
#define TEXT_DEFAULT_MAX_FONTS 8u
#define TEXT_DEFAULT_MAX_GLYPHS 4096u
#define TEXT_DEFAULT_MAX_QUADS 4096u
```

If a capacity is exceeded, log once and skip extra work. Do not silently allocate another system.

## Renderer/RHI Integration

Minimal RHI change:

- Add `GfxFormat_R8_UNorm`.
- Add bytes-per-pixel support for R8.
- Map it to `MTLPixelFormatR8Unorm` in Metal.
- Map it to `VK_FORMAT_R8_UNORM` in Vulkan.
- Existing `gfx_upload_texture` remains the upload path.

Renderer setup:

- Create one atlas texture:
  - width/height from `TextContextDesc`,
  - `GfxFormat_R8_UNorm`,
  - `GfxTextureUsageFlags_Sampled | GfxTextureUsageFlags_CopyDst`.
- Register atlas texture and sampler once.
- Create one alpha-blended text graphics pipeline:
  - depth test disabled,
  - depth write disabled,
  - source alpha blending,
  - no discard.
- Create a static U16 quad index buffer for `TEXT_DEFAULT_MAX_QUADS`.
- Create one registered quad storage buffer per frame in flight, or a small registered ring with explicit no-overwrite slices.

Per frame:

1. Call `text_prepare_draw`.
2. Upload each `TextAtlasUpload` with `gfx_upload_texture`.
3. Copy `TextQuad` data into the current frame's quad storage buffer.
4. Allocate root data with `gfx_allocate_temp`.
5. Submit one indexed draw for `quadCount * 6` indices.

Root data should be minimal:

```c
struct TextDrawRootData {
    U32 quadBuffer;
    U32 quadByteOffset;
    U32 atlasTexture;
    U32 atlasSampler;
    F32 targetWidth;
    F32 targetHeight;
};
```

Shader:

- Vertex shader uses custom vertex fetch.
- Index buffer expands one quad record into two triangles.
- Pixel coordinates convert to clip space in the vertex shader.
- Fragment shader samples `atlas.r` and multiplies it by `rgba8.a`.

Resource uses:

- quad buffer: vertex shader read,
- atlas texture: fragment shader read.

Draw placement:

- V0 text draws in a simple overlay pass/bin after scene rendering.
- In-world text is later work.

## Implementation Stages

### Stage 0: Dependency Spike

- Add `kb_text_shape.h` and FreeType to a branch.
- Load one bundled font from memory.
- Shape and rasterize:
  - `Hello`
  - `Olá`
  - `AV`
  - `office`
- Compare glyph IDs/positions loosely against `hb-shape`.
- Lock the unit conversion from kb shaping values to pixels.

Done when shaped glyph IDs can be rasterized by FreeType from the same font bytes.

### Stage 1: CPU Text Context

- Add `nstl/text`.
- Implement `TextContext`, font loading, copied font byte ownership, nil font behavior, and shutdown.
- Implement shaping for one string and hard newlines.
- Return CPU quads from `text_prepare_draw`.

Done when a unit/manual test can print quad positions for `Hello`.

### Stage 2: Glyph Cache And Atlas

- Rasterize missing glyphs.
- Pack into one R8 shelf atlas.
- Return dirty upload rects.
- Repeated calls for the same text should produce no new uploads after warmup.

Done when cache hit/miss counters behave as expected.

### Stage 3: RHI Format And Renderer Draw

- Add `GfxFormat_R8_UNorm` to both backends.
- Add the text shader/pipeline.
- Create atlas texture, sampler, quad buffer, index buffer.
- Upload atlas dirty rects.
- Draw one text string on screen.

Done when "Hello, text" is visible on macOS Metal and the same renderer path is ready for Windows Vulkan.

### Stage 4: Small Test Corpus

Draw a few hard-coded strings:

- ASCII: `Hello, world!`
- Portuguese: `Olá, ação, coração`
- Kerning: `AVATAR ToYo`
- Ligature: `office ffi fi fl`
- Newline: `one\ntwo\nthree`

V0 does not need to solve CJK, RTL, or emoji. Keep those tests listed as later correctness targets.

## Tests And Acceptance Criteria

Build:

- `./run`

Runtime:

- `./build/utils_app`

Acceptance:

- App shows at least one string on screen from a bundled font.
- Repeated static text does not rasterize or upload glyphs after warmup.
- Atlas overflow logs once and does not crash.
- Nil font produces no draw.
- Text rendering does not add a text API to `nstl/gfx`.
- Draw submission does not allocate or hash per glyph.
- Renderer/RHI resource ownership remains inspectable in capture.

Useful counters:

- glyph cache hits,
- glyph cache misses,
- atlas used pixels,
- atlas overflow count,
- upload rect count,
- upload bytes,
- quad count.

## Later Work

Add only after V0 is working:

- Font stacks and fallback.
- Explicit language/direction fields.
- Shaped-run cache for stable labels.
- Multiple atlas pages.
- Atlas eviction/rebuild.
- Color glyph path with `RGBA8`.
- CJK/RTL correctness work.
- HarfBuzz backend if `kb_text_shape.h` fails real tests.
- DirectWrite/CoreText/Android native compile-time backends if a platform needs them.
- SDF/MSDF for large or world-space text.
- Wrapping/layout if a real UI/debug console needs it.

## Sources

Local:

- `documentation/SEB_AALTONEN_2026_FINDINGS.md`
- `documentation/GFX_LONG_TERM_PLAN.md`
- `documentation/FUTURE_WORK.md`
- `nstl/gfx/gfx.hpp`
- `app/shaders/gfx_shader_abi.slang`

External:

- [`kb_text_shape.h`](https://github.com/JimmyLefevre/kb/blob/main/kb_text_shape.h)
- [FreeType Tutorial I](https://freetype.org/freetype2/docs/tutorial/step1.html)
- [FreeType Glyph Retrieval API](https://freetype.org/freetype2/docs/reference/ft2-glyph_retrieval.html)
- [HarfBuzz shaping API](https://harfbuzz.github.io/harfbuzz-hb-shape.html)
- [stb_truetype.h](https://github.com/nothings/stb/blob/master/stb_truetype.h)
- [DirectWrite `IDWriteTextAnalyzer::GetGlyphs`](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritetextanalyzer-getglyphs)
- [Apple Core Text](https://developer.apple.com/documentation/coretext/)
- [Android NDK Font APIs](https://developer.android.com/ndk/reference/group/font)
- [msdfgen](https://github.com/Chlumsky/msdfgen)
