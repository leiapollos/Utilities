#pragma once

#define UI_MAX_WIDGETS 4096u
#define UI_MAX_RETAINED 512u
#define UI_MAX_HIT_RECTS 1024u
#define UI_MAX_ROOTS 16u
#define UI_MAX_TEXT_EVENTS 64u
#define UI_MAX_UPLOADS 256u
#define UI_PARENT_STACK_DEPTH 32u
#define UI_EDIT_CAPACITY 256u
#define UI_NIL_INDEX 0xFFFFFFFFu

typedef U64 UI_Key;

enum UI_Axis {
    UI_Axis_X = 0,
    UI_Axis_Y,
    UI_Axis_COUNT,
};

enum UI_SizeKind {
    UI_SizeKind_Pixels = 0,
    UI_SizeKind_Text,
    UI_SizeKind_Pct,
    UI_SizeKind_Grow,
    UI_SizeKind_Fit,
};

struct UI_Size {
    UI_SizeKind kind;
    F32 value;
};

static inline UI_Size ui_px(F32 value) { UI_Size s = {UI_SizeKind_Pixels, value}; return s; }
static inline UI_Size ui_text_size() { UI_Size s = {UI_SizeKind_Text, 0.0f}; return s; }
static inline UI_Size ui_pct(F32 fraction) { UI_Size s = {UI_SizeKind_Pct, fraction}; return s; }
static inline UI_Size ui_grow(F32 weight) { UI_Size s = {UI_SizeKind_Grow, weight}; return s; }
static inline UI_Size ui_fit() { UI_Size s = {UI_SizeKind_Fit, 0.0f}; return s; }

enum UI_WidgetFlags {
    UI_WidgetFlag_Clickable      = (1u << 0u),
    UI_WidgetFlag_Scrollable     = (1u << 1u),
    UI_WidgetFlag_Focusable      = (1u << 2u),
    UI_WidgetFlag_Clip           = (1u << 3u),
    UI_WidgetFlag_DrawBackground = (1u << 4u),
    UI_WidgetFlag_DrawBorder     = (1u << 5u),
    UI_WidgetFlag_DrawText       = (1u << 6u),
    UI_WidgetFlag_BringToFront   = (1u << 7u),
};

enum UI_WidgetKind {
    UI_WidgetKind_Plain = 0,
    UI_WidgetKind_Button,
    UI_WidgetKind_Checkbox,
    UI_WidgetKind_Slider,
    UI_WidgetKind_ScrollRegion,
    UI_WidgetKind_Edit,
    UI_WidgetKind_Plot,
    UI_WidgetKind_Meter,
    UI_WidgetKind_AtlasPreview,
};

struct UI_Widget {
    UI_Key key;
    U32 flags;
    U32 kind;
    U32 parent;
    U32 firstChild;
    U32 lastChild;
    U32 nextSibling;
    U32 retainedIndex;
    U32 rootSlot;

    U32 layoutAxis;
    UI_Size semanticSize[UI_Axis_COUNT];
    F32 padding[UI_Axis_COUNT];
    F32 childGap;
    F32 alignMain;
    F32 alignCross;
    F32 rootAnchor[UI_Axis_COUNT];
    F32 rootOffset[UI_Axis_COUNT];

    U32 backgroundColor;
    U32 borderColor;
    F32 borderThickness;
    F32 textAlign;
    TextRunView text;
    U32 textColor;
    F32 kindParams[4];
    U32 kindBits;
    const F32* plotValues;
    U32 plotCount;
    U32 plotOffset;

    F32 size[UI_Axis_COUNT];
    F32 pos[UI_Axis_COUNT];
    F32 overflow[UI_Axis_COUNT];
};

struct UI_RetainedWidget {
    UI_Key key;
    U64 lastTouchedFrame;
    F32 rect[4];
    F32 hotT;
    F32 activeT;
    F32 scroll[UI_Axis_COUNT];
    F32 scrollTarget[UI_Axis_COUNT];
    F32 contentSize[UI_Axis_COUNT];
    F32 viewSize[UI_Axis_COUNT];
};

struct UI_HitRect {
    UI_Key key;
    F32 rect[4];
    U32 flags;
    U32 rootSlot;
};

struct UI_Root {
    UI_Key key;
    U32 zIndex;
};

struct UI_EditState {
    UI_Key editKey;
    U32 codepoints[UI_EDIT_CAPACITY];
    U32 length;
    U32 cursor;
    U32 mark;
    F32 scrollX;
    F32 caretClock;
    U32 revert[UI_EDIT_CAPACITY];
    U32 revertLength;
};

struct UI_Stats {
    U32 widgetCount;
    U32 retainedCount;
    U32 hitRectCount;
    U32 widgetOverflowCount;
    U32 duplicateKeyCount;
    U32 retainedEvictCount;
    U32 hitRectOverflowCount;
    U32 valueRunHits;
    U32 valueRunMisses;
    U32 valueRunUninsertable;
    U32 valueRunResolveFails;
    U32 valueRunNoVictim;
    U32 valueRunNoSlot;
};

#define UI_VALUE_RUN_SLOTS 1024u
#define UI_VALUE_RUN_PROBE 8u
#define UI_VALUE_RUN_EXPIRE_FRAMES 120u

struct UI_ValueRun {
    U64 valueKey;
    U64 runKey;
    U32 runSlot;
    U32 lastUsedFrame;
    F32 width;
    F32 height;
};

struct UI_State {
    U64 frameIndex;

    UI_Key hotKey;
    UI_Key activeKey;
    UI_Key focusKey;
    F32 mouseX;
    F32 mouseY;
    B32 mouseDown;
    F32 dragStartMouse[2];
    F32 dragStartValue[2];
    F32 clickClock;
    F32 lastClickX;
    F32 lastClickY;
    U32 clickCount;

    B32 wantMouse;
    B32 wantKeyboard;
    B32 wantTextInput;

    UI_Stats stats;

    U32 zCounter;
    U32 rootCount;
    UI_Root roots[UI_MAX_ROOTS];

    U32 hitRectCount;
    UI_HitRect hitRects[UI_MAX_HIT_RECTS];

    UI_RetainedWidget retained[UI_MAX_RETAINED];

    UI_ValueRun valueRuns[UI_VALUE_RUN_SLOTS];

    UI_EditState edit;
};

enum UI_TextEventKind {
    UI_TextEventKind_Char = 0,
    UI_TextEventKind_Left,
    UI_TextEventKind_Right,
    UI_TextEventKind_Home,
    UI_TextEventKind_End,
    UI_TextEventKind_Backspace,
    UI_TextEventKind_Delete,
    UI_TextEventKind_Enter,
    UI_TextEventKind_Escape,
    UI_TextEventKind_SelectAll,
};

enum UI_TextEventFlags {
    UI_TextEventFlag_Shift = (1u << 0u),
    UI_TextEventFlag_Word  = (1u << 1u),
};

struct UI_TextEvent {
    U32 kind;
    U32 flags;
    U32 codepoint;
};

struct UI_Context {
    UI_State* state;
    Arena* frameArena;
    TextContext* textContext;
    TextFont font;
    Draw2DContext* draw2d;
    F32 viewportWidth;
    F32 viewportHeight;
    F32 deltaSeconds;

    UI_Widget* widgets;
    U32 widgetCount;
    U32 parentStack[UI_PARENT_STACK_DEPTH];
    U32 parentDepth;

    UI_Key pressedKey;
    UI_Key clickedKey;
    UI_Key releasedKey;
    U32 pressClickCount;
    UI_TextEvent textEvents[UI_MAX_TEXT_EVENTS];
    U32 textEventCount;

    TextAtlasUpload* uploads;
    U32 uploadCount;

    B32 activeTouched;
    B32 focusTouched;
};

struct UI_BeginDesc {
    UI_State* state;
    Arena* frameArena;
    TextContext* textContext;
    TextFont font;
    Draw2DContext* draw2d;
    const OS_GraphicsEvent* events;
    U32 eventCount;
    F32 viewportWidth;
    F32 viewportHeight;
    F32 deltaSeconds;
};

struct UI_Output {
    const TextAtlasUpload* uploads;
    U32 uploadCount;
    B32 wantMouse;
    B32 wantKeyboard;
    B32 wantTextInput;
};

struct UI_Signal {
    B32 hovering;
    B32 pressed;
    B32 clicked;
    B32 released;
    B32 dragging;
    B32 doubleClicked;
    F32 dragDeltaX;
    F32 dragDeltaY;
    F32 prevRect[4];
    B32 hasPrevRect;
};

struct UI_PanelDesc {
    F32 anchorX;
    F32 anchorY;
    F32 offsetX;
    F32 offsetY;
    UI_Size width;
    UI_Size height;
};

struct UI_EditResult {
    B32 changed;
    B32 committed;
    B32 focused;
};

UI_Context* ui_begin(const UI_BeginDesc* desc);
UI_Output ui_end(UI_Context* ui);

void ui_panel_begin(UI_Context* ui, StringU8 label, const UI_PanelDesc* desc);
void ui_panel_end(UI_Context* ui);
void ui_row_begin(UI_Context* ui, UI_Size width, UI_Size height);
UI_Signal ui_row_begin_keyed(UI_Context* ui, StringU8 label, UI_Size width, UI_Size height, B32 highlighted);
void ui_row_end(UI_Context* ui);
void ui_plot(UI_Context* ui, const F32* values, U32 count, U32 offset, UI_Size width, UI_Size height);
void ui_column_begin(UI_Context* ui, UI_Size width, UI_Size height);
void ui_column_end(UI_Context* ui);
void ui_scroll_begin(UI_Context* ui, StringU8 label, UI_Size width, UI_Size height);
void ui_scroll_end(UI_Context* ui);
void ui_spacer(UI_Context* ui, UI_Size size);

void ui_label(UI_Context* ui, StringU8 text);
void ui_label_colored(UI_Context* ui, StringU8 text, U32 rgba8);
void ui_label_value_(UI_Context* ui, U32 rgba8, StringU8 fmt, const Str8FmtArg* args, U64 argCount);
void ui_label_value_cell_(UI_Context* ui, F32 cellWidth, F32 align, U32 rgba8, StringU8 fmt,
                          const Str8FmtArg* args, U64 argCount);
void ui_meter(UI_Context* ui, F32 fraction, U32 fillRgba8, UI_Size width, UI_Size height);
void ui_atlas_preview(UI_Context* ui, UI_Size width, UI_Size height);

#define ui_label_value(ui, rgba8, fmt, ...)                                                 \
    ([&](){                                                                                 \
        const Str8FmtArg uiValueArgs_[] = { Str8FmtArg(), __VA_ARGS__ };                    \
        const U64 uiValueCount_ = (U64)((sizeof(uiValueArgs_) / sizeof(Str8FmtArg)) - 1);   \
        const Str8FmtArg* uiValuePtr_ = (uiValueCount_ > 0) ? (uiValueArgs_ + 1) : nullptr; \
        ui_label_value_((ui), (rgba8), str8(fmt), uiValuePtr_, uiValueCount_);              \
    }())

#define ui_value_cell(ui, cellWidth, align, rgba8, fmt, ...)                                \
    ([&](){                                                                                 \
        const Str8FmtArg uiValueArgs_[] = { Str8FmtArg(), __VA_ARGS__ };                    \
        const U64 uiValueCount_ = (U64)((sizeof(uiValueArgs_) / sizeof(Str8FmtArg)) - 1);   \
        const Str8FmtArg* uiValuePtr_ = (uiValueCount_ > 0) ? (uiValueArgs_ + 1) : nullptr; \
        ui_label_value_cell_((ui), (cellWidth), (align), (rgba8), str8(fmt), uiValuePtr_,   \
                             uiValueCount_);                                                \
    }())
UI_Signal ui_button(UI_Context* ui, StringU8 label);
B32 ui_checkbox(UI_Context* ui, StringU8 label, B32* value);
B32 ui_slider(UI_Context* ui, StringU8 label, F32* value, F32 minValue, F32 maxValue);
UI_EditResult ui_text_edit(UI_Context* ui, StringU8 label, U8* buffer, U32 bufferCapacity, U32* ioLength);

UI_Signal ui_signal_for(UI_Context* ui, UI_Key key);
UI_Key ui_key_from_label(UI_Context* ui, StringU8 label);
