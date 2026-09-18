#include "Renderer/DeferredRenderer.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"
#include "Graphics/vertex.h"

#include "Logger.h"

#include <algorithm>
#include <iostream>
#include <numeric>

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
    mObjectDataBuffer.Init(sizeof(ObjectData) * MAX_OBJECTS, true);

    mSortOrder.reserve(MAX_OBJECTS);
    mObjects.reserve(MAX_OBJECTS);
    mBatches.reserve(256);
}

void DeferredRenderer::Render(RenderContext& context, const RenderQueue& renderQueue, const Camera* cam) {
    mFrameData.view = cam->GetViewMatrix();
    mFrameData.proj = cam->GetProjectionMatrix();
    PushConstants pConstants {
        mFrameDataBuffer.GetDeviceAddress(),
        mObjectDataBuffer.GetDeviceAddress()
    };
    mFrameDataBuffer.Upload(&mFrameData, sizeof(FrameData));

    BuildBatches(renderQueue);
    if (!mObjects.empty()) {
        mObjectDataBuffer.Upload(mObjects.data(), mObjects.size() * sizeof(ObjectData));
    }

    GraphicsCommands::BeginDraw();
    GraphicsCommands::WriteLightingDescriptorSets(mLightingDescriptors);
    GraphicsCommands::BeginGBufferPass();

    GraphicsCommands::SetViewport();
    GraphicsCommands::SetScissor();

    GraphicsCommands::BindPipeline(mGbufferPipeline);

    GraphicsCommands::SetDepthTestEnable(true);
    GraphicsCommands::SetDepthWriteEnable(true);

    GraphicsCommands::PushConstants(mPipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,  pConstants);

    for(const auto& batch : mBatches) {
        GraphicsCommands::BindGeometryBuffer(batch.mesh->buffer);
        GraphicsCommands::Draw(batch.mesh->indexCount, batch.instanceCount, 0, 0, batch.firstInstance);
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

void DeferredRenderer::BuildBatches(const RenderQueue& renderQueue) {
    const auto& commands = renderQueue.GetCommands();
    size_t count = commands.size();
    if (count > MAX_OBJECTS) {
        Logger::Log(Logger::WARNING, "Render queue has {} objects, only the first {} will be drawn", count, MAX_OBJECTS);
        count = MAX_OBJECTS;
    }

    mSortOrder.resize(count);
    std::iota(mSortOrder.begin(), mSortOrder.end(), 0u);
    std::ranges::sort(mSortOrder, {}, [&](uint32_t i) { return commands[i].mesh; });

    mObjects.clear();
    mBatches.clear();

    for (uint32_t index : mSortOrder) {
        const RenderCommand& command = commands[index];

        // New mesh: start a batch whose first instance is where this object will land.
        if (mBatches.empty() || mBatches.back().mesh != command.mesh) {
            mBatches.push_back({command.mesh, static_cast<uint32_t>(mObjects.size()), 0});
        }
        mBatches.back().instanceCount++;

        mObjects.push_back({
            command.Transform,
            glm::mat4(glm::transpose(glm::inverse(glm::mat3(command.Transform))))
        });
    }
}