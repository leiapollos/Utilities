cbuffer SceneData : register(b0, space0) {
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;
    float4x4 lightSpaceMatrix;
    float4 ambientColor;
    float4 sunDirection;
    float4 sunColor;
};

cbuffer MaterialData : register(b0, space1) {
    float4 colorFactor;
    float4 metalRoughFactor;
    float alphaCutoff;
};

Texture2D colorTex : register(t1, space1);
SamplerState colorSampler : register(s1, space1);

Texture2D<float> shadowMap : register(t0, space2);
SamplerState shadowSampler : register(s0, space2);

#define SHADOW_DEBUG_VISUALIZE 0

struct PSInput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float4 color : COLOR;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 lightSpacePos : TEXCOORD1;
    float4 position : SV_Position;
};

float calculate_shadow_pcf(float4 lightSpacePos, float nDotL) {
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }
    
    float currentDepth = projCoords.z;
    float shadow = 0.0;
    float2 texelSize;
    uint width, height;
    shadowMap.GetDimensions(width, height);
    texelSize = 1.0 / float2(width, height);
    float baseBias = max(0.0015f * (1.0f - nDotL), 0.0002f);
    float resolutionBias = 1.0f / max((float)width, (float)height);
    float bias = baseBias + (resolutionBias * 0.75f);
    
    float totalWeight = 0.0f;
    float filterRadius = 1.5f;
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            float2 sampleCoord = float2((float)x, (float)y);
            float2 offset = sampleCoord * texelSize * filterRadius;
            float weight = (3.0f - abs((float)x)) * (3.0f - abs((float)y));
            float shadowDepth = shadowMap.SampleLevel(shadowSampler, projCoords.xy + offset, 0);
            float lit = (currentDepth - bias <= shadowDepth) ? 1.0f : 0.0f;
            shadow += lit * weight;
            totalWeight += weight;
        }
    }
    shadow /= max(totalWeight, 0.0001f);
    
    return shadow;
}

float4 main(PSInput input) : SV_Target {
    float3 N = normalize(input.normal);
    float3 L = normalize(sunDirection.xyz);
    
    float NdotL = dot(N, L);
    if (NdotL < 0.0f) {
        NdotL = 0.0f;
    }
    
    float4 texColor = colorTex.Sample(colorSampler, input.uv);
    
    if (texColor.a < alphaCutoff) {
        discard;
    }
    
    float shadow = calculate_shadow_pcf(input.lightSpacePos, NdotL);

#if SHADOW_DEBUG_VISUALIZE
    float3 projCoords = input.lightSpacePos.xyz / input.lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return float4(1.0, 0.0, 1.0, 1.0);
    }
    return float4(shadow, shadow, shadow, 1.0);
#else
    float3 lighting = ambientColor.rgb + (sunColor.rgb * NdotL * shadow);
    float3 baseColor = texColor.rgb * input.color.rgb;

    return float4(baseColor * lighting, texColor.a * input.color.a);
#endif
}
