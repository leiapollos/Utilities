Texture2D<float4> cascade0Tex : register(t0, space0);
SamplerState cascade0Sampler : register(s0, space0);
Texture2D<float4> cascade1Tex : register(t1, space0);
SamplerState cascade1Sampler : register(s1, space0);
Texture2D<float4> cascade2Tex : register(t2, space0);
SamplerState cascade2Sampler : register(s2, space0);
Texture2D<float4> cascade3Tex : register(t3, space0);
SamplerState cascade3Sampler : register(s3, space0);
Texture2D<float4> cascade4Tex : register(t4, space0);
SamplerState cascade4Sampler : register(s4, space0);
Texture2D<float4> cascade5Tex : register(t5, space0);
SamplerState cascade5Sampler : register(s5, space0);
Texture2D<float4> cascade6Tex : register(t6, space0);
SamplerState cascade6Sampler : register(s6, space0);
Texture2D<float4> cascade7Tex : register(t7, space0);
SamplerState cascade7Sampler : register(s7, space0);
RWTexture2D<float4> outputImage : register(u8, space0);

struct Radiance2DResolvePushConstants {
    uint outputWidth;
    uint outputHeight;
    uint cascadeCount;
    uint _padding0;
    float intensity;
    float exposure;
};

[[vk::push_constant]]
Radiance2DResolvePushConstants g_pushConstants;

float3 sample_cascade(uint cascadeIndex, float2 uv) {
    switch (cascadeIndex) {
        case 0u:
            return cascade0Tex.SampleLevel(cascade0Sampler, uv, 0.0f).rgb;
        case 1u:
            return cascade1Tex.SampleLevel(cascade1Sampler, uv, 0.0f).rgb;
        case 2u:
            return cascade2Tex.SampleLevel(cascade2Sampler, uv, 0.0f).rgb;
        case 3u:
            return cascade3Tex.SampleLevel(cascade3Sampler, uv, 0.0f).rgb;
        case 4u:
            return cascade4Tex.SampleLevel(cascade4Sampler, uv, 0.0f).rgb;
        case 5u:
            return cascade5Tex.SampleLevel(cascade5Sampler, uv, 0.0f).rgb;
        case 6u:
            return cascade6Tex.SampleLevel(cascade6Sampler, uv, 0.0f).rgb;
        case 7u:
            return cascade7Tex.SampleLevel(cascade7Sampler, uv, 0.0f).rgb;
        default:
            return float3(0.0f, 0.0f, 0.0f);
    }
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= g_pushConstants.outputWidth || pixel.y >= g_pushConstants.outputHeight) {
        return;
    }

    float2 invOutput = float2(1.0f / (float) g_pushConstants.outputWidth,
                              1.0f / (float) g_pushConstants.outputHeight);
    float2 uv = (float2(pixel) + 0.5f) * invOutput;

    float3 indirect = float3(0.0f, 0.0f, 0.0f);
    uint count = min(g_pushConstants.cascadeCount, 8u);
    for (uint i = 0u; i < count; ++i) {
        indirect += sample_cascade(i, uv);
    }

    indirect *= g_pushConstants.intensity;
    float3 toneMapped = 1.0f - exp(-indirect * g_pushConstants.exposure);
    outputImage[pixel] = float4(toneMapped, 1.0f);
}
