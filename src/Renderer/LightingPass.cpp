#include "Renderer/LightingPass.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"

using namespace SUN;

LightingPass::LightingPass(
    std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& gBuffers,
    vk::raii::Sampler& sampler)
    : mGBuffers(gBuffers), mSampler(sampler) {

    std::array<vk::DescriptorSetLayoutBinding, 4> bindings;

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
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    bindings[3] = {
        .binding = 3,
        .descriptorType = vk::DescriptorType::eSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    mDescriptors =
        ResourceFactory::CreateDescriptorResources(bindings);

    mLayout = ResourceFactory::CreatePipelineLayout(
        vk::ShaderStageFlagBits::eFragment,
        sizeof(PushConstants),
        &mDescriptors
    );

    PipelineConfig lightingConfig = {
        .vertexFile = "./shaders/lightVert.spv",
        .vertexName = "lightVert",
        .fragFile = "./shaders/lightFrag.spv",
        .fragName = "lightFrag",
        .primitiveTopology = vk::PrimitiveTopology::eTriangleList,
        .colorAttachmentFormats = {
            vk::Format::eR16G16B16A16Sfloat,
            vk::Format::eR16G16B16A16Sfloat
        },
        .colorAttachmentLocations = {
            0,
            1
        },
        .useVertexInput = false
    };

    mPipeline = ResourceFactory::CreatePipeline(
        lightingConfig,
        mLayout,
        "Lighting Pipeline"
    );

    CreateImages(GraphicsCommands::GetSwapchainExtent());
}

LightingPass::~LightingPass() {
    DestroyImages();
}

void LightingPass::Execute(
    const RenderContext& context,
    const PushConstants& pushConstants) {

    const uint32_t frameIndex = context.frameIndex;
    const GBuffer& gbuffer = mGBuffers[frameIndex];

    vk::DescriptorImageInfo albedoInfo {
        .sampler = nullptr,
        .imageView = *gbuffer.albedo.image.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    vk::DescriptorImageInfo normalInfo {
        .sampler = nullptr,
        .imageView = *gbuffer.normal.image.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    vk::DescriptorImageInfo depthInfo {
        .sampler = nullptr,
        .imageView = *gbuffer.depth.image.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    vk::DescriptorImageInfo samplerInfo {
        .sampler = *mSampler
    };

    std::array<vk::WriteDescriptorSet, 4> writes {
        vk::WriteDescriptorSet {
            .dstSet = *mDescriptors.sets[frameIndex],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &albedoInfo
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
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &depthInfo
        },
        vk::WriteDescriptorSet {
            .dstSet = *mDescriptors.sets[frameIndex],
            .dstBinding = 3,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplerInfo
        }
    };

    GraphicsCommands::WriteDescriptors(writes);

    GraphicsCommands::BeginLabel(
        "Lighting pass",
        {0.76F, 0.32F, 0.32F, 1.F}
    );

    std::array<vk::ImageMemoryBarrier2, 2> barriers {
        GraphicsCommands::MakeImageBarrier(
            mHDRImages[frameIndex].image.image,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            {},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput
        ),
        GraphicsCommands::MakeImageBarrier(
            mBrightnessImages[frameIndex].image.image,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            {},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput
        )
    };

    GraphicsCommands::ImageBarriers(barriers);

    vk::RenderingAttachmentInfo hdrAttachment {
        .imageView = mHDRImages[frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingAttachmentInfo brightnessAttachment {
        .imageView = mBrightnessImages[frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    std::array<vk::RenderingAttachmentInfo, 2> attachments {
        hdrAttachment,
        brightnessAttachment
    };

    vk::RenderingInfo renderingInfo {
        .renderArea = {
            .offset = {0, 0},
            .extent = mHDRImages[frameIndex].extent
        },
        .layerCount = 1,
        .colorAttachmentCount =
            static_cast<uint32_t>(attachments.size()),
        .pColorAttachments = attachments.data()
    };

    GraphicsCommands::BeginRendering(renderingInfo);

    GraphicsCommands::PushConstants(
        mLayout,
        vk::ShaderStageFlagBits::eFragment,
        pushConstants
    );

    GraphicsCommands::SetDepthTestEnable(false);
    GraphicsCommands::SetDepthWriteEnable(false);

    GraphicsCommands::BindPipeline(mPipeline);
    GraphicsCommands::BindDescriptorSets(mLayout, mDescriptors);
    GraphicsCommands::DrawFullScreenTriangle();

    GraphicsCommands::EndRendering();

    GraphicsCommands::TransitionImageLayout(
        mBrightnessImages[frameIndex].image.image,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::AccessFlagBits2::eShaderRead,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eFragmentShader
    );

    GraphicsCommands::EndLabel();
}

void LightingPass::Resize(vk::Extent2D newSize) {
    DestroyImages();
    CreateImages(newSize);
}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>&
LightingPass::GetHDRImages() {
    return mHDRImages;
}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>&
LightingPass::GetBrightnessImages() {
    return mBrightnessImages;
}

void LightingPass::CreateImages(vk::Extent2D extent) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        mHDRImages[i] = ResourceFactory::CreateRenderImage(
            vk::Format::eR16G16B16A16Sfloat,
            extent,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageUsageFlagBits::eColorAttachment |
                vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eColor
        );

        mBrightnessImages[i] = ResourceFactory::CreateRenderImage(
            vk::Format::eR16G16B16A16Sfloat,
            extent,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageUsageFlagBits::eColorAttachment |
                vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eColor
        );
    }
}

void LightingPass::DestroyImages() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        GraphicsCommands::DestroyRenderImage(mHDRImages[i]);
        GraphicsCommands::DestroyRenderImage(mBrightnessImages[i]);
    }
}
