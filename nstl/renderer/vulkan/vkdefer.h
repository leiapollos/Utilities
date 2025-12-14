//
// Created by André Leite on 03/11/2025.
//

#ifndef VKDEFER_H
#define VKDEFER_H

#ifndef VKDEFER_MAX_FRAMES
#define VKDEFER_MAX_FRAMES 3
#endif

// Define VKDEFER_ENABLE_VMA in your build to enable VMA integration
#ifdef VKDEFER_ENABLE_VMA
#include <vk_mem_alloc.h>
#define VKDEFER_HAS_VMA 1
#else
#define VKDEFER_HAS_VMA 0
#endif

#if VKDEFER_HAS_VMA
// Composite handles for VMA-managed resources. VmaAllocator is passed via ctx->user at flush time.
struct VmaImage {
    VkImage image;
    VmaAllocation allocation;
};

struct VmaBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
};
#endif

// VMA shims use 'user' as VmaAllocator inside invokers
#if VKDEFER_HAS_VMA
#define VKDEFER_CALL_VMA_IMAGE(dev, h, alloc)                                   \
  do {                                                                           \
    if ((h).image) {                                                             \
      vmaDestroyImage((VmaAllocator)user, (h).image, (h).allocation);            \
    }                                                                            \
  } while (0)

#define VKDEFER_CALL_VMA_BUFFER(dev, h, alloc)                                   \
  do {                                                                           \
    if ((h).buffer) {                                                            \
      vmaDestroyBuffer((VmaAllocator)user, (h).buffer, (h).allocation);          \
    }                                                                            \
  } while (0)

#define VKDEFER_CALL_vmaDestroyAllocator(dev, h, alloc)                          \
  vmaDestroyAllocator((VmaAllocator)(h))
#else
#define VKDEFER_CALL_vmaDestroyAllocator(dev, h, alloc) ((void)0)
#endif

// Stage table: lower index runs earlier at flush
// VmaImage and VmaBuffer are handled specially due to macro wrappers
#define VKDEFER_STAGES                                                          \
  VKDEFER_STAGE(VkFramebuffer,          vkDestroyFramebuffer,            1)     \
  VKDEFER_STAGE(VkPipeline,             vkDestroyPipeline,               1)     \
  VKDEFER_STAGE(VkDescriptorSet,        0,                               0)     \
  VKDEFER_STAGE(VkImageView,            vkDestroyImageView,              1)     \
  VKDEFER_STAGE(VkBufferView,           vkDestroyBufferView,             1)     \
  VKDEFER_STAGE(VkSampler,              vkDestroySampler,                1)     \
  VKDEFER_STAGE(VkImage,                vkDestroyImage,                  1)     \
  VKDEFER_STAGE(VkBuffer,               vkDestroyBuffer,                 1)     \
  VKDEFER_STAGE(Memory,                 0,                               0)     \
  VKDEFER_STAGE(VkPipelineLayout,       vkDestroyPipelineLayout,         1)     \
  VKDEFER_STAGE(VkDescriptorSetLayout,  vkDestroyDescriptorSetLayout,    1)     \
  VKDEFER_STAGE(VkRenderPass,           vkDestroyRenderPass,             1)     \
  VKDEFER_STAGE(VkDescriptorPool,       vkDestroyDescriptorPool,         1)     \
  VKDEFER_STAGE(VkCommandPool,          vkDestroyCommandPool,            1)     \
  VKDEFER_STAGE(VkPipelineCache,        vkDestroyPipelineCache,          1)     \
  VKDEFER_STAGE(VkShaderModule,         vkDestroyShaderModule,           1)     \
  VKDEFER_STAGE(VkSemaphore,            vkDestroySemaphore,              1)     \
  VKDEFER_STAGE(VkFence,                vkDestroyFence,                  1)     \
  VKDEFER_STAGE(VkEvent,                vkDestroyEvent,                  1)     \
  VKDEFER_STAGE(VkSwapchainKHR,         vkDestroySwapchainKHR,           1)     \
  VKDEFER_STAGE(VmaAllocator,           VKDEFER_CALL_vmaDestroyAllocator, VKDEFER_HAS_VMA)

// VMA stages need separate enum entries but are handled specially
#if VKDEFER_HAS_VMA
#define VKDEFER_VMA_STAGES                                                      \
  VKDEFER_STAGE(VmaImage,               VKDEFER_CALL_VMA_IMAGE,          1)     \
  VKDEFER_STAGE(VmaBuffer,              VKDEFER_CALL_VMA_BUFFER,         1)
#else
#define VKDEFER_VMA_STAGES
#endif

enum VkDeferStage {
#define VKDEFER_STAGE(TokenOrType, CALL, HAS) VkDeferStage_##TokenOrType,
    VKDEFER_STAGES
    VKDEFER_VMA_STAGES
#undef VKDEFER_STAGE
    VkDeferStage_Count
};

typedef void (*VkDeferFn)(VkDevice device,
                          const void* payload,
                          VkAllocationCallbacks* alloc,
                          void* user);

struct alignas(8) VkDeferHeader {
    VkDeferFn fn;
    U32 payloadSize;
    U32 payloadAlign;
    U8 stage;
};

struct VkDeferBuffer {
    U8* base;
    U32 cap;
    U32 pos;
};

struct VkDeferCtx {
    VkDevice device;
    VkAllocationCallbacks* alloc;
    U32 framesInFlight;

    VkDeferBuffer frameBuf[VKDEFER_MAX_FRAMES];
    VkDeferBuffer globalBuf;

    // For VMA: set this to VmaAllocator
    void* user;
};

// ////////////////////////
// Function Declarations

VkDeferBuffer vkdefer_buffer_make(void* base, U32 capBytes);

void* vkdefer_push_staged(VkDeferBuffer* db,
                          VkDeferFn fn,
                          VkDeferStage stage,
                          U32 payloadSize,
                          U32 payloadAlign);

void vkdefer_exec_and_reset(VkDeferBuffer* db,
                            VkDevice device,
                            VkAllocationCallbacks* alloc,
                            void* user);

const char* vkdefer_stage_name(VkDeferStage s);

void vkdefer_init_memory(VkDeferCtx* ctx,
                         U32 framesInFlight,
                         void* perFrameMem,
                         U32 perFrameBytes,
                         void* globalMem,
                         U32 globalBytes);

void vkdefer_init_device(VkDeferCtx* ctx,
                         VkDevice device,
                         VkAllocationCallbacks* alloc);

void vkdefer_init(VkDeferCtx* ctx,
                  VkDevice device,
                  VkAllocationCallbacks* alloc,
                  U32 framesInFlight,
                  void* perFrameMem,
                  U32 perFrameBytes,
                  void* globalMem,
                  U32 globalBytes);

void vkdefer_set_user(VkDeferCtx* ctx, void* user);

#if VKDEFER_HAS_VMA
void vkdefer_set_vma_allocator(VkDeferCtx* ctx, VmaAllocator vma);
#endif

void vkdefer_begin_frame(VkDeferCtx* ctx, U32 frameIndex);

void vkdefer_shutdown(VkDeferCtx* ctx);

VkDeferBuffer* vkdefer_frame_buf(VkDeferCtx* ctx, U32 frameIndex);

VkDeferBuffer* vkdefer_global_buf(VkDeferCtx* ctx);

// ////////////////////////
// Conditional Macro Helpers

#define VKDEFER_IF_0(...)
#define VKDEFER_IF_1(...) __VA_ARGS__
#define VKDEFER_IF(c) NAME_CONCAT(VKDEFER_IF_, c)

// ////////////////////////
// Destroy Helper Declarations (generated by macro)

#define VKDEFER_DECLARE_DESTROY(name, HandleT)                                   \
  void vkdefer_destroy_##name(VkDeferBuffer* db, HandleT handle);

#define VKDEFER_STAGE(TokenOrType, CALL, HAS)                                    \
  VKDEFER_IF(HAS)(VKDEFER_DECLARE_DESTROY(TokenOrType, TokenOrType))
VKDEFER_STAGES
#undef VKDEFER_STAGE

// QueryPool declaration
void vkdefer_destroy_QueryPool(VkDeferBuffer* db, VkQueryPool handle);

// Memory and descriptor set declarations
void vkdefer_free_memory(VkDeferBuffer* db, VkDeviceMemory memory);

void vkdefer_free_descriptor_set(VkDeferBuffer* db,
                                 VkDescriptorPool pool,
                                 VkDescriptorSet set);

// VMA declarations - functions use _ suffix because they're wrapped by macros
#if VKDEFER_HAS_VMA
VmaImage vkdefer_make_VmaImage(VkImage image, VmaAllocation allocation);
VmaBuffer vkdefer_make_VmaBuffer(VkBuffer buffer, VmaAllocation allocation);

void vkdefer_destroy_VmaImage_(VkDeferBuffer* db, VmaImage handle);
void vkdefer_destroy_VmaBuffer_(VkDeferBuffer* db, VmaBuffer handle);

#define vkdefer_destroy_VmaImage(db, image, allocation)                         \
  vkdefer_destroy_VmaImage_((db), vkdefer_make_VmaImage((image), (allocation)))

#define vkdefer_destroy_VmaBuffer(db, buffer, allocation)                       \
  vkdefer_destroy_VmaBuffer_((db), vkdefer_make_VmaBuffer((buffer), (allocation)))
#endif

// ////////////////////////
// Implementation

#ifdef VKDEFER_IMPLEMENTATION

VkDeferBuffer vkdefer_buffer_make(void* base, U32 capBytes) {
    VkDeferBuffer b;
    b.base = (U8*)base;
    b.cap = capBytes;
    b.pos = 0;
    return b;
}

void* vkdefer_push_staged(VkDeferBuffer* db,
                          VkDeferFn fn,
                          VkDeferStage stage,
                          U32 payloadSize,
                          U32 payloadAlign) {
    if (payloadAlign == 0) {
        payloadAlign = 1;
    }

    U32 at = (U32)align_pow2((U64)db->pos, 8);
    U32 after = at + (U32)sizeof(VkDeferHeader);
    U32 payloadAt = (U32)align_pow2((U64)after, (U64)payloadAlign);
    U32 end = (U32)align_pow2((U64)payloadAt + (U64)payloadSize, 8);

    ASSERT_ALWAYS(end <= db->cap && "vkdefer: buffer overflow");

    VkDeferHeader* h = (VkDeferHeader*)(db->base + at);
    h->fn = fn;
    h->payloadSize = payloadSize;
    h->payloadAlign = payloadAlign;
    h->stage = (U8)stage;

    db->pos = end;
    return (void*)(db->base + payloadAt);
}

void vkdefer_exec_and_reset(VkDeferBuffer* db,
                            VkDevice device,
                            VkAllocationCallbacks* alloc,
                            void* user) {
    for (U32 s = 0; s < (U32)VkDeferStage_Count; ++s) {
        U32 at = 0;
        while (at < db->pos) {
            VkDeferHeader* h = (VkDeferHeader*)(db->base + at);
            U32 after = at + (U32)sizeof(VkDeferHeader);
            U32 payloadAt = (U32)align_pow2((U64)after, (U64)h->payloadAlign);
            const void* payload = (const void*)(db->base + payloadAt);

            if (h->stage == (U8)s) {
                h->fn(device, payload, alloc, user);
            }
            U32 next = (U32)align_pow2((U64)payloadAt + (U64)h->payloadSize, 8);
            at = next;
        }
    }
    db->pos = 0;
}

const char* vkdefer_stage_name(VkDeferStage s) {
    switch (s) {
#define VKDEFER_STAGE(TokenOrType, CALL, HAS) \
  case VkDeferStage_##TokenOrType:            \
    return #TokenOrType;
        VKDEFER_STAGES
        VKDEFER_VMA_STAGES
#undef VKDEFER_STAGE
        default:
            return "UNKNOWN";
    }
}

void vkdefer_init_memory(VkDeferCtx* ctx,
                         U32 framesInFlight,
                         void* perFrameMem,
                         U32 perFrameBytes,
                         void* globalMem,
                         U32 globalBytes) {
    ASSERT_ALWAYS(framesInFlight >= 1 &&
                  framesInFlight <= VKDEFER_MAX_FRAMES);
    ctx->device = VK_NULL_HANDLE;
    ctx->alloc = 0;
    ctx->framesInFlight = framesInFlight;
    ctx->user = 0;

    U8* p = (U8*)perFrameMem;
    for (U32 i = 0; i < framesInFlight; ++i) {
        ctx->frameBuf[i] = vkdefer_buffer_make(p, perFrameBytes);
        p += perFrameBytes;
    }
    ctx->globalBuf = vkdefer_buffer_make(globalMem, globalBytes);
}

void vkdefer_init_device(VkDeferCtx* ctx,
                         VkDevice device,
                         VkAllocationCallbacks* alloc) {
    ctx->device = device;
    ctx->alloc = alloc;
}

void vkdefer_init(VkDeferCtx* ctx,
                  VkDevice device,
                  VkAllocationCallbacks* alloc,
                  U32 framesInFlight,
                  void* perFrameMem,
                  U32 perFrameBytes,
                  void* globalMem,
                  U32 globalBytes) {
    vkdefer_init_memory(ctx, framesInFlight, perFrameMem, perFrameBytes,
                        globalMem, globalBytes);
    vkdefer_init_device(ctx, device, alloc);
}

void vkdefer_set_user(VkDeferCtx* ctx, void* user) {
    ctx->user = user;
}

#if VKDEFER_HAS_VMA
void vkdefer_set_vma_allocator(VkDeferCtx* ctx, VmaAllocator vma) {
    vkdefer_set_user(ctx, (void*)vma);
}
#endif

void vkdefer_begin_frame(VkDeferCtx* ctx, U32 frameIndex) {
    ASSERT_ALWAYS(frameIndex < ctx->framesInFlight);
    vkdefer_exec_and_reset(&ctx->frameBuf[frameIndex], ctx->device,
                           ctx->alloc, ctx->user);
}

void vkdefer_shutdown(VkDeferCtx* ctx) {
    vkdefer_exec_and_reset(&ctx->globalBuf, ctx->device, ctx->alloc,
                           ctx->user);
}

VkDeferBuffer* vkdefer_frame_buf(VkDeferCtx* ctx, U32 frameIndex) {
    ASSERT_ALWAYS(frameIndex < ctx->framesInFlight);
    return &ctx->frameBuf[frameIndex];
}

VkDeferBuffer* vkdefer_global_buf(VkDeferCtx* ctx) {
    return &ctx->globalBuf;
}

// Per-stage invoker. CALL is either vkDestroy* or VMA shim macro.
#define VKDEFER_STAGE(TokenOrType, CALL, HAS)                                   \
  VKDEFER_IF(HAS)(                                                               \
      void vkdefer_invoke_##TokenOrType(                                         \
          VkDevice dev, TokenOrType h, VkAllocationCallbacks* alloc,             \
          void* user) {                                                          \
        (void)user;                                                              \
        CALL(dev, h, alloc);                                                     \
      })

VKDEFER_STAGES
#undef VKDEFER_STAGE

// VMA invokers defined separately
#if VKDEFER_HAS_VMA
void vkdefer_invoke_VmaImage(VkDevice dev, VmaImage h,
                             VkAllocationCallbacks* alloc, void* user) {
    (void)alloc;
    if (h.image) {
        vmaDestroyImage((VmaAllocator)user, h.image, h.allocation);
    }
}

void vkdefer_invoke_VmaBuffer(VkDevice dev, VmaBuffer h,
                              VkAllocationCallbacks* alloc, void* user) {
    (void)alloc;
    if (h.buffer) {
        vmaDestroyBuffer((VmaAllocator)user, h.buffer, h.allocation);
    }
}
#endif

void vkdefer_invoke_QueryPool(VkDevice dev,
                              VkQueryPool h,
                              VkAllocationCallbacks* alloc,
                              void* user) {
    (void)user;
    vkDestroyQueryPool(dev, h, alloc);
}

// Destroy helper generator
#define VKDEFER_DEFINE_DESTROY(name, HandleT, STAGE_ENUM)                        \
  struct VkDeferPayload##name {                                                  \
    HandleT handle;                                                              \
  };                                                        \
  void vkdefer_call_##name(                                                      \
      VkDevice dev, const void* payload,                                         \
      VkAllocationCallbacks* alloc, void* user) {                                \
    const VkDeferPayload##name* p =                                              \
        (const VkDeferPayload##name*)payload;                                    \
    vkdefer_invoke_##name(dev, p->handle, alloc, user);                          \
  }                                                                              \
  void vkdefer_destroy_##name(VkDeferBuffer* db, HandleT handle) {               \
    VkDeferPayload##name* p =                                                    \
        (VkDeferPayload##name*)vkdefer_push_staged(                              \
            db, vkdefer_call_##name, STAGE_ENUM,                                 \
            (U32)sizeof(VkDeferPayload##name),                                   \
            alignof(VkDeferPayload##name));                                      \
    p->handle = handle;                                                          \
  }

#define VKDEFER_STAGE(TokenOrType, CALL, HAS)                                    \
  VKDEFER_IF(HAS)(VKDEFER_DEFINE_DESTROY(TokenOrType, TokenOrType,               \
                                         VkDeferStage_##TokenOrType))
VKDEFER_STAGES
#undef VKDEFER_STAGE

// VMA destroy helpers defined manually with _ suffix
#if VKDEFER_HAS_VMA
struct VkDeferPayloadVmaImage {
    VmaImage handle;
};

void vkdefer_call_VmaImage(VkDevice dev, const void* payload,
                           VkAllocationCallbacks* alloc, void* user) {
    const VkDeferPayloadVmaImage* p = (const VkDeferPayloadVmaImage*)payload;
    vkdefer_invoke_VmaImage(dev, p->handle, alloc, user);
}

void vkdefer_destroy_VmaImage_(VkDeferBuffer* db, VmaImage handle) {
    VkDeferPayloadVmaImage* p =
        (VkDeferPayloadVmaImage*)vkdefer_push_staged(
            db, vkdefer_call_VmaImage, VkDeferStage_VmaImage,
            (U32)sizeof(VkDeferPayloadVmaImage),
            alignof(VkDeferPayloadVmaImage));
    p->handle = handle;
}

struct VkDeferPayloadVmaBuffer {
    VmaBuffer handle;
};

void vkdefer_call_VmaBuffer(VkDevice dev, const void* payload,
                            VkAllocationCallbacks* alloc, void* user) {
    const VkDeferPayloadVmaBuffer* p = (const VkDeferPayloadVmaBuffer*)payload;
    vkdefer_invoke_VmaBuffer(dev, p->handle, alloc, user);
}

void vkdefer_destroy_VmaBuffer_(VkDeferBuffer* db, VmaBuffer handle) {
    VkDeferPayloadVmaBuffer* p =
        (VkDeferPayloadVmaBuffer*)vkdefer_push_staged(
            db, vkdefer_call_VmaBuffer, VkDeferStage_VmaBuffer,
            (U32)sizeof(VkDeferPayloadVmaBuffer),
            alignof(VkDeferPayloadVmaBuffer));
    p->handle = handle;
}
#endif  // VKDEFER_HAS_VMA

// QueryPool at the same stage as ShaderModule
VKDEFER_DEFINE_DESTROY(QueryPool,
                       VkQueryPool,
                       VkDeferStage_VkShaderModule)

// Extras that don't map 1:1 to a vkDestroy* in the table
struct VkDeferPayloadMemory {
    VkDeviceMemory memory;
};

void vkdefer_call_memory(VkDevice dev,
                         const void* payload,
                         VkAllocationCallbacks* alloc,
                         void* user) {
    (void)user;
    const VkDeferPayloadMemory* p = (const VkDeferPayloadMemory*)payload;
    if (p->memory) {
        vkFreeMemory(dev, p->memory, alloc);
    }
}

void vkdefer_free_memory(VkDeferBuffer* db, VkDeviceMemory memory) {
    VkDeferPayloadMemory* p =
        (VkDeferPayloadMemory*)vkdefer_push_staged(
            db, vkdefer_call_memory, VkDeferStage_Memory,
            (U32)sizeof(VkDeferPayloadMemory),
            alignof(VkDeferPayloadMemory));
    p->memory = memory;
}

// Descriptor set free (single set) — stage before pool destruction
struct VkDeferPayloadFreeSet {
    VkDescriptorPool pool;
    VkDescriptorSet set;
};

void vkdefer_call_free_set(VkDevice dev,
                           const void* payload,
                           VkAllocationCallbacks* alloc,
                           void* user) {
    (void)user;
    (void)alloc;
    const VkDeferPayloadFreeSet* p = (const VkDeferPayloadFreeSet*)payload;
    if (p->set && p->pool) {
        vkFreeDescriptorSets(dev, p->pool, 1, &p->set);
    }
}

void vkdefer_free_descriptor_set(VkDeferBuffer* db,
                                 VkDescriptorPool pool,
                                 VkDescriptorSet set) {
    VkDeferPayloadFreeSet* p =
        (VkDeferPayloadFreeSet*)vkdefer_push_staged(
            db, vkdefer_call_free_set,
            VkDeferStage_VkDescriptorSet,
            (U32)sizeof(VkDeferPayloadFreeSet),
            alignof(VkDeferPayloadFreeSet));
    p->pool = pool;
    p->set = set;
}

// VMA convenience wrappers
#if VKDEFER_HAS_VMA
VmaImage vkdefer_make_VmaImage(VkImage image, VmaAllocation allocation) {
    VmaImage h;
    h.image = image;
    h.allocation = allocation;
    return h;
}

VmaBuffer vkdefer_make_VmaBuffer(VkBuffer buffer, VmaAllocation allocation) {
    VmaBuffer h;
    h.buffer = buffer;
    h.allocation = allocation;
    return h;
}
#endif  // VKDEFER_HAS_VMA

#endif  // VKDEFER_IMPLEMENTATION

#endif  // VKDEFER_H
