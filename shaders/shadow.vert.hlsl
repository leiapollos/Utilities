struct GPUDrawPushConstants {
    float4x4 worldMatrix;
    uint64_t vertexBufferAddress;
    uint2 _padding;
    float4 color;
};

cbuffer SceneData : register(b0, space0) {
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;
    float4x4 lightSpaceMatrix;
    float4 ambientColor;
    float4 sunDirection;
    float4 sunColor;
};

struct Vertex {
    float3 position;
    float uvX;
    float3 normal;
    float uvY;
    float4 color;
};

[[vk::push_constant]]
GPUDrawPushConstants g_pushConstants;

float4 main(uint vertexId : SV_VertexID) : SV_Position {
    Vertex v = vk::RawBufferLoad<Vertex>(g_pushConstants.vertexBufferAddress + vertexId * sizeof(Vertex));
    float4 worldPos = mul(g_pushConstants.worldMatrix, float4(v.position, 1.0f));
    return mul(lightSpaceMatrix, worldPos);
}
