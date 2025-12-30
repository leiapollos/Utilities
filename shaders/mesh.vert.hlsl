cbuffer SceneData : register(b0, space0) {
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;
    float4 ambientColor;
    float4 sunDirection;
    float4 sunColor;
};

cbuffer MaterialData : register(b0, space1) {
    float4 colorFactor;
    float4 metalRoughFactor;
};

struct Vertex {
    float3 position;
    float uvX;
    float3 normal;
    float uvY;
    float4 color;
};

struct GPUDrawPushConstants {
    float4x4 worldMatrix;         // 64 bytes, offset 0
    uint64_t vertexBufferAddress; // 8 bytes, offset 64
    uint2 _padding;               // 8 bytes padding, offset 72
    float4 color;                 // 16 bytes, offset 80
};

[[vk::push_constant]]
GPUDrawPushConstants g_pushConstants;

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float4 color : COLOR;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID) {
    Vertex v = vk::RawBufferLoad<Vertex>(g_pushConstants.vertexBufferAddress + vertexId * sizeof(Vertex));
    
    float4 worldPos = mul(g_pushConstants.worldMatrix, float4(v.position, 1.0f));
    
    float3 vertColor = v.color.rgb;
    if (dot(vertColor, vertColor) < 0.001) {
        vertColor = float3(1.0, 1.0, 1.0);
    }
    
    VSOutput output;
    output.position = mul(viewproj, worldPos);
    output.normal = normalize(mul((float3x3)g_pushConstants.worldMatrix, v.normal));
    output.color.rgb = vertColor * colorFactor.rgb * g_pushConstants.color.rgb;
    output.color.a = g_pushConstants.color.a;
    output.uv = float2(v.uvX, v.uvY);
    return output;
}

