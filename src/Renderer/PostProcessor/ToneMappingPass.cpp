#include "Renderer/PostProcessor/ToneMappingPass.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"

using namespace SUN;

ToneMappingPass::ToneMappingPass(
    std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& bloomImages,
    std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& hdrImages,
    vk::raii::Sampler& sampler)
    : mBloomImages(bloomImages),
      mHDRImages(hdrImages),
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

    mDescriptors =
        ResourceFactory::CreateDescriptorResources(bindings);

    mLayout =
        ResourceFactory::CreatePipelineLayout(vk::ShaderStageFlagBits::eFragment, sizeof(PushConstants), &mDescriptors);

    PipelineConfig pipelineConfig {
        .vertexFile = "./shaders/lightVert.spv",
        .vertexName = "lightVert",
        .fragFile = "./shaders/toneMappingFrag.spv",
        .fragName = "toneMappingFrag",
        .primitiveTopology = vk::PrimitiveTopology::eTriangleList,
        .colorAttachmentFormats = {
            vk::Format::eR8G8B8A8Unorm
        },
        .colorAttachmentLocations = {
            0
        },
        .useVertexInput = false
    };

    mPipeline =
        ResourceFactory::CreatePipeline(pipelineConfig, mLayout, "Tone mapping Pipeline");

    CreateImages(GraphicsCommands::GetSwapchainExtent());

    UpdateDescriptors();
}

void ToneMappingPass::Execute(const PostProcessContext& context) {

    const uint32_t frameIndex =
        context.frameIndex;

    GraphicsCommands::BeginLabel(
        "Tone Mapping",
        {0.32F, 0.32F, 0.76F, 1.F}
    );

    const std::array<ImageTransition, 2>
        transitions {
            ImageTransition {
                &mHDRImages[frameIndex],
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eFragmentShader
            },
            ImageTransition {
                &mToneMappedImages[frameIndex],
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput
            }
        };

    GraphicsCommands::TransitionImages(transitions);

    vk::RenderingAttachmentInfo outputAttachment {
        .imageView = mToneMappedImages[frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue {
            vk::ClearColorValue {
                0.0f,
                0.0f,
                0.0f,
                0.0f
            }
        }
    };

    vk::RenderingInfo renderingInfo {
        .renderArea = {
            .offset = {0, 0},
            .extent = mToneMappedImages[frameIndex].extent
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &outputAttachment
    };

    GraphicsCommands::BeginRendering(renderingInfo);

    GraphicsCommands::SetViewportAndScissor(mToneMappedImages[frameIndex].extent);

    GraphicsCommands::SetDepthTestEnable(false);
    GraphicsCommands::SetDepthWriteEnable(false);

    GraphicsCommands::BindPipeline(mPipeline);

    GraphicsCommands::BindDescriptorSets(mLayout, mDescriptors);

    GraphicsCommands::BeginLabel(
        "Tone mapping fullscreen draw",
        {0.45F, 0.45F, 0.9F, 1.F}
    );
    GraphicsCommands::DrawFullScreenTriangle();
    GraphicsCommands::EndLabel();
    GraphicsCommands::EndRendering();

    GraphicsCommands::EndLabel();
}

void ToneMappingPass::Resize(vk::Extent2D newSize) {

    CreateImages(newSize);
    UpdateDescriptors();
}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& ToneMappingPass::GetOutputs() {
    return mToneMappedImages;
}

void ToneMappingPass::CreateImages(vk::Extent2D size) {

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        mToneMappedImages[i] =
            ResourceFactory::CreateRenderImage(
                vk::Format::eR8G8B8A8Unorm,
                size,
                vk::ImageLayout::eUndefined,
                vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageAspectFlagBits::eColor
            );
    }
}

void ToneMappingPass::UpdateDescriptors() {
    vk::DescriptorImageInfo samplerInfo {
        .sampler = *mSampler
    };

    for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex) {
        vk::DescriptorImageInfo hdrInfo {
            .sampler = nullptr,
            .imageView = *mHDRImages[frameIndex].image.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        vk::DescriptorImageInfo bloomInfo {
            .sampler = nullptr,
            .imageView = *mBloomImages[frameIndex].image.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        std::array<vk::WriteDescriptorSet, 3>
            writes {
                vk::WriteDescriptorSet {
                    .dstSet = *mDescriptors.sets[frameIndex],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eSampledImage,
                    .pImageInfo = &hdrInfo
                },
                vk::WriteDescriptorSet {
                    .dstSet = *mDescriptors.sets[frameIndex],
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eSampledImage,
                    .pImageInfo = &bloomInfo
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
