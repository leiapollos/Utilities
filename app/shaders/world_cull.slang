#include "gfx_shader_abi.slang"
#include "shader_records.generated.hpp"

#if GFX_SHADER_STAGE_COMPUTE

static const uint WORLD_COMPACT_GROUP_SIZE = 256u;
static const uint WORLD_ARGS_STRIDE_WORDS = 5u;

groupshared uint sharedScan[WORLD_COMPACT_GROUP_SIZE];
groupshared uint sharedChunkBase;

[shader("compute")]
[numthreads(64, 1, 1)]
void world_reset(uint3 threadId : SV_DispatchThreadID) {
    WorldCullRootData rootData = shd_load_WorldCullRootData_root();
    if (threadId.x < rootData.cellCount) {
        gfx_store_buffer_word(rootData.cellCountBuffer, rootData.cellCountByteOffset, threadId.x, 0u);
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void world_cull(uint3 threadId : SV_DispatchThreadID) {
    WorldCullRootData rootData = shd_load_WorldCullRootData_root();
    uint renderableIndex = threadId.x;
    if (renderableIndex >= rootData.renderableCount) {
        return;
    }

    WorldRenderableRecord renderable = shd_load_WorldRenderableRecord(rootData.renderableBuffer, rootData.renderableByteOffset, renderableIndex);
    WorldFrameRecord frame = shd_load_WorldFrameRecord(rootData.frameBuffer, rootData.frameByteOffset, 0u);

    float3 center = float3(renderable.boundsCenter[0], renderable.boundsCenter[1], renderable.boundsCenter[2]);
    float radius = renderable.boundsRadius;

    uint visible = 1u;
    [unroll] for (uint planeIndex = 0u; planeIndex < 6u; ++planeIndex) {
        float4 plane = float4(frame.frustumPlanes[planeIndex * 4u + 0u],
                              frame.frustumPlanes[planeIndex * 4u + 1u],
                              frame.frustumPlanes[planeIndex * 4u + 2u],
                              frame.frustumPlanes[planeIndex * 4u + 3u]);
        float distance = dot(plane.xyz, center) + plane.w;
        if (distance < -radius) {
            visible = 0u;
        }
    }

    gfx_store_buffer_word(rootData.flagsBuffer, rootData.flagsByteOffset, renderableIndex, visible);
    if (visible != 0u) {
        gfx_atomic_add_buffer_word(rootData.cellCountBuffer, rootData.cellCountByteOffset, renderable.cellIndex, 1u);
    }
}

[shader("compute")]
[numthreads(1, 1, 1)]
void world_prefix(uint3 threadId : SV_DispatchThreadID) {
    WorldCullRootData rootData = shd_load_WorldCullRootData_root();
    uint running = 0u;
    for (uint cell = 0u; cell < rootData.cellCount; ++cell) {
        gfx_store_buffer_word(rootData.cellOffsetBuffer, rootData.cellOffsetByteOffset, cell, running);
        running += gfx_load_buffer_word(rootData.cellCountBuffer, rootData.cellCountByteOffset, cell);
    }
}

// One threadgroup per cell; walks all renderables in chunks, keeping
// submission order within the cell (transparent bins rely on it).
[shader("compute")]
[numthreads(256, 1, 1)]
void world_compact(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID) {
    WorldCullRootData rootData = shd_load_WorldCullRootData_root();
    uint cell = groupId.x;
    if (cell >= rootData.cellCount) {
        return;
    }
    uint cellBase = gfx_load_buffer_word(rootData.cellOffsetBuffer, rootData.cellOffsetByteOffset, cell);

    if (localId.x == 0u) {
        sharedChunkBase = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint chunkStart = 0u; chunkStart < rootData.renderableCount; chunkStart += WORLD_COMPACT_GROUP_SIZE) {
        uint renderableIndex = chunkStart + localId.x;
        uint belongs = 0u;
        if (renderableIndex < rootData.renderableCount) {
            uint visible = gfx_load_buffer_word(rootData.flagsBuffer, rootData.flagsByteOffset, renderableIndex);
            if (visible != 0u) {
                WorldRenderableRecord renderable = shd_load_WorldRenderableRecord(rootData.renderableBuffer, rootData.renderableByteOffset, renderableIndex);
                belongs = (renderable.cellIndex == cell) ? 1u : 0u;
            }
        }

        sharedScan[localId.x] = belongs;
        GroupMemoryBarrierWithGroupSync();

        [unroll] for (uint step = 1u; step < WORLD_COMPACT_GROUP_SIZE; step <<= 1u) {
            uint value = (localId.x >= step) ? sharedScan[localId.x - step] : 0u;
            GroupMemoryBarrierWithGroupSync();
            sharedScan[localId.x] += value;
            GroupMemoryBarrierWithGroupSync();
        }

        if (belongs != 0u) {
            uint localPrefix = sharedScan[localId.x] - 1u;
            gfx_store_buffer_word(rootData.visibleBuffer, rootData.visibleByteOffset,
                                  cellBase + sharedChunkBase + localPrefix, renderableIndex);
        }
        GroupMemoryBarrierWithGroupSync();

        if (localId.x == 0u) {
            sharedChunkBase += sharedScan[WORLD_COMPACT_GROUP_SIZE - 1u];
        }
        GroupMemoryBarrierWithGroupSync();
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void world_args(uint3 threadId : SV_DispatchThreadID) {
    WorldCullRootData rootData = shd_load_WorldCullRootData_root();
    uint cell = threadId.x;
    if (cell >= rootData.cellCount) {
        return;
    }

    uint meshIndex = cell % rootData.meshCount;
    WorldMeshRecord mesh = shd_load_WorldMeshRecord(rootData.meshBuffer, rootData.meshByteOffset, meshIndex);
    uint instanceCount = gfx_load_buffer_word(rootData.cellCountBuffer, rootData.cellCountByteOffset, cell);

    uint argsWord = cell * WORLD_ARGS_STRIDE_WORDS;
    gfx_store_buffer_word(rootData.argsBuffer, rootData.argsByteOffset, argsWord + 0u, mesh.indexCount);
    gfx_store_buffer_word(rootData.argsBuffer, rootData.argsByteOffset, argsWord + 1u, instanceCount);
    gfx_store_buffer_word(rootData.argsBuffer, rootData.argsByteOffset, argsWord + 2u, mesh.firstIndex);
    gfx_store_buffer_word(rootData.argsBuffer, rootData.argsByteOffset, argsWord + 3u, mesh.baseVertex);
    gfx_store_buffer_word(rootData.argsBuffer, rootData.argsByteOffset, argsWord + 4u, 0u);
}

#endif
