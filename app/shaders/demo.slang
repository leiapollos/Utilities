#include "demo_shader_support.slang"

#if GFX_SHADER_STAGE_GRAPHICS
[shader("vertex")]
VertexOut triangle_vertex(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    DrawRootData rootData = gfx_load_draw_root_data();
    uint objectIndex = gfx_load_visible_object_index(rootData, instanceId);
    DemoObject object = gfx_load_demo_object(rootData, objectIndex);
    DemoVertex vertex = gfx_load_demo_vertex(rootData, vertexId);

    VertexOut output;
    float animationPhase = rootData.animationPhase + object.phaseOffset;
    float objectPhase = (float)object.objectId * 0.173;
    float scale = object.offsetScale.z * (1.0 + 0.055 * sin(animationPhase * 2.0 + objectPhase));
    float2 wobble = float2(sin(animationPhase * 1.7 + objectPhase),
                           cos(animationPhase * 1.3 + objectPhase)) * 0.014;
    float2 position = vertex.position * scale + object.offsetScale.xy + wobble;
    output.position = float4(position, object.offsetScale.w, 1.0);
    output.color = vertex.color;
    output.uv = float2(vertex.position.x * (1.0 / 1.1) + 0.5, 1.0 - (vertex.position.y + 0.45));
    output.materialIndex = object.materialIndex;
    output.objectId = object.objectId;
    output.flags = object.flags;
    output.animationPhase = animationPhase;
    output.depth = object.offsetScale.w;
    return output;
}

[shader("fragment")]
float4 triangle_fragment(VertexOut input) : SV_Target0 {
    DrawRootData rootData = gfx_load_draw_root_data();
    float leftEdge = input.uv.x - (0.5 - input.uv.y * 0.5);
    float rightEdge = (0.5 + input.uv.y * 0.5) - input.uv.x;
    float bottomEdge = 1.0 - input.uv.y;
    float edgeDistance = min(bottomEdge, min(leftEdge, rightEdge));
    float edgeWidth = max(fwidth(edgeDistance) * 1.8, 0.0015);
    if ((input.flags & GFX_DEMO_DRAW_FLAG_ALPHA_TEST) != 0u) {
        float2 cutoutCell = frac(input.uv * 2.5 + float2((float)(input.objectId & 3u) * 0.19,
                                                         (float)(input.objectId & 5u) * 0.11)) - 0.5;
        if (edgeDistance > edgeWidth * 8.0 && dot(cutoutCell, cutoutCell) < 0.045) {
            discard;
        }
    }

    Material material = gfx_load_material(rootData, input.materialIndex);
    float4 albedo = gfx_sample_albedo(material.albedoTexture, material.samplerIndex, input.uv);
    float brightness = 0.90 + 0.10 * sin(input.animationPhase * 3.0 + (float)input.objectId * 0.051);
    float4 color = input.color * material.baseColor * albedo;
    float depthShade = 1.20 - input.depth * 0.45;
    color.rgb *= brightness * depthShade;
    if ((input.flags & GFX_DEMO_DRAW_FLAG_TRANSPARENT) != 0u) {
        color.a *= 0.42;
    }

    float outline = 1.0 - smoothstep(0.0, edgeWidth, edgeDistance);
    color.rgb = lerp(color.rgb, float3(0.015, 0.018, 0.022), outline * 0.85);
    return color;
}
#endif

#if GFX_SHADER_STAGE_COMPUTE
bool demo_cull_object_visible(CullObject object) {
    bool xVisible = object.clipMax.x >= -1.0 && object.clipMin.x <= 1.0;
    bool yVisible = object.clipMax.y >= -1.0 && object.clipMin.y <= 1.0;
    bool zVisible = object.depthMax >= 0.0 && object.depthMin <= 1.0;
    return xVisible && yVisible && zVisible;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void demo_material_compute(uint3 dispatchThreadId : SV_DispatchThreadID) {
    MaterialComputeRootData rootData = gfx_load_compute_root_data();
    uint index = dispatchThreadId.x;
    if (index >= rootData.materialCount) {
        return;
    }

    Material source = gfx_load_source_material(rootData, index);
    float columnT = source.baseColor.x;
    float rowT = source.baseColor.y;
    float waveA = 0.5 + 0.5 * sin(rootData.animationPhase * 1.7 + columnT * 6.28318);
    float waveB = 0.5 + 0.5 * cos(rootData.animationPhase * 1.3 + rowT * 6.28318);
    float waveC = 0.5 + 0.5 * sin(rootData.animationPhase * 1.1 + (columnT + rowT) * 3.14159);

    Material material;
    material.baseColor = float4(0.25 + 0.45 * columnT + 0.20 * waveA,
                                0.25 + 0.45 * rowT + 0.20 * waveB,
                                0.55 + 0.30 * waveC,
                                1.0);
    material.albedoTexture = source.albedoTexture;
    material.samplerIndex = source.samplerIndex;
    material.flags = source.flags;
    material._padding = 0u;
    gfx_store_material(rootData, index, material);
}

[shader("compute")]
[numthreads(128, 1, 1)]
void demo_cull_bounds_compute(uint3 dispatchThreadId : SV_DispatchThreadID) {
    CullBoundsComputeRootData rootData = gfx_load_cull_bounds_compute_root_data();
    uint index = dispatchThreadId.x;
    if (index >= rootData.cullSourceCount) {
        return;
    }

    CullSource source = gfx_load_demo_cull_source(rootData, index);
    float objectScale = source.offsetScale.z * source.maxAnimationScale;
    float2 scaledMin = source.localMin * objectScale;
    float2 scaledMax = source.localMax * objectScale;
    float2 extentMin = min(scaledMin, scaledMax);
    float2 extentMax = max(scaledMin, scaledMax);
    float2 margin = float2(source.wobbleExtent, source.wobbleExtent);

    CullObject object;
    object.clipMin = extentMin + source.offsetScale.xy - margin;
    object.clipMax = extentMax + source.offsetScale.xy + margin;
    object.depthMin = source.offsetScale.w;
    object.depthMax = source.offsetScale.w;
    object.objectIndex = source.objectIndex;
    object.flags = source.flags;
    gfx_store_demo_cull_object(rootData, index, object);
}

groupshared uint gDemoVisibilityScan[128];

[shader("compute")]
[numthreads(128, 1, 1)]
void demo_visibility_count_compute(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID) {
    VisibilityComputeRootData rootData = gfx_load_visibility_compute_root_data();
    uint visibilityGroupIndex = groupId.x;
    if (visibilityGroupIndex >= rootData.groupCount) {
        return;
    }

    VisibilityGroup group = gfx_load_visibility_group(rootData, visibilityGroupIndex);
    uint localIndex = groupThreadId.x;
    uint activeCount = min(group.sourceIndexCount, GFX_DEMO_VISIBILITY_THREADS_PER_GROUP);
    uint visible = 0u;
    if (localIndex < activeCount) {
        uint sourceIndexByteOffset = group.sourceIndexByteOffset + localIndex * 4u;
        uint cullObjectIndex = gfx_load_source_cull_object_index(rootData, sourceIndexByteOffset);
        CullObject cullObject = gfx_load_demo_cull_object(rootData, cullObjectIndex);
        visible = demo_cull_object_visible(cullObject) ? 1u : 0u;
    }

    gDemoVisibilityScan[localIndex] = visible;
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 1u; offset < GFX_DEMO_VISIBILITY_THREADS_PER_GROUP; offset <<= 1u) {
        uint addend = (localIndex >= offset) ? gDemoVisibilityScan[localIndex - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        gDemoVisibilityScan[localIndex] += addend;
        GroupMemoryBarrierWithGroupSync();
    }

    if (localIndex == 0u) {
        uint visibleCount = (activeCount == 0u) ? 0u : gDemoVisibilityScan[activeCount - 1u];
        gfx_store_visibility_group_count(rootData, visibilityGroupIndex, visibleCount);
    }
}

[shader("compute")]
[numthreads(128, 1, 1)]
void demo_visibility_prefix_compute(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID) {
    VisibilityComputeRootData rootData = gfx_load_visibility_compute_root_data();
    uint binIndex = groupId.x;
    if (binIndex >= rootData.binCount) {
        return;
    }

    VisibilityBin bin = gfx_load_visibility_bin(rootData, binIndex);
    uint localIndex = groupThreadId.x;
    uint activeCount = min(bin.groupCount, GFX_DEMO_VISIBILITY_THREADS_PER_GROUP);
    uint count = 0u;
    if (localIndex < activeCount) {
        count = gfx_load_visibility_group_count(rootData, bin.groupStart + localIndex);
    }

    gDemoVisibilityScan[localIndex] = count;
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 1u; offset < GFX_DEMO_VISIBILITY_THREADS_PER_GROUP; offset <<= 1u) {
        uint addend = (localIndex >= offset) ? gDemoVisibilityScan[localIndex - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        gDemoVisibilityScan[localIndex] += addend;
        GroupMemoryBarrierWithGroupSync();
    }

    if (localIndex < activeCount) {
        uint groupOffset = (localIndex == 0u) ? 0u : gDemoVisibilityScan[localIndex - 1u];
        gfx_store_visibility_group_offset(rootData, bin.groupStart + localIndex, groupOffset);
    }

    if (localIndex == 0u) {
        uint visibleCount = (activeCount == 0u) ? 0u : gDemoVisibilityScan[activeCount - 1u];
        DrawIndexedIndirectArgs args;
        args.indexCount = 3u;
        args.instanceCount = visibleCount;
        args.firstIndex = 0u;
        args.baseVertex = 0;
        args.firstInstance = 0u;
        gfx_store_draw_indexed_indirect_args(rootData, bin.indirectArgsByteOffset, args);
    }
}

[shader("compute")]
[numthreads(128, 1, 1)]
void demo_visibility_compact_compute(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID) {
    VisibilityComputeRootData rootData = gfx_load_visibility_compute_root_data();
    uint visibilityGroupIndex = groupId.x;
    if (visibilityGroupIndex >= rootData.groupCount) {
        return;
    }

    VisibilityGroup group = gfx_load_visibility_group(rootData, visibilityGroupIndex);
    uint groupOutputOffset = gfx_load_visibility_group_offset(rootData, visibilityGroupIndex);
    uint localIndex = groupThreadId.x;
    uint activeCount = min(group.sourceIndexCount, GFX_DEMO_VISIBILITY_THREADS_PER_GROUP);
    uint visible = 0u;
    uint objectIndex = 0u;
    if (localIndex < activeCount) {
        uint sourceIndexByteOffset = group.sourceIndexByteOffset + localIndex * 4u;
        uint cullObjectIndex = gfx_load_source_cull_object_index(rootData, sourceIndexByteOffset);
        CullObject cullObject = gfx_load_demo_cull_object(rootData, cullObjectIndex);
        objectIndex = cullObject.objectIndex;
        visible = demo_cull_object_visible(cullObject) ? 1u : 0u;
    }

    gDemoVisibilityScan[localIndex] = visible;
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 1u; offset < GFX_DEMO_VISIBILITY_THREADS_PER_GROUP; offset <<= 1u) {
        uint addend = (localIndex >= offset) ? gDemoVisibilityScan[localIndex - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        gDemoVisibilityScan[localIndex] += addend;
        GroupMemoryBarrierWithGroupSync();
    }

    uint visiblePrefix = (localIndex == 0u) ? 0u : gDemoVisibilityScan[localIndex - 1u];
    if (visible != 0u) {
        uint visibleIndexByteOffset = group.visibleIndexByteOffset + (groupOutputOffset + visiblePrefix) * 4u;
        gfx_store_visible_object_index(rootData, visibleIndexByteOffset, objectIndex);
    }
}
#endif
