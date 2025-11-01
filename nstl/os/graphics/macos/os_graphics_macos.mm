//
// Created by André Leite on 26/07/2025.
//

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#include "os_graphics_macos.hpp"
#include "../os_graphics.hpp"

@interface OS_MacOS_AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation OS_MacOS_AppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    return NSTerminateCancel;
}
@end

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

    if (localEntity->window.window) {
        NSWindow* window = localEntity->window.window;
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
        memset(entity, 0, sizeof(OS_MACOS_GraphicsEntity));
        g_OS_MacOSGraphicsState.freeEntities = next;
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
    }

    return 1;
}

static void OS_graphics_shutdown() {
    if (!g_OS_MacOSGraphicsState.initialized) {
        return;
    }

    @autoreleasepool {
        if (g_OS_MacOSGraphicsState.entityArena) {
            arena_release(g_OS_MacOSGraphicsState.entityArena);
            g_OS_MacOSGraphicsState.entityArena = 0;
        }
        g_OS_MacOSGraphicsState.application = 0;
        g_OS_MacOSGraphicsState.initialized = 0;
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

    @autoreleasepool {
        if (entity->window.window) {
            NSWindow* window = entity->window.window;
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
            [NSApp sendEvent:event];
        }

        if (processedEvent) {
            [NSApp updateWindows];
        }
    }

    return processedEvent;
}

