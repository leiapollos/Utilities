//
// Created by André Leite on 26/07/2025.
//

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>


@interface OS_MacOS_AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation OS_MacOS_AppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    return NSTerminateCancel;
}
@end

static OS_WindowHandle os_make_window_handle_from_entity(OS_MACOS_GraphicsEntity* entity) {
    OS_WindowHandle handle = {0};
    if (!entity) {
        return handle;
    }
    handle.handle = (U64*) entity;
    return handle;
}

static void os_register_active_graphics_entity(OS_MACOS_GraphicsEntity* entity) {
    if (!entity) {
        return;
    }

    entity->activeNext = g_OS_MacOSGraphicsState.activeEntities;
    g_OS_MacOSGraphicsState.activeEntities = entity;
}

static void os_unregister_active_graphics_entity(OS_MACOS_GraphicsEntity* entity) {
    if (!entity) {
        return;
    }

    OS_MACOS_GraphicsEntity** current = &g_OS_MacOSGraphicsState.activeEntities;
    while (*current) {
        if (*current == entity) {
            *current = entity->activeNext;
            break;
        }
        current = &(*current)->activeNext;
    }

    entity->activeNext = 0;
}

static OS_MACOS_GraphicsEntity* os_find_graphics_entity_for_window(NSWindow* window) {
    if (!window) {
        return 0;
    }

    OS_MACOS_GraphicsEntity* current = g_OS_MacOSGraphicsState.activeEntities;
    while (current) {
        if (current->type == OS_MACOS_GraphicsEntityType_Window && current->window.window == window) {
            return current;
        }
        current = current->activeNext;
    }

    return 0;
}

static OS_WindowHandle os_make_window_handle_from_nswindow(NSWindow* window) {
    OS_MACOS_GraphicsEntity* entity = os_find_graphics_entity_for_window(window);
    return os_make_window_handle_from_entity(entity);
}

static OS_GraphicsEventNode* os_acquire_graphics_event_node() {
    OS_GraphicsEventQueue* queue = &g_OS_MacOSGraphicsState.eventQueue;
    OS_GraphicsEventNode* node = 0;
    FREELIST_POP(queue->freeList, node, next);
    if (node) {
        node->next = 0;
        return node;
    }

    Arena* arena = g_OS_MacOSGraphicsState.eventArena;
    if (!arena) {
        return 0;
    }

    OS_GraphicsEventNode* newNode = (OS_GraphicsEventNode*) arena_push(arena, sizeof(OS_GraphicsEventNode), alignof(OS_GraphicsEventNode));
    if (!newNode) {
        return 0;
    }

    MEMSET(newNode, 0, sizeof(OS_GraphicsEventNode));
    return newNode;
}

static void os_release_graphics_event_node(OS_GraphicsEventNode* node) {
    if (!node) {
        return;
    }
    MEMSET(&node->event, 0, sizeof(OS_GraphicsEvent));
    FREELIST_PUSH(g_OS_MacOSGraphicsState.eventQueue.freeList, node, next);
}

static void os_push_graphics_event(OS_GraphicsEvent event) {
    OS_GraphicsEventQueue* queue = &g_OS_MacOSGraphicsState.eventQueue;

    OS_GraphicsEventNode* node = os_acquire_graphics_event_node();
    if (!node) {
        return;
    }

    node->event = event;

    if (!queue->head) {
        queue->head = node;
        queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }

    if (queue->count < 0xFFFFFFFFu) {
        queue->count += 1u;
    }
}

static U32 os_translate_modifier_flags(NSEventModifierFlags flags) {
    U32 result = OS_KeyModifiers_None;

    if (FLAGS_HAS(flags, NSEventModifierFlagShift)) {
        result |= OS_KeyModifiers_Shift;
    }
    if (FLAGS_HAS(flags, NSEventModifierFlagControl)) {
        result |= OS_KeyModifiers_Control;
    }
    if (FLAGS_HAS(flags, NSEventModifierFlagOption)) {
        result |= OS_KeyModifiers_Alt;
    }
    if (FLAGS_HAS(flags, NSEventModifierFlagCommand)) {
        result |= OS_KeyModifiers_Super;
    }
    if (FLAGS_HAS(flags, NSEventModifierFlagFunction)) {
        result |= OS_KeyModifiers_Function;
    }
    if (FLAGS_HAS(flags, NSEventModifierFlagCapsLock)) {
        result |= OS_KeyModifiers_CapsLock;
    }

    return result;
}

static enum OS_MouseButton os_translate_mouse_button_from_event(NSEvent* event) {
    enum OS_MouseButton button = OS_MouseButton_None;
    NSInteger buttonNumber = [event buttonNumber];
    if (buttonNumber == 0) {
        button = OS_MouseButton_Left;
    } else if (buttonNumber == 1) {
        button = OS_MouseButton_Right;
    } else if (buttonNumber == 2) {
        button = OS_MouseButton_Middle;
    } else if (buttonNumber == 3) {
        button = OS_MouseButton_Button4;
    } else if (buttonNumber == 4) {
        button = OS_MouseButton_Button5;
    }
    return button;
}

static OS_WindowHandle os_window_handle_from_event(NSEvent* event) {
    NSWindow* window = [event window];
    if (!window) {
        window = [NSApp keyWindow];
    }
    if (!window) {
        window = [NSApp mainWindow];
    }
    return os_make_window_handle_from_nswindow(window);
}

static void os_push_window_event(OS_MACOS_GraphicsEntity* entity, enum OS_GraphicsEventType type, NSWindow* window) {
    OS_GraphicsEvent event = {};
    event.type = type;
    event.window = os_make_window_handle_from_entity(entity);

    if (window) {
        NSRect frame = [window frame];
        event.windowEvent.width = (U32) frame.size.width;
        event.windowEvent.height = (U32) frame.size.height;
    }

    os_push_graphics_event(event);
}

static void os_push_mouse_event(NSEvent* event, enum OS_GraphicsEventType type) {
    OS_GraphicsEvent graphicsEvent = {};
    graphicsEvent.type = type;
    graphicsEvent.window = os_window_handle_from_event(event);

    NSWindow* window = [event window];
    if (!window && graphicsEvent.window.handle) {
        OS_MACOS_GraphicsEntity* entity = (OS_MACOS_GraphicsEntity*) graphicsEvent.window.handle;
        window = entity->window.window;
    }

    if (!graphicsEvent.window.handle) {
        return;
    }

    NSPoint screenPoint = [NSEvent mouseLocation];
    NSPoint windowPoint = screenPoint;
    NSPoint localPoint = screenPoint;
    B32 isInWindow = 0;

    if (window) {
        windowPoint = [window convertPointFromScreen:screenPoint];
        NSView* contentView = [window contentView];
        if (contentView) {
            localPoint = [contentView convertPoint:windowPoint fromView:nil];
            NSRect bounds = [contentView bounds];
            isInWindow = NSPointInRect(localPoint, bounds) ? 1 : 0;
        }
    }

    graphicsEvent.mouse.x = (F32) localPoint.x;
    graphicsEvent.mouse.y = (F32) localPoint.y;
    graphicsEvent.mouse.deltaX = (F32) [event deltaX];
    graphicsEvent.mouse.deltaY = (F32) [event deltaY];
    graphicsEvent.mouse.modifiers = os_translate_modifier_flags([event modifierFlags]);
    graphicsEvent.mouse.button = os_translate_mouse_button_from_event(event);

    graphicsEvent.mouse.globalX = (F32) screenPoint.x;
    graphicsEvent.mouse.globalY = (F32) screenPoint.y;
    graphicsEvent.mouse.isInWindow = isInWindow;

    if (type == OS_GraphicsEventType_MouseScroll) {
        graphicsEvent.mouse.deltaX = (F32) [event scrollingDeltaX];
        graphicsEvent.mouse.deltaY = (F32) [event scrollingDeltaY];
        graphicsEvent.mouse.clickCount = 0;
    } else if (type == OS_GraphicsEventType_MouseButtonDown || type == OS_GraphicsEventType_MouseButtonUp) {
        graphicsEvent.mouse.clickCount = (U32) [event clickCount];
    }

    os_push_graphics_event(graphicsEvent);
}

static enum OS_KeyCode os_key_code_from_character_(unichar c) {
    if (c >= 'a' && c <= 'z') {
        return (enum OS_KeyCode)(OS_KeyCode_A + (c - 'a'));
    }
    if (c >= 'A' && c <= 'Z') {
        return (enum OS_KeyCode)(OS_KeyCode_A + (c - 'A'));
    }
    if (c >= '0' && c <= '9') {
        return (enum OS_KeyCode)(OS_KeyCode_0 + (c - '0'));
    }

    switch (c) {
        case '-':
        case '_':
            return OS_KeyCode_Minus;
        case '=':
        case '+':
            return OS_KeyCode_Equal;
        case '[':
        case '{':
            return OS_KeyCode_LeftBracket;
        case ']':
        case '}':
            return OS_KeyCode_RightBracket;
        case ';':
        case ':':
            return OS_KeyCode_Semicolon;
        case '\'':
        case '"':
            return OS_KeyCode_Apostrophe;
        case ',':
        case '<':
            return OS_KeyCode_Comma;
        case '.':
        case '>':
            return OS_KeyCode_Period;
        case '/':
        case '?':
            return OS_KeyCode_Slash;
        case '\\':
        case '|':
            return OS_KeyCode_Backslash;
        case '`':
        case '~':
            return OS_KeyCode_GraveAccent;
        case ' ':
            return OS_KeyCode_Space;

        case NSEnterCharacter:
        case NSCarriageReturnCharacter:
        case NSNewlineCharacter:
            return OS_KeyCode_Enter;

        case NSTabCharacter:
        case NSBackTabCharacter:
            return OS_KeyCode_Tab;

        case NSBackspaceCharacter:
            return OS_KeyCode_Backspace;

        case NSDeleteCharacter:
            return OS_KeyCode_Delete;

        case NSUpArrowFunctionKey:
            return OS_KeyCode_UpArrow;
        case NSDownArrowFunctionKey:
            return OS_KeyCode_DownArrow;
        case NSLeftArrowFunctionKey:
            return OS_KeyCode_LeftArrow;
        case NSRightArrowFunctionKey:
            return OS_KeyCode_RightArrow;
        case NSHomeFunctionKey:
            return OS_KeyCode_Home;
        case NSEndFunctionKey:
            return OS_KeyCode_End;
        case NSPageUpFunctionKey:
            return OS_KeyCode_PageUp;
        case NSPageDownFunctionKey:
            return OS_KeyCode_PageDown;
        case NSDeleteFunctionKey:
            return OS_KeyCode_Delete;

        case NSF1FunctionKey:
            return OS_KeyCode_F1;
        case NSF2FunctionKey:
            return OS_KeyCode_F2;
        case NSF3FunctionKey:
            return OS_KeyCode_F3;
        case NSF4FunctionKey:
            return OS_KeyCode_F4;
        case NSF5FunctionKey:
            return OS_KeyCode_F5;
        case NSF6FunctionKey:
            return OS_KeyCode_F6;
        case NSF7FunctionKey:
            return OS_KeyCode_F7;
        case NSF8FunctionKey:
            return OS_KeyCode_F8;
        case NSF9FunctionKey:
            return OS_KeyCode_F9;
        case NSF10FunctionKey:
            return OS_KeyCode_F10;
        case NSF11FunctionKey:
            return OS_KeyCode_F11;
        case NSF12FunctionKey:
            return OS_KeyCode_F12;

        case 0x2318: /* Command (\u2318) */
            return OS_KeyCode_Super;
        case 0x2325: /* Option (\u2325) */
            return OS_KeyCode_Alt;
        case 0x2303: /* Control (\u2303) */
            return OS_KeyCode_Control;
        case 0x21E7: /* Shift (\u21E7) */
            return OS_KeyCode_Shift;
        case 0x21EA: /* Caps Lock (\u21EA) */
            return OS_KeyCode_CapsLock;
        default:
            break;
    }

    return OS_KeyCode_None;
}

static enum OS_KeyCode os_key_code_from_characters_(NSString* characters) {
    if (!characters || [characters length] == 0) {
        return OS_KeyCode_None;
    }

    unichar first = [characters characterAtIndex:0];
    enum OS_KeyCode key = os_key_code_from_character_(first);
    if (key != OS_KeyCode_None) {
        return key;
    }

    NSString* lowercase = [characters lowercaseString];
    if (lowercase && [lowercase length] > 0) {
        unichar lowered = [lowercase characterAtIndex:0];
        if (lowered != first) {
            key = os_key_code_from_character_(lowered);
            if (key != OS_KeyCode_None) {
                return key;
            }
        }
    }

    return OS_KeyCode_None;
}

static enum OS_KeyCode os_key_code_from_mac_virtual_key_(U16 keyCode) {
    enum MacVirtualKey {
        MacVirtualKey_Return        = 0x24,
        MacVirtualKey_Tab           = 0x30,
        MacVirtualKey_Space         = 0x31,
        MacVirtualKey_Delete        = 0x33,
        MacVirtualKey_Escape        = 0x35,
        MacVirtualKey_CapsLock      = 0x39,
        MacVirtualKey_LeftCommand   = 0x37,
        MacVirtualKey_RightCommand  = 0x36,
        MacVirtualKey_LeftShift     = 0x38,
        MacVirtualKey_RightShift    = 0x3C,
        MacVirtualKey_LeftOption    = 0x3A,
        MacVirtualKey_RightOption   = 0x3D,
        MacVirtualKey_LeftControl   = 0x3B,
        MacVirtualKey_RightControl  = 0x3E,
    };

    switch (keyCode) {
        case MacVirtualKey_Return:       return OS_KeyCode_Enter;
        case MacVirtualKey_Tab:          return OS_KeyCode_Tab;
        case MacVirtualKey_Space:        return OS_KeyCode_Space;
        case MacVirtualKey_Delete:       return OS_KeyCode_Backspace;
        case MacVirtualKey_Escape:       return OS_KeyCode_Escape;
        case MacVirtualKey_CapsLock:     return OS_KeyCode_CapsLock;
        case MacVirtualKey_LeftCommand:  return OS_KeyCode_LeftSuper;
        case MacVirtualKey_RightCommand: return OS_KeyCode_RightSuper;
        case MacVirtualKey_LeftShift:    return OS_KeyCode_LeftShift;
        case MacVirtualKey_RightShift:   return OS_KeyCode_RightShift;
        case MacVirtualKey_LeftOption:   return OS_KeyCode_LeftAlt;
        case MacVirtualKey_RightOption:  return OS_KeyCode_RightAlt;
        case MacVirtualKey_LeftControl:  return OS_KeyCode_LeftControl;
        case MacVirtualKey_RightControl: return OS_KeyCode_RightControl;
        default:
            return OS_KeyCode_None;
    }
}

static enum OS_KeyCode os_translate_macos_event_key_(NSEvent* event) {
    if (!event) {
        return OS_KeyCode_None;
    }

    enum OS_KeyCode key = os_key_code_from_characters_([event charactersIgnoringModifiers]);
    if (key != OS_KeyCode_None) {
        return key;
    }

    key = os_key_code_from_characters_([event characters]);
    if (key != OS_KeyCode_None) {
        return key;
    }

    return os_key_code_from_mac_virtual_key_((U16) [event keyCode]);
}

static void os_push_key_event(NSEvent* event, enum OS_GraphicsEventType type) {
    OS_GraphicsEvent graphicsEvent = {};
    graphicsEvent.type = type;
    graphicsEvent.window = os_window_handle_from_event(event);
    if (!graphicsEvent.window.handle) {
        return;
    }
    graphicsEvent.key.keyCode = os_translate_macos_event_key_(event);
    graphicsEvent.key.modifiers = os_translate_modifier_flags([event modifierFlags]);
    graphicsEvent.key.isRepeat = ([event isARepeat] != 0) ? 1 : 0;
    graphicsEvent.key.character = 0;

    NSString* characters = [event characters];
    if (characters && [characters length] > 0) {
        unichar code = [characters characterAtIndex:0];
        graphicsEvent.key.character = (U32) code;
    }

    os_push_graphics_event(graphicsEvent);

    if (type == OS_GraphicsEventType_KeyDown && characters && [characters length] > 0) {
        for (NSUInteger index = 0; index < [characters length]; ++index) {
            unichar code = [characters characterAtIndex:index];
            OS_GraphicsEvent textEvent = {};
            textEvent.type = OS_GraphicsEventType_TextInput;
            textEvent.window = graphicsEvent.window;
            textEvent.text.codepoint = (U32) code;
            textEvent.text.modifiers = graphicsEvent.key.modifiers;
            os_push_graphics_event(textEvent);
        }
    }
}

static void os_process_nsevent(NSEvent* event) {
    if (!event) {
        return;
    }

    NSEventType type = [event type];

    switch (type) {
        case NSEventTypeLeftMouseDown:
        case NSEventTypeRightMouseDown:
        case NSEventTypeOtherMouseDown:
            os_push_mouse_event(event, OS_GraphicsEventType_MouseButtonDown);
            break;

        case NSEventTypeLeftMouseUp:
        case NSEventTypeRightMouseUp:
        case NSEventTypeOtherMouseUp:
            os_push_mouse_event(event, OS_GraphicsEventType_MouseButtonUp);
            break;

        case NSEventTypeMouseMoved:
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeOtherMouseDragged:
            os_push_mouse_event(event, OS_GraphicsEventType_MouseMove);
            break;

        case NSEventTypeScrollWheel:
            os_push_mouse_event(event, OS_GraphicsEventType_MouseScroll);
            break;

        case NSEventTypeKeyDown:
            os_push_key_event(event, OS_GraphicsEventType_KeyDown);
            break;

        case NSEventTypeKeyUp:
            os_push_key_event(event, OS_GraphicsEventType_KeyUp);
            break;

        default:
            break;
    }
}

@interface OS_MacOS_WindowDelegate : NSObject <NSWindowDelegate>
{
@public
    OS_MACOS_GraphicsEntity* entity;
}
- (instancetype)initWithEntity:(OS_MACOS_GraphicsEntity*)entity;
@end

@implementation OS_MacOS_WindowDelegate
- (instancetype)initWithEntity:(OS_MACOS_GraphicsEntity*)entityParam {
    self = [super init];
    if (self) {
        entity = entityParam;
    }
    return self;
}

- (void)windowWillClose:(NSNotification*)notification {
    OS_MACOS_GraphicsEntity* localEntity = entity;
    if (!localEntity) {
        return;
    }
    if (localEntity->type != OS_MACOS_GraphicsEntityType_Window) {
        entity = 0;
        return;
    }

    NSWindow* window = localEntity->window.window;
    if (window) {
        os_push_window_event(localEntity, OS_GraphicsEventType_WindowClosed, window);
        localEntity->window.window = 0;
        [window setDelegate:nil];
        [window orderOut:nil];
        [window release];
    }

    if (localEntity->window.windowDelegate) {
        NSObject* delegateObj = (NSObject*) localEntity->window.windowDelegate;
        localEntity->window.windowDelegate = 0;
        entity = 0;
        [delegateObj release];
    } else {
        entity = 0;
    }
}

- (void)windowDidResize:(NSNotification *)notification {
    OS_MACOS_GraphicsEntity* localEntity = entity;
    if (!localEntity) {
        return;
    }
    if (localEntity->type != OS_MACOS_GraphicsEntityType_Window) {
        return;
    }

    NSWindow* window = localEntity->window.window;
    if (window) {
        os_push_window_event(localEntity, OS_GraphicsEventType_WindowResized, window);
    }
}

- (void)windowDidBecomeKey:(NSNotification *)notification {
    OS_MACOS_GraphicsEntity* localEntity = entity;
    if (!localEntity || localEntity->type != OS_MACOS_GraphicsEntityType_Window) {
        return;
    }
    os_push_window_event(localEntity, OS_GraphicsEventType_WindowFocused, localEntity->window.window);
}

- (void)windowDidResignKey:(NSNotification *)notification {
    OS_MACOS_GraphicsEntity* localEntity = entity;
    if (!localEntity || localEntity->type != OS_MACOS_GraphicsEntityType_Window) {
        return;
    }
    os_push_window_event(localEntity, OS_GraphicsEventType_WindowUnfocused, localEntity->window.window);
}
@end

static OS_MACOS_GraphicsEntity* alloc_OS_graphics_entity() {
    OS_MACOS_GraphicsEntity* entity = g_OS_MacOSGraphicsState.freeEntities;
    if (entity) {
        OS_MACOS_GraphicsEntity* next = entity->next;
        g_OS_MacOSGraphicsState.freeEntities = next;
        memset(entity, 0, sizeof(OS_MACOS_GraphicsEntity));
        return entity;
    }

    Arena* arena = g_OS_MacOSGraphicsState.entityArena;
    OS_MACOS_GraphicsEntity* newEntity = (OS_MACOS_GraphicsEntity*) arena_push(arena, sizeof(OS_MACOS_GraphicsEntity), alignof(OS_MACOS_GraphicsEntity));
    memset(newEntity, 0, sizeof(OS_MACOS_GraphicsEntity));
    return newEntity;
}

static void free_OS_graphics_entity(OS_MACOS_GraphicsEntity* entity) {
    if (!entity) {
        return;
    }

    os_unregister_active_graphics_entity(entity);

    OS_MACOS_GraphicsEntity* next = g_OS_MacOSGraphicsState.freeEntities;
    memset(entity, 0, sizeof(OS_MACOS_GraphicsEntity));
    entity->next = next;
    g_OS_MacOSGraphicsState.freeEntities = entity;
}

// ////////////////////////
// Graphics System

B32 OS_graphics_init() {
    if (g_OS_MacOSGraphicsState.initialized) {
        return 1;
    }

    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        OS_MacOS_AppDelegate* delegate = [[OS_MacOS_AppDelegate alloc] init];
        [app setDelegate:delegate];

        [NSApp finishLaunching];

        g_OS_MacOSGraphicsState.application = app;
        g_OS_MacOSGraphicsState.initialized = 1;

        Arena* entityArena = arena_alloc();
        g_OS_MacOSGraphicsState.entityArena = entityArena;
        Arena* eventArena = arena_alloc();
        g_OS_MacOSGraphicsState.eventArena = eventArena;
        g_OS_MacOSGraphicsState.freeEntities = 0;
        g_OS_MacOSGraphicsState.activeEntities = 0;
        MEMSET(&g_OS_MacOSGraphicsState.eventQueue, 0, sizeof(OS_GraphicsEventQueue));
    }

    return 1;
}

void OS_graphics_shutdown() {
    if (!g_OS_MacOSGraphicsState.initialized) {
        return;
    }

    @autoreleasepool {
        // Ensure all active graphics entities are released before shutdown.
        while (g_OS_MacOSGraphicsState.activeEntities) {
            OS_MACOS_GraphicsEntity* entity = g_OS_MacOSGraphicsState.activeEntities;
            OS_WindowHandle handle = os_make_window_handle_from_entity(entity);
            OS_window_destroy(handle);
        }

        ASSERT_DEBUG(g_OS_MacOSGraphicsState.activeEntities == 0);

        if (g_OS_MacOSGraphicsState.entityArena) {
            arena_release(g_OS_MacOSGraphicsState.entityArena);
            g_OS_MacOSGraphicsState.entityArena = 0;
        }
        if (g_OS_MacOSGraphicsState.eventArena) {
            arena_release(g_OS_MacOSGraphicsState.eventArena);
            g_OS_MacOSGraphicsState.eventArena = 0;
        }
        g_OS_MacOSGraphicsState.application = 0;
        g_OS_MacOSGraphicsState.initialized = 0;
        g_OS_MacOSGraphicsState.freeEntities = 0;
        g_OS_MacOSGraphicsState.activeEntities = 0;
        MEMSET(&g_OS_MacOSGraphicsState.eventQueue, 0, sizeof(OS_GraphicsEventQueue));
    }
}

// ////////////////////////
// Window Management

OS_WindowHandle OS_window_create(OS_WindowDesc desc) {
    if (!g_OS_MacOSGraphicsState.initialized) {
        OS_WindowHandle empty = {0};
        return empty;
    }

    OS_MACOS_GraphicsEntity* entity = alloc_OS_graphics_entity();
    entity->type = OS_MACOS_GraphicsEntityType_Window;
    os_register_active_graphics_entity(entity);

    @autoreleasepool {
        NSRect frame = NSMakeRect(0, 0, (CGFloat) desc.width, (CGFloat) desc.height);
        NSWindowStyleMask styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable;

        NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:styleMask
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];

        if (desc.title) {
            NSString* titleString = [NSString stringWithUTF8String:desc.title];
            [window setTitle:titleString];
        }

        NSView* contentView = [[NSView alloc] initWithFrame:frame];
        [contentView setWantsLayer:YES];

        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        [metalLayer setDevice:MTLCreateSystemDefaultDevice()];
        [metalLayer setPixelFormat:MTLPixelFormatBGRA8Unorm];
        NSScreen* screen = [window screen];
        CGFloat scaleFactor = (screen) ? [screen backingScaleFactor] : 1.0;
        [metalLayer setContentsScale:scaleFactor];
        [contentView setLayer:metalLayer];
        [contentView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

        [window setContentView:contentView];
        [contentView release];

        OS_MacOS_WindowDelegate* windowDelegate = [[OS_MacOS_WindowDelegate alloc] initWithEntity:entity];
        entity->window.windowDelegate = windowDelegate;
        [window setDelegate:windowDelegate];

        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        entity->window.window = [window retain];

        os_push_window_event(entity, OS_GraphicsEventType_WindowShown, window);
    }

    OS_WindowHandle handle = {(U64*) entity};
    return handle;
}

void OS_window_destroy(OS_WindowHandle windowHandle) {
    if (!windowHandle.handle) {
        return;
    }

    OS_MACOS_GraphicsEntity* entity = (OS_MACOS_GraphicsEntity*) windowHandle.handle;
    if (entity->type != OS_MACOS_GraphicsEntityType_Window) {
        return;
    }

    U32 windowWidth = 0;
    U32 windowHeight = 0;

    @autoreleasepool {
        if (entity->window.window) {
            NSWindow* window = entity->window.window;
            NSRect frame = [window frame];
            windowWidth = (U32) frame.size.width;
            windowHeight = (U32) frame.size.height;
            [window close];
            if (entity->window.window) {
                [window orderOut:nil];
                [window release];
                entity->window.window = 0;
            }
        }

        if (entity->window.windowDelegate) {
            NSObject* delegateObj = (NSObject*) entity->window.windowDelegate;
            entity->window.windowDelegate = 0;
            [delegateObj release];
        }
    }

    OS_GraphicsEvent destroyEvent = {};
    destroyEvent.type = OS_GraphicsEventType_WindowDestroyed;
    destroyEvent.window = os_make_window_handle_from_entity(entity);
    destroyEvent.windowEvent.width = windowWidth;
    destroyEvent.windowEvent.height = windowHeight;
    os_push_graphics_event(destroyEvent);

    entity->type = OS_MACOS_GraphicsEntityType_Invalid;
    free_OS_graphics_entity(entity);
}

void* OS_window_get_native_handle(OS_WindowHandle windowHandle) {
    if (!windowHandle.handle) {
        return 0;
    }

    OS_MACOS_GraphicsEntity* entity = (OS_MACOS_GraphicsEntity*) windowHandle.handle;
    if (entity->type != OS_MACOS_GraphicsEntityType_Window) {
        return 0;
    }

    NSView* contentView = [entity->window.window contentView];
    return (__bridge void*) contentView;
}

OS_WindowSurfaceInfo OS_window_get_surface_info(OS_WindowHandle windowHandle) {
    OS_WindowSurfaceInfo info = {};

    if (!windowHandle.handle) {
        return info;
    }

    OS_MACOS_GraphicsEntity* entity = (OS_MACOS_GraphicsEntity*) windowHandle.handle;
    if (entity->type != OS_MACOS_GraphicsEntityType_Window) {
        return info;
    }

    if (!entity->window.window) {
        return info;
    }

    @autoreleasepool {
        NSView* contentView = [entity->window.window contentView];
        if (contentView) {
            info.viewPtr = (__bridge void*) contentView;

            CALayer* layer = [contentView layer];
            if (layer && [layer isKindOfClass:[CAMetalLayer class]]) {
                info.metalLayerPtr = (__bridge void*) ((CAMetalLayer*) layer);
            }
        }
    }

    return info;
}

B32 OS_window_is_open(OS_WindowHandle windowHandle) {
    if (!windowHandle.handle) {
        return 0;
    }

    OS_MACOS_GraphicsEntity* entity = (OS_MACOS_GraphicsEntity*) windowHandle.handle;
    if (entity->type != OS_MACOS_GraphicsEntityType_Window) {
        return 0;
    }

    if (!entity->window.window) {
        return 0;
    }

    @autoreleasepool {
        NSWindow* window = entity->window.window;
        return ([window isVisible]) ? 1 : 0;
    }
}

U32 OS_graphics_poll_events(OS_GraphicsEvent* outEvents, U32 maxEvents) {
    if (!outEvents || maxEvents == 0) {
        return 0;
    }

    OS_GraphicsEventQueue* queue = &g_OS_MacOSGraphicsState.eventQueue;
    U32 count = 0;

    while (count < maxEvents) {
        OS_GraphicsEventNode* node = queue->head;
        if (!node) {
            break;
        }

        queue->head = node->next;
        if (!queue->head) {
            queue->tail = 0;
        }

        if (queue->count > 0) {
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
    if (!g_OS_MacOSGraphicsState.initialized) {
        return 0;
    }

    B32 processedEvent = 0;

    @autoreleasepool {
        while (1) {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantPast]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (!event) {
                break;
            }

            processedEvent = 1;
            os_process_nsevent(event);
            [NSApp sendEvent:event];
        }

        if (processedEvent) {
            [NSApp updateWindows];
        }
    }

    return processedEvent;
}

