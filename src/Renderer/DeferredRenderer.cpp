#include "Renderer/DeferredRenderer.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"
#include "Graphics/vertex.h"
#include "AssetManagement/AssetManager.h"

#include "Logger.h"

#include <algorithm>
#include <numeric>

using namespace SUN;

DeferredRenderer::DeferredRenderer(AssetManager& assetManager)
    : mAssetManager(assetManager) {

    mPipelineLayout = ResourceFactory::CreatePipelineLayout(
        vk::ShaderStageFlagBits::eVertex,
        sizeof(PushConstants),
        &mAssetManager.GetTextureDescriptors()
    );

    PipelineConfig gBufferConfig = {
        .vertexFile = "./shaders/vertMain.spv",
        .vertexName = "vertMain",
        .fragFile = "./shaders/gBufferFrag.spv",
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

    mPipeline = ResourceFactory::CreatePipeline(
        gBufferConfig,
        mPipelineLayout,
        "GBuffer pipeline"
    );

    mFrameDataBuffer.Init(sizeof(FrameData));
    mObjectDataBuffer.Init(sizeof(ObjectData) * MAX_OBJECTS, true);
    mDirectionalLightDataBuffer.Init(
        sizeof(GPUDirectionalLight) * MAX_DIRECTIONAL_LIGHTS,
        true
    );
    mPointLightDataBuffer.Init(
        sizeof(GPUPointLight) * MAX_POINT_LIGHTS,
        true
    );

    mSortOrder.reserve(MAX_OBJECTS);
    mObjects.reserve(MAX_OBJECTS);
    mBatches.reserve(256);

    CreateGBuffers(GraphicsCommands::GetSwapchainExtent());
}

DeferredRenderer::~DeferredRenderer() {
    DestroyGBuffers();
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
    mFrameData.directionalLightCount =
        static_cast<uint32_t>(directionalLights.size());
    mFrameData.pointLightCount =
        static_cast<uint32_t>(pointLights.size());

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

void DeferredRenderer::Execute(
    const RenderContext& context,
    const PushConstants& pushConstants) {

    GBuffer& gbuffer = mGBuffers[context.frameIndex];

    GraphicsCommands::BeginLabel(
        "GBuffer pass",
        {0.5F, 0.76F, 0.32F, 1.F}
    );

    std::array<vk::ImageMemoryBarrier2, 3> beginBarriers {
        GraphicsCommands::MakeImageBarrier(
            gbuffer.albedo.image.image,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlagBits2::eShaderRead,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput
        ),
        GraphicsCommands::MakeImageBarrier(
            gbuffer.normal.image.image,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlagBits2::eShaderRead,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput
        ),
        GraphicsCommands::MakeImageBarrier(
            gbuffer.depth.image.image,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::AccessFlagBits2::eShaderRead,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::ImageAspectFlagBits::eDepth
        )
    };

    GraphicsCommands::ImageBarriers(beginBarriers);

    vk::RenderingAttachmentInfo albedoAttachment {
        .imageView = gbuffer.albedo.image.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingAttachmentInfo normalAttachment {
        .imageView = gbuffer.normal.image.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingAttachmentInfo depthAttachment {
        .imageView = gbuffer.depth.image.view,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearDepthStencilValue{1.0f, 0}
        }
    };

    std::array<vk::RenderingAttachmentInfo, 2> colorAttachments {
        albedoAttachment,
        normalAttachment
    };

    vk::RenderingInfo renderingInfo {
        .renderArea = {
            .offset = {0, 0},
            .extent = gbuffer.albedo.extent
        },
        .layerCount = 1,
        .colorAttachmentCount =
            static_cast<uint32_t>(colorAttachments.size()),
        .pColorAttachments = colorAttachments.data(),
        .pDepthAttachment = &depthAttachment
    };

    GraphicsCommands::BeginRendering(renderingInfo);

    GraphicsCommands::SetViewport();
    GraphicsCommands::SetScissor();

    GraphicsCommands::BindPipeline(mPipeline);
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

    GraphicsCommands::EndRendering();

    std::array<vk::ImageMemoryBarrier2, 3> endBarriers {
        GraphicsCommands::MakeImageBarrier(
            gbuffer.albedo.image.image,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eFragmentShader
        ),
        GraphicsCommands::MakeImageBarrier(
            gbuffer.normal.image.image,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eFragmentShader
        ),
        GraphicsCommands::MakeImageBarrier(
            gbuffer.depth.image.image,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::ImageAspectFlagBits::eDepth
        )
    };

    GraphicsCommands::ImageBarriers(endBarriers);
    GraphicsCommands::EndLabel();
}

void DeferredRenderer::Resize(vk::Extent2D newSize) {
    DestroyGBuffers();
    CreateGBuffers(newSize);
}

std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& DeferredRenderer::GetGBuffers() {
    return mGBuffers;
}

void DeferredRenderer::CreateGBuffers(vk::Extent2D extent) {
    for (auto& gbuffer : mGBuffers) {
        gbuffer.albedo = ResourceFactory::CreateRenderImage(
            vk::Format::eR16G16B16A16Sfloat,
            extent,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageUsageFlagBits::eColorAttachment |
                vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eColor
        );

        gbuffer.normal = ResourceFactory::CreateRenderImage(
            vk::Format::eR16G16B16A16Sfloat,
            extent,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageUsageFlagBits::eColorAttachment |
                vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eColor
        );

        gbuffer.depth = ResourceFactory::CreateRenderImage(
            vk::Format::eD32Sfloat,
            extent,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment |
                vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eDepth
        );
    }
}

void DeferredRenderer::DestroyGBuffers() {
    for (auto& gbuffer : mGBuffers) {
        GraphicsCommands::DestroyRenderImage(gbuffer.albedo);
        GraphicsCommands::DestroyRenderImage(gbuffer.normal);
        GraphicsCommands::DestroyRenderImage(gbuffer.depth);
    }
}

void DeferredRenderer::BuildBatches(const RenderQueue& renderQueue) {
    const auto& commands = renderQueue.GetCommands();
    size_t count = commands.size();

    if (count > MAX_OBJECTS) {
        Logger::Log(
            Logger::WARNING,
            "Render queue has {} objects, only the first {} will be drawn",
            count,
            MAX_OBJECTS
        );
        count = MAX_OBJECTS;
    }

    mSortOrder.resize(count);
    std::iota(mSortOrder.begin(), mSortOrder.end(), 0u);

    std::ranges::sort(
        mSortOrder,
        {},
        [&](uint32_t i) {
            return commands[i].mesh;
        }
    );

    mObjects.clear();
    mBatches.clear();

    for (uint32_t index : mSortOrder) {
        const RenderCommand& command = commands[index];

        if (mBatches.empty() || mBatches.back().mesh != command.mesh) {
            mBatches.push_back({
                command.mesh,
                static_cast<uint32_t>(mObjects.size()),
                0
            });
        }

        mBatches.back().instanceCount++;

        mObjects.push_back({
            command.Transform,
            glm::mat4(
                glm::transpose(
                    glm::inverse(
                        glm::mat3(command.Transform)
                    )
                )
            ),
            command.albedoTextureIndex,
            command.normalTextureIndex,
            command.materialTexturIndex,
            command.metalicFactor,
            command.roughnessFactor,
            0,
            0
        });
    }
}
