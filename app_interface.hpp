//
// Created by André Leite on 02/11/2025.
//

#pragma once

#include "nstl/base/base_include.hpp"
#include "nstl/os/core/os_core.hpp"
#include "nstl/os/graphics/os_graphics.hpp"
#include "nstl/gfx/gfx_include.hpp"

#define APP_ABI_VERSION 23u
#define APP_STATE_SCHEMA_VERSION 2u
#if defined(PLATFORM_OS_WINDOWS)
#define APP_MODULE_SOURCE_RELATIVE "hot/utilities_app.dll"
#else
#define APP_MODULE_SOURCE_RELATIVE "hot/utilities_app.dylib"
#endif
#define HOT_STATE_STORE_MAGIC 0x4853544F52453031ull
#define HOT_STATE_STORE_VERSION 1u
#define HOT_STATE_STORE_MAX_SLOTS 64u

#define PLATFORM_OS_FUNCTIONS(X) \
    X(OS_graphics_init) \
    X(OS_graphics_shutdown) \
    X(OS_graphics_pump_events) \
    X(OS_graphics_poll_events) \
    X(OS_window_create) \
    X(OS_window_destroy) \
    X(OS_window_is_open) \
    X(OS_window_get_info) \
    X(OS_reserve) \
    X(OS_commit) \
    X(OS_decommit) \
    X(OS_release) \
    X(OS_set_environment_variable) \
    X(OS_get_environment_variable) \
    X(OS_execute) \
    X(OS_library_open) \
    X(OS_library_close) \
    X(OS_library_load_symbol) \
    X(OS_library_last_error) \
    X(OS_get_time_microseconds) \
    X(OS_get_time_nanoseconds) \
    X(OS_sleep_milliseconds) \
    X(OS_thread_create) \
    X(OS_thread_join) \
    X(OS_thread_detach) \
    X(OS_thread_yield) \
    X(OS_cpu_pause) \
    X(OS_get_thread_id_u32) \
    X(OS_mutex_create) \
    X(OS_mutex_destroy) \
    X(OS_mutex_lock) \
    X(OS_mutex_unlock) \
    X(OS_condition_variable_create) \
    X(OS_condition_variable_destroy) \
    X(OS_condition_variable_wait) \
    X(OS_condition_variable_signal) \
    X(OS_condition_variable_broadcast) \
    X(OS_barrier_create) \
    X(OS_barrier_destroy) \
    X(OS_barrier_wait) \
    X(OS_get_system_info) \
    X(OS_abort)

struct AppOSApi {
#define PLATFORM_DECLARE_OS_FN(name) decltype(&name) name;
    PLATFORM_OS_FUNCTIONS(PLATFORM_DECLARE_OS_FN)
#undef PLATFORM_DECLARE_OS_FN
};

#define APP_OS_FN(host, name) ((host)->os.name)
#define APP_OS_CALL(host, name, ...) ((host)->os.name(__VA_ARGS__))

enum HOT_StateSlotFlags {
    HOT_StateSlotFlag_None = 0,
    HOT_StateSlotFlag_NeedsInit = (1u << 0),
};

struct HOT_StateSlot {
    U64 id;
    U32 version;
    U32 flags;
    U64 offset;
    U64 size;
    U64 alignment;
};

struct HOT_StateStore {
    U64 magic;
    U32 version;
    U32 slotCount;
    U64 used;
    U64 capacity;
    U8* base;
    HOT_StateSlot slots[HOT_STATE_STORE_MAX_SLOTS];
};

static U64 hot_align_forward_(U64 value, U64 alignment) {
    if (alignment == 0u) {
        alignment = sizeof(void*);
    }
    ASSERT_ALWAYS(is_power_of_two(alignment));
    return align_pow2(value, alignment);
}

static void hot_state_store_init(HOT_StateStore* store, void* base, U64 capacity) {
    ASSERT_ALWAYS(store != 0);
    ASSERT_ALWAYS(base != 0);
    ASSERT_ALWAYS(capacity != 0u);

    MEMSET(store, 0, sizeof(*store));
    store->magic = HOT_STATE_STORE_MAGIC;
    store->version = HOT_STATE_STORE_VERSION;
    store->capacity = capacity;
    store->base = (U8*) base;
}

static B32 hot_state_store_is_valid(HOT_StateStore* store) {
    if (store == 0) {
        return 0;
    }
    if (store->magic != HOT_STATE_STORE_MAGIC) {
        return 0;
    }
    if (store->version != HOT_STATE_STORE_VERSION) {
        return 0;
    }
    if (store->base == 0 || store->capacity == 0u) {
        return 0;
    }
    return 1;
}

static HOT_StateSlot* hot_state_store_find_slot(HOT_StateStore* store, U64 id) {
    if (!hot_state_store_is_valid(store) || id == 0u) {
        return 0;
    }

    for (U32 index = 0; index < store->slotCount; ++index) {
        HOT_StateSlot* slot = &store->slots[index];
        if (slot->id == id) {
            return slot;
        }
    }

    return 0;
}

static void* hot_state_store_require(HOT_StateStore* store, U64 id, U32 version, U64 size, U64 alignment) {
    if (!hot_state_store_is_valid(store) || id == 0u || size == 0u) {
        return 0;
    }

    HOT_StateSlot* slot = hot_state_store_find_slot(store, id);
    if (slot != 0 && slot->version == version && slot->size >= size) {
        if ((slot->offset + slot->size) <= store->capacity) {
            return store->base + slot->offset;
        }
    }

    if (slot == 0) {
        if (store->slotCount >= HOT_STATE_STORE_MAX_SLOTS) {
            return 0;
        }
        slot = &store->slots[store->slotCount++];
        MEMSET(slot, 0, sizeof(*slot));
        slot->id = id;
    }

    U64 offset = hot_align_forward_(store->used, alignment);
    if ((offset + size) > store->capacity) {
        return 0;
    }

    slot->version = version;
    slot->flags = HOT_StateSlotFlag_NeedsInit;
    slot->offset = offset;
    slot->size = size;
    slot->alignment = alignment;
    store->used = offset + size;

    void* result = store->base + offset;
    MEMSET(result, 0, size);
    return result;
}

static B32 hot_state_store_take_needs_init(HOT_StateStore* store, U64 id) {
    HOT_StateSlot* slot = hot_state_store_find_slot(store, id);
    if (slot == 0 || ((slot->flags & HOT_StateSlotFlag_NeedsInit) == 0u)) {
        return 0;
    }

    slot->flags &= ~HOT_StateSlotFlag_NeedsInit;
    return 1;
}

struct AppHost {
    B32 shouldQuit;
    U32 logicalCoreCount;
    Arena* frameArena;
    Arena* stateArena;
    OS_WindowHandle window;
    GfxDevice* gfxDevice;
    U32 windowWidth;
    U32 windowHeight;

    AppOSApi os;
};

struct AppInput {
    F32 deltaSeconds;
    const OS_GraphicsEvent* events;
    U32 eventCount;
};

struct AppLoadParams {
    U32 size;
    U32 abiVersion;
    U64 moduleGeneration;
    AppHost* host;
    HOT_StateStore* store;
};

typedef B32 AppBootProc(AppHost* host, HOT_StateStore* store);
typedef void AppBeforeReloadProc(AppHost* host, HOT_StateStore* store);
typedef B32 AppAfterReloadProc(AppHost* host, HOT_StateStore* store);
typedef void AppFrameProc(AppHost* host, HOT_StateStore* store, const AppInput* input);
typedef void AppShutdownProc(AppHost* host, HOT_StateStore* store);

struct AppCode {
    U32 size;
    U32 abiVersion;
    U32 schemaVersion;
    AppBootProc* boot;
    AppBeforeReloadProc* before_reload;
    AppAfterReloadProc* after_reload;
    AppFrameProc* frame;
    AppShutdownProc* shutdown;
};

#if defined(PLATFORM_OS_WINDOWS)
#define APP_EXPORT extern "C" __declspec(dllexport)
#elif defined(COMPILER_CLANG) || defined(COMPILER_GCC)
#define APP_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define APP_EXPORT extern "C"
#endif

APP_EXPORT B32 app_load(AppLoadParams* params, AppCode* outCode);
