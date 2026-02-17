Texture2D<float4> emissiveTex : register(t0, space0);
SamplerState emissiveSampler : register(s0, space0);
Texture2D<float4> occluderTex : register(t1, space0);
SamplerState occluderSampler : register(s1, space0);
Texture2D<float4> parentCascadeTex : register(t2, space0);
SamplerState parentCascadeSampler : register(s2, space0);
RWTexture2D<float4> outputCascade : register(u3, space0);

struct Radiance2DCascadePushConstants {
    uint gridWidth;
    uint gridHeight;
    uint cascadeIndex;
    uint cascadeCount;
    uint raysPerProbeBase;
    uint maxSteps;
};

[[vk::push_constant]]
Radiance2DCascadePushConstants g_pushConstants;

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= g_pushConstants.gridWidth || pixel.y >= g_pushConstants.gridHeight) {
        return;
    }

    float2 invGrid = float2(1.0f / (float) g_pushConstants.gridWidth,
                            1.0f / (float) g_pushConstants.gridHeight);
    float2 uv = (float2(pixel) + 0.5f) * invGrid;

    uint rayCount = g_pushConstants.raysPerProbeBase << g_pushConstants.cascadeIndex;
    rayCount = max(rayCount, 1u);
    rayCount = min(rayCount, 256u);

    uint intervalStart = (g_pushConstants.cascadeIndex == 0u) ? 1u : (1u << (g_pushConstants.cascadeIndex - 1u));
    uint intervalEnd = 1u << g_pushConstants.cascadeIndex;
    intervalEnd = max(intervalEnd, intervalStart);

    float gridScale = 1.0f / (float) max(g_pushConstants.gridWidth, g_pushConstants.gridHeight);
    uint stepCount = max(g_pushConstants.maxSteps, 1u);

    float3 radiance = float3(0.0f, 0.0f, 0.0f);

    for (uint rayIndex = 0u; rayIndex < rayCount; ++rayIndex) {
        float angle = ((float) rayIndex + 0.5f) * (6.28318530718f / (float) rayCount);
        float2 rayDir = float2(cos(angle), sin(angle));

        float3 rayContribution = float3(0.0f, 0.0f, 0.0f);
        bool blocked = false;

        for (uint step = 0u; step < stepCount; ++step) {
            float t = ((float) step + 0.5f) / (float) stepCount;
            float sampleDistance = lerp((float) intervalStart, (float) intervalEnd, t);
            float2 sampleUV = uv + rayDir * (sampleDistance * gridScale);

            if (sampleUV.x < 0.0f || sampleUV.x > 1.0f || sampleUV.y < 0.0f || sampleUV.y > 1.0f) {
                break;
            }

            float4 occ = occluderTex.SampleLevel(occluderSampler, sampleUV, 0.0f);
            if (occ.a > 0.5f) {
                blocked = true;
                break;
            }

            float3 emit = emissiveTex.SampleLevel(emissiveSampler, sampleUV, 0.0f).rgb;
            rayContribution += emit;
        }

        if (!blocked) {
            float3 parent = parentCascadeTex.SampleLevel(parentCascadeSampler, uv, 0.0f).rgb;
            rayContribution += parent;
        }

        radiance += rayContribution;
    }

    radiance /= (float) rayCount;
    outputCascade[pixel] = float4(radiance, 1.0f);
}
