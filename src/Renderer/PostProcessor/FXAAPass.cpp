#include "Renderer/PostProcessor/FXAAPass.h"

#include "Graphics/ResourceFactory.h"
#include "Graphics/GraphicsCommands.h"

using namespace SUN;

FXAAPass::FXAAPass(std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& toneMappingImages) : mToneMappingImages (toneMappingImages){
    SamplerConfig samplerConfig{
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
    mSampler = ResourceFactory::CreateSampler(samplerConfig);

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
    mDescriptors = ResourceFactory::CreateDescriptorResources(bindings);
    mLayout = ResourceFactory::CreatePipelineLayout(vk::ShaderStageFlagBits::eFragment, sizeof(PushConstants), &mDescriptors);

    PipelineConfig pipelineConfig = {
        .vertexFile = "./shaders/lightVert.spv",
        .vertexName = "lightVert",
        .fragFile = "./shaders/FXAA.spv",
        .fragName = "FXAA",

        .primitiveTopology = vk::PrimitiveTopology::eTriangleList,

        .colorAttachmentFormats = {
            vk::Format::eR8G8B8A8Unorm
        },
        .colorAttachmentLocations = {
            0
        },
        .useVertexInput = false
    };
    mPipeline = ResourceFactory::CreatePipeline(pipelineConfig, mLayout, "FXAA Pipeline");
}

void FXAAPass::Execute(const PostProcessContext& context) {
    GraphicsCommands::BeginLabel("FXAA", {0.76F, 0.32F, .55F, 1.F});

    std::array<vk::ImageMemoryBarrier2, 2> barriers {
        GraphicsCommands::MakeImageBarrier(
            mToneMappingImages[context.frameIndex].image.image,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,

            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::AccessFlagBits2::eShaderRead,

            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eFragmentShader
        ),
        GraphicsCommands::MakeImageBarrier(
            GraphicsCommands::GetCurrentSwapchainImage(),
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,

            {}, // src access
            vk::AccessFlagBits2::eColorAttachmentWrite,

            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput
        )
    };
    GraphicsCommands::ImageBarriers(barriers);
    vk::DescriptorImageInfo finalImageInfo {
        .sampler = nullptr,
        .imageView = *mToneMappingImages[context.frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    
    vk::DescriptorImageInfo samplerInfo{
        .sampler = *mSampler
    };
    std::array<vk::WriteDescriptorSet, 2> writes{
        vk::WriteDescriptorSet{
            .dstSet = *mDescriptors.sets[context.frameIndex],

            .dstBinding = 0,
            .descriptorCount = 1,

            .descriptorType =
                vk::DescriptorType::eSampledImage,

            .pImageInfo = &finalImageInfo
        },     
        vk::WriteDescriptorSet{
            .dstSet = *mDescriptors.sets[context.frameIndex],
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplerInfo
        }
    };

    GraphicsCommands::WriteDescriptors(writes);

        vk::RenderingAttachmentInfo swapchain{
        .imageView = GraphicsCommands::GetCurrentSwapchainImageView(),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingInfo renderingInfo{
        .renderArea = {
            .offset = {0, 0},
            .extent = GraphicsCommands::GetSwapchainExtent()
        },
        .layerCount = 1,          // Required when viewMask == 0
        .colorAttachmentCount = 1,
        .pColorAttachments = &swapchain,
    };
    GraphicsCommands::BeginRendering(renderingInfo);
    GraphicsCommands::BindPipeline(mPipeline);
    GraphicsCommands::BindDescriptorSets(mLayout, mDescriptors);
    GraphicsCommands::DrawFullScreenTriangle();
    GraphicsCommands::EndRendering();
    GraphicsCommands::EndLabel();
}

void FXAAPass::Resize(vk::Extent2D newSize) {

}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& FXAAPass::GetOutput(uint32_t frameIndex) {
    return mToneMappingImages;
}