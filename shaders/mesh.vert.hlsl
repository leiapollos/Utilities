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
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float3 color : COLOR;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float alpha : ALPHA;
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
    output.color = vertColor * colorFactor.rgb;
    output.uv = float2(v.uvX, v.uvY);
    output.alpha = g_pushConstants.alpha;
    return output;
}

