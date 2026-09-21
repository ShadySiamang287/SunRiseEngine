#include "Renderer/PostProcessor/ToneMappingPass.h"
#include "Graphics/ResourceFactory.h"

#include "Graphics/GraphicsCommands.h"

using namespace SUN;

ToneMappingPass::ToneMappingPass(std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& bloomImages, std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& hdrImages, vk::raii::Sampler& sampler) : mBloomImages (bloomImages), mHDRImages(hdrImages), mSampler(sampler) {
    
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
    mLayout = ResourceFactory::CreatePipelineLayout(vk::ShaderStageFlagBits::eFragment, sizeof(PushConstants), &mDescriptors);
    
    PipelineConfig pipelineConfig = {
        .vertexFile = "./shaders/lightVert.spv",
        .vertexName = "lightVert",
        .fragFile = "./shaders/toneMappingFrag.spv",
        .fragName = "toneMappingFrag",

        .primitiveTopology = vk::PrimitiveTopology::eTriangleList,

        .colorAttachmentFormats = {
            GraphicsCommands::GetSwapchainFormat()
        },
        .colorAttachmentLocations = {
            0
        },
        .useVertexInput = false
    };
    mPipeline = ResourceFactory::CreatePipeline(pipelineConfig, mLayout, "Tone mapping Pipeline");
}

void ToneMappingPass::Execute(const PostProcessContext& context){
    GraphicsCommands::BeginLabel("Tone Mapping", {0.32F, 0.32F, .76F, 1.F});
    
    std::array<vk::ImageMemoryBarrier2, 2> barriers {
        GraphicsCommands::MakeImageBarrier(
            mHDRImages[context.frameIndex].image.image,
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

        vk::RenderingAttachmentInfo swapchain{
        .imageView = GraphicsCommands::GetCurrentSwapchainImageView(),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::DescriptorImageInfo hdrInfo{
        .sampler = nullptr,
        .imageView = *mHDRImages[context.frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    vk::DescriptorImageInfo bloomInfo {
        .sampler = nullptr,
        .imageView = *mBloomImages[context.frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    vk::DescriptorImageInfo samplerInfo{
        .sampler = *mSampler
    };

    std::array<vk::WriteDescriptorSet, 3> writes{
        vk::WriteDescriptorSet{
            .dstSet = *mDescriptors.sets[context.frameIndex],

            .dstBinding = 0,
            .descriptorCount = 1,

            .descriptorType =
                vk::DescriptorType::eSampledImage,

            .pImageInfo = &hdrInfo
        },     
       vk::WriteDescriptorSet{
            .dstSet = *mDescriptors.sets[context.frameIndex],

            .dstBinding = 1,
            .descriptorCount = 1,

            .descriptorType =
                vk::DescriptorType::eSampledImage,

            .pImageInfo = &bloomInfo
        },

        vk::WriteDescriptorSet{
            .dstSet = *mDescriptors.sets[context.frameIndex],
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplerInfo
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

void ToneMappingPass::Resize(vk::Extent2D) {
    return;
}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& ToneMappingPass::GetOutput(uint32_t frameIndex){
    return mBloomImages;
}