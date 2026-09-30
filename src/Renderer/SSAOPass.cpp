#include "Renderer/SSAOPass.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"

#include <algorithm>
#include <random>

using namespace SUN;

SSAOPass::SSAOPass(std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& gBuffers, vk::raii::Sampler& gBufferSampler)
    : mGBuffers(gBuffers), mGBufferSampler(gBufferSampler) {

    std::array<vk::DescriptorSetLayoutBinding, 6> bindings;
    bindings[0] = { .binding = 0, .descriptorType = vk::DescriptorType::eSampledImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment };
    bindings[1] = { .binding = 1, .descriptorType = vk::DescriptorType::eSampledImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment };
    bindings[2] = { .binding = 2, .descriptorType = vk::DescriptorType::eSampledImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment };
    bindings[3] = { .binding = 3, .descriptorType = vk::DescriptorType::eSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment };
    bindings[4] = { .binding = 4, .descriptorType = vk::DescriptorType::eSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment };
    bindings[5] = { .binding = 5, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment };

    mDescriptors = ResourceFactory::CreateDescriptorResources(bindings);
    mLayout = ResourceFactory::CreatePipelineLayout(vk::ShaderStageFlagBits::eFragment, sizeof(PushConstants), &mDescriptors);

    SamplerConfig noiseSamplerConfig{
        .minFilter = vk::Filter::eNearest,
        .magFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .anisotropy = false,
        .compare = false
    };
    mNoiseSampler = ResourceFactory::CreateSampler(noiseSamplerConfig);

    mSSAODataBuffer.Init(sizeof(SSAOData));
    GenerateKernel();
    CreateNoiseTexture();
    CreateImages(GraphicsCommands::GetSwapchainExtent());
    UpdateDescriptors();

    // TODO: create the SSAO pipeline once your shader exists.
    //
    // PipelineConfig config{
    //     .vertexFile = "./shaders/lightVert.spv",
    //     .vertexName = "lightVert",
    //     .fragFile = "./shaders/ssao.spv",
    //     .fragName = "ssao",
    //     .primitiveTopology = vk::PrimitiveTopology::eTriangleList,
    //     .colorAttachmentFormats = { vk::Format::eR8Unorm },
    //     .colorAttachmentLocations = { 0 },
    //     .useVertexInput = false
    // };
    // mPipeline = ResourceFactory::CreatePipeline(config, mLayout, "SSAO Pipeline");
}

void SSAOPass::Execute(const RenderContext& context, const PushConstants& pushConstants) {
    (void)context;
    (void)pushConstants;

    // LearnOpenGL mapping for this renderer:
    //
    // gPosition  -> reconstruct view-space position from binding 0 depth using
    //               FrameData.inverseProjection from pushConstants.frameDataAddress.
    // gNormal    -> binding 1; your G-buffer normal is world-space, so transform
    //               it to view-space with FrameData.view before building the TBN.
    // texNoise   -> binding 2; values are stored as UNORM, so decode xyz * 2 - 1.
    // samples[64], radius, bias, kernelSize -> binding 5 SSAOData.
    // projection -> FrameData.proj.
    //
    // Descriptor layout:
    //   0 = depth
    //   1 = normal
    //   2 = 4x4 noise texture
    //   3 = G-buffer clamp sampler
    //   4 = repeating noise sampler
    //   5 = SSAOData uniform buffer
    //
    // TODO:
    // 1. Transition mAOImages[context.frameIndex] to eColorAttachmentOptimal.
    // 2. Begin dynamic rendering with that image as the single color attachment.
    // 3. Set viewport/scissor to mAOImages[context.frameIndex].extent.
    // 4. Disable depth testing/writes.
    // 5. Bind mPipeline, mDescriptors and pushConstants.
    // 6. DrawFullScreenTriangle().
    // 7. End rendering.
    // 8. Transition the AO image to eShaderReadOnlyOptimal.
}

void SSAOPass::Resize(vk::Extent2D newSize) {
    CreateImages(newSize);
    UpdateDescriptors();
}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& SSAOPass::GetOutputs() {
    return mAOImages;
}

void SSAOPass::GenerateKernel() {
    std::default_random_engine generator(0);
    std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);

    for (uint32_t i = 0; i < SSAO_KERNEL_SIZE; ++i) {
        glm::vec3 sample{
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator)
        };

        sample = glm::normalize(sample);
        sample *= randomFloats(generator);

        float scale = static_cast<float>(i) / static_cast<float>(SSAO_KERNEL_SIZE);
        scale = glm::mix(0.1f, 1.0f, scale * scale);
        sample *= scale;

        mSSAOData.samples[i] = glm::vec4(sample, 0.0f);
    }

    for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex) {
        mSSAODataBuffer.Upload(frameIndex, &mSSAOData, sizeof(SSAOData));
    }
}

void SSAOPass::CreateNoiseTexture() {
    std::default_random_engine generator(1);
    std::uniform_real_distribution<float> randomFloats(-1.0f, 1.0f);

    std::array<uint8_t, 4 * 4 * 4> pixels{};

    auto encodeUNorm = [](float value) {
        const float encoded = std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f);
        return static_cast<uint8_t>(encoded * 255.0f);
    };

    for (uint32_t i = 0; i < 16; ++i) {
        pixels[i * 4 + 0] = encodeUNorm(randomFloats(generator));
        pixels[i * 4 + 1] = encodeUNorm(randomFloats(generator));
        pixels[i * 4 + 2] = encodeUNorm(0.0f);
        pixels[i * 4 + 3] = 255;
    }

    mNoiseTexture = ResourceFactory::CreateTexture2D(pixels.data(), 4, 4, false);
}

void SSAOPass::CreateImages(vk::Extent2D sourceExtent) {
    // Full resolution matches the LearnOpenGL tutorial. Once it is working,
    // this can be changed to sourceExtent / 2 without changing the algorithm.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        mAOImages[i] = ResourceFactory::CreateRenderImage(
            vk::Format::eR8Unorm,
            sourceExtent,
            vk::ImageLayout::eUndefined,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eColor
        );
    }
}

void SSAOPass::UpdateDescriptors() {
    vk::DescriptorImageInfo gBufferSamplerInfo{ .sampler = *mGBufferSampler };
    vk::DescriptorImageInfo noiseSamplerInfo{ .sampler = *mNoiseSampler };

    for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex) {
        const GBuffer& gbuffer = mGBuffers[frameIndex];

        vk::DescriptorImageInfo depthInfo{
            .imageView = *gbuffer.depth.image.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        vk::DescriptorImageInfo normalInfo{
            .imageView = *gbuffer.normal.image.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        vk::DescriptorImageInfo noiseInfo{
            .imageView = mNoiseTexture.GetView(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        vk::DescriptorBufferInfo dataInfo{
            .buffer = mSSAODataBuffer.GetHandle(frameIndex),
            .offset = 0,
            .range = sizeof(SSAOData)
        };

        std::array<vk::WriteDescriptorSet, 6> writes{
            vk::WriteDescriptorSet{ .dstSet = *mDescriptors.sets[frameIndex], .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampledImage, .pImageInfo = &depthInfo },
            vk::WriteDescriptorSet{ .dstSet = *mDescriptors.sets[frameIndex], .dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampledImage, .pImageInfo = &normalInfo },
            vk::WriteDescriptorSet{ .dstSet = *mDescriptors.sets[frameIndex], .dstBinding = 2, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampledImage, .pImageInfo = &noiseInfo },
            vk::WriteDescriptorSet{ .dstSet = *mDescriptors.sets[frameIndex], .dstBinding = 3, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampler, .pImageInfo = &gBufferSamplerInfo },
            vk::WriteDescriptorSet{ .dstSet = *mDescriptors.sets[frameIndex], .dstBinding = 4, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampler, .pImageInfo = &noiseSamplerInfo },
            vk::WriteDescriptorSet{ .dstSet = *mDescriptors.sets[frameIndex], .dstBinding = 5, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eUniformBuffer, .pBufferInfo = &dataInfo }
        };

        GraphicsCommands::WriteDescriptors(writes);
    }
}
