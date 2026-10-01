#include "Renderer/PostProcessor/FXAAPass.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"

using namespace SUN;

FXAAPass::FXAAPass(std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& toneMappingImages)
    : mToneMappingImages(toneMappingImages) {

    SamplerConfig samplerConfig {
        .minFilter = vk::Filter::eLinear,
        .magFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .mipLodBias = 0.0f,
        .anisotropy = false,
        .maxAnisotropy = 1.0f,
        .compare = false,
        .compareOp = vk::CompareOp::eAlways,
        .borderColor = vk::BorderColor::eFloatOpaqueBlack
    };

    mSampler =
        ResourceFactory::CreateSampler(samplerConfig);

    std::array<vk::DescriptorSetLayoutBinding, 2> bindings;

    bindings[0] = {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    bindings[1] = {
        .binding = 1,
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
        .fragFile = "./shaders/FXAA.spv",
        .fragName = "FXAA",
        .primitiveTopology = vk::PrimitiveTopology::eTriangleList,
        .colorAttachmentFormats = {
            GraphicsCommands::GetSwapchainFormat()
        },
        .colorAttachmentLocations = {
            0
        },
        .useVertexInput = false
    };

    mPipeline =
        ResourceFactory::CreatePipeline(pipelineConfig, mLayout, "FXAA Pipeline");

    UpdateDescriptors();
}

void FXAAPass::Execute(const PostProcessContext& context) {

    const uint32_t frameIndex =
        context.frameIndex;

    GraphicsCommands::BeginLabel(
        "FXAA",
        {0.76F, 0.32F, 0.55F, 1.F}
    );

    GraphicsCommands::TransitionImage(
        mToneMappingImages[frameIndex],
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::AccessFlagBits2::eShaderRead,
        vk::PipelineStageFlagBits2::eFragmentShader
    );

    GraphicsCommands::TransitionImageLayout(
        GraphicsCommands::GetCurrentSwapchainImage(),
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        {},
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eNone,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    vk::RenderingAttachmentInfo swapchainAttachment {
        .imageView = GraphicsCommands::GetCurrentSwapchainImageView(),
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

    const vk::Extent2D extent =
        GraphicsCommands::GetSwapchainExtent();

    vk::RenderingInfo renderingInfo {
        .renderArea = {
            .offset = {0, 0},
            .extent = extent
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &swapchainAttachment
    };

    GraphicsCommands::BeginRendering(renderingInfo);

    GraphicsCommands::SetViewportAndScissor(extent);

    GraphicsCommands::SetDepthTestEnable(false);
    GraphicsCommands::SetDepthWriteEnable(false);

    GraphicsCommands::BindPipeline(mPipeline);

    GraphicsCommands::BindDescriptorSets(mLayout, mDescriptors);

    GraphicsCommands::BeginLabel(
        "FXAA fullscreen draw",
        {0.9F, 0.45F, 0.7F, 1.F}
    );
    GraphicsCommands::DrawFullScreenTriangle();
    GraphicsCommands::EndLabel();
    GraphicsCommands::EndRendering();

    GraphicsCommands::EndLabel();
}

void FXAAPass::Resize(vk::Extent2D) {

    UpdateDescriptors();
}

void FXAAPass::UpdateDescriptors() {
    vk::DescriptorImageInfo samplerInfo {
        .sampler = *mSampler
    };

    for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex) {
        vk::DescriptorImageInfo finalImageInfo {
            .sampler = nullptr,
            .imageView = *mToneMappingImages[frameIndex].image.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        std::array<vk::WriteDescriptorSet, 2>
            writes {
                vk::WriteDescriptorSet {
                    .dstSet = *mDescriptors.sets[frameIndex],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eSampledImage,
                    .pImageInfo = &finalImageInfo
                },
                vk::WriteDescriptorSet {
                    .dstSet = *mDescriptors.sets[frameIndex],
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eSampler,
                    .pImageInfo = &samplerInfo
                }
            };

        GraphicsCommands::WriteDescriptors(writes);
    }
}
