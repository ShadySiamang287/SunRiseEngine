#include "Renderer/DeferredRenderer.h"

#include "AssetManagement/AssetManager.h"
#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"
#include "Logger.h"

#include <algorithm>
#include <array>

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

    mPipeline =
        ResourceFactory::CreatePipeline(
            gBufferConfig,
            mPipelineLayout,
            "GBuffer pipeline"
        );

    mCullPipelineLayout =
        ResourceFactory::CreatePipelineLayout(
            vk::ShaderStageFlagBits::eCompute,
            sizeof(CullPushConstants)
        );

    mCullPipeline =
        ResourceFactory::CreateComputePipeline(
            "./shaders/cullMain.spv",
            "cullMain",
            mCullPipelineLayout,
            "GPU frustum culling pipeline"
        );

    mIndirectBuffer.Init(
        sizeof(vk::DrawIndexedIndirectCommand) * MAX_OBJECTS,
        true,
        vk::BufferUsageFlagBits::eIndirectBuffer
    );

    mObjects.reserve(MAX_OBJECTS);

    CreateGBuffers(GraphicsCommands::GetSwapchainExtent());
}

void DeferredRenderer::Prepare(
    const RenderQueue& renderQueue
) {
    BuildObjects(renderQueue);
}

void DeferredRenderer::Execute(const RenderContext& context, const PushConstants& pushConstants) {

    if (mDrawCount > 0) {
        GraphicsCommands::BeginLabel(
            "GPU frustum culling",
            {0.85F, 0.55F, 0.15F, 1.0F}
        );

        GraphicsCommands::BindComputePipeline(
            mCullPipeline
        );

        const CullPushConstants cullConstants {
            .frameDataAddress =
                pushConstants.frameDataAddress,
            .objectDataAddress =
                pushConstants.objectDataAddress,
            .indirectCommandAddress =
                mIndirectBuffer.GetDeviceAddress(),
            .objectCount = mDrawCount
        };

        GraphicsCommands::PushCullConstants(
            mCullPipelineLayout,
            cullConstants
        );

        constexpr uint32_t CULL_GROUP_SIZE = 64;

        GraphicsCommands::Dispatch(
            (mDrawCount + CULL_GROUP_SIZE - 1) /
                CULL_GROUP_SIZE
        );

        GraphicsCommands::BufferBarrier(
            mIndirectBuffer.GetHandle(),
            sizeof(vk::DrawIndexedIndirectCommand) *
                mDrawCount,
            vk::AccessFlagBits2::eShaderWrite,
            vk::AccessFlagBits2::eIndirectCommandRead,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::PipelineStageFlagBits2::eDrawIndirect
        );

        GraphicsCommands::EndLabel();
    }

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

    if (mDrawCount > 0) {
        GraphicsCommands::BindGeometryBuffer(
            mAssetManager.GetGeometryBuffer()
        );

        GraphicsCommands::DrawIndexedIndirect(
            mIndirectBuffer.GetHandle(),
            0,
            mDrawCount,
            sizeof(vk::DrawIndexedIndirectCommand)
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

void DeferredRenderer::BuildObjects(
    const RenderQueue& renderQueue
) {
    const auto& commands =
        renderQueue.GetCommands();

    if (commands.size() > MAX_OBJECTS) {
        Logger::Log(
            Logger::WARNING,
            "Render queue has {} objects, only the first {} "
            "will be considered for GPU culling",
            commands.size(),
            MAX_OBJECTS
        );
    }

    mObjects.clear();

    const std::size_t count =
        std::min(
            commands.size(),
            static_cast<std::size_t>(MAX_OBJECTS)
        );

    for (std::size_t i = 0; i < count; ++i) {
        const RenderCommand& command =
            commands[i];

        if (!command.mesh) {
            continue;
        }

        const BoundingBox& bounds =
            command.mesh->bounds;

        mObjects.push_back({
            .model = command.Transform,
            .normal = glm::mat4(
                glm::transpose(
                    glm::inverse(
                        glm::mat3(command.Transform)
                    )
                )
            ),
            .boundsCenter =
                glm::vec4(bounds.Center(), 1.0f),
            .boundsExtents =
                glm::vec4(bounds.Extents(), 0.0f),
            .materialIndex = command.materialIndex,
            .firstIndex = command.mesh->firstIndex,
            .indexCount = command.mesh->indexCount,
            .vertexOffset = command.mesh->vertexOffset
        });
    }

    mDrawCount =
        static_cast<uint32_t>(mObjects.size());
}
