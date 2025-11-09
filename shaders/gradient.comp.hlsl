[[vk::binding(0, 0)]]
RWTexture2D<float4> image;

struct GradientPushConstants {
    float4 tileBorderColor;
};

[[vk::push_constant]]
GradientPushConstants g_pushConstants;

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID,
          uint3 groupThreadId : SV_GroupThreadID) {
    uint2 texelCoord = dispatchThreadId.xy;

    uint width = 0u;
    uint height = 0u;
    image.GetDimensions(width, height);

    if ((texelCoord.x < width) && (texelCoord.y < height)) {
        float invWidth = (width > 0u) ? (1.0f / (float) width) : 0.0f;
        float invHeight = (height > 0u) ? (1.0f / (float) height) : 0.0f;
        float2 uv = float2((float) texelCoord.x * invWidth,
                           (float) texelCoord.y * invHeight);
        float4 gradientColor = float4(uv.x, uv.y, 0.0f, 1.0f);

        bool onGroupEdge = (groupThreadId.x == 0u) || (groupThreadId.y == 0u);
        float4 finalColor = onGroupEdge ? g_pushConstants.tileBorderColor : gradientColor;

        image[texelCoord] = finalColor;
    }
}