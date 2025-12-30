cbuffer SceneData : register(b0, space0) {
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;
    float4 ambientColor;
    float4 sunDirection;
    float4 sunColor;
};

Texture2D colorTex : register(t1, space1);
SamplerState colorSampler : register(s1, space1);

struct PSInput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float3 color : COLOR;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float alpha : ALPHA;
    float4 position : SV_Position;
};

float4 main(PSInput input) : SV_Target {
    float3 N = normalize(input.normal);
    float3 L = normalize(sunDirection.xyz);
    
    float NdotL = dot(N, L) * 0.5 + 0.5;
    
    float2 uv = input.uv;
    
    float3 texColor = colorTex.Sample(colorSampler, uv).rgb;
    float3 baseColor = input.color * texColor;
    
    float3 lit = baseColor * NdotL * sunColor.rgb + baseColor * ambientColor.rgb;
    
    return float4(lit, input.alpha);
}

