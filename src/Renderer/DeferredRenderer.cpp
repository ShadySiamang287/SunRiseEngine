#include "Renderer/DeferredRenderer.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"
#include "Graphics/vertex.h"

#include <iostream>

using namespace SUN;

DeferredRenderer::DeferredRenderer() {
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings;
    bindings[0] = {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eInputAttachment,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    bindings[1] = {
        .binding = 1,
        .descriptorType = vk::DescriptorType::eInputAttachment,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    mLightingDescriptors = ResourceFactory::CreateDescriptorResources(bindings);
    mLightingLayout = ResourceFactory::CreatePipelineLayout(&mLightingDescriptors);

    mPipelineLayout = ResourceFactory::CreatePipelineLayout();

    PipelineConfig gBuffer = {
        .vertexFile = "./shaders/slang.spv",
        .vertexName = "vertMain",
        .fragFile =  "./shaders/slang.spv",
        .fragName = "gBufferFrag",
        .primitiveTopology = vk::PrimitiveTopology::eTriangleList,
        .colorAttachmentFormats = {
            vk::Format::eR16G16B16A16Sfloat,
            vk::Format::eR16G16B16A16Sfloat
        },
        .colorAttachmentLocations = {
            0,
            1
        },
        .depthAttachmentFormat = vk::Format::eD32Sfloat,
        .useVertexInput = true
    };
    mGbufferPipeline = ResourceFactory::CreatePipeline(gBuffer, mPipelineLayout, "GBuffer pipeline");

    PipelineConfig lighting = {
        .vertexFile = "./shaders/slang.spv",
        .vertexName = "lightVert",
        .fragFile = "./shaders/slang.spv",
        .fragName = "lightFrag",

        .primitiveTopology = vk::PrimitiveTopology::eTriangleList,

        .colorAttachmentFormats = {
            GraphicsCommands::GetSwapchainFormat()
        },
        .colorAttachmentLocations = {
            0
        },
        .useVertexInput = false
    };
    mLightingPipeline = ResourceFactory::CreatePipeline(lighting, mLightingLayout, "Lighting Pipeline");

    mFrameDataBuffer.Init(sizeof(FrameData));
}

void DeferredRenderer::Render(RenderContext& context, const RenderQueue& renderQueue, const Camera* cam) {
    mFrameData.view = cam->GetViewMatrix();
    mFrameData.proj = cam->GetProjectionMatrix();
    PushConstants pConstants {
        mFrameDataBuffer.GetDeviceAddress()
    };
    mFrameDataBuffer.Upload(&mFrameData, sizeof(FrameData));

    GraphicsCommands::BeginDraw();
    GraphicsCommands::WriteLightingDescriptorSets(mLightingDescriptors);
    GraphicsCommands::BeginGBufferPass();

    GraphicsCommands::SetViewport();
    GraphicsCommands::SetScissor();

    GraphicsCommands::BindPipeline(mGbufferPipeline);

    GraphicsCommands::SetDepthTestEnable(true);
    GraphicsCommands::SetDepthWriteEnable(true);

    GraphicsCommands::PushConstants(mPipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,  pConstants);

    for (const auto& command : renderQueue.GetCommands()) {
        GraphicsCommands::BindGeometryBuffer(command.mesh->buffer);
        GraphicsCommands::Draw(command.mesh->indexCount, 1, 0, 0, 0);
    }
    GraphicsCommands::EndGBufferPass();
    GraphicsCommands::BeginLightingPass();

    GraphicsCommands::SetDepthTestEnable(false);
    GraphicsCommands::SetDepthWriteEnable(false);

    GraphicsCommands::BindPipeline(mLightingPipeline);
    GraphicsCommands::BindDescriptorSets(mLightingLayout, mLightingDescriptors);
    GraphicsCommands::Draw(
        3,  // fullscreen triangle
        1,
        0,
        0,
        0
    );

    GraphicsCommands::EndLightingPass();
    
    GraphicsCommands::EndDraw();
}