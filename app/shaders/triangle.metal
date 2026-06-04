#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float4 color [[attribute(1)]];
};

struct DrawData {
    float4 offsetScale;
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]], constant DrawData& drawData [[buffer(8)]]) {
    VertexOut out;
    float2 position = in.position * drawData.offsetScale.z + drawData.offsetScale.xy;
    out.position = float4(position, 0.0, 1.0);
    out.color = in.color;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]]) {
    return in.color;
}
