//
// AUTO-GENERATED FILE - DO NOT EDIT
// Generated from: ../nstl/os/graphics/os_graphics.metadef
//

#pragma once

// ////////////////////////
// Sum Type: OS_GraphicsEvent

struct OS_GraphicsEvent_None {};
struct OS_GraphicsEvent_WindowClosed {};
struct OS_GraphicsEvent_WindowFocused {};
struct OS_GraphicsEvent_WindowUnfocused {};
struct OS_GraphicsEvent_WindowDestroyed {};

enum OS_GraphicsEvent_Tag {
    OS_GraphicsEvent_Tag_None = 0,
    OS_GraphicsEvent_Tag_WindowShown = 1,
    OS_GraphicsEvent_Tag_WindowClosed = 2,
    OS_GraphicsEvent_Tag_WindowResized = 3,
    OS_GraphicsEvent_Tag_WindowFocused = 4,
    OS_GraphicsEvent_Tag_WindowUnfocused = 5,
    OS_GraphicsEvent_Tag_WindowDestroyed = 6,
    OS_GraphicsEvent_Tag_KeyDown = 7,
    OS_GraphicsEvent_Tag_KeyUp = 8,
    OS_GraphicsEvent_Tag_TextInput = 9,
    OS_GraphicsEvent_Tag_MouseButtonDown = 10,
    OS_GraphicsEvent_Tag_MouseButtonUp = 11,
    OS_GraphicsEvent_Tag_MouseMove = 12,
    OS_GraphicsEvent_Tag_MouseScroll = 13,
};

struct OS_GraphicsEvent_WindowShown {
    U32 width;
    U32 height;
};

struct OS_GraphicsEvent_WindowResized {
    U32 width;
    U32 height;
};

struct OS_GraphicsEvent_KeyDown {
    OS_KeyCode keyCode;
    U32 modifiers;
    U32 character;
    B32 isRepeat;
};

struct OS_GraphicsEvent_KeyUp {
    OS_KeyCode keyCode;
    U32 modifiers;
};

struct OS_GraphicsEvent_TextInput {
    U32 codepoint;
    U32 modifiers;
};

struct OS_GraphicsEvent_MouseButtonDown {
    F32 x;
    F32 y;
    OS_MouseButton button;
    U32 modifiers;
    U32 clickCount;
};

struct OS_GraphicsEvent_MouseButtonUp {
    F32 x;
    F32 y;
    OS_MouseButton button;
    U32 modifiers;
};

struct OS_GraphicsEvent_MouseMove {
    F32 x;
    F32 y;
    F32 deltaX;
    F32 deltaY;
    F32 globalX;
    F32 globalY;
    U32 modifiers;
    B32 isInWindow;
};

struct OS_GraphicsEvent_MouseScroll {
    F32 x;
    F32 y;
    F32 deltaX;
    F32 deltaY;
    U32 modifiers;
};

struct OS_GraphicsEvent {
    OS_GraphicsEvent_Tag tag;
    OS_WindowHandle window;

    union {
        OS_GraphicsEvent_WindowShown windowShown;
        OS_GraphicsEvent_WindowResized windowResized;
        OS_GraphicsEvent_KeyDown keyDown;
        OS_GraphicsEvent_KeyUp keyUp;
        OS_GraphicsEvent_TextInput textInput;
        OS_GraphicsEvent_MouseButtonDown mouseButtonDown;
        OS_GraphicsEvent_MouseButtonUp mouseButtonUp;
        OS_GraphicsEvent_MouseMove mouseMove;
        OS_GraphicsEvent_MouseScroll mouseScroll;
    };

    static OS_GraphicsEvent none(OS_WindowHandle window) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_None;
        result.window = window;
        return result;
    }
    static OS_GraphicsEvent window_shown(OS_WindowHandle window, U32 width, U32 height) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_WindowShown;
        result.window = window;
        result.windowShown.width = width;
        result.windowShown.height = height;
        return result;
    }
    static OS_GraphicsEvent window_closed(OS_WindowHandle window) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_WindowClosed;
        result.window = window;
        return result;
    }
    static OS_GraphicsEvent window_resized(OS_WindowHandle window, U32 width, U32 height) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_WindowResized;
        result.window = window;
        result.windowResized.width = width;
        result.windowResized.height = height;
        return result;
    }
    static OS_GraphicsEvent window_focused(OS_WindowHandle window) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_WindowFocused;
        result.window = window;
        return result;
    }
    static OS_GraphicsEvent window_unfocused(OS_WindowHandle window) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_WindowUnfocused;
        result.window = window;
        return result;
    }
    static OS_GraphicsEvent window_destroyed(OS_WindowHandle window) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_WindowDestroyed;
        result.window = window;
        return result;
    }
    static OS_GraphicsEvent key_down(OS_WindowHandle window, OS_KeyCode keyCode, U32 modifiers, U32 character, B32 isRepeat) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_KeyDown;
        result.window = window;
        result.keyDown.keyCode = keyCode;
        result.keyDown.modifiers = modifiers;
        result.keyDown.character = character;
        result.keyDown.isRepeat = isRepeat;
        return result;
    }
    static OS_GraphicsEvent key_up(OS_WindowHandle window, OS_KeyCode keyCode, U32 modifiers) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_KeyUp;
        result.window = window;
        result.keyUp.keyCode = keyCode;
        result.keyUp.modifiers = modifiers;
        return result;
    }
    static OS_GraphicsEvent text_input(OS_WindowHandle window, U32 codepoint, U32 modifiers) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_TextInput;
        result.window = window;
        result.textInput.codepoint = codepoint;
        result.textInput.modifiers = modifiers;
        return result;
    }
    static OS_GraphicsEvent mouse_button_down(OS_WindowHandle window, F32 x, F32 y, OS_MouseButton button, U32 modifiers, U32 clickCount) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_MouseButtonDown;
        result.window = window;
        result.mouseButtonDown.x = x;
        result.mouseButtonDown.y = y;
        result.mouseButtonDown.button = button;
        result.mouseButtonDown.modifiers = modifiers;
        result.mouseButtonDown.clickCount = clickCount;
        return result;
    }
    static OS_GraphicsEvent mouse_button_up(OS_WindowHandle window, F32 x, F32 y, OS_MouseButton button, U32 modifiers) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_MouseButtonUp;
        result.window = window;
        result.mouseButtonUp.x = x;
        result.mouseButtonUp.y = y;
        result.mouseButtonUp.button = button;
        result.mouseButtonUp.modifiers = modifiers;
        return result;
    }
    static OS_GraphicsEvent mouse_move(OS_WindowHandle window, F32 x, F32 y, F32 deltaX, F32 deltaY, F32 globalX, F32 globalY, U32 modifiers, B32 isInWindow) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_MouseMove;
        result.window = window;
        result.mouseMove.x = x;
        result.mouseMove.y = y;
        result.mouseMove.deltaX = deltaX;
        result.mouseMove.deltaY = deltaY;
        result.mouseMove.globalX = globalX;
        result.mouseMove.globalY = globalY;
        result.mouseMove.modifiers = modifiers;
        result.mouseMove.isInWindow = isInWindow;
        return result;
    }
    static OS_GraphicsEvent mouse_scroll(OS_WindowHandle window, F32 x, F32 y, F32 deltaX, F32 deltaY, U32 modifiers) {
        OS_GraphicsEvent result = {};
        result.tag = OS_GraphicsEvent_Tag_MouseScroll;
        result.window = window;
        result.mouseScroll.x = x;
        result.mouseScroll.y = y;
        result.mouseScroll.deltaX = deltaX;
        result.mouseScroll.deltaY = deltaY;
        result.mouseScroll.modifiers = modifiers;
        return result;
    }

    B32 is_none() const { return tag == OS_GraphicsEvent_Tag_None; }
    B32 is_window_shown() const { return tag == OS_GraphicsEvent_Tag_WindowShown; }
    B32 is_window_closed() const { return tag == OS_GraphicsEvent_Tag_WindowClosed; }
    B32 is_window_resized() const { return tag == OS_GraphicsEvent_Tag_WindowResized; }
    B32 is_window_focused() const { return tag == OS_GraphicsEvent_Tag_WindowFocused; }
    B32 is_window_unfocused() const { return tag == OS_GraphicsEvent_Tag_WindowUnfocused; }
    B32 is_window_destroyed() const { return tag == OS_GraphicsEvent_Tag_WindowDestroyed; }
    B32 is_key_down() const { return tag == OS_GraphicsEvent_Tag_KeyDown; }
    B32 is_key_up() const { return tag == OS_GraphicsEvent_Tag_KeyUp; }
    B32 is_text_input() const { return tag == OS_GraphicsEvent_Tag_TextInput; }
    B32 is_mouse_button_down() const { return tag == OS_GraphicsEvent_Tag_MouseButtonDown; }
    B32 is_mouse_button_up() const { return tag == OS_GraphicsEvent_Tag_MouseButtonUp; }
    B32 is_mouse_move() const { return tag == OS_GraphicsEvent_Tag_MouseMove; }
    B32 is_mouse_scroll() const { return tag == OS_GraphicsEvent_Tag_MouseScroll; }

    OS_GraphicsEvent_WindowShown* as_window_shown() {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_WindowShown);
        return (tag == OS_GraphicsEvent_Tag_WindowShown) ? &windowShown : nullptr;
    }
    const OS_GraphicsEvent_WindowShown* as_window_shown() const {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_WindowShown);
        return (tag == OS_GraphicsEvent_Tag_WindowShown) ? &windowShown : nullptr;
    }
    OS_GraphicsEvent_WindowResized* as_window_resized() {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_WindowResized);
        return (tag == OS_GraphicsEvent_Tag_WindowResized) ? &windowResized : nullptr;
    }
    const OS_GraphicsEvent_WindowResized* as_window_resized() const {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_WindowResized);
        return (tag == OS_GraphicsEvent_Tag_WindowResized) ? &windowResized : nullptr;
    }
    OS_GraphicsEvent_KeyDown* as_key_down() {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_KeyDown);
        return (tag == OS_GraphicsEvent_Tag_KeyDown) ? &keyDown : nullptr;
    }
    const OS_GraphicsEvent_KeyDown* as_key_down() const {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_KeyDown);
        return (tag == OS_GraphicsEvent_Tag_KeyDown) ? &keyDown : nullptr;
    }
    OS_GraphicsEvent_KeyUp* as_key_up() {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_KeyUp);
        return (tag == OS_GraphicsEvent_Tag_KeyUp) ? &keyUp : nullptr;
    }
    const OS_GraphicsEvent_KeyUp* as_key_up() const {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_KeyUp);
        return (tag == OS_GraphicsEvent_Tag_KeyUp) ? &keyUp : nullptr;
    }
    OS_GraphicsEvent_TextInput* as_text_input() {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_TextInput);
        return (tag == OS_GraphicsEvent_Tag_TextInput) ? &textInput : nullptr;
    }
    const OS_GraphicsEvent_TextInput* as_text_input() const {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_TextInput);
        return (tag == OS_GraphicsEvent_Tag_TextInput) ? &textInput : nullptr;
    }
    OS_GraphicsEvent_MouseButtonDown* as_mouse_button_down() {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_MouseButtonDown);
        return (tag == OS_GraphicsEvent_Tag_MouseButtonDown) ? &mouseButtonDown : nullptr;
    }
    const OS_GraphicsEvent_MouseButtonDown* as_mouse_button_down() const {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_MouseButtonDown);
        return (tag == OS_GraphicsEvent_Tag_MouseButtonDown) ? &mouseButtonDown : nullptr;
    }
    OS_GraphicsEvent_MouseButtonUp* as_mouse_button_up() {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_MouseButtonUp);
        return (tag == OS_GraphicsEvent_Tag_MouseButtonUp) ? &mouseButtonUp : nullptr;
    }
    const OS_GraphicsEvent_MouseButtonUp* as_mouse_button_up() const {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_MouseButtonUp);
        return (tag == OS_GraphicsEvent_Tag_MouseButtonUp) ? &mouseButtonUp : nullptr;
    }
    OS_GraphicsEvent_MouseMove* as_mouse_move() {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_MouseMove);
        return (tag == OS_GraphicsEvent_Tag_MouseMove) ? &mouseMove : nullptr;
    }
    const OS_GraphicsEvent_MouseMove* as_mouse_move() const {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_MouseMove);
        return (tag == OS_GraphicsEvent_Tag_MouseMove) ? &mouseMove : nullptr;
    }
    OS_GraphicsEvent_MouseScroll* as_mouse_scroll() {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_MouseScroll);
        return (tag == OS_GraphicsEvent_Tag_MouseScroll) ? &mouseScroll : nullptr;
    }
    const OS_GraphicsEvent_MouseScroll* as_mouse_scroll() const {
        ASSERT_DEBUG(tag == OS_GraphicsEvent_Tag_MouseScroll);
        return (tag == OS_GraphicsEvent_Tag_MouseScroll) ? &mouseScroll : nullptr;
    }

    template<typename... Visitors>
    auto match(Visitors&&... visitors) {
        struct Overloaded : Visitors... { using Visitors::operator()...; };
        Overloaded overloaded{static_cast<Visitors&&>(visitors)...};

        switch (tag) {
            case OS_GraphicsEvent_Tag_None:
                return overloaded(static_cast<OS_GraphicsEvent_None*>(nullptr));
            case OS_GraphicsEvent_Tag_WindowShown:
                return overloaded(&windowShown);
            case OS_GraphicsEvent_Tag_WindowClosed:
                return overloaded(static_cast<OS_GraphicsEvent_WindowClosed*>(nullptr));
            case OS_GraphicsEvent_Tag_WindowResized:
                return overloaded(&windowResized);
            case OS_GraphicsEvent_Tag_WindowFocused:
                return overloaded(static_cast<OS_GraphicsEvent_WindowFocused*>(nullptr));
            case OS_GraphicsEvent_Tag_WindowUnfocused:
                return overloaded(static_cast<OS_GraphicsEvent_WindowUnfocused*>(nullptr));
            case OS_GraphicsEvent_Tag_WindowDestroyed:
                return overloaded(static_cast<OS_GraphicsEvent_WindowDestroyed*>(nullptr));
            case OS_GraphicsEvent_Tag_KeyDown:
                return overloaded(&keyDown);
            case OS_GraphicsEvent_Tag_KeyUp:
                return overloaded(&keyUp);
            case OS_GraphicsEvent_Tag_TextInput:
                return overloaded(&textInput);
            case OS_GraphicsEvent_Tag_MouseButtonDown:
                return overloaded(&mouseButtonDown);
            case OS_GraphicsEvent_Tag_MouseButtonUp:
                return overloaded(&mouseButtonUp);
            case OS_GraphicsEvent_Tag_MouseMove:
                return overloaded(&mouseMove);
            case OS_GraphicsEvent_Tag_MouseScroll:
                return overloaded(&mouseScroll);
            default:
                ASSERT_DEBUG(false && "Invalid tag");
                return overloaded(static_cast<void*>(nullptr));
        }
    }
};

