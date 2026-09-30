#include "Renderer/PostProcessor/BloomPass.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"
#include "Renderer/RenderingStructs.h"

using namespace SUN;

BloomPass::BloomPass(
    std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& brightnessImages,
    vk::raii::Sampler& sampler)
    : mBrightnessImages(brightnessImages),
      mSampler(sampler) {

    std::array<vk::DescriptorSetLayoutBinding, 2>
        bindings;

    bindings[0] = {
        .binding = 0,
        .descriptorType =
            vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags =
            vk::ShaderStageFlagBits::eFragment
    };

    bindings[1] = {
        .binding = 1,
        .descriptorType =
            vk::DescriptorType::eSampler,
        .descriptorCount = 1,
        .stageFlags =
            vk::ShaderStageFlagBits::eFragment
    };

    mHorizontalDescriptors =
        ResourceFactory::CreateDescriptorResources(
            bindings
        );

    mVerticalDescriptors =
        ResourceFactory::CreateDescriptorResources(
            bindings
        );

    mLayout =
        ResourceFactory::CreatePipelineLayout(
            vk::ShaderStageFlagBits::eFragment,
            sizeof(BloomPushConstants),
            &mHorizontalDescriptors
        );

    PipelineConfig bloomConfig {
        .vertexFile = "./shaders/lightVert.spv",
        .vertexName = "lightVert",
        .fragFile = "./shaders/bloom.spv",
        .fragName = "bloom",
        .primitiveTopology =
            vk::PrimitiveTopology::eTriangleList,
        .colorAttachmentFormats = {
            vk::Format::eR16G16B16A16Sfloat
        },
        .colorAttachmentLocations = {
            0
        },
        .useVertexInput = false
    };

    mPipeline =
        ResourceFactory::CreatePipeline(
            bloomConfig,
            mLayout,
            "Bloom Pipeline"
        );

    CreateImages(
        GraphicsCommands::GetSwapchainExtent()
    );

    UpdateDescriptors();
}

void BloomPass::Execute(
    const PostProcessContext& context) {

    const uint32_t frameIndex =
        context.frameIndex;

    GraphicsCommands::BeginLabel(
        "Bloom",
        {0.32F, 0.76F, 0.76F, 1.F}
    );

    GraphicsCommands::TransitionImage(
        mBlurHorizontal[frameIndex],
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    vk::RenderingAttachmentInfo horizontalAttachment {
        .imageView =
            mBlurHorizontal[frameIndex].image.view,
        .imageLayout =
            vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp =
            vk::AttachmentLoadOp::eClear,
        .storeOp =
            vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue {
            vk::ClearColorValue {
                0.0f,
                0.0f,
                0.0f,
                0.0f
            }
        }
    };

    vk::RenderingInfo horizontalInfo {
        .renderArea = {
            .offset = {0, 0},
            .extent =
                mBlurHorizontal[frameIndex].extent
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments =
            &horizontalAttachment
    };

    GraphicsCommands::BeginRendering(
        horizontalInfo
    );

    GraphicsCommands::SetViewportAndScissor(
        mBlurHorizontal[frameIndex].extent
    );

    GraphicsCommands::SetDepthTestEnable(false);
    GraphicsCommands::SetDepthWriteEnable(false);

    GraphicsCommands::BindPipeline(
        mPipeline
    );

    GraphicsCommands::PushBloomConstants(
        mLayout,
        true
    );

    GraphicsCommands::BindDescriptorSets(
        mLayout,
        mHorizontalDescriptors
    );

    GraphicsCommands::DrawFullScreenTriangle();
    GraphicsCommands::EndRendering();

    const std::array<ImageTransition, 2>
        middleTransitions {
            ImageTransition {
                &mBlurHorizontal[frameIndex],
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eFragmentShader
            },
            ImageTransition {
                &mBlurVertical[frameIndex],
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput
            }
        };

    GraphicsCommands::TransitionImages(
        middleTransitions
    );

    vk::RenderingAttachmentInfo verticalAttachment {
        .imageView =
            mBlurVertical[frameIndex].image.view,
        .imageLayout =
            vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp =
            vk::AttachmentLoadOp::eClear,
        .storeOp =
            vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue {
            vk::ClearColorValue {
                0.0f,
                0.0f,
                0.0f,
                0.0f
            }
        }
    };

    vk::RenderingInfo verticalInfo {
        .renderArea = {
            .offset = {0, 0},
            .extent =
                mBlurVertical[frameIndex].extent
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments =
            &verticalAttachment
    };

    GraphicsCommands::BeginRendering(
        verticalInfo
    );

    GraphicsCommands::SetViewportAndScissor(
        mBlurVertical[frameIndex].extent
    );

    GraphicsCommands::PushBloomConstants(
        mLayout,
        false
    );

    GraphicsCommands::BindDescriptorSets(
        mLayout,
        mVerticalDescriptors
    );

    GraphicsCommands::DrawFullScreenTriangle();
    GraphicsCommands::EndRendering();

    GraphicsCommands::TransitionImage(
        mBlurVertical[frameIndex],
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::AccessFlagBits2::eShaderRead,
        vk::PipelineStageFlagBits2::eFragmentShader
    );

    GraphicsCommands::EndLabel();
}

void BloomPass::Resize(
    vk::Extent2D newSize) {

    CreateImages(newSize);
    UpdateDescriptors();
}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>&
BloomPass::GetOutputs() {
    return mBlurVertical;
}

void BloomPass::CreateImages(
    vk::Extent2D size) {

    for (
        uint32_t i = 0;
        i < MAX_FRAMES_IN_FLIGHT;
        ++i
    ) {
        mBlurHorizontal[i] =
            ResourceFactory::CreateRenderImage(
                vk::Format::eR16G16B16A16Sfloat,
                size,
                vk::ImageLayout::eUndefined,
                vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageAspectFlagBits::eColor
            );

        mBlurVertical[i] =
            ResourceFactory::CreateRenderImage(
                vk::Format::eR16G16B16A16Sfloat,
                size,
                vk::ImageLayout::eUndefined,
                vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageAspectFlagBits::eColor
            );
    }
}

void BloomPass::UpdateDescriptors() {
    vk::DescriptorImageInfo samplerInfo {
        .sampler = *mSampler
    };

    for (
        uint32_t frameIndex = 0;
        frameIndex < MAX_FRAMES_IN_FLIGHT;
        ++frameIndex
    ) {
        vk::DescriptorImageInfo brightnessInfo {
            .sampler = nullptr,
            .imageView =
                *mBrightnessImages[frameIndex].image.view,
            .imageLayout =
                vk::ImageLayout::eShaderReadOnlyOptimal
        };

        vk::DescriptorImageInfo horizontalInfo {
            .sampler = nullptr,
            .imageView =
                *mBlurHorizontal[frameIndex].image.view,
            .imageLayout =
                vk::ImageLayout::eShaderReadOnlyOptimal
        };

        std::array<vk::WriteDescriptorSet, 4>
            writes {
                vk::WriteDescriptorSet {
                    .dstSet =
                        *mHorizontalDescriptors.sets[frameIndex],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType =
                        vk::DescriptorType::eSampledImage,
                    .pImageInfo =
                        &brightnessInfo
                },
                vk::WriteDescriptorSet {
                    .dstSet =
                        *mHorizontalDescriptors.sets[frameIndex],
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType =
                        vk::DescriptorType::eSampler,
                    .pImageInfo =
                        &samplerInfo
                },
                vk::WriteDescriptorSet {
                    .dstSet =
                        *mVerticalDescriptors.sets[frameIndex],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType =
                        vk::DescriptorType::eSampledImage,
                    .pImageInfo =
                        &horizontalInfo
                },
                vk::WriteDescriptorSet {
                    .dstSet =
                        *mVerticalDescriptors.sets[frameIndex],
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType =
                        vk::DescriptorType::eSampler,
                    .pImageInfo =
                        &samplerInfo
                }
            };

        GraphicsCommands::WriteDescriptors(
            writes
        );
    }
}
