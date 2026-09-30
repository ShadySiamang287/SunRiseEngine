#include "Renderer/SSAOBlurPass.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"

using namespace SUN;

SSAOBlurPass::SSAOBlurPass(std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& ssaoImages, vk::raii::Sampler& sampler)
    : mSSAOImages(ssaoImages), mSampler(sampler) {

    std::array<vk::DescriptorSetLayoutBinding, 2> bindings;
    bindings[0] = { .binding = 0, .descriptorType = vk::DescriptorType::eSampledImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment };
    bindings[1] = { .binding = 1, .descriptorType = vk::DescriptorType::eSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment };

    mDescriptors = ResourceFactory::CreateDescriptorResources(bindings);
    mLayout = ResourceFactory::CreatePipelineLayout(vk::ShaderStageFlagBits::eFragment, sizeof(PushConstants), &mDescriptors);

    CreateImages(GraphicsCommands::GetSwapchainExtent());
    UpdateDescriptors();

    // TODO: create the blur pipeline once your shader exists.
    //
    // This corresponds to the tutorial's ssaoColorBufferBlur pass.
    // The shader can copy its 4x4 box blur almost directly:
    //   texelSize = 1.0 / input dimensions
    //   x, y from -2 to < 2
    //   average 16 samples
    //
    // PipelineConfig config{
    //     .vertexFile = "./shaders/lightVert.spv",
    //     .vertexName = "lightVert",
    //     .fragFile = "./shaders/ssaoBlur.spv",
    //     .fragName = "ssaoBlur",
    //     .primitiveTopology = vk::PrimitiveTopology::eTriangleList,
    //     .colorAttachmentFormats = { vk::Format::eR8Unorm },
    //     .colorAttachmentLocations = { 0 },
    //     .useVertexInput = false
    // };
    // mPipeline = ResourceFactory::CreatePipeline(config, mLayout, "SSAO Blur Pipeline");
}

void SSAOBlurPass::Execute(const RenderContext& context) {
    (void)context;

    // TODO:
    // 1. Transition mBlurredAOImages[context.frameIndex] to eColorAttachmentOptimal.
    // 2. Begin rendering into it.
    // 3. Set viewport/scissor to the AO extent.
    // 4. Disable depth testing/writes.
    // 5. Bind mPipeline and mDescriptors.
    // 6. DrawFullScreenTriangle().
    // 7. End rendering.
    // 8. Transition blurred AO to eShaderReadOnlyOptimal for LightingPass.
}

void SSAOBlurPass::Resize(vk::Extent2D newSize) {
    CreateImages(newSize);
    UpdateDescriptors();
}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& SSAOBlurPass::GetOutputs() {
    return mBlurredAOImages;
}

void SSAOBlurPass::CreateImages(vk::Extent2D extent) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        mBlurredAOImages[i] = ResourceFactory::CreateRenderImage(
            vk::Format::eR8Unorm,
            extent,
            vk::ImageLayout::eUndefined,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eColor
        );
    }
}

void SSAOBlurPass::UpdateDescriptors() {
    vk::DescriptorImageInfo samplerInfo{ .sampler = *mSampler };

    for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex) {
        vk::DescriptorImageInfo inputInfo{
            .imageView = *mSSAOImages[frameIndex].image.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        std::array<vk::WriteDescriptorSet, 2> writes{
            vk::WriteDescriptorSet{ .dstSet = *mDescriptors.sets[frameIndex], .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampledImage, .pImageInfo = &inputInfo },
            vk::WriteDescriptorSet{ .dstSet = *mDescriptors.sets[frameIndex], .dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampler, .pImageInfo = &samplerInfo }
        };

        GraphicsCommands::WriteDescriptors(writes);
    }
}
