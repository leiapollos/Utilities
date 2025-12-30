struct PSInput {
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float alpha : ALPHA;
};

float4 main(PSInput input) : SV_Target {
    return float4(input.color, input.alpha);
}
