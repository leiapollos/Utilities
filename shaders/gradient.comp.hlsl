[[vk::binding(0, 0)]]
RWTexture2D<float4> image;

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID,
          uint3 groupThreadId : SV_GroupThreadID) {
    uint2 texelCoord = dispatchThreadId.xy;

    uint width = 0u;
    uint height = 0u;
    image.GetDimensions(width, height);

    if ((texelCoord.x < width) && (texelCoord.y < height)) {
        float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);

        if ((groupThreadId.x != 0u) && (groupThreadId.y != 0u)) {
            color.x = (float) texelCoord.x / (float) width;
            color.y = (float) texelCoord.y / (float) height;
        }

        image[texelCoord] = color;
    }
}