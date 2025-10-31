//
// Created by André Leite on 26/07/2025.
//

#import <Cocoa/Cocoa.h>
#include "os_graphics_macos.hpp"
#include "../os_graphics.hpp"

@interface OS_MacOS_AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation OS_MacOS_AppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    return NSTerminateCancel;
}
@end

static OS_MACOS_GraphicsEntity* alloc_OS_graphics_entity() {
    OS_MACOS_GraphicsEntity* entity = g_OS_MacOSGraphicsState.freeEntities;
    if (entity) {
        g_OS_MacOSGraphicsState.freeEntities = entity->next;
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
    entity->next = g_OS_MacOSGraphicsState.freeEntities;
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
            [window release];
            entity->window.window = nil;
        }
    }

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

    return (__bridge void*) entity->window.window;
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

    @autoreleasepool {
        NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (event) {
            [NSApp sendEvent:event];
            [NSApp updateWindows];
            return 1;
        }
    }

    return 0;
}

