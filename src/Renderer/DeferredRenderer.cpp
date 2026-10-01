#include "Renderer/DeferredRenderer.h"

#include "AssetManagement/AssetManager.h"
#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"
#include "Logger.h"

#include <algorithm>
#include <numeric>

using namespace SUN;

DeferredRenderer::DeferredRenderer(AssetManager& assetManager)
    : mAssetManager(assetManager) {

    mPipelineLayout = ResourceFactory::CreatePipelineLayout(
        vk::ShaderStageFlagBits::eVertex |
            vk::ShaderStageFlagBits::eFragment,
        sizeof(PushConstants),
        &mAssetManager.GetTextureDescriptors()
    );

    PipelineConfig gBufferConfig {
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

    mPipeline = ResourceFactory::CreatePipeline(gBufferConfig, mPipelineLayout, "GBuffer pipeline");

    mSortOrder.reserve(MAX_OBJECTS);
    mObjects.reserve(MAX_OBJECTS);
    mBatches.reserve(256);

    CreateGBuffers(GraphicsCommands::GetSwapchainExtent());
}

void DeferredRenderer::Prepare(const RenderQueue& renderQueue) {

    BuildBatches(renderQueue);
}

void DeferredRenderer::Execute(const RenderContext& context, const PushConstants& pushConstants) {

    GBuffer& gbuffer =
        mGBuffers[context.frameIndex];

    GraphicsCommands::BeginLabel(
        "GBuffer pass",
        {0.5F, 0.76F, 0.32F, 1.F}
    );

    const std::array<ImageTransition, 3>
        beginTransitions {
            ImageTransition {
                &gbuffer.albedo,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput
            },
            ImageTransition {
                &gbuffer.normal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput
            },
            ImageTransition {
                &gbuffer.depth,
                vk::ImageLayout::eDepthAttachmentOptimal,
                vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits2::eLateFragmentTests
            }
        };

    GraphicsCommands::TransitionImages(beginTransitions);

    vk::RenderingAttachmentInfo albedoAttachment {
        .imageView = gbuffer.albedo.image.view,
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

    vk::RenderingAttachmentInfo normalAttachment {
        .imageView = gbuffer.normal.image.view,
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

    vk::RenderingAttachmentInfo depthAttachment {
        .imageView = gbuffer.depth.image.view,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue {
            vk::ClearDepthStencilValue {
                1.0f,
                0
            }
        }
    };

    std::array<vk::RenderingAttachmentInfo, 2>
        colorAttachments {
            albedoAttachment,
            normalAttachment
        };

    vk::RenderingInfo renderingInfo {
        .renderArea = {
            .offset = {0, 0},
            .extent = gbuffer.albedo.extent
        },
        .layerCount = 1,
        .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
        .pColorAttachments = colorAttachments.data(),
        .pDepthAttachment = &depthAttachment
    };

    GraphicsCommands::BeginRendering(renderingInfo);

    GraphicsCommands::SetViewportAndScissor(gbuffer.albedo.extent);

    GraphicsCommands::BindPipeline(mPipeline);

    GraphicsCommands::BindDescriptorSets(mPipelineLayout, mAssetManager.GetTextureDescriptors());

    GraphicsCommands::SetDepthTestEnable(true);
    GraphicsCommands::SetDepthWriteEnable(true);

    GraphicsCommands::PushConstants(
        mPipelineLayout,
        vk::ShaderStageFlagBits::eVertex |
            vk::ShaderStageFlagBits::eFragment,
        pushConstants
    );

    if (!mBatches.empty()) {
        GraphicsCommands::BindGeometryBuffer(
            mAssetManager.GetGeometryBuffer()
        );
    }

    for (const auto& batch : mBatches) {
        GraphicsCommands::DrawIndexed(
            batch.mesh->indexCount,
            batch.instanceCount,
            batch.mesh->firstIndex,
            batch.mesh->vertexOffset,
            batch.firstInstance
        );
    }

    GraphicsCommands::EndRendering();

    const std::array<ImageTransition, 3>
        endTransitions {
            ImageTransition {
                &gbuffer.albedo,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eFragmentShader
            },
            ImageTransition {
                &gbuffer.normal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eFragmentShader
            },
            ImageTransition {
                &gbuffer.depth,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eFragmentShader
            }
        };

    GraphicsCommands::TransitionImages(endTransitions);

    GraphicsCommands::EndLabel();
}

void DeferredRenderer::Resize(vk::Extent2D newSize) {

    CreateGBuffers(newSize);
}

std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& DeferredRenderer::GetGBuffers() {
    return mGBuffers;
}

std::span<const ObjectData> DeferredRenderer::GetObjects() const {
    return mObjects;
}

void DeferredRenderer::CreateGBuffers(vk::Extent2D extent) {

    for (auto& gbuffer : mGBuffers) {
        gbuffer.albedo =
            ResourceFactory::CreateRenderImage(
                vk::Format::eR16G16B16A16Sfloat,
                extent,
                vk::ImageLayout::eUndefined,
                vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageAspectFlagBits::eColor
            );

        gbuffer.normal =
            ResourceFactory::CreateRenderImage(
                vk::Format::eR16G16B16A16Sfloat,
                extent,
                vk::ImageLayout::eUndefined,
                vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageAspectFlagBits::eColor
            );

        gbuffer.depth =
            ResourceFactory::CreateRenderImage(
                vk::Format::eD32Sfloat,
                extent,
                vk::ImageLayout::eUndefined,
                vk::ImageUsageFlagBits::eDepthStencilAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageAspectFlagBits::eDepth
            );
    }
}

void DeferredRenderer::BuildBatches(const RenderQueue& renderQueue) {

    const auto& commands =
        renderQueue.GetCommands();

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
        const RenderCommand& command =
            commands[index];

        if (mBatches.empty() || mBatches.back().mesh != command.mesh) {
            mBatches.push_back({ command.mesh, static_cast<uint32_t>(mObjects.size()), 0 });
        }

        mBatches.back().instanceCount++;

        mObjects.push_back({
            command.Transform,
            glm::mat4(glm::transpose(glm::inverse(glm::mat3(command.Transform)))),
            command.materialIndex,
            0,
            0,
            0
        });
    }
}
