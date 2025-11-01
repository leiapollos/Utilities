//
// Created by André Leite on 26/07/2025.
//

#pragma once

// ////////////////////////
// Graphics System

enum OS_KeyModifiers {
    OS_KeyModifiers_None        = 0,
    OS_KeyModifiers_Shift       = (1u << 0),
    OS_KeyModifiers_Control     = (1u << 1),
    OS_KeyModifiers_Alt         = (1u << 2),
    OS_KeyModifiers_Super       = (1u << 3),
    OS_KeyModifiers_Function    = (1u << 4),
    OS_KeyModifiers_CapsLock    = (1u << 5),
};

enum OS_MouseButton {
    OS_MouseButton_None    = 0,
    OS_MouseButton_Left    = 1,
    OS_MouseButton_Right   = 2,
    OS_MouseButton_Middle  = 3,
    OS_MouseButton_Button4 = 4,
    OS_MouseButton_Button5 = 5,
};

enum OS_GraphicsEventType {
    OS_GraphicsEventType_None = 0,
    OS_GraphicsEventType_WindowShown,
    OS_GraphicsEventType_WindowClosed,
    OS_GraphicsEventType_WindowDestroyed,
    OS_GraphicsEventType_KeyDown,
    OS_GraphicsEventType_KeyUp,
    OS_GraphicsEventType_TextInput,
    OS_GraphicsEventType_MouseButtonDown,
    OS_GraphicsEventType_MouseButtonUp,
    OS_GraphicsEventType_MouseMove,
    OS_GraphicsEventType_MouseScroll,
};

struct OS_WindowHandle {
    U64* handle;
};

struct OS_GraphicsWindowEvent {
    U32 width;
    U32 height;
};

struct OS_GraphicsKeyEvent {
    U32 scanCode;
    U32 modifiers;
    U32 character;
    B32 isRepeat;
};

struct OS_GraphicsTextEvent {
    U32 codepoint;
    U32 modifiers;
};

struct OS_GraphicsMouseEvent {
    F32 x;
    F32 y;
    F32 deltaX;
    F32 deltaY;
    F32 globalX;
    F32 globalY;
    U32 modifiers;
    enum OS_MouseButton button;
    U32 clickCount;
    B32 isInWindow;
};

struct OS_GraphicsEvent {
    enum OS_GraphicsEventType type;
    OS_WindowHandle window;

    union {
        OS_GraphicsWindowEvent windowEvent;
        OS_GraphicsKeyEvent key;
        OS_GraphicsTextEvent text;
        OS_GraphicsMouseEvent mouse;
    };
};

struct OS_GraphicsEventNode {
    OS_GraphicsEvent event;
    struct OS_GraphicsEventNode* next;
};

struct OS_GraphicsEventQueue {
    struct OS_GraphicsEventNode* head;
    struct OS_GraphicsEventNode* tail;
    struct OS_GraphicsEventNode* freeList;
    U32 count;
};

struct OS_WindowDesc {
    const char* title;
    U32         width;
    U32         height;
};

struct OS_WindowSurfaceInfo {
    void* viewPtr;
    void* metalLayerPtr;
};

static B32 OS_graphics_init();
static void OS_graphics_shutdown();

static OS_WindowHandle OS_window_create(OS_WindowDesc desc);
static void OS_window_destroy(OS_WindowHandle window);
static void* OS_window_get_native_handle(OS_WindowHandle window);
static B32 OS_window_is_open(OS_WindowHandle window);
static OS_WindowSurfaceInfo OS_window_get_surface_info(OS_WindowHandle window);
static U32 OS_graphics_poll_events(OS_GraphicsEvent* outEvents, U32 maxEvents);
static B32 OS_graphics_pump_events();

