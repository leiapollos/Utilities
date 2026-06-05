#include <metal_stdlib>
using namespace metal;

#define GFX_SHADER_SLOT_DISPATCH_DATA 8
#define GFX_SHADER_SLOT_PASS_DATA 10

struct Material {
    float4 baseColor;
    uint albedoTexture;
    uint sampler;
    uint flags;
    uint _padding;
};

struct MaterialComputeData {
    uint materialCount;
    uint columns;
    uint rows;
    uint albedoTexture;
    uint sampler;
    uint _padding0;
    uint _padding1;
    uint _padding2;
};

kernel void material_main(constant MaterialComputeData& data [[buffer(GFX_SHADER_SLOT_DISPATCH_DATA)]],
                          device Material* materials [[buffer(GFX_SHADER_SLOT_PASS_DATA)]],
                          uint index [[thread_position_in_grid]]) {
    if (index >= data.materialCount) {
        return;
    }

    uint column = index % data.columns;
    uint row = index / data.columns;
    float columnT = (data.columns > 1u) ? ((float)column / (float)(data.columns - 1u)) : 0.0;
    float rowT = (data.rows > 1u) ? ((float)row / (float)(data.rows - 1u)) : 0.0;

    materials[index].baseColor = float4(0.30 + 0.70 * columnT,
                                        0.30 + 0.70 * rowT,
                                        1.00 - 0.50 * ((columnT + rowT) * 0.5),
                                        1.0);
    materials[index].albedoTexture = data.albedoTexture;
    materials[index].sampler = data.sampler;
    materials[index].flags = 1u;
    materials[index]._padding = 0u;
}
