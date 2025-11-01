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

static void os_push_graphics_event(OS_GraphicsEvent event) {
    OS_GraphicsEventQueue* queue = &g_OS_MacOSGraphicsState.eventQueue;

    queue->events[queue->writeIndex] = event;
    queue->writeIndex = (queue->writeIndex + 1u) % OS_GRAPHICS_EVENT_CAPACITY;

    if (queue->count == OS_GRAPHICS_EVENT_CAPACITY) {
        queue->readIndex = queue->writeIndex;
    } else {
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
    graphicsEvent.mouse.clickCount = (U32) [event clickCount];

    graphicsEvent.mouse.globalX = (F32) screenPoint.x;
    graphicsEvent.mouse.globalY = (F32) screenPoint.y;
    graphicsEvent.mouse.isInWindow = isInWindow;

    if (type == OS_GraphicsEventType_MouseScroll) {
        graphicsEvent.mouse.deltaX = (F32) [event scrollingDeltaX];
        graphicsEvent.mouse.deltaY = (F32) [event scrollingDeltaY];
    }

    os_push_graphics_event(graphicsEvent);
}

static void os_push_key_event(NSEvent* event, enum OS_GraphicsEventType type) {
    OS_GraphicsEvent graphicsEvent = {};
    graphicsEvent.type = type;
    graphicsEvent.window = os_window_handle_from_event(event);
    if (!graphicsEvent.window.handle) {
        return;
    }
    graphicsEvent.key.scanCode = (U32) [event keyCode];
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

static B32 OS_graphics_init() {
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
        g_OS_MacOSGraphicsState.freeEntities = 0;
        g_OS_MacOSGraphicsState.activeEntities = 0;
        MEMSET(&g_OS_MacOSGraphicsState.eventQueue, 0, sizeof(OS_GraphicsEventQueue));
    }

    return 1;
}

static void OS_graphics_shutdown() {
    if (!g_OS_MacOSGraphicsState.initialized) {
        return;
    }

    @autoreleasepool {
        ASSERT_DEBUG(g_OS_MacOSGraphicsState.activeEntities == 0);

        if (g_OS_MacOSGraphicsState.entityArena) {
            arena_release(g_OS_MacOSGraphicsState.entityArena);
            g_OS_MacOSGraphicsState.entityArena = 0;
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

static OS_WindowHandle OS_window_create(OS_WindowDesc desc) {
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

static void OS_window_destroy(OS_WindowHandle windowHandle) {
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

static void* OS_window_get_native_handle(OS_WindowHandle windowHandle) {
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

static OS_WindowSurfaceInfo OS_window_get_surface_info(OS_WindowHandle windowHandle) {
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

static B32 OS_window_is_open(OS_WindowHandle windowHandle) {
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

static U32 OS_graphics_poll_events(OS_GraphicsEvent* outEvents, U32 maxEvents) {
    if (!outEvents || maxEvents == 0) {
        return 0;
    }

    OS_GraphicsEventQueue* queue = &g_OS_MacOSGraphicsState.eventQueue;
    U32 count = 0;

    while (count < maxEvents && queue->count > 0) {
        outEvents[count] = queue->events[queue->readIndex];
        queue->readIndex = (queue->readIndex + 1u) % OS_GRAPHICS_EVENT_CAPACITY;
        queue->count -= 1u;
        count += 1u;
    }

    return count;
}

static B32 OS_graphics_pump_events() {
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

