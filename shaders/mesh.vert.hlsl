struct Vertex {
    float3 position;
    float uvX;
    float3 normal;
    float uvY;
    float4 color;
};

struct GPUDrawPushConstants {
    float4x4 worldMatrix;
    uint64_t vertexBufferAddress;
    float alpha;
    float _padding0;
    float _padding1;
    float _padding2;
};

[[vk::push_constant]]
GPUDrawPushConstants g_pushConstants;

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float alpha : ALPHA;
};

VSOutput main(uint vertexId : SV_VertexID) {
    Vertex v = vk::RawBufferLoad<Vertex>(g_pushConstants.vertexBufferAddress + vertexId * sizeof(Vertex));
    
    VSOutput output;
    output.position = mul(g_pushConstants.worldMatrix, float4(v.position, 1.0f));
    output.color = v.color.rgb;
    output.uv = float2(v.uvX, v.uvY);
    output.alpha = g_pushConstants.alpha;
    return output;
}
