//
// Texture upload batch layout. The shared pre-pass is the single source of
// truth for per-region staging offsets and the all-or-nothing contract; both
// backends consume it verbatim. The mip-layout bug lived in this seam —
// per-region uploads desyncing whole-image layout state — so the batch must
// validate completely or lay out nothing.
//

static void test_gfx_upload_(void) {
    U32 marker = 0u;

    // Full BC1 mip chain, 64x64 -> 16x16, engine-cooked pitch (256-aligned).
    GfxTextureUploadRegion chain[3] = {};
    U32 mipWidth = 64u;
    U32 mipHeight = 64u;
    for (U32 mip = 0u; mip < 3u; ++mip) {
        chain[mip].src = &marker;
        chain[mip].mip = mip;
        chain[mip].layerCount = 1u;
        chain[mip].width = mipWidth;
        chain[mip].height = mipHeight;
        chain[mip].depth = 1u;
        chain[mip].bytesPerRow = 256u;
        chain[mip].rowsPerImage = (mipHeight + 3u) / 4u;
        mipWidth >>= 1u;
        mipHeight >>= 1u;
    }

    U64 offsets[3] = {};
    GfxTextureUploadValidation validations[3] = {};
    U64 total = 0u;
    TEST_CHECK(gfx_validate_texture_upload_batch(GfxFormat_BC1_RGBA_UNorm, 64u, 64u, 3u,
                                                 chain, 3u, offsets, validations, &total));
    // Block rows per mip: 16, 8, 4 at 256B pitch.
    TEST_CHECK(validations[0].sourceBytesPerImage == 4096u);
    TEST_CHECK(validations[1].sourceBytesPerImage == 2048u);
    TEST_CHECK(validations[2].sourceBytesPerImage == 1024u);
    TEST_CHECK(offsets[0] == 0u);
    TEST_CHECK(offsets[1] == 4096u);
    TEST_CHECK(offsets[2] == 6144u);
    TEST_CHECK(total == 7168u);
    B32 offsetsAligned = 1;
    for (U32 at = 0u; at < 3u; ++at) {
        if ((offsets[at] % GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT) != 0u) {
            offsetsAligned = 0;
        }
    }
    TEST_CHECK(offsetsAligned);

    // Atlas-style partial rects (R8, 1-mip): offsets accumulate per region.
    GfxTextureUploadRegion rects[2] = {};
    for (U32 at = 0u; at < 2u; ++at) {
        rects[at].src = &marker;
        rects[at].layerCount = 1u;
        rects[at].x = at * 32u;
        rects[at].y = 16u;
        rects[at].width = 8u;
        rects[at].height = (at == 0u) ? 3u : 8u;
        rects[at].depth = 1u;
        rects[at].bytesPerRow = 256u;
        rects[at].rowsPerImage = rects[at].height;
    }
    U64 rectOffsets[2] = {};
    GfxTextureUploadValidation rectValidations[2] = {};
    U64 rectTotal = 0u;
    TEST_CHECK(gfx_validate_texture_upload_batch(GfxFormat_R8_UNorm, 256u, 256u, 1u,
                                                 rects, 2u, rectOffsets, rectValidations, &rectTotal));
    TEST_CHECK(rectOffsets[0] == 0u);
    TEST_CHECK(rectOffsets[1] == 768u);
    TEST_CHECK(rectTotal == 768u + 2048u);

    // All-or-nothing: one bad region anywhere rejects the whole batch.
    GfxTextureUploadRegion mixed[2] = {};
    mixed[0] = rects[1];
    mixed[1] = rects[1];
    mixed[1].src = 0;
    TEST_CHECK(!gfx_validate_texture_upload_batch(GfxFormat_R8_UNorm, 256u, 256u, 1u,
                                                  mixed, 2u, rectOffsets, rectValidations, &rectTotal));
    TEST_CHECK(rectTotal == 0u);

    // Rejections: mip out of range, unaligned pitch, pitch below the packed
    // row, out-of-bounds rect, empty batch, missing out params.
    GfxTextureUploadRegion bad = rects[1];
    bad.mip = 1u;
    TEST_CHECK(!gfx_validate_texture_upload_batch(GfxFormat_R8_UNorm, 256u, 256u, 1u,
                                                  &bad, 1u, rectOffsets, rectValidations, &rectTotal));
    bad = rects[1];
    bad.bytesPerRow = 200u;
    TEST_CHECK(!gfx_validate_texture_upload_batch(GfxFormat_R8_UNorm, 256u, 256u, 1u,
                                                  &bad, 1u, rectOffsets, rectValidations, &rectTotal));
    bad = chain[0];
    bad.bytesPerRow = 256u;
    bad.width = 256u;
    TEST_CHECK(!gfx_validate_texture_upload_batch(GfxFormat_BC1_RGBA_UNorm, 256u, 256u, 1u,
                                                  &bad, 1u, rectOffsets, rectValidations, &rectTotal));
    bad = rects[1];
    bad.x = 252u;
    TEST_CHECK(!gfx_validate_texture_upload_batch(GfxFormat_R8_UNorm, 256u, 256u, 1u,
                                                  &bad, 1u, rectOffsets, rectValidations, &rectTotal));
    TEST_CHECK(!gfx_validate_texture_upload_batch(GfxFormat_R8_UNorm, 256u, 256u, 1u,
                                                  rects, 0u, rectOffsets, rectValidations, &rectTotal));
    TEST_CHECK(!gfx_validate_texture_upload_batch(GfxFormat_R8_UNorm, 256u, 256u, 1u,
                                                  rects, 2u, 0, rectValidations, &rectTotal));
}
