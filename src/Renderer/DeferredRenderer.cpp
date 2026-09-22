#include "Renderer/DeferredRenderer.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"
#include "Graphics/vertex.h"

#include "Logger.h"

#include <algorithm>
#include <iostream>
#include <numeric>

using namespace SUN;

DeferredRenderer::DeferredRenderer(vk::raii::Sampler& sampler) : mImageSampler(sampler) {
    std::array<vk::DescriptorSetLayoutBinding, 4> lightingBindings;
    lightingBindings[0] = {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    lightingBindings[1] = {
        .binding = 1,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    lightingBindings[2] = {
        .binding = 2,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    lightingBindings[3] = {
        .binding = 3,
        .descriptorType = vk::DescriptorType::eSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    mLightingDescriptors = ResourceFactory::CreateDescriptorResources(lightingBindings);
    mLightingLayout = ResourceFactory::CreatePipelineLayout(vk::ShaderStageFlagBits::eFragment, sizeof(PushConstants), &mLightingDescriptors);


    mPipelineLayout = ResourceFactory::CreatePipelineLayout(vk::ShaderStageFlagBits::eVertex, sizeof(PushConstants));

    PipelineConfig gBuffer = {
        .vertexFile = "./shaders/vertMain.spv",
        .vertexName = "vertMain",
        .fragFile =  "./shaders/gBufferFrag.spv",
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
    mLightingPipeline = ResourceFactory::CreatePipeline(lighting, mLightingLayout, "Lighting Pipeline");

    mFrameDataBuffer.Init(sizeof(FrameData));
    mObjectDataBuffer.Init(sizeof(ObjectData) * MAX_OBJECTS, true);
    mDirectionalLightDataBuffer.Init(sizeof(GPUDirectionalLight) * MAX_DIRECTIONAL_LIGHTS, true);
    mPointLightDataBuffer.Init(sizeof(GPUPointLight) * MAX_POINT_LIGHTS, true);

    mSortOrder.reserve(MAX_OBJECTS);
    mObjects.reserve(MAX_OBJECTS);
    mBatches.reserve(256);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){
        mHDRImages[i] = ResourceFactory::CreateRenderImage(vk::Format::eR16G16B16A16Sfloat, GraphicsCommands::GetSwapchainExtent(), vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eColor);
        mBrightnessImages[i] = ResourceFactory::CreateRenderImage(vk::Format::eR16G16B16A16Sfloat, GraphicsCommands::GetSwapchainExtent(), vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eColor);
    }
}

DeferredRenderer::~DeferredRenderer() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        GraphicsCommands::DestroyRenderImage(mHDRImages[i]);
        GraphicsCommands::DestroyRenderImage(mBrightnessImages[i]);
    }
}

void DeferredRenderer::Render(RenderContext& context, const RenderQueue& renderQueue, const Camera* cam,
    std::span<const GPUDirectionalLight> directionalLights,
    std::span<const GPUPointLight> pointLights) {
    mFrameData.view = cam->GetViewMatrix();
    mFrameData.inverseView = glm::inverse(mFrameData.view);
    mFrameData.proj = cam->GetProjectionMatrix();
    mFrameData.inverseProjection = glm::inverse(mFrameData.proj);
    mFrameData.cameraPosition = {cam->Position, 1.f};
    mFrameData.directionalLightCount = directionalLights.size();
    mFrameData.pointLightCount = pointLights.size();

    PushConstants pConstants {
        mFrameDataBuffer.GetDeviceAddress(),
        mObjectDataBuffer.GetDeviceAddress(),
        mDirectionalLightDataBuffer.GetDeviceAddress(),
        mPointLightDataBuffer.GetDeviceAddress()
    };
    mFrameDataBuffer.Upload(&mFrameData, sizeof(FrameData));
    mDirectionalLightDataBuffer.Upload(directionalLights.data(), sizeof(GPUDirectionalLight) * directionalLights.size());
    mPointLightDataBuffer.Upload(pointLights.data(), sizeof(GPUPointLight) * pointLights.size());

    BuildBatches(renderQueue);
    if (!mObjects.empty()) {
        mObjectDataBuffer.Upload(mObjects.data(), mObjects.size() * sizeof(ObjectData));
    }

    //GraphicsCommands::BeginDraw();
    GraphicsCommands::WriteLightingDescriptorSets(mLightingDescriptors, mImageSampler);
    GraphicsCommands::BeginGBufferPass();

    GraphicsCommands::SetViewport();
    GraphicsCommands::SetScissor();

    GraphicsCommands::BindPipeline(mGbufferPipeline);

    GraphicsCommands::SetDepthTestEnable(true);
    GraphicsCommands::SetDepthWriteEnable(true);

    GraphicsCommands::PushConstants(mPipelineLayout, vk::ShaderStageFlagBits::eVertex,  pConstants);

    for(const auto& batch : mBatches) {
        GraphicsCommands::BindGeometryBuffer(batch.mesh->buffer);
        GraphicsCommands::DrawIndexed(batch.mesh->buffer.GetIndexCount(), batch.instanceCount, 0, 0, batch.firstInstance);
    }

    GraphicsCommands::EndGBufferPass();

    GraphicsCommands::BeginLabel("Lighting pass", {0.76F, 0.32F, .32F, 1.F});
    std::array<vk::ImageMemoryBarrier2, 2> barriers {
        GraphicsCommands::MakeImageBarrier(
            mHDRImages[context.frameIndex].image.image,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,

            {}, // src access
            vk::AccessFlagBits2::eColorAttachmentWrite,

            vk::PipelineStageFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput
        ),
        GraphicsCommands::MakeImageBarrier(
            mBrightnessImages[context.frameIndex].image.image,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,

            {}, // src access
            vk::AccessFlagBits2::eColorAttachmentWrite,

            vk::PipelineStageFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput
        )
    };

    GraphicsCommands::ImageBarriers(barriers);

    vk::RenderingAttachmentInfo brightAttachment {
        .imageView = mBrightnessImages[context.frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingAttachmentInfo hdrAttachment{
        .imageView = mHDRImages[context.frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    std::array<vk::RenderingAttachmentInfo, 2> attachments {
        hdrAttachment,
        brightAttachment
    };

    vk::RenderingInfo renderingInfo{
        .renderArea = {
            .offset = {0, 0},
            .extent = GraphicsCommands::GetSwapchainExtent()
        },
        .layerCount = 1,          // Required when viewMask == 0
        .colorAttachmentCount = 2,
        .pColorAttachments = attachments.data(),
    };
    GraphicsCommands::BeginRendering(renderingInfo);

    GraphicsCommands::PushConstants(mLightingLayout, vk::ShaderStageFlagBits::eFragment,  pConstants);

    GraphicsCommands::SetDepthTestEnable(false);
    GraphicsCommands::SetDepthWriteEnable(false);

    GraphicsCommands::BindPipeline(mLightingPipeline);
    GraphicsCommands::BindDescriptorSets(mLightingLayout, mLightingDescriptors);
    GraphicsCommands::Draw(
        3,  // fullscreen triangle
        1,
        0,
        0
    );

    GraphicsCommands::EndRendering();
    std::array<vk::ImageMemoryBarrier2, 1> postBarriers {
        GraphicsCommands::MakeImageBarrier(
            mBrightnessImages[context.frameIndex].image.image,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,

            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::AccessFlagBits2::eShaderRead,

            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eFragmentShader
        )
    };
    GraphicsCommands::ImageBarriers(postBarriers);
    GraphicsCommands::EndLabel();
}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& DeferredRenderer::GetBrightnessImages() {
    return mBrightnessImages;
}
std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& DeferredRenderer::GetHDRImages() {
    return mHDRImages;
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