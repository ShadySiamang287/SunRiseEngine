#include "Renderer/SSAOPass.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"

#include <algorithm>

using namespace SUN;

SSAOPass::SSAOPass(std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& gBuffers, vk::raii::Sampler& sampler)
    : mGBuffers(gBuffers),
      mSampler(sampler) {

    std::array<vk::DescriptorSetLayoutBinding, 3> bindings;

    bindings[0] = {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    bindings[1] = {
        .binding = 1,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    bindings[2] = {
        .binding = 2,
        .descriptorType = vk::DescriptorType::eSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    mDescriptors = ResourceFactory::CreateDescriptorResources(bindings);

    mLayout =
        ResourceFactory::CreatePipelineLayout(vk::ShaderStageFlagBits::eFragment, sizeof(PushConstants), &mDescriptors);

    CreateImages(GraphicsCommands::GetSwapchainExtent());

    UpdateDescriptors();

    // TODO:
    // Create the SSAO graphics pipeline here once the SSAO shader exists.
    //
    // Recommended output format:
    //     vk::Format::eR8Unorm
    //
    // Recommended inputs:
    //     binding 0 = GBuffer depth
    //     binding 1 = GBuffer normal
    //     binding 2 = sampler
}

void SSAOPass::Execute(const RenderContext& context, const PushConstants& pushConstants) {

    (void)context;
    (void)pushConstants;

    // TODO: Implement SSAO.
    //
    // Suggested order:
    //
    // 1. Get the current frame's GBuffer and AO image.
    //
    // 2. Transition AO image:
    //      eUndefined / eShaderReadOnlyOptimal
    //          -> eColorAttachmentOptimal
    //
    //    GraphicsCommands::TransitionImage(...) will use the
    //    RenderImage's tracked state for the old layout.
    //
    // 3. Begin dynamic rendering with mAOImages[context.frameIndex]
    //    as a single R8_UNORM color attachment.
    //
    // 4. Set the half-resolution viewport/scissor:
    //
    //    GraphicsCommands::SetViewportAndScissor(
    //        mAOImages[context.frameIndex].extent
    //    );
    //
    // 5. Bind:
    //      mPipeline
    //      mDescriptors
    //      pushConstants
    //
    // 6. DrawFullScreenTriangle().
    //
    // 7. End rendering.
    //
    // 8. Transition AO image to:
    //      eShaderReadOnlyOptimal
    //
    //    so LightingPass can sample it.
}

void SSAOPass::Resize(vk::Extent2D newSize) {
    CreateImages(newSize);
    UpdateDescriptors();
}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& SSAOPass::GetOutputs() {
    return mAOImages;
}

void SSAOPass::CreateImages(vk::Extent2D sourceExtent) {
    const vk::Extent2D aoExtent {
        std::max(1u, sourceExtent.width / 2),
        std::max(1u, sourceExtent.height / 2)
    };

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        mAOImages[i] =
            ResourceFactory::CreateRenderImage(
                vk::Format::eR8Unorm,
                aoExtent,
                vk::ImageLayout::eUndefined,
                vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageAspectFlagBits::eColor
            );
    }
}

void SSAOPass::UpdateDescriptors() {
    vk::DescriptorImageInfo samplerInfo {
        .sampler = *mSampler
    };

    for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex) {
        const GBuffer& gbuffer =
            mGBuffers[frameIndex];

        vk::DescriptorImageInfo depthInfo {
            .sampler = nullptr,
            .imageView = *gbuffer.depth.image.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        vk::DescriptorImageInfo normalInfo {
            .sampler = nullptr,
            .imageView = *gbuffer.normal.image.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        std::array<vk::WriteDescriptorSet, 3> writes {
            vk::WriteDescriptorSet {
                .dstSet = *mDescriptors.sets[frameIndex],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &depthInfo
            },
            vk::WriteDescriptorSet {
                .dstSet = *mDescriptors.sets[frameIndex],
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &normalInfo
            },
            vk::WriteDescriptorSet {
                .dstSet = *mDescriptors.sets[frameIndex],
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampler,
                .pImageInfo = &samplerInfo
            }
        };

        GraphicsCommands::WriteDescriptors(writes);
    }
}
