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

enum OS_KeyCode {
    OS_KeyCode_None = 0,
    
    // Letter keys
    OS_KeyCode_A, OS_KeyCode_B, OS_KeyCode_C, OS_KeyCode_D, OS_KeyCode_E,
    OS_KeyCode_F, OS_KeyCode_G, OS_KeyCode_H, OS_KeyCode_I, OS_KeyCode_J,
    OS_KeyCode_K, OS_KeyCode_L, OS_KeyCode_M, OS_KeyCode_N, OS_KeyCode_O,
    OS_KeyCode_P, OS_KeyCode_Q, OS_KeyCode_R, OS_KeyCode_S, OS_KeyCode_T,
    OS_KeyCode_U, OS_KeyCode_V, OS_KeyCode_W, OS_KeyCode_X, OS_KeyCode_Y,
    OS_KeyCode_Z,
    
    // Number keys (top row)
    OS_KeyCode_0, OS_KeyCode_1, OS_KeyCode_2, OS_KeyCode_3, OS_KeyCode_4,
    OS_KeyCode_5, OS_KeyCode_6, OS_KeyCode_7, OS_KeyCode_8, OS_KeyCode_9,
    
    // Special character keys
    OS_KeyCode_Minus,          // -
    OS_KeyCode_Equal,          // =
    OS_KeyCode_LeftBracket,    // [
    OS_KeyCode_RightBracket,   // ]
    OS_KeyCode_Semicolon,      // ;
    OS_KeyCode_Apostrophe,     // '
    OS_KeyCode_Comma,          // ,
    OS_KeyCode_Period,         // .
    OS_KeyCode_Slash,          // forward slash
    OS_KeyCode_Backslash,      // backslash
    OS_KeyCode_GraveAccent,    // grave accent
    
    // Function keys
    OS_KeyCode_F1,  OS_KeyCode_F2,  OS_KeyCode_F3,  OS_KeyCode_F4,
    OS_KeyCode_F5,  OS_KeyCode_F6,  OS_KeyCode_F7,  OS_KeyCode_F8,
    OS_KeyCode_F9,  OS_KeyCode_F10, OS_KeyCode_F11, OS_KeyCode_F12,
    
    // Control keys
    OS_KeyCode_Escape,
    OS_KeyCode_Tab,
    OS_KeyCode_CapsLock,
    OS_KeyCode_Shift,
    OS_KeyCode_Control,
    OS_KeyCode_Alt,
    OS_KeyCode_Super,          // Command on macOS, Windows key on Windows
    OS_KeyCode_Space,
    OS_KeyCode_Enter,
    OS_KeyCode_Backspace,
    OS_KeyCode_Delete,
    
    // Arrow keys
    OS_KeyCode_LeftArrow,
    OS_KeyCode_RightArrow,
    OS_KeyCode_UpArrow,
    OS_KeyCode_DownArrow,
    
    // Navigation keys
    OS_KeyCode_Home,
    OS_KeyCode_End,
    OS_KeyCode_PageUp,
    OS_KeyCode_PageDown,
    
    // Modifier keys (right variants)
    OS_KeyCode_LeftShift,
    OS_KeyCode_RightShift,
    OS_KeyCode_LeftControl,
    OS_KeyCode_RightControl,
    OS_KeyCode_LeftAlt,
    OS_KeyCode_RightAlt,
    OS_KeyCode_LeftSuper,
    OS_KeyCode_RightSuper,
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
    enum OS_KeyCode keyCode;
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

B32 OS_graphics_init();
void OS_graphics_shutdown();

OS_WindowHandle OS_window_create(OS_WindowDesc desc);
void OS_window_destroy(OS_WindowHandle window);
void* OS_window_get_native_handle(OS_WindowHandle window);
B32 OS_window_is_open(OS_WindowHandle window);
OS_WindowSurfaceInfo OS_window_get_surface_info(OS_WindowHandle window);
U32 OS_graphics_poll_events(OS_GraphicsEvent* outEvents, U32 maxEvents);
B32 OS_graphics_pump_events();

