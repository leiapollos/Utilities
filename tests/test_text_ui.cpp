//
// Glyph cache open addressing (synthetic TextContext, no FreeType calls)
// and UI key chaining (##/###, seed inheritance).
//

static void test_text_ui_(void) {
    // Glyph cache: a hand-built context exercises the table statics.
    enum { TEST_GLYPH_CAP = 8u };
    TextGlyphCacheEntry entries[TEST_GLYPH_CAP] = {};
    U64 slotKeys[TEST_GLYPH_CAP] = {};
    U32 slotValues[TEST_GLYPH_CAP] = {};
    TextContext text = {};
    text.glyphs = entries;
    text.maxGlyphs = TEST_GLYPH_CAP;
    text.glyphSlotKeys = slotKeys;
    text.glyphSlotValues = slotValues;
    text.glyphSlotCapacity = TEST_GLYPH_CAP;

    TEST_CHECK(text_find_glyph(&text, 1u, 7u, 16u) == 0);

    text_insert_glyph_slot(&text, 1u, 7u, 16u, 0u);
    text_insert_glyph_slot(&text, 1u, 7u, 17u, 1u);
    text_insert_glyph_slot(&text, 2u, 7u, 16u, 2u);
    TEST_CHECK(text_find_glyph(&text, 1u, 7u, 16u) == entries + 0u);
    TEST_CHECK(text_find_glyph(&text, 1u, 7u, 17u) == entries + 1u);
    TEST_CHECK(text_find_glyph(&text, 2u, 7u, 16u) == entries + 2u);
    TEST_CHECK(text_find_glyph(&text, 3u, 3u, 3u) == 0);

    // Probing: fill to capacity − 1 (collisions inevitable at 87% load),
    // every entry stays findable.
    for (U32 at = 3u; at < TEST_GLYPH_CAP - 1u; ++at) {
        text_insert_glyph_slot(&text, 1u, 100u + at, 16u, at);
    }
    B32 allFound = 1;
    for (U32 at = 3u; at < TEST_GLYPH_CAP - 1u; ++at) {
        if (text_find_glyph(&text, 1u, 100u + at, 16u) != entries + at) {
            allFound = 0;
        }
    }
    TEST_CHECK(allFound);

    // Full table: insert drops silently (find of the dropped key misses,
    // existing entries survive). The runtime never gets here — maxGlyphs
    // gates inserts — but the table's own behavior is pinned.
    text_insert_glyph_slot(&text, 9u, 9u, 9u, 7u);
    text_insert_glyph_slot(&text, 9u, 10u, 9u, 7u);
    TEST_CHECK(text_find_glyph(&text, 1u, 7u, 16u) == entries + 0u);

    // Key packing keeps the three axes distinct.
    TEST_CHECK(text_glyph_key(1u, 7u, 16u) != text_glyph_key(1u, 7u, 17u));
    TEST_CHECK(text_glyph_key(1u, 7u, 16u) != text_glyph_key(2u, 7u, 16u));
    TEST_CHECK(text_glyph_key(1u, 7u, 16u) != text_glyph_key(1u, 8u, 16u));

    // UI keys: a zeroed context has no keyed ancestor → seed 0.
    UI_Context ui = {};
    StringU8 save = str8("Save");
    UI_Key saveKey = ui_key_from_label(&ui, save);
    TEST_CHECK(saveKey != 0ull && saveKey != UI_DEAD_KEY);
    TEST_CHECK(saveKey == ui_key_from_label(&ui, save));
    TEST_CHECK(saveKey != ui_key_from_label(&ui, str8("Load")));

    // ### hashes only the suffix: different display text, same key.
    UI_Key a3 = ui_key_from_label(&ui, str8("Save###42"));
    UI_Key b3 = ui_key_from_label(&ui, str8("Load###42"));
    TEST_CHECK(a3 == b3);
    TEST_CHECK(a3 != saveKey);

    // ## participates in the key (it is display-strip only).
    UI_Key a2 = ui_key_from_label(&ui, str8("A##x"));
    TEST_CHECK(a2 != ui_key_from_label(&ui, str8("B##x")));
    TEST_CHECK(a2 != ui_key_from_label(&ui, str8("A")));
    TEST_CHECK(ui_display_from_label(str8("A##x")).size == 1u);
    TEST_CHECK(ui_display_from_label(str8("Save###42")).size == 4u);

    // Seed chaining: a keyed ancestor changes the key; a keyless ancestor
    // falls through to the next keyed one (none here → seed 0).
    UI_Widget widgets[1] = {};
    ui.widgets = widgets;
    ui.parentStack[0] = 0u;
    ui.parentDepth = 1u;
    widgets[0].key = 0xABCDEF0123ull;
    UI_Key chained = ui_key_from_label(&ui, save);
    TEST_CHECK(chained != saveKey);
    widgets[0].key = 0ull;
    TEST_CHECK(ui_key_from_label(&ui, save) == saveKey);
}
