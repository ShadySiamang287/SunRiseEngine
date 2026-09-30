#include "Renderer/DeferredRenderer.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"
#include "Graphics/vertex.h"
#include "AssetManagement/AssetManager.h"

#include "Logger.h"

#include <algorithm>
#include <iostream>
#include <numeric>

using namespace SUN;

DeferredRenderer::DeferredRenderer(vk::raii::Sampler& sampler, AssetManager& assetManager)
    : mImageSampler(sampler), mAssetManager(assetManager) {
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


    mPipelineLayout = ResourceFactory::CreatePipelineLayout(
        vk::ShaderStageFlagBits::eVertex,
        sizeof(PushConstants),
        &mAssetManager.GetTextureDescriptors()
    );

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

    CreateImages(GraphicsCommands::GetSwapchainExtent());
}

DeferredRenderer::~DeferredRenderer() {
    DestroyImages();
}

void DeferredRenderer::Render(RenderContext& context, const RenderQueue& renderQueue, const Camera* cam,
    std::span<const GPUDirectionalLight> directionalLights,
    std::span<const GPUPointLight> pointLights) {
    const PushConstants pushConstants = PrepareFrame(
        renderQueue,
        cam,
        directionalLights,
        pointLights
    );

    RenderGBufferPass(pushConstants);

    GraphicsCommands::WriteLightingDescriptorSets(
        mLightingDescriptors,
        mImageSampler
    );

    RenderLightingPass(context, pushConstants);
}

PushConstants DeferredRenderer::PrepareFrame(
    const RenderQueue& renderQueue,
    const Camera* cam,
    std::span<const GPUDirectionalLight> directionalLights,
    std::span<const GPUPointLight> pointLights) {

    mFrameData.view = cam->GetViewMatrix();
    mFrameData.inverseView = glm::inverse(mFrameData.view);
    mFrameData.proj = cam->GetProjectionMatrix();
    mFrameData.inverseProjection = glm::inverse(mFrameData.proj);
    mFrameData.cameraPosition = {cam->Position, 1.f};
    mFrameData.directionalLightCount = directionalLights.size();
    mFrameData.pointLightCount = pointLights.size();

    PushConstants pushConstants {
        mFrameDataBuffer.GetDeviceAddress(),
        mObjectDataBuffer.GetDeviceAddress(),
        mDirectionalLightDataBuffer.GetDeviceAddress(),
        mPointLightDataBuffer.GetDeviceAddress()
    };

    mFrameDataBuffer.Upload(&mFrameData, sizeof(FrameData));

    if (!directionalLights.empty()) {
        mDirectionalLightDataBuffer.Upload(
            directionalLights.data(),
            sizeof(GPUDirectionalLight) * directionalLights.size()
        );
    }

    if (!pointLights.empty()) {
        mPointLightDataBuffer.Upload(
            pointLights.data(),
            sizeof(GPUPointLight) * pointLights.size()
        );
    }

    BuildBatches(renderQueue);

    if (!mObjects.empty()) {
        mObjectDataBuffer.Upload(
            mObjects.data(),
            mObjects.size() * sizeof(ObjectData)
        );
    }

    return pushConstants;
}

void DeferredRenderer::RenderGBufferPass(const PushConstants& pushConstants) {
    GraphicsCommands::BeginGBufferPass();

    GraphicsCommands::SetViewport();
    GraphicsCommands::SetScissor();

    GraphicsCommands::BindPipeline(mGbufferPipeline);
    GraphicsCommands::BindDescriptorSets(
        mPipelineLayout,
        mAssetManager.GetTextureDescriptors()
    );

    GraphicsCommands::SetDepthTestEnable(true);
    GraphicsCommands::SetDepthWriteEnable(true);

    GraphicsCommands::PushConstants(
        mPipelineLayout,
        vk::ShaderStageFlagBits::eVertex,
        pushConstants
    );

    for (const auto& batch : mBatches) {
        GraphicsCommands::BindGeometryBuffer(batch.mesh->buffer);
        GraphicsCommands::DrawIndexed(
            batch.mesh->buffer.GetIndexCount(),
            batch.instanceCount,
            0,
            0,
            batch.firstInstance
        );
    }

    GraphicsCommands::EndGBufferPass();
}

void DeferredRenderer::RenderLightingPass(
    RenderContext& context,
    const PushConstants& pushConstants) {

    GraphicsCommands::BeginLabel(
        "Lighting pass",
        {0.76F, 0.32F, .32F, 1.F}
    );

    const uint32_t frameIndex = context.frameIndex;

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

    vk::RenderingAttachmentInfo brightAttachment {
        .imageView = mBrightnessImages[frameIndex].image.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingAttachmentInfo hdrAttachment {
        .imageView = mHDRImages[frameIndex].image.view,
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

    vk::RenderingInfo renderingInfo {
        .renderArea = {
            .offset = {0, 0},
            .extent = GraphicsCommands::GetSwapchainExtent()
        },
        .layerCount = 1,
        .colorAttachmentCount = static_cast<uint32_t>(attachments.size()),
        .pColorAttachments = attachments.data(),
    };

    GraphicsCommands::BeginRendering(renderingInfo);

    GraphicsCommands::PushConstants(
        mLightingLayout,
        vk::ShaderStageFlagBits::eFragment,
        pushConstants
    );

    GraphicsCommands::SetDepthTestEnable(false);
    GraphicsCommands::SetDepthWriteEnable(false);

    GraphicsCommands::BindPipeline(mLightingPipeline);
    GraphicsCommands::BindDescriptorSets(
        mLightingLayout,
        mLightingDescriptors
    );

    GraphicsCommands::Draw(
        3,
        1,
        0,
        0
    );

    GraphicsCommands::EndRendering();

    std::array<vk::ImageMemoryBarrier2, 1> postBarriers {
        GraphicsCommands::MakeImageBarrier(
            mBrightnessImages[frameIndex].image.image,
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

void DeferredRenderer::Resize(vk::Extent2D newSize){
    DestroyImages();
    CreateImages(newSize);
}

void DeferredRenderer::DestroyImages() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        GraphicsCommands::DestroyRenderImage(mHDRImages[i]);
        GraphicsCommands::DestroyRenderImage(mBrightnessImages[i]);
    }
}

void DeferredRenderer::CreateImages(vk::Extent2D extent){
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
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
            glm::mat4(glm::transpose(glm::inverse(glm::mat3(command.Transform)))),
            command.albedoTextureIndex,
            command.normalTextureIndex,
            command.materialTexturIndex,
            0,
        });
    }
}