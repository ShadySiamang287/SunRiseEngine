struct FrameData {
    float4x4 view;
    float4x4 proj;
    float4x4 inverseView;
    float4x4 inverseProjection;
    float4 cameraPosition;
    uint directionalLightCount;
    uint pointLightCount;
    uint padding0;
    uint padding1;
};

struct ObjectData {
    float4x4 model;
    float4x4 normal;
    uint albedoTextureIndex;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct GPUDirectionalLight { float4 directionIntensity; float4 color; };
struct GPUPointLight { float4 positionRange; float4 colorIntensity; };

struct PushConstants {
    FrameData* frameData;
    ObjectData* objectData;
    GPUDirectionalLight* directionalLightData;
    GPUPointLight* pointLightData;
};

[[vk::push_constant]] PushConstants pc;

struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float3 color;
    [[vk::location(3)]] float2 uv;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normal;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 uv;
    [[vk::location(3)]] nointerpolation uint albedoTextureIndex;
};

[shader("vertex")]
VSOutput vertMain(VSInput input, uint instanceID : SV_VulkanInstanceID) {
    FrameData frame = pc.frameData[0];
    ObjectData object = pc.objectData[instanceID];

    float4 worldPosition = mul(object.model, float4(input.position, 1.0));

    VSOutput output;
    output.position = mul(frame.proj, mul(frame.view, worldPosition));
    output.normal = normalize(mul((float3x3)object.normal, input.normal));
    output.color = input.color;
    output.uv = input.uv;
    output.albedoTextureIndex = object.albedoTextureIndex;
    return output;
}

[[vk::binding(0, 0)]] Texture2D<float4> gTextures[];
[[vk::binding(1, 0)]] SamplerState gTextureSampler;

struct GBufferOutput {
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
};

[shader("fragment")]
GBufferOutput gBufferFrag(VSOutput input) {
    uint textureIndex = NonUniformResourceIndex(input.albedoTextureIndex);
    float4 texel = gTextures[textureIndex].Sample(gTextureSampler, input.uv);

    GBufferOutput output;
    output.albedo = texel * float4(input.color, 1.0);
    output.normal = float4(normalize(input.normal), 1.0);
    return output;
}
