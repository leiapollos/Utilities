#define UI_DEAD_KEY 0xFFFFFFFFFFFFFFFFull
#define UI_LAYER Draw2DLayer_UI

#define UI_STYLE_TEXT_SIZE 18.0f
#define UI_STYLE_ROW_HEIGHT 28.0f
#define UI_STYLE_PANEL_PADDING 14.0f
#define UI_STYLE_PANEL_GAP 8.0f
#define UI_STYLE_BUTTON_PAD_X 12.0f
#define UI_STYLE_BUTTON_PAD_Y 5.0f
#define UI_STYLE_CHECK_SIZE 18.0f
#define UI_STYLE_SLIDER_THUMB_W 10.0f
#define UI_STYLE_SLIDER_TRACK_H 16.0f
#define UI_STYLE_SCROLLBAR_W 8.0f
#define UI_STYLE_SCROLL_MIN_THUMB 24.0f
#define UI_STYLE_ANIM_SPEED 14.0f
#define UI_STYLE_SCROLL_ANIM_SPEED 22.0f
#define UI_STYLE_CARET_BLINK_PERIOD 1.2f
#define UI_STYLE_CARET_BLINK_ON 0.8f
#define UI_STYLE_DOUBLE_CLICK_SECONDS 0.30f
#define UI_STYLE_DOUBLE_CLICK_DISTANCE 6.0f

#define UI_COLOR_PANEL_BG 0x10141AF0u
#define UI_COLOR_PANEL_BORDER 0x3A4148FFu
#define UI_COLOR_WIDGET_BG 0x1C232BFFu
#define UI_COLOR_WIDGET_BG_HOT 0x2A333DFFu
#define UI_COLOR_WIDGET_BG_ACTIVE 0x39434FFFu
#define UI_COLOR_WIDGET_BORDER 0x46505BFFu
#define UI_COLOR_EDIT_BG 0x14181DFFu
#define UI_COLOR_TEXT 0xD9D4C7FFu
#define UI_COLOR_TEXT_DIM 0x9AA7B4FFu
#define UI_COLOR_TEXT_BRIGHT 0xF4F1E8FFu
#define UI_COLOR_ACCENT 0x4F8A6AFFu
#define UI_COLOR_ACCENT_HOT 0x63A581FFu
#define UI_COLOR_CARET 0xF4F1E8FFu
#define UI_COLOR_SELECTION 0x3A5A8AC0u
#define UI_COLOR_SCROLL_TRACK 0x161B21FFu

enum UI_EditKindBits {
    UI_EditKindBit_Focused = (1u << 0u),
    UI_EditKindBit_CaretVisible = (1u << 1u),
    UI_EditKindBit_Selection = (1u << 2u),
};

static UI_Widget g_uiNilWidget;

static F32 ui_floor(F32 value) {
    F32 truncated = (F32)(S64)value;
    return (truncated > value) ? (truncated - 1.0f) : truncated;
}

static F32 ui_saturate(F32 value) {
    return CLAMP(value, 0.0f, 1.0f);
}

static U32 ui_color_lerp(U32 a, U32 b, F32 t) {
    t = ui_saturate(t);
    U32 result = 0u;
    for (U32 shift = 0u; shift < 32u; shift += 8u) {
        F32 ca = (F32)((a >> shift) & 0xFFu);
        F32 cb = (F32)((b >> shift) & 0xFFu);
        U32 c = (U32)(ca + (cb - ca) * t + 0.5f);
        result |= (c & 0xFFu) << shift;
    }
    return result;
}

// ////////////////////////
// Keys

static UI_Key ui_hash_bytes(UI_Key seed, const U8* data, U64 size) {
    U64 hash = (seed != 0ull) ? seed : 0xCBF29CE484222325ull;
    for (U64 index = 0; index < size; ++index) {
        hash = (hash ^ (U64)data[index]) * 0x100000001B3ull;
    }
    if (hash == 0ull || hash == UI_DEAD_KEY) {
        hash = 0x9E3779B97F4A7C15ull;
    }
    return hash;
}

static U64 ui_label_find(StringU8 label, const char* marker) {
    U64 markerSize = (marker[2] != 0) ? 3u : 2u;
    if (label.size < markerSize) {
        return label.size;
    }
    for (U64 index = 0; index + markerSize <= label.size; ++index) {
        B32 match = 1;
        for (U64 at = 0; at < markerSize; ++at) {
            if (label.data[index + at] != (U8)marker[at]) {
                match = 0;
                break;
            }
        }
        if (match) {
            return index;
        }
    }
    return label.size;
}

static StringU8 ui_display_from_label(StringU8 label) {
    U64 cut = ui_label_find(label, "##");
    StringU8 result = label;
    result.size = cut;
    return result;
}

static UI_Key ui_seed_key(UI_Context* ui) {
    for (U32 depth = ui->parentDepth; depth-- > 0u;) {
        UI_Widget* parent = ui->widgets + ui->parentStack[depth];
        if (parent->key != 0ull) {
            return parent->key;
        }
    }
    return 0ull;
}

UI_Key ui_key_from_label(UI_Context* ui, StringU8 label) {
    if (label.size == 0u) {
        return 0ull;
    }
    UI_Key seed = ui_seed_key(ui);
    U64 hashOnly = ui_label_find(label, "###");
    if (hashOnly < label.size) {
        return ui_hash_bytes(seed, label.data + hashOnly, label.size - hashOnly);
    }
    return ui_hash_bytes(seed, label.data, label.size);
}

static UI_Key ui_key_derive(UI_Key key, const char* salt) {
    StringU8 saltStr = str8(salt);
    return ui_hash_bytes(key, saltStr.data, saltStr.size);
}

// ////////////////////////
// Retained table

static UI_RetainedWidget* ui_retained_find(UI_State* state, UI_Key key) {
    if (key == 0ull) {
        return 0;
    }
    for (U32 index = 0; index < UI_MAX_RETAINED; ++index) {
        if (state->retained[index].key == key) {
            return state->retained + index;
        }
    }
    return 0;
}

static UI_RetainedWidget* ui_retained_require(UI_State* state, UI_Key key) {
    UI_RetainedWidget* found = ui_retained_find(state, key);
    if (found) {
        return found;
    }

    UI_RetainedWidget* slot = 0;
    for (U32 index = 0; index < UI_MAX_RETAINED; ++index) {
        if (state->retained[index].key == 0ull) {
            slot = state->retained + index;
            break;
        }
    }
    if (!slot) {
        slot = state->retained;
        for (U32 index = 1; index < UI_MAX_RETAINED; ++index) {
            if (state->retained[index].lastTouchedFrame < slot->lastTouchedFrame) {
                slot = state->retained + index;
            }
        }
        state->stats.retainedEvictCount += 1u;
    }

    MEMSET(slot, 0, sizeof(*slot));
    slot->key = key;
    return slot;
}

// ////////////////////////
// Hit testing against last frame's rects

static U32 ui_hit_scan(UI_State* state, F32 x, F32 y, U32 requiredFlags) {
    for (U32 index = state->hitRectCount; index-- > 0u;) {
        UI_HitRect* rect = state->hitRects + index;
        if ((rect->flags & requiredFlags) == 0u) {
            continue;
        }
        if (x >= rect->rect[0] && x < rect->rect[2] && y >= rect->rect[1] && y < rect->rect[3]) {
            return index;
        }
    }
    return UI_NIL_INDEX;
}

static void ui_update_hot(UI_State* state) {
    U32 hitIndex = ui_hit_scan(state, state->mouseX, state->mouseY,
                               UI_WidgetFlag_Clickable | UI_WidgetFlag_Scrollable | UI_WidgetFlag_Focusable);
    UI_Key hitKey = (hitIndex != UI_NIL_INDEX) ? state->hitRects[hitIndex].key : 0ull;
    if (state->activeKey == 0ull || state->activeKey == hitKey) {
        state->hotKey = hitKey;
    } else {
        state->hotKey = 0ull;
    }
}

// ////////////////////////
// Input events

static B32 ui_codepoint_insertable(U32 codepoint, U32 modifiers) {
    if (modifiers & (OS_KeyModifiers_Control | OS_KeyModifiers_Super)) {
        return 0;
    }
    if (codepoint < 0x20u || codepoint == 0x7Fu) {
        return 0;
    }
    if (codepoint >= 0xF700u && codepoint <= 0xF8FFu) {
        return 0;
    }
    if (codepoint >= 0xD800u && codepoint <= 0xDFFFu) {
        return 0;
    }
    return 1;
}

static void ui_push_text_event(UI_Context* ui, U32 kind, U32 flags, U32 codepoint) {
    if (ui->textEventCount >= UI_MAX_TEXT_EVENTS) {
        return;
    }
    UI_TextEvent* event = ui->textEvents + ui->textEventCount;
    event->kind = kind;
    event->flags = flags;
    event->codepoint = codepoint;
    ui->textEventCount += 1u;
}

static void ui_process_key_down(UI_Context* ui, const OS_GraphicsEvent_KeyDown* key) {
    UI_State* state = ui->state;
    if (state->focusKey == 0ull) {
        return;
    }

    U32 flags = 0u;
    if (key->modifiers & OS_KeyModifiers_Shift) {
        flags |= UI_TextEventFlag_Shift;
    }
    if (key->modifiers & (OS_KeyModifiers_Alt | OS_KeyModifiers_Control)) {
        flags |= UI_TextEventFlag_Word;
    }
    B32 line = (key->modifiers & OS_KeyModifiers_Super) != 0u;

    switch (key->keyCode) {
        case OS_KeyCode_LeftArrow: ui_push_text_event(ui, line ? UI_TextEventKind_Home : UI_TextEventKind_Left, flags, 0u); break;
        case OS_KeyCode_RightArrow: ui_push_text_event(ui, line ? UI_TextEventKind_End : UI_TextEventKind_Right, flags, 0u); break;
        case OS_KeyCode_Home: ui_push_text_event(ui, UI_TextEventKind_Home, flags, 0u); break;
        case OS_KeyCode_End: ui_push_text_event(ui, UI_TextEventKind_End, flags, 0u); break;
        case OS_KeyCode_Backspace: ui_push_text_event(ui, UI_TextEventKind_Backspace, flags, 0u); break;
        case OS_KeyCode_Delete: ui_push_text_event(ui, UI_TextEventKind_Delete, flags, 0u); break;
        case OS_KeyCode_Enter: ui_push_text_event(ui, UI_TextEventKind_Enter, flags, 0u); break;
        case OS_KeyCode_Escape: ui_push_text_event(ui, UI_TextEventKind_Escape, flags, 0u); break;
        case OS_KeyCode_A: {
            if (key->modifiers & (OS_KeyModifiers_Super | OS_KeyModifiers_Control)) {
                ui_push_text_event(ui, UI_TextEventKind_SelectAll, flags, 0u);
            }
        }
        break;
        default: break;
    }
}

static void ui_process_mouse_down(UI_Context* ui, const OS_GraphicsEvent_MouseButtonDown* down) {
    UI_State* state = ui->state;
    if (down->button != OS_MouseButton_Left) {
        return;
    }

    state->mouseX = down->x;
    state->mouseY = down->y;

    F32 dx = down->x - state->lastClickX;
    F32 dy = down->y - state->lastClickY;
    B32 nearLast = (dx * dx + dy * dy) <= (UI_STYLE_DOUBLE_CLICK_DISTANCE * UI_STYLE_DOUBLE_CLICK_DISTANCE);
    if (state->clickClock <= UI_STYLE_DOUBLE_CLICK_SECONDS && nearLast) {
        state->clickCount += 1u;
    } else {
        state->clickCount = 1u;
    }
    state->clickClock = 0.0f;
    state->lastClickX = down->x;
    state->lastClickY = down->y;
    U32 clickCount = MAX(state->clickCount, down->clickCount);

    U32 hitIndex = ui_hit_scan(state, down->x, down->y, UI_WidgetFlag_Clickable | UI_WidgetFlag_Focusable);
    state->mouseDown = 1;
    state->dragStartMouse[0] = down->x;
    state->dragStartMouse[1] = down->y;

    if (hitIndex != UI_NIL_INDEX) {
        UI_HitRect* rect = state->hitRects + hitIndex;
        state->activeKey = rect->key;
        ui->pressedKey = rect->key;
        ui->pressClickCount = clickCount;
        state->focusKey = (rect->flags & UI_WidgetFlag_Focusable) ? rect->key : 0ull;
        if (rect->rootSlot < UI_MAX_ROOTS && state->roots[rect->rootSlot].key != 0ull) {
            state->zCounter += 1u;
            state->roots[rect->rootSlot].zIndex = state->zCounter;
        }
    } else {
        state->activeKey = UI_DEAD_KEY;
        state->focusKey = 0ull;
    }
    ui_update_hot(state);
}

static void ui_process_mouse_up(UI_Context* ui, const OS_GraphicsEvent_MouseButtonUp* up) {
    UI_State* state = ui->state;
    if (up->button != OS_MouseButton_Left) {
        return;
    }

    state->mouseX = up->x;
    state->mouseY = up->y;
    if (state->activeKey != 0ull && state->activeKey != UI_DEAD_KEY) {
        ui->releasedKey = state->activeKey;
        U32 hitIndex = ui_hit_scan(state, up->x, up->y, UI_WidgetFlag_Clickable | UI_WidgetFlag_Focusable);
        if (hitIndex != UI_NIL_INDEX && state->hitRects[hitIndex].key == state->activeKey) {
            ui->clickedKey = state->activeKey;
        }
    }
    state->activeKey = 0ull;
    state->mouseDown = 0;
    ui_update_hot(state);
}

static void ui_process_scroll(UI_Context* ui, const OS_GraphicsEvent_MouseScroll* scroll) {
    UI_State* state = ui->state;
    state->mouseX = scroll->x;
    state->mouseY = scroll->y;

    U32 hitIndex = ui_hit_scan(state, scroll->x, scroll->y, UI_WidgetFlag_Scrollable);
    if (hitIndex == UI_NIL_INDEX) {
        return;
    }
    UI_RetainedWidget* retained = ui_retained_find(state, state->hitRects[hitIndex].key);
    if (!retained) {
        return;
    }

    F32 deltas[UI_Axis_COUNT] = {scroll->deltaX, scroll->deltaY};
    for (U32 axis = 0; axis < UI_Axis_COUNT; ++axis) {
        F32 delta = deltas[axis];
        if (delta == 0.0f) {
            continue;
        }
        F32 maxScroll = MAX(0.0f, retained->contentSize[axis] - retained->viewSize[axis]);
        retained->scrollTarget[axis] = CLAMP(retained->scrollTarget[axis] - delta, 0.0f, maxScroll);
    }
}

static void ui_process_events(UI_Context* ui, const OS_GraphicsEvent* events, U32 eventCount) {
    UI_State* state = ui->state;
    for (U32 eventIndex = 0; eventIndex < eventCount; ++eventIndex) {
        const OS_GraphicsEvent* event = events + eventIndex;
        switch (event->tag) {
            case OS_GraphicsEvent_Tag_MouseMove: {
                state->mouseX = event->mouseMove.x;
                state->mouseY = event->mouseMove.y;
                ui_update_hot(state);
            }
            break;
            case OS_GraphicsEvent_Tag_MouseButtonDown: ui_process_mouse_down(ui, &event->mouseButtonDown); break;
            case OS_GraphicsEvent_Tag_MouseButtonUp: ui_process_mouse_up(ui, &event->mouseButtonUp); break;
            case OS_GraphicsEvent_Tag_MouseScroll: ui_process_scroll(ui, &event->mouseScroll); break;
            case OS_GraphicsEvent_Tag_KeyDown: ui_process_key_down(ui, &event->keyDown); break;
            case OS_GraphicsEvent_Tag_TextInput: {
                if (state->focusKey != 0ull &&
                    ui_codepoint_insertable(event->textInput.codepoint, event->textInput.modifiers)) {
                    ui_push_text_event(ui, UI_TextEventKind_Char, 0u, event->textInput.codepoint);
                }
            }
            break;
            default: break;
        }
    }
}

// ////////////////////////
// Text shaping helpers

static void ui_accumulate_uploads(UI_Context* ui, const TextRunView* view) {
    for (U32 uploadIndex = 0; uploadIndex < view->uploadCount; ++uploadIndex) {
        if (ui->uploadCount >= UI_MAX_UPLOADS) {
            return;
        }
        ui->uploads[ui->uploadCount] = view->uploads[uploadIndex];
        ui->uploadCount += 1u;
    }
}

static TextRunView ui_shape_text(UI_Context* ui, StringU8 text, F32 pixelSize) {
    TextRunView view = {};
    if (!ui->textContext || ui->font.generation == 0u || text.size == 0u) {
        return view;
    }

    TextRunDesc desc = {};
    desc.font = ui->font;
    desc.text = text;
    desc.pixelSize = pixelSize;
    view = text_prepare_run(ui->textContext, ui->frameArena, &desc);
    ui_accumulate_uploads(ui, &view);
    return view;
}

static F32 ui_measure_text_width(UI_Context* ui, StringU8 text, F32 pixelSize) {
    TextRunView view = ui_shape_text(ui, text, pixelSize);
    return view.width;
}

// ////////////////////////
// Widget construction

static UI_Widget* ui_widget(UI_Context* ui, U32 index) {
    if (index == UI_NIL_INDEX || index >= ui->widgetCount) {
        return &g_uiNilWidget;
    }
    return ui->widgets + index;
}

static U32 ui_widget_add(UI_Context* ui, UI_Key key, U32 flags, U32 kind, UI_Size width, UI_Size height) {
    UI_State* state = ui->state;
    if (ui->widgetCount >= UI_MAX_WIDGETS) {
        state->stats.widgetOverflowCount += 1u;
        return UI_NIL_INDEX;
    }

    U32 retainedIndex = UI_NIL_INDEX;
    if (key != 0ull) {
        UI_RetainedWidget* retained = ui_retained_require(state, key);
        if (retained->lastTouchedFrame == state->frameIndex) {
            state->stats.duplicateKeyCount += 1u;
            key = 0ull;
        } else {
            retained->lastTouchedFrame = state->frameIndex;
            retainedIndex = (U32)(retained - state->retained);

            F32 rate = CLAMP(UI_STYLE_ANIM_SPEED * ui->deltaSeconds, 0.0f, 1.0f);
            F32 hotTarget = (state->hotKey == key) ? 1.0f : 0.0f;
            retained->hotT += (hotTarget - retained->hotT) * rate;
            if (state->activeKey == key) {
                retained->activeT = 1.0f;
            } else {
                retained->activeT += (0.0f - retained->activeT) * rate;
            }

            if (state->activeKey == key) {
                ui->activeTouched = 1;
            }
            if (state->focusKey == key) {
                ui->focusTouched = 1;
            }
        }
    }

    U32 index = ui->widgetCount;
    ui->widgetCount += 1u;
    UI_Widget* widget = ui->widgets + index;
    MEMSET(widget, 0, sizeof(*widget));
    widget->key = key;
    widget->flags = flags;
    widget->kind = kind;
    widget->parent = UI_NIL_INDEX;
    widget->firstChild = UI_NIL_INDEX;
    widget->lastChild = UI_NIL_INDEX;
    widget->nextSibling = UI_NIL_INDEX;
    widget->retainedIndex = retainedIndex;
    widget->rootSlot = UI_NIL_INDEX;
    widget->semanticSize[UI_Axis_X] = width;
    widget->semanticSize[UI_Axis_Y] = height;
    widget->layoutAxis = UI_Axis_Y;
    widget->textAlign = 0.0f;

    if (width.kind == UI_SizeKind_Pixels) {
        widget->size[UI_Axis_X] = width.value;
    }
    if (height.kind == UI_SizeKind_Pixels) {
        widget->size[UI_Axis_Y] = height.value;
    }

    if (ui->parentDepth != 0u) {
        U32 parentIndex = ui->parentStack[ui->parentDepth - 1u];
        UI_Widget* parent = ui->widgets + parentIndex;
        widget->parent = parentIndex;
        if (parent->lastChild == UI_NIL_INDEX) {
            parent->firstChild = index;
        } else {
            ui->widgets[parent->lastChild].nextSibling = index;
        }
        parent->lastChild = index;
    }

    return index;
}

static void ui_widget_set_run(UI_Context* ui, U32 index, TextRunView view, B32 hasText, U32 rgba8) {
    UI_Widget* widget = ui_widget(ui, index);
    widget->text = view;
    widget->textColor = rgba8;
    for (U32 axis = 0; axis < UI_Axis_COUNT; ++axis) {
        if (widget->semanticSize[axis].kind == UI_SizeKind_Text) {
            F32 textSize = (axis == UI_Axis_X) ? widget->text.width : widget->text.height;
            widget->size[axis] = textSize + 2.0f * widget->padding[axis];
        }
    }
    if (widget->text.quadCount != 0u || hasText) {
        widget->flags |= UI_WidgetFlag_DrawText;
    }
}

static void ui_widget_set_text(UI_Context* ui, U32 index, StringU8 text, F32 pixelSize, U32 rgba8) {
    ui_widget_set_run(ui, index, ui_shape_text(ui, text, pixelSize), text.size != 0u, rgba8);
}

static void ui_push_parent(UI_Context* ui, U32 index) {
    if (index == UI_NIL_INDEX || ui->parentDepth >= UI_PARENT_STACK_DEPTH) {
        return;
    }
    ui->parentStack[ui->parentDepth] = index;
    ui->parentDepth += 1u;
}

static void ui_pop_parent(UI_Context* ui) {
    if (ui->parentDepth == 0u) {
        return;
    }
    ui->parentDepth -= 1u;
    U32 index = ui->parentStack[ui->parentDepth];
    UI_Widget* widget = ui->widgets + index;

    for (U32 axis = 0; axis < UI_Axis_COUNT; ++axis) {
        if (widget->semanticSize[axis].kind != UI_SizeKind_Fit) {
            continue;
        }
        F32 total = 0.0f;
        F32 maxChild = 0.0f;
        U32 childCount = 0u;
        for (U32 childIndex = widget->firstChild; childIndex != UI_NIL_INDEX;) {
            UI_Widget* child = ui->widgets + childIndex;
            total += child->size[axis];
            maxChild = MAX(maxChild, child->size[axis]);
            childCount += 1u;
            childIndex = child->nextSibling;
        }
        if (axis == widget->layoutAxis) {
            F32 gaps = (childCount > 1u) ? widget->childGap * (F32)(childCount - 1u) : 0.0f;
            widget->size[axis] = total + gaps + 2.0f * widget->padding[axis];
        } else {
            widget->size[axis] = maxChild + 2.0f * widget->padding[axis];
        }
    }
}

// ////////////////////////
// Signals

UI_Signal ui_signal_for(UI_Context* ui, UI_Key key) {
    UI_Signal signal = {};
    if (key == 0ull) {
        return signal;
    }

    UI_State* state = ui->state;
    signal.hovering = (state->hotKey == key);
    signal.pressed = (ui->pressedKey == key);
    signal.clicked = (ui->clickedKey == key);
    signal.released = (ui->releasedKey == key);
    signal.dragging = (state->activeKey == key) && state->mouseDown;
    signal.doubleClicked = signal.pressed && (ui->pressClickCount >= 2u);
    if (signal.dragging || signal.pressed || signal.released) {
        signal.dragDeltaX = state->mouseX - state->dragStartMouse[0];
        signal.dragDeltaY = state->mouseY - state->dragStartMouse[1];
    }

    UI_RetainedWidget* retained = ui_retained_find(state, key);
    if (retained && retained->rect[2] > retained->rect[0]) {
        signal.prevRect[0] = retained->rect[0];
        signal.prevRect[1] = retained->rect[1];
        signal.prevRect[2] = retained->rect[2];
        signal.prevRect[3] = retained->rect[3];
        signal.hasPrevRect = 1;
    }
    return signal;
}

// ////////////////////////
// Frame begin

UI_Context* ui_begin(const UI_BeginDesc* desc) {
    if (!desc || !desc->state || !desc->frameArena) {
        return 0;
    }

    UI_Context* ui = ARENA_PUSH_STRUCT(desc->frameArena, UI_Context);
    if (!ui) {
        return 0;
    }
    MEMSET(ui, 0, sizeof(*ui));

    ui->state = desc->state;
    ui->frameArena = desc->frameArena;
    ui->textContext = desc->textContext;
    ui->font = desc->font;
    ui->draw2d = desc->draw2d;
    ui->viewportWidth = desc->viewportWidth;
    ui->viewportHeight = desc->viewportHeight;
    ui->deltaSeconds = desc->deltaSeconds;

    ui->widgets = ARENA_PUSH_ARRAY(desc->frameArena, UI_Widget, UI_MAX_WIDGETS);
    ui->uploads = ARENA_PUSH_ARRAY(desc->frameArena, TextAtlasUpload, UI_MAX_UPLOADS);
    if (!ui->widgets || !ui->uploads) {
        return 0;
    }

    UI_State* state = ui->state;
    state->frameIndex += 1u;
    state->stats.valueRunHits = 0u;
    state->stats.valueRunMisses = 0u;
    state->stats.valueRunUninsertable = 0u;
    state->stats.valueRunResolveFails = 0u;
    state->stats.valueRunNoVictim = 0u;
    state->stats.valueRunNoSlot = 0u;
    state->clickClock += desc->deltaSeconds;
    if (state->clickClock > 10.0f) {
        state->clickClock = 10.0f;
    }

    ui_process_events(ui, desc->events, desc->eventCount);

    state->wantMouse = (state->hotKey != 0ull) ||
                       (state->activeKey != 0ull && state->activeKey != UI_DEAD_KEY);
    state->wantKeyboard = (state->focusKey != 0ull);
    state->wantTextInput = state->wantKeyboard;

    return ui;
}

// ////////////////////////
// Layout solve

static void ui_solve_root(UI_Context* ui, U32 rootIndex, U32* stack) {
    UI_Widget* root = ui->widgets + rootIndex;
    for (U32 axis = 0; axis < UI_Axis_COUNT; ++axis) {
        F32 viewport = (axis == UI_Axis_X) ? ui->viewportWidth : ui->viewportHeight;
        switch (root->semanticSize[axis].kind) {
            case UI_SizeKind_Pct: root->size[axis] = root->semanticSize[axis].value * viewport; break;
            case UI_SizeKind_Grow: root->size[axis] = viewport; break;
            default: break;
        }
        root->pos[axis] = ui_floor(root->rootAnchor[axis] * (viewport - root->size[axis]) + root->rootOffset[axis]);
    }

    U32 stackCount = 0u;
    stack[stackCount++] = rootIndex;

    while (stackCount != 0u) {
        U32 index = stack[--stackCount];
        UI_Widget* widget = ui->widgets + index;
        if (widget->firstChild == UI_NIL_INDEX) {
            continue;
        }

        U32 main = widget->layoutAxis;
        U32 cross = 1u - main;
        F32 inner[UI_Axis_COUNT];
        inner[UI_Axis_X] = MAX(0.0f, widget->size[UI_Axis_X] - 2.0f * widget->padding[UI_Axis_X]);
        inner[UI_Axis_Y] = MAX(0.0f, widget->size[UI_Axis_Y] - 2.0f * widget->padding[UI_Axis_Y]);

        F32 fixedSum = 0.0f;
        F32 growSum = 0.0f;
        U32 childCount = 0u;
        for (U32 childIndex = widget->firstChild; childIndex != UI_NIL_INDEX;) {
            UI_Widget* child = ui->widgets + childIndex;
            childCount += 1u;
            for (U32 axis = 0; axis < UI_Axis_COUNT; ++axis) {
                if (child->semanticSize[axis].kind == UI_SizeKind_Pct) {
                    child->size[axis] = child->semanticSize[axis].value * inner[axis];
                }
            }
            if (child->semanticSize[cross].kind == UI_SizeKind_Grow) {
                child->size[cross] = inner[cross];
            }
            if (child->semanticSize[main].kind == UI_SizeKind_Grow) {
                F32 weight = child->semanticSize[main].value;
                growSum += (weight > 0.0f) ? weight : 1.0f;
            } else {
                fixedSum += child->size[main];
            }
            childIndex = child->nextSibling;
        }

        F32 gaps = (childCount > 1u) ? widget->childGap * (F32)(childCount - 1u) : 0.0f;
        F32 freeSpace = inner[main] - fixedSum - gaps;
        F32 growTotal = 0.0f;
        if (growSum > 0.0f) {
            for (U32 childIndex = widget->firstChild; childIndex != UI_NIL_INDEX;) {
                UI_Widget* child = ui->widgets + childIndex;
                if (child->semanticSize[main].kind == UI_SizeKind_Grow) {
                    F32 weight = child->semanticSize[main].value;
                    weight = (weight > 0.0f) ? weight : 1.0f;
                    child->size[main] = (freeSpace > 0.0f) ? (freeSpace * weight / growSum) : 0.0f;
                    growTotal += child->size[main];
                }
                childIndex = child->nextSibling;
            }
        }

        F32 content[UI_Axis_COUNT];
        content[main] = fixedSum + growTotal + gaps;
        content[cross] = 0.0f;
        for (U32 childIndex = widget->firstChild; childIndex != UI_NIL_INDEX;) {
            UI_Widget* child = ui->widgets + childIndex;
            content[cross] = MAX(content[cross], child->size[cross]);
            childIndex = child->nextSibling;
        }
        widget->overflow[UI_Axis_X] = MAX(0.0f, content[UI_Axis_X] - inner[UI_Axis_X]);
        widget->overflow[UI_Axis_Y] = MAX(0.0f, content[UI_Axis_Y] - inner[UI_Axis_Y]);

        F32 scrollOffset[UI_Axis_COUNT] = {0.0f, 0.0f};
        if (widget->kind == UI_WidgetKind_ScrollRegion && widget->retainedIndex != UI_NIL_INDEX) {
            UI_RetainedWidget* retained = ui->state->retained + widget->retainedIndex;
            F32 rate = CLAMP(UI_STYLE_SCROLL_ANIM_SPEED * ui->deltaSeconds, 0.0f, 1.0f);
            for (U32 axis = 0; axis < UI_Axis_COUNT; ++axis) {
                F32 maxScroll = MAX(0.0f, content[axis] - inner[axis]);
                retained->scrollTarget[axis] = CLAMP(retained->scrollTarget[axis], 0.0f, maxScroll);
                retained->scroll[axis] += (retained->scrollTarget[axis] - retained->scroll[axis]) * rate;
                F32 distance = retained->scrollTarget[axis] - retained->scroll[axis];
                if (distance < 0.5f && distance > -0.5f) {
                    retained->scroll[axis] = retained->scrollTarget[axis];
                }
                retained->contentSize[axis] = content[axis];
                retained->viewSize[axis] = inner[axis];
                scrollOffset[axis] = ui_floor(retained->scroll[axis]);
            }
        }

        F32 leftover = (growSum > 0.0f) ? 0.0f : MAX(0.0f, freeSpace);
        F32 cursor = widget->pos[main] + widget->padding[main] + widget->alignMain * leftover - scrollOffset[main];
        for (U32 childIndex = widget->firstChild; childIndex != UI_NIL_INDEX;) {
            UI_Widget* child = ui->widgets + childIndex;
            child->pos[main] = ui_floor(cursor);
            F32 crossSlack = MAX(0.0f, inner[cross] - child->size[cross]);
            child->pos[cross] = ui_floor(widget->pos[cross] + widget->padding[cross] +
                                         widget->alignCross * crossSlack - scrollOffset[cross]);
            cursor += child->size[main] + widget->childGap;

            if (stackCount < UI_MAX_WIDGETS) {
                stack[stackCount++] = childIndex;
            }
            childIndex = child->nextSibling;
        }
    }
}

// ////////////////////////
// Paint

struct UI_PaintFrame {
    U32 widget;
    U32 nextChild;
    F32 clip[4];
};

static void ui_store_hit_rect(UI_Context* ui, UI_Widget* widget, const F32* clip, U32 rootSlot,
                              UI_Key key, U32 flags, const F32* rectOverride) {
    UI_State* state = ui->state;
    F32 rect[4];
    if (rectOverride) {
        rect[0] = rectOverride[0];
        rect[1] = rectOverride[1];
        rect[2] = rectOverride[2];
        rect[3] = rectOverride[3];
    } else {
        rect[0] = widget->pos[UI_Axis_X];
        rect[1] = widget->pos[UI_Axis_Y];
        rect[2] = widget->pos[UI_Axis_X] + widget->size[UI_Axis_X];
        rect[3] = widget->pos[UI_Axis_Y] + widget->size[UI_Axis_Y];
    }
    rect[0] = MAX(rect[0], clip[0]);
    rect[1] = MAX(rect[1], clip[1]);
    rect[2] = MIN(rect[2], clip[2]);
    rect[3] = MIN(rect[3], clip[3]);
    if (rect[0] >= rect[2] || rect[1] >= rect[3]) {
        return;
    }
    if (state->hitRectCount >= UI_MAX_HIT_RECTS) {
        state->stats.hitRectOverflowCount += 1u;
        return;
    }
    UI_HitRect* hit = state->hitRects + state->hitRectCount;
    hit->key = key;
    hit->rect[0] = rect[0];
    hit->rect[1] = rect[1];
    hit->rect[2] = rect[2];
    hit->rect[3] = rect[3];
    hit->flags = flags;
    hit->rootSlot = rootSlot;
    state->hitRectCount += 1u;
}

static void ui_paint_widget_enter(UI_Context* ui, UI_Widget* widget, const F32* clip, U32 rootSlot) {
    Draw2DContext* draw = ui->draw2d;
    F32 minX = widget->pos[UI_Axis_X];
    F32 minY = widget->pos[UI_Axis_Y];
    F32 maxX = minX + widget->size[UI_Axis_X];
    F32 maxY = minY + widget->size[UI_Axis_Y];

    if (widget->key != 0ull && widget->retainedIndex != UI_NIL_INDEX) {
        UI_RetainedWidget* retained = ui->state->retained + widget->retainedIndex;
        retained->rect[0] = minX;
        retained->rect[1] = minY;
        retained->rect[2] = maxX;
        retained->rect[3] = maxY;
        U32 interactive = widget->flags & (UI_WidgetFlag_Clickable | UI_WidgetFlag_Scrollable | UI_WidgetFlag_Focusable);
        if (interactive) {
            ui_store_hit_rect(ui, widget, clip, rootSlot, widget->key, widget->flags, 0);
        }
    }

    if (widget->flags & UI_WidgetFlag_DrawBackground) {
        draw2d_rect(draw, UI_LAYER, minX, minY, maxX, maxY, widget->backgroundColor);
    }

    switch (widget->kind) {
        case UI_WidgetKind_Checkbox: {
            if (widget->kindParams[0] > 0.5f) {
                F32 inset = 4.0f;
                U32 color = (widget->kindParams[1] > 0.0f) ? UI_COLOR_ACCENT_HOT : UI_COLOR_ACCENT;
                draw2d_rect(draw, UI_LAYER, minX + inset, minY + inset, maxX - inset, maxY - inset, color);
            }
        }
        break;
        case UI_WidgetKind_Slider: {
            F32 t = widget->kindParams[0];
            F32 trackMinY = minY + (maxY - minY) * 0.5f - 2.0f;
            F32 trackMaxY = trackMinY + 4.0f;
            F32 half = UI_STYLE_SLIDER_THUMB_W * 0.5f;
            F32 usableMin = minX + half;
            F32 usableMax = maxX - half;
            F32 thumbX = usableMin + t * MAX(0.0f, usableMax - usableMin);
            draw2d_rect(draw, UI_LAYER, minX, trackMinY, maxX, trackMaxY, UI_COLOR_SCROLL_TRACK);
            draw2d_rect(draw, UI_LAYER, minX, trackMinY, thumbX, trackMaxY, UI_COLOR_ACCENT);
            U32 thumbColor = (widget->kindParams[1] > 0.0f) ? UI_COLOR_TEXT_BRIGHT : UI_COLOR_TEXT;
            draw2d_rect(draw, UI_LAYER, thumbX - half, minY, thumbX + half, maxY, thumbColor);
        }
        break;
        case UI_WidgetKind_Plot: {
            if (widget->plotValues && widget->plotCount != 0u) {
                F32 innerMinX = minX + 2.0f;
                F32 innerMaxX = maxX - 2.0f;
                F32 innerMinY = minY + 2.0f;
                F32 innerMaxY = maxY - 2.0f;
                F32 innerW = MAX(0.0f, innerMaxX - innerMinX);
                F32 innerH = MAX(0.0f, innerMaxY - innerMinY);
                F32 maxValue = 0.0f;
                for (U32 at = 0u; at < widget->plotCount; ++at) {
                    maxValue = MAX(maxValue, widget->plotValues[at]);
                }
                if (maxValue > 0.0f && innerW > 0.0f) {
                    F32 barWidth = innerW / (F32)widget->plotCount;
                    for (U32 at = 0u; at < widget->plotCount; ++at) {
                        U32 valueIndex = (widget->plotOffset + at) % widget->plotCount;
                        F32 value = widget->plotValues[valueIndex];
                        if (value <= 0.0f) {
                            continue;
                        }
                        F32 fraction = ui_saturate(value / maxValue);
                        F32 barMinX = innerMinX + (F32)at * barWidth;
                        F32 barMaxX = barMinX + MAX(1.0f, barWidth - 1.0f);
                        F32 barMinY = innerMaxY - fraction * innerH;
                        draw2d_rect(draw, UI_LAYER, barMinX, barMinY, barMaxX, innerMaxY, UI_COLOR_ACCENT);
                    }
                }
            }
        }
        break;
        case UI_WidgetKind_Meter: {
            F32 fraction = ui_saturate(widget->kindParams[0]);
            F32 inset = 1.0f;
            F32 innerW = MAX(0.0f, (maxX - minX) - 2.0f * inset);
            F32 fillMaxX = minX + inset + fraction * innerW;
            if (fillMaxX > minX + inset) {
                draw2d_rect(draw, UI_LAYER, minX + inset, minY + inset, fillMaxX, maxY - inset,
                            widget->kindBits);
            }
        }
        break;
        case UI_WidgetKind_AtlasPreview: {
            Draw2DQuad quad;
            quad.minX = minX + 2.0f;
            quad.minY = minY + 2.0f;
            quad.maxX = maxX - 2.0f;
            quad.maxY = maxY - 2.0f;
            quad.minU = 0.0f;
            quad.minV = 0.0f;
            quad.maxU = 1.0f;
            quad.maxV = 1.0f;
            quad.rgba8 = 0xFFFFFFFFu;
            draw2d_glyph_quads(draw, UI_LAYER, &quad, 1u, 0.0f, 0.0f);
        }
        break;
        case UI_WidgetKind_Edit: {
            F32 padX = widget->padding[UI_Axis_X];
            F32 innerMinX = minX + padX;
            F32 innerMaxX = maxX - padX;
            F32 scrollX = widget->kindParams[3];
            F32 textX = ui_floor(innerMinX - scrollX);
            F32 textY = ui_floor(minY + (widget->size[UI_Axis_Y] - widget->text.height) * 0.5f);

            draw2d_push_clip(ui->draw2d, innerMinX, minY, innerMaxX, maxY);
            if (widget->kindBits & UI_EditKindBit_Selection) {
                F32 selMin = innerMinX + widget->kindParams[1] - scrollX;
                F32 selMax = innerMinX + widget->kindParams[2] - scrollX;
                draw2d_rect(draw, UI_LAYER, selMin, minY + 3.0f, selMax, maxY - 3.0f, UI_COLOR_SELECTION);
            }
            if (widget->text.quadCount != 0u) {
                draw2d_glyph_run(draw, UI_LAYER, (const Draw2DQuad*)widget->text.quads,
                                 widget->text.quadCount, textX, textY, widget->textColor);
            }
            if ((widget->kindBits & UI_EditKindBit_Focused) && (widget->kindBits & UI_EditKindBit_CaretVisible)) {
                F32 caretX = ui_floor(innerMinX + widget->kindParams[0] - scrollX);
                draw2d_rect(draw, UI_LAYER, caretX, minY + 4.0f, caretX + 1.0f, maxY - 4.0f, UI_COLOR_CARET);
            }
            draw2d_pop_clip(ui->draw2d);
        }
        break;
        default: break;
    }

    if ((widget->flags & UI_WidgetFlag_DrawText) && widget->kind != UI_WidgetKind_Edit &&
        widget->text.quadCount != 0u) {
        F32 innerW = MAX(0.0f, widget->size[UI_Axis_X] - 2.0f * widget->padding[UI_Axis_X]);
        F32 slack = MAX(0.0f, innerW - widget->text.width);
        F32 textX = ui_floor(minX + widget->padding[UI_Axis_X] + widget->textAlign * slack);
        F32 textY = ui_floor(minY + (widget->size[UI_Axis_Y] - widget->text.height) * 0.5f);
        draw2d_glyph_run(ui->draw2d, UI_LAYER, (const Draw2DQuad*)widget->text.quads,
                         widget->text.quadCount, textX, textY, widget->textColor);
    }
}

static void ui_paint_widget_exit(UI_Context* ui, UI_Widget* widget, const F32* clip, U32 rootSlot) {
    Draw2DContext* draw = ui->draw2d;
    F32 minX = widget->pos[UI_Axis_X];
    F32 minY = widget->pos[UI_Axis_Y];
    F32 maxX = minX + widget->size[UI_Axis_X];
    F32 maxY = minY + widget->size[UI_Axis_Y];

    if (widget->kind == UI_WidgetKind_ScrollRegion && widget->retainedIndex != UI_NIL_INDEX &&
        widget->overflow[UI_Axis_Y] > 0.0f) {
        UI_RetainedWidget* retained = ui->state->retained + widget->retainedIndex;
        F32 content = retained->contentSize[UI_Axis_Y];
        F32 view = retained->viewSize[UI_Axis_Y];
        if (content > view && view > 0.0f) {
            F32 trackMinX = maxX - UI_STYLE_SCROLLBAR_W - 2.0f;
            F32 trackMaxX = maxX - 2.0f;
            F32 trackMinY = minY + 2.0f;
            F32 trackMaxY = maxY - 2.0f;
            F32 trackH = trackMaxY - trackMinY;
            F32 thumbH = CLAMP(trackH * view / content, UI_STYLE_SCROLL_MIN_THUMB, trackH);
            F32 maxScroll = content - view;
            F32 scrollT = (maxScroll > 0.0f) ? ui_saturate(retained->scroll[UI_Axis_Y] / maxScroll) : 0.0f;
            F32 thumbMinY = trackMinY + scrollT * (trackH - thumbH);

            UI_Key thumbKey = ui_key_derive(widget->key, "##thumb");
            U32 thumbColor = UI_COLOR_WIDGET_BORDER;
            if (ui->state->activeKey == thumbKey) {
                thumbColor = UI_COLOR_TEXT_BRIGHT;
            } else if (ui->state->hotKey == thumbKey) {
                thumbColor = UI_COLOR_TEXT;
            }
            draw2d_rect(draw, UI_LAYER, trackMinX, trackMinY, trackMaxX, trackMaxY, UI_COLOR_SCROLL_TRACK);
            draw2d_rect(draw, UI_LAYER, trackMinX, thumbMinY, trackMaxX, thumbMinY + thumbH, thumbColor);

            F32 thumbRect[4] = {trackMinX - 2.0f, thumbMinY, trackMaxX + 2.0f, thumbMinY + thumbH};
            ui_store_hit_rect(ui, widget, clip, rootSlot, thumbKey, UI_WidgetFlag_Clickable, thumbRect);
        }
    }

    if (widget->flags & UI_WidgetFlag_DrawBorder) {
        F32 thickness = (widget->borderThickness > 0.0f) ? widget->borderThickness : 1.0f;
        draw2d_box(draw, UI_LAYER, minX, minY, maxX, maxY, thickness, widget->borderColor);
    }
}

static void ui_paint_root(UI_Context* ui, U32 rootIndex, UI_PaintFrame* frames) {
    UI_Widget* root = ui->widgets + rootIndex;
    U32 rootSlot = root->rootSlot;

    U32 depth = 0u;
    UI_PaintFrame* frame = frames + depth;
    frame->widget = rootIndex;
    frame->nextChild = root->firstChild;
    frame->clip[0] = -1.0e9f;
    frame->clip[1] = -1.0e9f;
    frame->clip[2] = 1.0e9f;
    frame->clip[3] = 1.0e9f;

    ui_paint_widget_enter(ui, root, frame->clip, rootSlot);
    if (root->flags & UI_WidgetFlag_Clip) {
        draw2d_push_clip(ui->draw2d, root->pos[0], root->pos[1],
                         root->pos[0] + root->size[0], root->pos[1] + root->size[1]);
    }

    for (;;) {
        frame = frames + depth;
        if (frame->nextChild != UI_NIL_INDEX) {
            U32 childIndex = frame->nextChild;
            UI_Widget* child = ui->widgets + childIndex;
            frame->nextChild = child->nextSibling;

            UI_Widget* parent = ui->widgets + frame->widget;
            F32 childClip[4];
            childClip[0] = frame->clip[0];
            childClip[1] = frame->clip[1];
            childClip[2] = frame->clip[2];
            childClip[3] = frame->clip[3];
            if (parent->flags & UI_WidgetFlag_Clip) {
                childClip[0] = MAX(childClip[0], parent->pos[0]);
                childClip[1] = MAX(childClip[1], parent->pos[1]);
                childClip[2] = MIN(childClip[2], parent->pos[0] + parent->size[0]);
                childClip[3] = MIN(childClip[3], parent->pos[1] + parent->size[1]);
            }

            ui_paint_widget_enter(ui, child, childClip, rootSlot);
            if (child->firstChild != UI_NIL_INDEX || (child->flags & (UI_WidgetFlag_Clip | UI_WidgetFlag_DrawBorder)) ||
                child->kind == UI_WidgetKind_ScrollRegion) {
                if (depth + 1u >= UI_PARENT_STACK_DEPTH) {
                    ui_paint_widget_exit(ui, child, childClip, rootSlot);
                    continue;
                }
                if (child->flags & UI_WidgetFlag_Clip) {
                    draw2d_push_clip(ui->draw2d, child->pos[0], child->pos[1],
                                     child->pos[0] + child->size[0], child->pos[1] + child->size[1]);
                }
                depth += 1u;
                UI_PaintFrame* childFrame = frames + depth;
                childFrame->widget = childIndex;
                childFrame->nextChild = child->firstChild;
                childFrame->clip[0] = childClip[0];
                childFrame->clip[1] = childClip[1];
                childFrame->clip[2] = childClip[2];
                childFrame->clip[3] = childClip[3];
            } else {
                ui_paint_widget_exit(ui, child, childClip, rootSlot);
            }
        } else {
            UI_Widget* widget = ui->widgets + frame->widget;
            if (widget->flags & UI_WidgetFlag_Clip) {
                draw2d_pop_clip(ui->draw2d);
            }
            ui_paint_widget_exit(ui, widget, frame->clip, rootSlot);
            if (depth == 0u) {
                break;
            }
            depth -= 1u;
        }
    }
}

// ////////////////////////
// Frame end

UI_Output ui_end(UI_Context* ui) {
    UI_Output output = {};
    if (!ui) {
        return output;
    }

    UI_State* state = ui->state;
    ASSERT_DEBUG(ui->parentDepth == 0u);

    U32 rootIndices[UI_MAX_ROOTS];
    U32 rootCount = 0u;
    for (U32 index = 0; index < ui->widgetCount && rootCount < UI_MAX_ROOTS; ++index) {
        if (ui->widgets[index].parent == UI_NIL_INDEX) {
            rootIndices[rootCount++] = index;
        }
    }

    U32* solveStack = ARENA_PUSH_ARRAY(ui->frameArena, U32, UI_MAX_WIDGETS);
    UI_PaintFrame* paintFrames = ARENA_PUSH_ARRAY(ui->frameArena, UI_PaintFrame, UI_PARENT_STACK_DEPTH);
    if (!solveStack || !paintFrames) {
        return output;
    }

    {
        PROF_SCOPE("ui solve");
        for (U32 rootAt = 0; rootAt < rootCount; ++rootAt) {
            ui_solve_root(ui, rootIndices[rootAt], solveStack);
        }
    }

    for (U32 sortAt = 1; sortAt < rootCount; ++sortAt) {
        U32 hold = rootIndices[sortAt];
        U32 holdZ = (ui->widgets[hold].rootSlot < UI_MAX_ROOTS) ? state->roots[ui->widgets[hold].rootSlot].zIndex : 0u;
        U32 shift = sortAt;
        while (shift > 0u) {
            U32 prev = rootIndices[shift - 1u];
            U32 prevZ = (ui->widgets[prev].rootSlot < UI_MAX_ROOTS) ? state->roots[ui->widgets[prev].rootSlot].zIndex : 0u;
            if (prevZ <= holdZ) {
                break;
            }
            rootIndices[shift] = prev;
            shift -= 1u;
        }
        rootIndices[shift] = hold;
    }

    state->hitRectCount = 0u;
    if (ui->draw2d) {
        PROF_SCOPE("ui paint");
        for (U32 rootAt = 0; rootAt < rootCount; ++rootAt) {
            ui_paint_root(ui, rootIndices[rootAt], paintFrames);
        }
    }

    if (state->activeKey != 0ull && state->activeKey != UI_DEAD_KEY && !ui->activeTouched) {
        state->activeKey = 0ull;
    }
    if (state->focusKey != 0ull && !ui->focusTouched) {
        state->focusKey = 0ull;
        state->edit.editKey = 0ull;
    }

    for (U32 rootSlot = 0; rootSlot < state->rootCount; ++rootSlot) {
        if (state->roots[rootSlot].key == 0ull) {
            continue;
        }
        B32 touched = 0;
        for (U32 rootAt = 0; rootAt < rootCount; ++rootAt) {
            UI_Widget* root = ui->widgets + rootIndices[rootAt];
            if (root->rootSlot == rootSlot && root->key == state->roots[rootSlot].key) {
                touched = 1;
                break;
            }
        }
        if (!touched) {
            state->roots[rootSlot].key = 0ull;
            state->roots[rootSlot].zIndex = 0u;
        }
    }

    U32 retainedCount = 0u;
    for (U32 index = 0; index < UI_MAX_RETAINED; ++index) {
        if (state->retained[index].key != 0ull) {
            retainedCount += 1u;
        }
    }
    state->stats.widgetCount = ui->widgetCount;
    state->stats.retainedCount = retainedCount;
    state->stats.hitRectCount = state->hitRectCount;

    output.uploads = ui->uploads;
    output.uploadCount = ui->uploadCount;
    output.wantMouse = state->wantMouse;
    output.wantKeyboard = state->wantKeyboard;
    output.wantTextInput = state->wantTextInput;
    return output;
}

// ////////////////////////
// Containers

void ui_panel_begin(UI_Context* ui, StringU8 label, const UI_PanelDesc* desc) {
    if (!ui || !desc) {
        return;
    }
    ASSERT_DEBUG(ui->parentDepth == 0u);

    UI_Key key = ui_key_from_label(ui, label);
    U32 index = ui_widget_add(ui, key,
                              UI_WidgetFlag_Clickable | UI_WidgetFlag_DrawBackground |
                              UI_WidgetFlag_DrawBorder | UI_WidgetFlag_BringToFront,
                              UI_WidgetKind_Plain, desc->width, desc->height);
    UI_Widget* panel = ui_widget(ui, index);
    panel->layoutAxis = UI_Axis_Y;
    panel->padding[UI_Axis_X] = UI_STYLE_PANEL_PADDING;
    panel->padding[UI_Axis_Y] = UI_STYLE_PANEL_PADDING;
    panel->childGap = UI_STYLE_PANEL_GAP;
    panel->backgroundColor = UI_COLOR_PANEL_BG;
    panel->borderColor = UI_COLOR_PANEL_BORDER;
    panel->borderThickness = 1.0f;
    panel->rootAnchor[UI_Axis_X] = desc->anchorX;
    panel->rootAnchor[UI_Axis_Y] = desc->anchorY;
    panel->rootOffset[UI_Axis_X] = desc->offsetX;
    panel->rootOffset[UI_Axis_Y] = desc->offsetY;

    UI_State* state = ui->state;
    U32 rootSlot = UI_NIL_INDEX;
    U32 freeSlot = UI_NIL_INDEX;
    for (U32 slot = 0; slot < state->rootCount; ++slot) {
        if (state->roots[slot].key == key) {
            rootSlot = slot;
            break;
        }
        if (freeSlot == UI_NIL_INDEX && state->roots[slot].key == 0ull) {
            freeSlot = slot;
        }
    }
    if (rootSlot == UI_NIL_INDEX) {
        if (freeSlot == UI_NIL_INDEX && state->rootCount < UI_MAX_ROOTS) {
            freeSlot = state->rootCount;
            state->rootCount += 1u;
        }
        if (freeSlot != UI_NIL_INDEX) {
            rootSlot = freeSlot;
            state->zCounter += 1u;
            state->roots[rootSlot].key = key;
            state->roots[rootSlot].zIndex = state->zCounter;
        }
    }
    panel->rootSlot = rootSlot;

    ui_push_parent(ui, index);

    StringU8 title = ui_display_from_label(label);
    if (title.size != 0u) {
        U32 titleIndex = ui_widget_add(ui, 0ull, 0u, UI_WidgetKind_Plain, ui_text_size(), ui_text_size());
        ui_widget_set_text(ui, titleIndex, title, UI_STYLE_TEXT_SIZE, UI_COLOR_TEXT_BRIGHT);
    }
}

void ui_panel_end(UI_Context* ui) {
    if (!ui) {
        return;
    }
    ui_pop_parent(ui);
}

static U32 ui_container_begin(UI_Context* ui, U32 axis, UI_Size width, UI_Size height) {
    U32 index = ui_widget_add(ui, 0ull, 0u, UI_WidgetKind_Plain, width, height);
    UI_Widget* widget = ui_widget(ui, index);
    widget->layoutAxis = axis;
    widget->childGap = 8.0f;
    widget->alignCross = (axis == UI_Axis_X) ? 0.5f : 0.0f;
    ui_push_parent(ui, index);
    return index;
}

void ui_row_begin(UI_Context* ui, UI_Size width, UI_Size height) {
    if (!ui) {
        return;
    }
    ui_container_begin(ui, UI_Axis_X, width, height);
}

UI_Signal ui_row_begin_keyed(UI_Context* ui, StringU8 label, UI_Size width, UI_Size height, B32 highlighted) {
    UI_Signal signal = {};
    if (!ui) {
        return signal;
    }

    UI_Key key = ui_key_from_label(ui, label);
    signal = ui_signal_for(ui, key);

    U32 index = ui_widget_add(ui, key, UI_WidgetFlag_Clickable | UI_WidgetFlag_DrawBackground,
                              UI_WidgetKind_Plain, width, height);
    UI_Widget* row = ui_widget(ui, index);
    row->layoutAxis = UI_Axis_X;
    row->childGap = 8.0f;
    row->alignCross = 0.5f;

    F32 hotT = (row->retainedIndex != UI_NIL_INDEX) ? ui->state->retained[row->retainedIndex].hotT : 0.0f;
    U32 base = highlighted ? UI_COLOR_WIDGET_BG : 0x00000000u;
    row->backgroundColor = ui_color_lerp(base, UI_COLOR_WIDGET_BG_HOT, hotT * 0.7f);

    ui_push_parent(ui, index);
    return signal;
}

void ui_plot(UI_Context* ui, const F32* values, U32 count, U32 offset, UI_Size width, UI_Size height) {
    if (!ui) {
        return;
    }
    U32 index = ui_widget_add(ui, 0ull, UI_WidgetFlag_DrawBackground | UI_WidgetFlag_DrawBorder,
                              UI_WidgetKind_Plot, width, height);
    UI_Widget* widget = ui_widget(ui, index);
    widget->backgroundColor = UI_COLOR_EDIT_BG;
    widget->borderColor = UI_COLOR_PANEL_BORDER;
    widget->plotValues = values;
    widget->plotCount = count;
    widget->plotOffset = offset;
}

void ui_row_end(UI_Context* ui) {
    if (!ui) {
        return;
    }
    ui_pop_parent(ui);
}

void ui_column_begin(UI_Context* ui, UI_Size width, UI_Size height) {
    if (!ui) {
        return;
    }
    ui_container_begin(ui, UI_Axis_Y, width, height);
}

void ui_column_end(UI_Context* ui) {
    if (!ui) {
        return;
    }
    ui_pop_parent(ui);
}

void ui_scroll_begin(UI_Context* ui, StringU8 label, UI_Size width, UI_Size height) {
    if (!ui) {
        return;
    }

    UI_Key key = ui_key_from_label(ui, label);
    U32 index = ui_widget_add(ui, key,
                              UI_WidgetFlag_Clickable | UI_WidgetFlag_Scrollable |
                              UI_WidgetFlag_Clip | UI_WidgetFlag_DrawBackground | UI_WidgetFlag_DrawBorder,
                              UI_WidgetKind_ScrollRegion, width, height);
    UI_Widget* region = ui_widget(ui, index);
    region->layoutAxis = UI_Axis_Y;
    region->padding[UI_Axis_X] = 8.0f;
    region->padding[UI_Axis_Y] = 6.0f;
    region->childGap = 4.0f;
    region->backgroundColor = UI_COLOR_EDIT_BG;
    region->borderColor = UI_COLOR_PANEL_BORDER;

    if (key != 0ull && region->retainedIndex != UI_NIL_INDEX) {
        UI_State* state = ui->state;
        UI_RetainedWidget* retained = state->retained + region->retainedIndex;
        UI_Key thumbKey = ui_key_derive(key, "##thumb");
        UI_Signal thumbSignal = ui_signal_for(ui, thumbKey);
        if (thumbSignal.pressed) {
            state->dragStartValue[1] = retained->scrollTarget[UI_Axis_Y];
        }
        if (thumbSignal.dragging) {
            F32 content = retained->contentSize[UI_Axis_Y];
            F32 view = retained->viewSize[UI_Axis_Y];
            if (content > view && view > 0.0f) {
                F32 trackH = view - 4.0f;
                F32 thumbH = CLAMP(trackH * view / content, UI_STYLE_SCROLL_MIN_THUMB, trackH);
                F32 denom = trackH - thumbH;
                if (denom > 0.0f) {
                    F32 target = state->dragStartValue[1] +
                                 thumbSignal.dragDeltaY * (content - view) / denom;
                    retained->scrollTarget[UI_Axis_Y] = CLAMP(target, 0.0f, content - view);
                    retained->scroll[UI_Axis_Y] = retained->scrollTarget[UI_Axis_Y];
                }
            }
        }
        if (state->activeKey == thumbKey) {
            ui->activeTouched = 1;
        }
    }

    ui_push_parent(ui, index);
}

void ui_scroll_end(UI_Context* ui) {
    if (!ui) {
        return;
    }
    ui_pop_parent(ui);
}

void ui_spacer(UI_Context* ui, UI_Size size) {
    if (!ui) {
        return;
    }
    U32 parentIndex = (ui->parentDepth != 0u) ? ui->parentStack[ui->parentDepth - 1u] : UI_NIL_INDEX;
    UI_Widget* parent = ui_widget(ui, parentIndex);
    UI_Size cross = ui_px(0.0f);
    if (parent->layoutAxis == UI_Axis_X) {
        ui_widget_add(ui, 0ull, 0u, UI_WidgetKind_Plain, size, cross);
    } else {
        ui_widget_add(ui, 0ull, 0u, UI_WidgetKind_Plain, cross, size);
    }
}

// ////////////////////////
// Widgets

void ui_label_colored(UI_Context* ui, StringU8 text, U32 rgba8) {
    if (!ui) {
        return;
    }
    U32 index = ui_widget_add(ui, 0ull, 0u, UI_WidgetKind_Plain, ui_text_size(), ui_text_size());
    ui_widget_set_text(ui, index, text, UI_STYLE_TEXT_SIZE, rgba8);
}

void ui_label(UI_Context* ui, StringU8 text) {
    ui_label_colored(ui, text, UI_COLOR_TEXT);
}

static U64 ui_value_args_key_(StringU8 fmt, const Str8FmtArg* args, U64 argCount) {
    U64 hash = 0xCBF29CE484222325ull;
    for (U64 at = 0u; at < fmt.size; ++at) {
        hash = (hash ^ (U64)fmt.data[at]) * 0x100000001B3ull;
    }
    for (U64 argIndex = 0u; argIndex < argCount; ++argIndex) {
        const Str8FmtArg* arg = args + argIndex;
        hash = (hash ^ (U64)arg->kind) * 0x100000001B3ull;
        switch (arg->kind) {
            case Str8FmtKind_STRINGU8: {
                for (U64 at = 0u; at < arg->stringU8Val.size; ++at) {
                    hash = (hash ^ (U64)arg->stringU8Val.data[at]) * 0x100000001B3ull;
                }
            } break;
            case Str8FmtKind_CSTR: {
                for (const char* at = arg->cstrVal; at && *at; ++at) {
                    hash = (hash ^ (U64)(U8)*at) * 0x100000001B3ull;
                }
            } break;
            case Str8FmtKind_CHAR: {
                hash = (hash ^ (U64)arg->charVal) * 0x100000001B3ull;
            } break;
            default: {
                hash = (hash ^ arg->u64Val) * 0x100000001B3ull;
            } break;
        }
    }
    hash ^= hash >> 33u;
    hash *= 0xFF51AFD7ED558CCDull;
    hash ^= hash >> 33u;
    hash *= 0xC4CEB9FE1A85EC53ull;
    hash ^= hash >> 33u;
    return hash ? hash : 1ull;
}

static void ui_label_value_impl_(UI_Context* ui, F32 cellWidth, F32 align, U32 rgba8,
                                 StringU8 fmt, const Str8FmtArg* args, U64 argCount) {
    if (!ui) {
        return;
    }
    UI_Size widthSize = (cellWidth > 0.0f) ? ui_px(cellWidth) : ui_text_size();
    UI_State* state = ui->state;
    U64 valueKey = ui_value_args_key_(fmt, args, argCount);
    U32 frame = (U32)state->frameIndex;

    UI_ValueRun* victim = 0;
    for (U32 probe = 0u; probe < UI_VALUE_RUN_PROBE; ++probe) {
        UI_ValueRun* entry = state->valueRuns + ((valueKey + probe) & (UI_VALUE_RUN_SLOTS - 1u));
        if (entry->valueKey == valueKey) {
            TextRunView view = {};
            if (ui->textContext &&
                text_run_resolve(ui->textContext, entry->runSlot, entry->runKey, &view)) {
                entry->lastUsedFrame = frame;
                state->stats.valueRunHits += 1u;
                U32 index = ui_widget_add(ui, 0ull, 0u, UI_WidgetKind_Plain, widthSize, ui_text_size());
                ui_widget(ui, index)->textAlign = align;
                ui_widget_set_run(ui, index, view, 1, rgba8);
                return;
            }
            state->stats.valueRunResolveFails += 1u;
            victim = entry;
            break;
        }
        if (entry->valueKey == 0ull) {
            victim = entry;
            break;
        }
        if (!victim || entry->lastUsedFrame < victim->lastUsedFrame) {
            victim = entry;
        }
    }

    StringU8 text = str8_fmt_(ui->frameArena, fmt, args, argCount);
    TextRunView view = ui_shape_text(ui, text, UI_STYLE_TEXT_SIZE);
    state->stats.valueRunMisses += 1u;
    B32 victimUsable = victim &&
        (victim->valueKey == 0ull || victim->valueKey == valueKey ||
         victim->lastUsedFrame + UI_VALUE_RUN_EXPIRE_FRAMES <= frame);
    if (!victimUsable || view.slot == TEXT_RUN_NO_SLOT) {
        state->stats.valueRunUninsertable += 1u;
        if (!victimUsable) {
            state->stats.valueRunNoVictim += 1u;
        }
        if (view.slot == TEXT_RUN_NO_SLOT) {
            state->stats.valueRunNoSlot += 1u;
        }
    }
    if (victimUsable && view.slot != TEXT_RUN_NO_SLOT) {
        victim->valueKey = valueKey;
        victim->runKey = view.key;
        victim->runSlot = view.slot;
        victim->lastUsedFrame = frame;
        victim->width = view.width;
        victim->height = view.height;
    }
    U32 index = ui_widget_add(ui, 0ull, 0u, UI_WidgetKind_Plain, widthSize, ui_text_size());
    ui_widget(ui, index)->textAlign = align;
    ui_widget_set_run(ui, index, view, text.size != 0u, rgba8);
}

void ui_label_value_(UI_Context* ui, U32 rgba8, StringU8 fmt, const Str8FmtArg* args, U64 argCount) {
    ui_label_value_impl_(ui, 0.0f, 0.0f, rgba8, fmt, args, argCount);
}

void ui_label_value_cell_(UI_Context* ui, F32 cellWidth, F32 align, U32 rgba8, StringU8 fmt,
                          const Str8FmtArg* args, U64 argCount) {
    ui_label_value_impl_(ui, cellWidth, align, rgba8, fmt, args, argCount);
}

void ui_meter(UI_Context* ui, F32 fraction, U32 fillRgba8, UI_Size width, UI_Size height) {
    if (!ui) {
        return;
    }
    U32 index = ui_widget_add(ui, 0ull, UI_WidgetFlag_DrawBackground | UI_WidgetFlag_DrawBorder,
                              UI_WidgetKind_Meter, width, height);
    UI_Widget* widget = ui_widget(ui, index);
    widget->backgroundColor = UI_COLOR_EDIT_BG;
    widget->borderColor = UI_COLOR_PANEL_BORDER;
    widget->kindParams[0] = fraction;
    widget->kindBits = fillRgba8;
}

void ui_atlas_preview(UI_Context* ui, UI_Size width, UI_Size height) {
    if (!ui) {
        return;
    }
    U32 index = ui_widget_add(ui, 0ull, UI_WidgetFlag_DrawBackground | UI_WidgetFlag_DrawBorder,
                              UI_WidgetKind_AtlasPreview, width, height);
    UI_Widget* widget = ui_widget(ui, index);
    widget->backgroundColor = UI_COLOR_EDIT_BG;
    widget->borderColor = UI_COLOR_PANEL_BORDER;
}

UI_Signal ui_button(UI_Context* ui, StringU8 label) {
    UI_Signal signal = {};
    if (!ui) {
        return signal;
    }

    UI_Key key = ui_key_from_label(ui, label);
    signal = ui_signal_for(ui, key);

    U32 index = ui_widget_add(ui, key,
                              UI_WidgetFlag_Clickable | UI_WidgetFlag_DrawBackground | UI_WidgetFlag_DrawBorder,
                              UI_WidgetKind_Button, ui_text_size(), ui_text_size());
    UI_Widget* widget = ui_widget(ui, index);
    widget->padding[UI_Axis_X] = UI_STYLE_BUTTON_PAD_X;
    widget->padding[UI_Axis_Y] = UI_STYLE_BUTTON_PAD_Y;
    widget->textAlign = 0.5f;
    widget->borderColor = UI_COLOR_WIDGET_BORDER;

    F32 hotT = 0.0f;
    F32 activeT = 0.0f;
    if (widget->retainedIndex != UI_NIL_INDEX) {
        hotT = ui->state->retained[widget->retainedIndex].hotT;
        activeT = ui->state->retained[widget->retainedIndex].activeT;
    }
    U32 background = ui_color_lerp(UI_COLOR_WIDGET_BG, UI_COLOR_WIDGET_BG_HOT, hotT);
    background = ui_color_lerp(background, UI_COLOR_WIDGET_BG_ACTIVE, activeT);
    widget->backgroundColor = background;

    U32 textColor = (signal.hovering || signal.dragging) ? UI_COLOR_TEXT_BRIGHT : UI_COLOR_TEXT;
    ui_widget_set_text(ui, index, ui_display_from_label(label), UI_STYLE_TEXT_SIZE, textColor);
    return signal;
}

B32 ui_checkbox(UI_Context* ui, StringU8 label, B32* value) {
    if (!ui || !value) {
        return 0;
    }

    UI_Key key = ui_key_from_label(ui, label);
    UI_Signal signal = ui_signal_for(ui, key);
    B32 changed = 0;
    if (signal.clicked) {
        *value = !*value;
        changed = 1;
    }

    U32 rowIndex = ui_widget_add(ui, key, UI_WidgetFlag_Clickable, UI_WidgetKind_Plain, ui_fit(), ui_fit());
    UI_Widget* row = ui_widget(ui, rowIndex);
    row->layoutAxis = UI_Axis_X;
    row->childGap = 8.0f;
    row->alignCross = 0.5f;
    ui_push_parent(ui, rowIndex);

    F32 hotT = (row->retainedIndex != UI_NIL_INDEX) ? ui->state->retained[row->retainedIndex].hotT : 0.0f;
    U32 boxIndex = ui_widget_add(ui, 0ull, UI_WidgetFlag_DrawBackground | UI_WidgetFlag_DrawBorder,
                                 UI_WidgetKind_Checkbox, ui_px(UI_STYLE_CHECK_SIZE), ui_px(UI_STYLE_CHECK_SIZE));
    UI_Widget* box = ui_widget(ui, boxIndex);
    box->backgroundColor = ui_color_lerp(UI_COLOR_EDIT_BG, UI_COLOR_WIDGET_BG_HOT, hotT);
    box->borderColor = UI_COLOR_WIDGET_BORDER;
    box->kindParams[0] = (*value) ? 1.0f : 0.0f;
    box->kindParams[1] = signal.hovering ? 1.0f : 0.0f;

    U32 labelIndex = ui_widget_add(ui, 0ull, 0u, UI_WidgetKind_Plain, ui_text_size(), ui_text_size());
    ui_widget_set_text(ui, labelIndex, ui_display_from_label(label), UI_STYLE_TEXT_SIZE,
                       signal.hovering ? UI_COLOR_TEXT_BRIGHT : UI_COLOR_TEXT);

    ui_pop_parent(ui);
    return changed;
}

B32 ui_slider(UI_Context* ui, StringU8 label, F32* value, F32 minValue, F32 maxValue) {
    if (!ui || !value || maxValue <= minValue) {
        return 0;
    }

    UI_Key key = ui_key_from_label(ui, label);
    UI_Signal signal = ui_signal_for(ui, key);
    UI_State* state = ui->state;

    F32 clamped = CLAMP(*value, minValue, maxValue);
    F32 t = (clamped - minValue) / (maxValue - minValue);
    B32 changed = 0;

    if (signal.hasPrevRect) {
        F32 half = UI_STYLE_SLIDER_THUMB_W * 0.5f;
        F32 usableMin = signal.prevRect[0] + half;
        F32 usableMax = signal.prevRect[2] - half;
        F32 usable = usableMax - usableMin;
        if (signal.pressed && usable > 0.0f) {
            F32 thumbX = usableMin + t * usable;
            F32 fromThumb = state->dragStartMouse[0] - thumbX;
            state->dragStartValue[0] = (fromThumb >= -half - 1.0f && fromThumb <= half + 1.0f) ? fromThumb : 0.0f;
        }
        if (signal.dragging && usable > 0.0f) {
            F32 newT = ui_saturate((state->mouseX - state->dragStartValue[0] - usableMin) / usable);
            F32 newValue = minValue + newT * (maxValue - minValue);
            if (newValue != *value) {
                *value = newValue;
                changed = 1;
            }
            t = newT;
        }
    }

    ui_container_begin(ui, UI_Axis_X, ui_grow(1.0f), ui_px(UI_STYLE_ROW_HEIGHT));

    U32 labelIndex = ui_widget_add(ui, 0ull, 0u, UI_WidgetKind_Plain, ui_text_size(), ui_text_size());
    ui_widget_set_text(ui, labelIndex, ui_display_from_label(label), UI_STYLE_TEXT_SIZE, UI_COLOR_TEXT);

    U32 trackIndex = ui_widget_add(ui, key, UI_WidgetFlag_Clickable, UI_WidgetKind_Slider,
                                   ui_grow(1.0f), ui_px(UI_STYLE_SLIDER_TRACK_H));
    UI_Widget* track = ui_widget(ui, trackIndex);
    track->kindParams[0] = t;
    track->kindParams[1] = (signal.hovering || signal.dragging) ? 1.0f : 0.0f;

    StringU8 valueText = str8_from_F64(ui->frameArena, (F64)*value, 2);
    U32 valueIndex = ui_widget_add(ui, 0ull, 0u, UI_WidgetKind_Plain, ui_px(64.0f), ui_text_size());
    UI_Widget* valueWidget = ui_widget(ui, valueIndex);
    valueWidget->textAlign = 1.0f;
    ui_widget_set_text(ui, valueIndex, valueText, UI_STYLE_TEXT_SIZE, UI_COLOR_TEXT_DIM);

    ui_pop_parent(ui);
    return changed;
}

// ////////////////////////
// Text edit

static U32 ui_utf8_decode(const U8* data, U32 size, U32* outCodepoints, U32 capacity) {
    U32 count = 0u;
    U32 at = 0u;
    while (at < size && count < capacity) {
        U8 byte = data[at];
        U32 codepoint = 0xFFFDu;
        U32 advance = 1u;
        if (byte < 0x80u) {
            codepoint = byte;
        } else if ((byte & 0xE0u) == 0xC0u && at + 1u < size) {
            codepoint = ((U32)(byte & 0x1Fu) << 6u) | (U32)(data[at + 1u] & 0x3Fu);
            advance = 2u;
        } else if ((byte & 0xF0u) == 0xE0u && at + 2u < size) {
            codepoint = ((U32)(byte & 0x0Fu) << 12u) |
                        ((U32)(data[at + 1u] & 0x3Fu) << 6u) |
                        (U32)(data[at + 2u] & 0x3Fu);
            advance = 3u;
        } else if ((byte & 0xF8u) == 0xF0u && at + 3u < size) {
            codepoint = ((U32)(byte & 0x07u) << 18u) |
                        ((U32)(data[at + 1u] & 0x3Fu) << 12u) |
                        ((U32)(data[at + 2u] & 0x3Fu) << 6u) |
                        (U32)(data[at + 3u] & 0x3Fu);
            advance = 4u;
        }
        outCodepoints[count++] = codepoint;
        at += advance;
    }
    return count;
}

static U32 ui_utf8_encoded_size(U32 codepoint) {
    if (codepoint < 0x80u) { return 1u; }
    if (codepoint < 0x800u) { return 2u; }
    if (codepoint < 0x10000u) { return 3u; }
    return 4u;
}

static U32 ui_utf8_encode(const U32* codepoints, U32 count, U8* outData, U32 capacity) {
    U32 at = 0u;
    for (U32 index = 0; index < count; ++index) {
        U32 codepoint = codepoints[index];
        U32 needed = ui_utf8_encoded_size(codepoint);
        if (at + needed > capacity) {
            break;
        }
        if (needed == 1u) {
            outData[at++] = (U8)codepoint;
        } else if (needed == 2u) {
            outData[at++] = (U8)(0xC0u | (codepoint >> 6u));
            outData[at++] = (U8)(0x80u | (codepoint & 0x3Fu));
        } else if (needed == 3u) {
            outData[at++] = (U8)(0xE0u | (codepoint >> 12u));
            outData[at++] = (U8)(0x80u | ((codepoint >> 6u) & 0x3Fu));
            outData[at++] = (U8)(0x80u | (codepoint & 0x3Fu));
        } else {
            outData[at++] = (U8)(0xF0u | (codepoint >> 18u));
            outData[at++] = (U8)(0x80u | ((codepoint >> 12u) & 0x3Fu));
            outData[at++] = (U8)(0x80u | ((codepoint >> 6u) & 0x3Fu));
            outData[at++] = (U8)(0x80u | (codepoint & 0x3Fu));
        }
    }
    return at;
}

static B32 ui_codepoint_is_space(U32 codepoint) {
    return codepoint == (U32)' ' || codepoint == (U32)'\t';
}

static U32 ui_edit_word_left(const UI_EditState* edit, U32 from) {
    U32 at = from;
    while (at > 0u && ui_codepoint_is_space(edit->codepoints[at - 1u])) {
        at -= 1u;
    }
    while (at > 0u && !ui_codepoint_is_space(edit->codepoints[at - 1u])) {
        at -= 1u;
    }
    return at;
}

static U32 ui_edit_word_right(const UI_EditState* edit, U32 from) {
    U32 at = from;
    while (at < edit->length && !ui_codepoint_is_space(edit->codepoints[at])) {
        at += 1u;
    }
    while (at < edit->length && ui_codepoint_is_space(edit->codepoints[at])) {
        at += 1u;
    }
    return at;
}

static void ui_edit_delete_range(UI_EditState* edit, U32 start, U32 end) {
    if (start >= end || end > edit->length) {
        return;
    }
    for (U32 at = start; at + (end - start) < edit->length; ++at) {
        edit->codepoints[at] = edit->codepoints[at + (end - start)];
    }
    edit->length -= (end - start);
    edit->cursor = start;
    edit->mark = start;
}

static B32 ui_edit_delete_selection(UI_EditState* edit) {
    if (edit->cursor == edit->mark) {
        return 0;
    }
    U32 start = MIN(edit->cursor, edit->mark);
    U32 end = MAX(edit->cursor, edit->mark);
    ui_edit_delete_range(edit, start, end);
    return 1;
}

static U32 ui_edit_encoded_length(const UI_EditState* edit) {
    U32 total = 0u;
    for (U32 index = 0; index < edit->length; ++index) {
        total += ui_utf8_encoded_size(edit->codepoints[index]);
    }
    return total;
}

static F32 ui_edit_prefix_width(UI_Context* ui, const UI_EditState* edit, U32 count, F32 pixelSize) {
    if (count == 0u) {
        return 0.0f;
    }
    U8* scratch = ARENA_PUSH_ARRAY(ui->frameArena, U8, count * 4u);
    if (!scratch) {
        return 0.0f;
    }
    U32 encoded = ui_utf8_encode(edit->codepoints, count, scratch, count * 4u);
    StringU8 text = str8((U8*)scratch, encoded);
    return ui_measure_text_width(ui, text, pixelSize);
}

static U32 ui_edit_locate(UI_Context* ui, const UI_EditState* edit, F32 localX, F32 pixelSize) {
    if (edit->length == 0u || localX <= 0.0f) {
        return 0u;
    }
    U32 low = 0u;
    U32 high = edit->length;
    while (low < high) {
        U32 mid = (low + high + 1u) / 2u;
        F32 width = ui_edit_prefix_width(ui, edit, mid, pixelSize);
        if (width <= localX) {
            low = mid;
        } else {
            high = mid - 1u;
        }
    }
    if (low < edit->length) {
        F32 widthLow = ui_edit_prefix_width(ui, edit, low, pixelSize);
        F32 widthNext = ui_edit_prefix_width(ui, edit, low + 1u, pixelSize);
        if ((localX - widthLow) > (widthNext - localX)) {
            low += 1u;
        }
    }
    return low;
}

static B32 ui_edit_apply_event(UI_EditState* edit, const UI_TextEvent* event,
                               U32 byteCapacity, B32* outCommitted, B32* outRevert) {
    B32 shift = (event->flags & UI_TextEventFlag_Shift) != 0u;
    B32 word = (event->flags & UI_TextEventFlag_Word) != 0u;
    B32 changed = 0;

    switch (event->kind) {
        case UI_TextEventKind_Char: {
            ui_edit_delete_selection(edit);
            U32 needed = ui_utf8_encoded_size(event->codepoint);
            if (edit->length < UI_EDIT_CAPACITY - 1u &&
                ui_edit_encoded_length(edit) + needed < byteCapacity) {
                for (U32 at = edit->length; at > edit->cursor; --at) {
                    edit->codepoints[at] = edit->codepoints[at - 1u];
                }
                edit->codepoints[edit->cursor] = event->codepoint;
                edit->length += 1u;
                edit->cursor += 1u;
                edit->mark = edit->cursor;
            }
            changed = 1;
        }
        break;
        case UI_TextEventKind_Left: {
            U32 target;
            if (word) {
                target = ui_edit_word_left(edit, edit->cursor);
            } else if (!shift && edit->cursor != edit->mark) {
                target = MIN(edit->cursor, edit->mark);
            } else {
                target = (edit->cursor > 0u) ? edit->cursor - 1u : 0u;
            }
            edit->cursor = target;
            if (!shift) {
                edit->mark = edit->cursor;
            }
        }
        break;
        case UI_TextEventKind_Right: {
            U32 target;
            if (word) {
                target = ui_edit_word_right(edit, edit->cursor);
            } else if (!shift && edit->cursor != edit->mark) {
                target = MAX(edit->cursor, edit->mark);
            } else {
                target = (edit->cursor < edit->length) ? edit->cursor + 1u : edit->length;
            }
            edit->cursor = target;
            if (!shift) {
                edit->mark = edit->cursor;
            }
        }
        break;
        case UI_TextEventKind_Home: {
            edit->cursor = 0u;
            if (!shift) {
                edit->mark = 0u;
            }
        }
        break;
        case UI_TextEventKind_End: {
            edit->cursor = edit->length;
            if (!shift) {
                edit->mark = edit->cursor;
            }
        }
        break;
        case UI_TextEventKind_Backspace: {
            if (!ui_edit_delete_selection(edit)) {
                U32 start = word ? ui_edit_word_left(edit, edit->cursor)
                                 : ((edit->cursor > 0u) ? edit->cursor - 1u : 0u);
                ui_edit_delete_range(edit, start, edit->cursor);
            }
            changed = 1;
        }
        break;
        case UI_TextEventKind_Delete: {
            if (!ui_edit_delete_selection(edit)) {
                U32 end = word ? ui_edit_word_right(edit, edit->cursor)
                               : ((edit->cursor < edit->length) ? edit->cursor + 1u : edit->length);
                ui_edit_delete_range(edit, edit->cursor, end);
            }
            changed = 1;
        }
        break;
        case UI_TextEventKind_SelectAll: {
            edit->mark = 0u;
            edit->cursor = edit->length;
        }
        break;
        case UI_TextEventKind_Enter: {
            *outCommitted = 1;
        }
        break;
        case UI_TextEventKind_Escape: {
            *outRevert = 1;
        }
        break;
        default: break;
    }
    return changed;
}

UI_EditResult ui_text_edit(UI_Context* ui, StringU8 label, U8* buffer, U32 bufferCapacity, U32* ioLength) {
    UI_EditResult result = {};
    if (!ui || !buffer || !ioLength || bufferCapacity == 0u) {
        return result;
    }

    UI_State* state = ui->state;
    UI_EditState* edit = &state->edit;
    UI_Key key = ui_key_from_label(ui, label);
    UI_Signal signal = ui_signal_for(ui, key);
    B32 focused = (state->focusKey == key);

    if (focused && edit->editKey != key) {
        edit->editKey = key;
        edit->length = ui_utf8_decode(buffer, *ioLength, edit->codepoints, UI_EDIT_CAPACITY - 1u);
        edit->cursor = edit->length;
        edit->mark = edit->length;
        edit->scrollX = 0.0f;
        edit->caretClock = -0.3f;
        MEMCPY(edit->revert, edit->codepoints, sizeof(U32) * edit->length);
        edit->revertLength = edit->length;
    }
    if (!focused && edit->editKey == key) {
        edit->editKey = 0ull;
    }

    B32 changed = 0;
    B32 committed = 0;
    if (focused && edit->editKey == key) {
        edit->caretClock += ui->deltaSeconds;
        if (edit->caretClock > UI_STYLE_CARET_BLINK_PERIOD) {
            edit->caretClock -= UI_STYLE_CARET_BLINK_PERIOD;
        }

        if (signal.pressed && signal.hasPrevRect) {
            F32 localX = state->dragStartMouse[0] - (signal.prevRect[0] + 8.0f) + edit->scrollX;
            if (ui->pressClickCount >= 2u) {
                edit->mark = 0u;
                edit->cursor = edit->length;
            } else {
                edit->cursor = ui_edit_locate(ui, edit, localX, UI_STYLE_TEXT_SIZE);
                edit->mark = edit->cursor;
            }
            edit->caretClock = -0.3f;
        }

        B32 revert = 0;
        for (U32 eventIndex = 0; eventIndex < ui->textEventCount; ++eventIndex) {
            const UI_TextEvent* event = ui->textEvents + eventIndex;
            B32 eventChanged = ui_edit_apply_event(edit, event, bufferCapacity, &committed, &revert);
            if (eventChanged ||
                event->kind == UI_TextEventKind_Left || event->kind == UI_TextEventKind_Right ||
                event->kind == UI_TextEventKind_Home || event->kind == UI_TextEventKind_End) {
                edit->caretClock = -0.3f;
            }
            changed |= eventChanged;
        }
        if (revert) {
            MEMCPY(edit->codepoints, edit->revert, sizeof(U32) * edit->revertLength);
            edit->length = edit->revertLength;
            edit->cursor = edit->length;
            edit->mark = edit->length;
            changed = 1;
            state->focusKey = 0ull;
            edit->editKey = 0ull;
            focused = 0;
        }
        if (committed) {
            state->focusKey = 0ull;
            edit->editKey = 0ull;
            focused = 0;
        }

        if (changed || committed) {
            *ioLength = ui_utf8_encode(edit->codepoints, edit->length, buffer, bufferCapacity - 1u);
            buffer[*ioLength] = 0u;
        } else if (edit->editKey == key) {
            *ioLength = ui_utf8_encode(edit->codepoints, edit->length, buffer, bufferCapacity - 1u);
            buffer[*ioLength] = 0u;
        }
    }

    U32 index = ui_widget_add(ui, key,
                              UI_WidgetFlag_Clickable | UI_WidgetFlag_Focusable |
                              UI_WidgetFlag_DrawBackground | UI_WidgetFlag_DrawBorder,
                              UI_WidgetKind_Edit, ui_grow(1.0f), ui_px(UI_STYLE_ROW_HEIGHT));
    UI_Widget* widget = ui_widget(ui, index);
    widget->padding[UI_Axis_X] = 8.0f;
    widget->backgroundColor = UI_COLOR_EDIT_BG;
    widget->borderColor = focused ? UI_COLOR_ACCENT : UI_COLOR_WIDGET_BORDER;

    StringU8 shown = str8((U8*)buffer, *ioLength);
    widget->text = ui_shape_text(ui, shown, UI_STYLE_TEXT_SIZE);
    widget->textColor = UI_COLOR_TEXT_BRIGHT;

    if (focused && edit->editKey == key) {
        F32 caretX = ui_edit_prefix_width(ui, edit, edit->cursor, UI_STYLE_TEXT_SIZE);
        if (signal.hasPrevRect) {
            F32 innerW = MAX(0.0f, (signal.prevRect[2] - signal.prevRect[0]) - 16.0f);
            F32 jump = innerW * 0.25f;
            if (caretX < edit->scrollX) {
                edit->scrollX = MAX(0.0f, caretX - jump);
            } else if (innerW > 0.0f && caretX - innerW >= edit->scrollX) {
                edit->scrollX = caretX - innerW + jump;
            }
        }

        widget->kindBits |= UI_EditKindBit_Focused;
        B32 caretVisible = (edit->caretClock <= 0.0f) ||
                           (edit->caretClock < UI_STYLE_CARET_BLINK_ON);
        if (caretVisible) {
            widget->kindBits |= UI_EditKindBit_CaretVisible;
        }
        widget->kindParams[0] = caretX;
        widget->kindParams[3] = edit->scrollX;
        if (edit->cursor != edit->mark) {
            U32 selMin = MIN(edit->cursor, edit->mark);
            U32 selMax = MAX(edit->cursor, edit->mark);
            widget->kindBits |= UI_EditKindBit_Selection;
            widget->kindParams[1] = ui_edit_prefix_width(ui, edit, selMin, UI_STYLE_TEXT_SIZE);
            widget->kindParams[2] = ui_edit_prefix_width(ui, edit, selMax, UI_STYLE_TEXT_SIZE);
        }
    }

    result.changed = changed;
    result.committed = committed;
    result.focused = focused;
    return result;
}
