//
// Created by André Leite on 26/07/2025.
//

#define OS_WINDOWS_WINDOW_CLASS_NAME "UtilitiesWindowClass"

OS_WINDOWS_GraphicsState g_OS_WindowsGraphicsState = {};

static OS_WindowHandle os_make_window_handle_from_entity(OS_WINDOWS_GraphicsEntity* entity) {
    OS_WindowHandle handle = {};
    if (entity) {
        handle.handle = (U64*)entity;
    }
    return handle;
}

static void os_graphics_event_queue_init(OS_GraphicsEventQueue* queue) {
    ASSERT_ALWAYS(queue != 0);
    MEMSET(queue, 0, sizeof(*queue));
    queue->head = &queue->nilNode;
    queue->tail = &queue->nilNode;
    queue->nilNode.next = &queue->nilNode;
}

static void os_register_active_graphics_entity(OS_WINDOWS_GraphicsEntity* entity) {
    if (!entity) {
        return;
    }
    entity->activeNext = g_OS_WindowsGraphicsState.activeEntities;
    g_OS_WindowsGraphicsState.activeEntities = entity;
}

static void os_unregister_active_graphics_entity(OS_WINDOWS_GraphicsEntity* entity) {
    if (!entity) {
        return;
    }

    OS_WINDOWS_GraphicsEntity** current = &g_OS_WindowsGraphicsState.activeEntities;
    while (*current) {
        if (*current == entity) {
            *current = entity->activeNext;
            break;
        }
        current = &(*current)->activeNext;
    }
    entity->activeNext = 0;
}

static OS_GraphicsEventNode* os_acquire_graphics_event_node() {
    OS_GraphicsEventQueue* queue = &g_OS_WindowsGraphicsState.eventQueue;
    OS_GraphicsEventNode* node = 0;
    FREELIST_POP(queue->freeList, node, next);
    if (node) {
        node->next = 0;
        return node;
    }

    Arena* arena = g_OS_WindowsGraphicsState.eventArena;
    if (!arena) {
        return 0;
    }

    node = ARENA_PUSH_STRUCT(arena, OS_GraphicsEventNode);
    if (node) {
        MEMSET(node, 0, sizeof(*node));
    }
    return node;
}

static void os_release_graphics_event_node(OS_GraphicsEventNode* node) {
    if (!node) {
        return;
    }
    MEMSET(&node->event, 0, sizeof(OS_GraphicsEvent));
    FREELIST_PUSH(g_OS_WindowsGraphicsState.eventQueue.freeList, node, next);
}

static void os_push_graphics_event(OS_GraphicsEvent event) {
    OS_GraphicsEventQueue* queue = &g_OS_WindowsGraphicsState.eventQueue;
    OS_GraphicsEventNode* node = os_acquire_graphics_event_node();
    if (!node) {
        return;
    }

    node->event = event;
    SLL_QUEUE_PUSH_NZ(&queue->nilNode, queue->head, queue->tail, node, next);
    if (queue->count < 0xffffffffu) {
        queue->count += 1u;
    }
}

static U32 os_translate_modifier_flags() {
    U32 result = OS_KeyModifiers_None;
    if (GetKeyState(VK_SHIFT) & 0x8000) {
        result |= OS_KeyModifiers_Shift;
    }
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        result |= OS_KeyModifiers_Control;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
        result |= OS_KeyModifiers_Alt;
    }
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) {
        result |= OS_KeyModifiers_Super;
    }
    if (GetKeyState(VK_CAPITAL) & 0x0001) {
        result |= OS_KeyModifiers_CapsLock;
    }
    return result;
}

static OS_KeyCode os_translate_key(WPARAM key) {
    if (key >= 'A' && key <= 'Z') {
        return (OS_KeyCode)(OS_KeyCode_A + (key - 'A'));
    }
    if (key >= '0' && key <= '9') {
        return (OS_KeyCode)(OS_KeyCode_0 + (key - '0'));
    }

    switch (key) {
        case VK_ESCAPE: return OS_KeyCode_Escape;
        case VK_TAB: return OS_KeyCode_Tab;
        case VK_CAPITAL: return OS_KeyCode_CapsLock;
        case VK_SHIFT: return OS_KeyCode_Shift;
        case VK_CONTROL: return OS_KeyCode_Control;
        case VK_MENU: return OS_KeyCode_Alt;
        case VK_LWIN:
        case VK_RWIN: return OS_KeyCode_Super;
        case VK_SPACE: return OS_KeyCode_Space;
        case VK_RETURN: return OS_KeyCode_Enter;
        case VK_BACK: return OS_KeyCode_Backspace;
        case VK_DELETE: return OS_KeyCode_Delete;
        case VK_LEFT: return OS_KeyCode_LeftArrow;
        case VK_RIGHT: return OS_KeyCode_RightArrow;
        case VK_UP: return OS_KeyCode_UpArrow;
        case VK_DOWN: return OS_KeyCode_DownArrow;
        case VK_HOME: return OS_KeyCode_Home;
        case VK_END: return OS_KeyCode_End;
        case VK_PRIOR: return OS_KeyCode_PageUp;
        case VK_NEXT: return OS_KeyCode_PageDown;
        case VK_F1: return OS_KeyCode_F1;
        case VK_F2: return OS_KeyCode_F2;
        case VK_F3: return OS_KeyCode_F3;
        case VK_F4: return OS_KeyCode_F4;
        case VK_F5: return OS_KeyCode_F5;
        case VK_F6: return OS_KeyCode_F6;
        case VK_F7: return OS_KeyCode_F7;
        case VK_F8: return OS_KeyCode_F8;
        case VK_F9: return OS_KeyCode_F9;
        case VK_F10: return OS_KeyCode_F10;
        case VK_F11: return OS_KeyCode_F11;
        case VK_F12: return OS_KeyCode_F12;
        case VK_OEM_MINUS: return OS_KeyCode_Minus;
        case VK_OEM_PLUS: return OS_KeyCode_Equal;
        case VK_OEM_4: return OS_KeyCode_LeftBracket;
        case VK_OEM_6: return OS_KeyCode_RightBracket;
        case VK_OEM_1: return OS_KeyCode_Semicolon;
        case VK_OEM_7: return OS_KeyCode_Apostrophe;
        case VK_OEM_COMMA: return OS_KeyCode_Comma;
        case VK_OEM_PERIOD: return OS_KeyCode_Period;
        case VK_OEM_2: return OS_KeyCode_Slash;
        case VK_OEM_5: return OS_KeyCode_Backslash;
        case VK_OEM_3: return OS_KeyCode_GraveAccent;
        case VK_LSHIFT: return OS_KeyCode_LeftShift;
        case VK_RSHIFT: return OS_KeyCode_RightShift;
        case VK_LCONTROL: return OS_KeyCode_LeftControl;
        case VK_RCONTROL: return OS_KeyCode_RightControl;
        case VK_LMENU: return OS_KeyCode_LeftAlt;
        case VK_RMENU: return OS_KeyCode_RightAlt;
    }

    return OS_KeyCode_None;
}

static OS_MouseButton os_translate_mouse_button(UINT message) {
    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP: return OS_MouseButton_Left;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: return OS_MouseButton_Right;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP: return OS_MouseButton_Middle;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: return OS_MouseButton_Button4;
    }
    return OS_MouseButton_None;
}

static void os_window_get_drawable_size(HWND window, U32* outWidth, U32* outHeight) {
    if (outWidth) {
        *outWidth = 0u;
    }
    if (outHeight) {
        *outHeight = 0u;
    }
    if (!window || !outWidth || !outHeight) {
        return;
    }

    RECT rect = {};
    if (GetClientRect(window, &rect)) {
        *outWidth = (U32)(rect.right - rect.left);
        *outHeight = (U32)(rect.bottom - rect.top);
    }
}

static void os_push_window_event(OS_WINDOWS_GraphicsEntity* entity, OS_GraphicsEvent_Tag tag) {
    OS_WindowHandle handle = os_make_window_handle_from_entity(entity);
    U32 width = 0u;
    U32 height = 0u;
    if (entity && entity->window.window) {
        os_window_get_drawable_size(entity->window.window, &width, &height);
    }

    OS_GraphicsEvent event = {};
    if (tag == OS_GraphicsEvent_Tag_WindowShown) {
        event = OS_GraphicsEvent::window_shown(handle, width, height);
    } else if (tag == OS_GraphicsEvent_Tag_WindowClosed) {
        event = OS_GraphicsEvent::window_closed(handle);
    } else if (tag == OS_GraphicsEvent_Tag_WindowResized) {
        event = OS_GraphicsEvent::window_resized(handle, width, height);
    } else if (tag == OS_GraphicsEvent_Tag_WindowFocused) {
        event = OS_GraphicsEvent::window_focused(handle);
    } else if (tag == OS_GraphicsEvent_Tag_WindowUnfocused) {
        event = OS_GraphicsEvent::window_unfocused(handle);
    } else if (tag == OS_GraphicsEvent_Tag_WindowDestroyed) {
        event = OS_GraphicsEvent::window_destroyed(handle);
    } else {
        event = OS_GraphicsEvent::none(handle);
    }
    os_push_graphics_event(event);
}

static F32 os_mouse_x_from_lparam(LPARAM lParam) {
    return (F32)(S32)(short)(lParam & 0xffff);
}

static F32 os_mouse_y_from_lparam(LPARAM lParam) {
    return (F32)(S32)(short)((lParam >> 16) & 0xffff);
}

static LRESULT CALLBACK os_windows_window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    OS_WINDOWS_GraphicsEntity* entity = (OS_WINDOWS_GraphicsEntity*)GetWindowLongPtrA(window, GWLP_USERDATA);

    if (message == WM_NCCREATE) {
        CREATESTRUCTA* create = (CREATESTRUCTA*)lParam;
        entity = (OS_WINDOWS_GraphicsEntity*)create->lpCreateParams;
        SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)entity);
    }

    OS_WindowHandle handle = os_make_window_handle_from_entity(entity);
    switch (message) {
        case WM_CLOSE: {
            os_push_window_event(entity, OS_GraphicsEvent_Tag_WindowClosed);
            DestroyWindow(window);
            return 0;
        }

        case WM_DESTROY: {
            if (entity) {
                os_push_window_event(entity, OS_GraphicsEvent_Tag_WindowDestroyed);
                entity->window.window = 0;
            }
            return 0;
        }

        case WM_SIZE: {
            if (entity && wParam != SIZE_MINIMIZED) {
                os_push_window_event(entity, OS_GraphicsEvent_Tag_WindowResized);
            }
            return 0;
        }

        case WM_SETFOCUS: {
            os_push_window_event(entity, OS_GraphicsEvent_Tag_WindowFocused);
            return 0;
        }

        case WM_KILLFOCUS: {
            os_push_window_event(entity, OS_GraphicsEvent_Tag_WindowUnfocused);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            OS_KeyCode key = os_translate_key(wParam);
            U32 modifiers = os_translate_modifier_flags();
            B32 isRepeat = (lParam & (1u << 30u)) ? 1 : 0;
            os_push_graphics_event(OS_GraphicsEvent::key_down(handle, key, modifiers, 0u, isRepeat));
            return 0;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP: {
            OS_KeyCode key = os_translate_key(wParam);
            U32 modifiers = os_translate_modifier_flags();
            os_push_graphics_event(OS_GraphicsEvent::key_up(handle, key, modifiers));
            return 0;
        }

        case WM_CHAR: {
            os_push_graphics_event(OS_GraphicsEvent::text_input(handle, (U32)wParam, os_translate_modifier_flags()));
            return 0;
        }

        case WM_MOUSEMOVE: {
            POINT globalPoint = {};
            GetCursorPos(&globalPoint);
            os_push_graphics_event(OS_GraphicsEvent::mouse_move(handle,
                                                                os_mouse_x_from_lparam(lParam),
                                                                os_mouse_y_from_lparam(lParam),
                                                                0.0f,
                                                                0.0f,
                                                                (F32)globalPoint.x,
                                                                (F32)globalPoint.y,
                                                                os_translate_modifier_flags(),
                                                                1));
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN: {
            SetCapture(window);
            os_push_graphics_event(OS_GraphicsEvent::mouse_button_down(handle,
                                                                       os_mouse_x_from_lparam(lParam),
                                                                       os_mouse_y_from_lparam(lParam),
                                                                       os_translate_mouse_button(message),
                                                                       os_translate_modifier_flags(),
                                                                       1u));
            return 0;
        }

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP: {
            ReleaseCapture();
            os_push_graphics_event(OS_GraphicsEvent::mouse_button_up(handle,
                                                                     os_mouse_x_from_lparam(lParam),
                                                                     os_mouse_y_from_lparam(lParam),
                                                                     os_translate_mouse_button(message),
                                                                     os_translate_modifier_flags()));
            return 0;
        }

        case WM_MOUSEWHEEL: {
            S32 wheel = (S32)(short)((wParam >> 16) & 0xffff);
            os_push_graphics_event(OS_GraphicsEvent::mouse_scroll(handle,
                                                                  os_mouse_x_from_lparam(lParam),
                                                                  os_mouse_y_from_lparam(lParam),
                                                                  0.0f,
                                                                  (F32)wheel / 120.0f,
                                                                  os_translate_modifier_flags()));
            return 0;
        }
    }

    return DefWindowProcA(window, message, wParam, lParam);
}

B32 OS_graphics_init() {
    if (g_OS_WindowsGraphicsState.initialized) {
        return 1;
    }

    g_OS_WindowsGraphicsState.instance = GetModuleHandleA(0);
    g_OS_WindowsGraphicsState.entityArena = arena_alloc();
    g_OS_WindowsGraphicsState.eventArena = arena_alloc();
    os_graphics_event_queue_init(&g_OS_WindowsGraphicsState.eventQueue);

    WNDCLASSEXA windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc = os_windows_window_proc;
    windowClass.hInstance = g_OS_WindowsGraphicsState.instance;
    windowClass.hCursor = LoadCursorA(0, IDC_ARROW);
    windowClass.lpszClassName = OS_WINDOWS_WINDOW_CLASS_NAME;
    if (!RegisterClassExA(&windowClass)) {
        return 0;
    }

    g_OS_WindowsGraphicsState.windowClassRegistered = 1;
    g_OS_WindowsGraphicsState.initialized = 1;
    return 1;
}

void OS_graphics_shutdown() {
    OS_WINDOWS_GraphicsEntity* entity = g_OS_WindowsGraphicsState.activeEntities;
    while (entity) {
        OS_WINDOWS_GraphicsEntity* next = entity->activeNext;
        OS_window_destroy(os_make_window_handle_from_entity(entity));
        entity = next;
    }

    if (g_OS_WindowsGraphicsState.windowClassRegistered) {
        UnregisterClassA(OS_WINDOWS_WINDOW_CLASS_NAME, g_OS_WindowsGraphicsState.instance);
    }
    if (g_OS_WindowsGraphicsState.entityArena) {
        arena_release(g_OS_WindowsGraphicsState.entityArena);
    }
    if (g_OS_WindowsGraphicsState.eventArena) {
        arena_release(g_OS_WindowsGraphicsState.eventArena);
    }

    MEMSET(&g_OS_WindowsGraphicsState, 0, sizeof(g_OS_WindowsGraphicsState));
}

OS_WindowHandle OS_window_create(OS_WindowDesc desc) {
    OS_WindowHandle result = {};
    if (!g_OS_WindowsGraphicsState.initialized) {
        return result;
    }

    OS_WINDOWS_GraphicsEntity* entity = alloc_OS_graphics_entity();
    if (!entity) {
        return result;
    }
    entity->type = OS_WINDOWS_GraphicsEntityType_Window;
    entity->window.instance = g_OS_WindowsGraphicsState.instance;

    RECT rect = {0, 0, (LONG)desc.width, (LONG)desc.height};
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!desc.hidden) {
        style |= WS_VISIBLE;
    }
    AdjustWindowRect(&rect, style, FALSE);

    HWND window = CreateWindowExA(0,
                                  OS_WINDOWS_WINDOW_CLASS_NAME,
                                  desc.title ? desc.title : "Utilities",
                                  style,
                                  CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  rect.right - rect.left,
                                  rect.bottom - rect.top,
                                  0,
                                  0,
                                  g_OS_WindowsGraphicsState.instance,
                                  entity);
    if (!window) {
        free_OS_graphics_entity(entity);
        return result;
    }

    entity->window.window = window;
    os_register_active_graphics_entity(entity);
    if (!desc.hidden) {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
        os_push_window_event(entity, OS_GraphicsEvent_Tag_WindowShown);
    }

    result.handle = (U64*)entity;
    return result;
}

void OS_window_show(OS_WindowHandle windowHandle) {
    if (!windowHandle.handle) {
        return;
    }

    OS_WINDOWS_GraphicsEntity* entity = (OS_WINDOWS_GraphicsEntity*)windowHandle.handle;
    if (entity->type != OS_WINDOWS_GraphicsEntityType_Window || !entity->window.window) {
        return;
    }

    if (!IsWindowVisible(entity->window.window)) {
        ShowWindow(entity->window.window, SW_SHOW);
        UpdateWindow(entity->window.window);
        os_push_window_event(entity, OS_GraphicsEvent_Tag_WindowShown);
    }
}

void OS_window_destroy(OS_WindowHandle windowHandle) {
    if (!windowHandle.handle) {
        return;
    }

    OS_WINDOWS_GraphicsEntity* entity = (OS_WINDOWS_GraphicsEntity*)windowHandle.handle;
    if (entity->type != OS_WINDOWS_GraphicsEntityType_Window) {
        return;
    }

    if (entity->window.window) {
        DestroyWindow(entity->window.window);
        entity->window.window = 0;
    }

    os_unregister_active_graphics_entity(entity);
    entity->type = OS_WINDOWS_GraphicsEntityType_Invalid;
    free_OS_graphics_entity(entity);
}

B32 OS_window_is_open(OS_WindowHandle windowHandle) {
    if (!windowHandle.handle) {
        return 0;
    }

    OS_WINDOWS_GraphicsEntity* entity = (OS_WINDOWS_GraphicsEntity*)windowHandle.handle;
    if (entity->type != OS_WINDOWS_GraphicsEntityType_Window || !entity->window.window) {
        return 0;
    }

    return (IsWindow(entity->window.window) && IsWindowVisible(entity->window.window)) ? 1 : 0;
}

OS_WindowInfo OS_window_get_info(OS_WindowHandle windowHandle) {
    OS_WindowInfo info = {};
    if (!windowHandle.handle) {
        return info;
    }

    OS_WINDOWS_GraphicsEntity* entity = (OS_WINDOWS_GraphicsEntity*)windowHandle.handle;
    if (entity->type != OS_WINDOWS_GraphicsEntityType_Window || !entity->window.window) {
        return info;
    }

    os_window_get_drawable_size(entity->window.window, &info.drawableWidth, &info.drawableHeight);
    return info;
}

U32 OS_graphics_poll_events(OS_GraphicsEvent* outEvents, U32 maxEvents) {
    if (!outEvents || maxEvents == 0u) {
        return 0u;
    }

    OS_GraphicsEventQueue* queue = &g_OS_WindowsGraphicsState.eventQueue;
    U32 count = 0u;
    while (count < maxEvents) {
        OS_GraphicsEventNode* node = queue->head;
        if (CHECK_NIL(&queue->nilNode, node)) {
            break;
        }

        SLL_QUEUE_POP_NZ(&queue->nilNode, queue->head, queue->tail, next);
        if (queue->count > 0u) {
            queue->count -= 1u;
        }
        outEvents[count] = node->event;
        count += 1u;
        node->next = 0;
        os_release_graphics_event_node(node);
    }
    return count;
}

B32 OS_graphics_pump_events() {
    MSG message = {};
    B32 processed = 0;
    while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) {
        processed = 1;
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return processed;
}

static OS_WINDOWS_GraphicsEntity* alloc_OS_graphics_entity() {
    OS_WINDOWS_GraphicsEntity* entity = g_OS_WindowsGraphicsState.freeEntities;
    if (entity) {
        g_OS_WindowsGraphicsState.freeEntities = entity->next;
        MEMSET(entity, 0, sizeof(*entity));
        return entity;
    }

    Arena* arena = g_OS_WindowsGraphicsState.entityArena;
    if (!arena) {
        return 0;
    }
    entity = ARENA_PUSH_STRUCT(arena, OS_WINDOWS_GraphicsEntity);
    if (entity) {
        MEMSET(entity, 0, sizeof(*entity));
    }
    return entity;
}

static void free_OS_graphics_entity(OS_WINDOWS_GraphicsEntity* entity) {
    if (!entity) {
        return;
    }
    OS_WINDOWS_GraphicsEntity* next = g_OS_WindowsGraphicsState.freeEntities;
    MEMSET(entity, 0, sizeof(*entity));
    entity->next = next;
    g_OS_WindowsGraphicsState.freeEntities = entity;
}
