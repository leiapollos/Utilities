//
// Created by André Leite on 12/06/2026.
//
// The second consumer: a window, a panel, a text edit, a button, a
// stat line. No world, no sim, no audio — every engine byte this file
// is forced to touch but doesn't want is a carve defect.
//

static void hello_state_init_(EngContext* ctx, void* memory) {
    HelloState* hello = (HelloState*)memory;
    MEMSET(hello, 0, sizeof(*hello));
    StringU8 seed = str8("hello, hot reload");
    MEMCPY(hello->text, seed.data, seed.size);
    hello->textLength = (U32)seed.size;
}

static void hello_panels_(EngContext* ctx, UI_Context* ui) {
    HelloState* hello = (HelloState*)ctx->project;

    UI_PanelDesc desc = {};
    desc.offsetX = 40.0f;
    desc.offsetY = 40.0f;
    desc.width = ui_px(420.0f);
    desc.height = ui_fit();
    ui_panel_begin(ui, str8("hello###hello"), &desc);

    ui_text_edit(ui, str8("###hello_text"), hello->text,
                 (U32)sizeof(hello->text), &hello->textLength);
    if (ui_button(ui, str8("click me")).clicked) {
        hello->clickCount += 1u;
    }
    ui_label(ui, str8_fmt(ui->frameArena, "clicks {}  text {} chars  frame {}",
                          hello->clickCount, hello->textLength,
                          ctx->engine->frameCounter));

    ui_panel_end(ui);
}

static const EngProject* eng_project_(void) {
    static const EngProject project = {
        .name = "hello",
        .stateId = ENG_STATE_ID('H', 'E', 'L', 'O'),
        .stateVersion = HELLO_STATE_VERSION,
        .stateSize = sizeof(HelloState),
        .stateAlignment = alignof(HelloState),
        .state_init = hello_state_init_,
        .panels = hello_panels_,
    };
    return &project;
}
