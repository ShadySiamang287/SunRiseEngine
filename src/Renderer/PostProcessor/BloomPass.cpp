#include "Renderer/PostProcessor/BloomPass.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"

#include "Renderer/RenderingStructs.h"

using namespace SUN;

BloomPass::BloomPass(std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& brightnessImages, vk::raii::Sampler& sampler) : mBrightnessImages(brightnessImages), mSampler(sampler) {    
    vk::Extent2D swapchainExtent = GraphicsCommands::GetSwapchainExtent();
    CreateImages(swapchainExtent);
    
    std::array<vk::DescriptorSetLayoutBinding, 2> bloomMappingBindings;
    bloomMappingBindings[0] = {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    
    bloomMappingBindings[1] = {
        .binding = 1,
        .descriptorType = vk::DescriptorType::eSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    mHorizontalDescriptors = ResourceFactory::CreateDescriptorResources(bloomMappingBindings);
    mVerticalDescriptors = ResourceFactory::CreateDescriptorResources(bloomMappingBindings);
    mLayout = ResourceFactory::CreatePipelineLayout(vk::ShaderStageFlagBits::eFragment, sizeof(BloomPushConstants), &mHorizontalDescriptors);


    PipelineConfig bloomConfig {
        .vertexFile = "./shaders/lightVert.spv",
        .vertexName = "lightVert",
        .fragFile = "./shaders/bloom.spv",
        .fragName = "bloom",

        .primitiveTopology = vk::PrimitiveTopology::eTriangleList,

        .colorAttachmentFormats = {
            vk::Format::eR16G16B16A16Sfloat
        },
        .colorAttachmentLocations = {
            0
        },
        .useVertexInput = false
    };
    mPipeline = ResourceFactory::CreatePipeline(bloomConfig, mLayout, "Bloom Pipeline");
}

void BloomPass::Execute(const PostProcessContext& context) {

    vk::DescriptorImageInfo brightnessInfo {
        .sampler = nullptr,
        .imageView = mBrightnessImages[context.frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };

    vk::DescriptorImageInfo pingInfo {
        .sampler = nullptr,
        .imageView = mBlurHorizontal[context.frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };

    vk::DescriptorImageInfo samplerInfo{
        .sampler = *mSampler
    };

    std::array<vk::WriteDescriptorSet, 4> writes {
        vk::WriteDescriptorSet{
            .dstSet = *mHorizontalDescriptors.sets[context.frameIndex],

            .dstBinding = 0,
            .descriptorCount = 1,

            .descriptorType =
                vk::DescriptorType::eSampledImage,

            .pImageInfo = &brightnessInfo
        },

        vk::WriteDescriptorSet{
            .dstSet = *mHorizontalDescriptors.sets[context.frameIndex],
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplerInfo
        },
        vk::WriteDescriptorSet{
            .dstSet = *mVerticalDescriptors.sets[context.frameIndex],

            .dstBinding = 0,
            .descriptorCount = 1,

            .descriptorType =
                vk::DescriptorType::eSampledImage,

            .pImageInfo = &pingInfo
        },

        vk::WriteDescriptorSet{
            .dstSet = *mVerticalDescriptors.sets[context.frameIndex],
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplerInfo
        }
    };

    GraphicsCommands::WriteDescriptors(writes);

    GraphicsCommands::TransitionImageLayout(
        mBlurHorizontal[context.frameIndex].image.image,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eColorAttachmentOptimal,

        vk::AccessFlagBits2::eShaderRead,
        vk::AccessFlagBits2::eColorAttachmentWrite,

        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );


    vk::RenderingAttachmentInfo pass1Attachment{
        .imageView = mBlurHorizontal[context.frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingInfo pass1Info{
        .renderArea = {
            .offset = {0, 0},
            .extent = mBlurHorizontal[context.frameIndex].extent
        },
        .layerCount = 1,          // Required when viewMask == 0
        .colorAttachmentCount = 1,
        .pColorAttachments = &pass1Attachment,
    };

    GraphicsCommands::BeginLabel("Bloom", {0.32F, 0.76F, .76F, 1.F});
    GraphicsCommands::BeginRendering(pass1Info);
    GraphicsCommands::BindPipeline(mPipeline);
    GraphicsCommands::PushBloomConstants(mLayout, true);
    GraphicsCommands::BindDescriptorSets(mLayout, mHorizontalDescriptors);
    GraphicsCommands::DrawFullScreenTriangle();
    GraphicsCommands::EndRendering();

    std::array<vk::ImageMemoryBarrier2, 2> barriers {
        GraphicsCommands::MakeImageBarrier(
            mBlurHorizontal[context.frameIndex].image.image,

            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,

            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::AccessFlagBits2::eShaderRead,

            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eFragmentShader
        ),
        GraphicsCommands::MakeImageBarrier(
            mBlurVertical[context.frameIndex].image.image,

            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,

            vk::AccessFlagBits2::eShaderRead,
            vk::AccessFlagBits2::eColorAttachmentWrite,

            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput
        )
    };
    GraphicsCommands::ImageBarriers(barriers);

    vk::RenderingAttachmentInfo pass2Attachment{
        .imageView = mBlurVertical[context.frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingInfo pass2Info{
        .renderArea = {
            .offset = {0, 0},
            .extent = mBlurVertical[context.frameIndex].extent
        },
        .layerCount = 1,          // Required when viewMask == 0
        .colorAttachmentCount = 1,
        .pColorAttachments = &pass2Attachment,
    };
    GraphicsCommands::BeginRendering(pass2Info);
    GraphicsCommands::PushBloomConstants(mLayout, false);
    GraphicsCommands::BindDescriptorSets(mLayout, mVerticalDescriptors);
    GraphicsCommands::DrawFullScreenTriangle();
    GraphicsCommands::EndRendering();
    GraphicsCommands::TransitionImageLayout(
        mBlurVertical[context.frameIndex].image.image,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,

        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::AccessFlagBits2::eShaderRead,

        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eFragmentShader
    );

    GraphicsCommands::EndLabel();

}

void BloomPass::Resize(vk::Extent2D newSize) {
    CleanupImages();
    CreateImages(newSize);
}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& BloomPass::GetOutput(uint32_t frameIndex) {
    return mBlurVertical;

}

void BloomPass::CreateImages(vk::Extent2D size) {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        mBlurHorizontal[i] = ResourceFactory::CreateRenderImage(vk::Format::eR16G16B16A16Sfloat, size, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eColor);
        mBlurVertical[i] = ResourceFactory::CreateRenderImage(vk::Format::eR16G16B16A16Sfloat, size, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eColor);
    }
}

void BloomPass::CleanupImages(){
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){
        GraphicsCommands::DestroyRenderImage(mBlurHorizontal[i]);
        GraphicsCommands::DestroyRenderImage(mBlurVertical[i]);
    }
}