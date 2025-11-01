//
// Created by André Leite on 26/07/2025.
//

#pragma once

#ifdef __OBJC__
@class NSApplication;
@class NSWindow;
@class NSObject;
@class NSView;
#else
typedef void NSApplication;
typedef void NSWindow;
typedef void NSObject;
typedef void NSView;
#endif

enum OS_MACOS_GraphicsEntityType : U64 {
    OS_MACOS_GraphicsEntityType_Invalid = 0,
    OS_MACOS_GraphicsEntityType_Window  = 1,
};

struct OS_MACOS_GraphicsEntity {
    OS_MACOS_GraphicsEntity* next;
    OS_MACOS_GraphicsEntity* activeNext;
    OS_MACOS_GraphicsEntityType type;

    union {
        struct {
            NSWindow* window;
            void* windowDelegate;
        } window;
    };
};

static OS_MACOS_GraphicsEntity* alloc_OS_graphics_entity();
static void free_OS_graphics_entity(OS_MACOS_GraphicsEntity* entity);

struct OS_MACOS_GraphicsState {
    NSApplication* application;
    B32 initialized;
    Arena* entityArena;
    OS_MACOS_GraphicsEntity* freeEntities;
    OS_MACOS_GraphicsEntity* activeEntities;
    OS_GraphicsEventQueue eventQueue;
};

static OS_MACOS_GraphicsState g_OS_MacOSGraphicsState = {};
