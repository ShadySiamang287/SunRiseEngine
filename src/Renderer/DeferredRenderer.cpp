#include "Renderer/DeferredRenderer.h"

#include "AssetManagement/AssetManager.h"
#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"
#include "Logger.h"

#include <algorithm>
#include <array>
#include <numeric>

using namespace SUN;

namespace {
    bool IsOutsideFrustum(
        const BoundingBox& bounds,
        const glm::mat4& model,
        const glm::mat4& viewProjection
    ) {
        const glm::vec3 minimum = bounds.min;
        const glm::vec3 maximum = bounds.max;

        bool outsideLeft = true;
        bool outsideRight = true;
        bool outsideBottom = true;
        bool outsideTop = true;
        bool outsideNear = true;
        bool outsideFar = true;

        for (uint32_t cornerIndex = 0;
             cornerIndex < 8;
             ++cornerIndex) {
            const glm::vec3 localCorner {
                (cornerIndex & 1u)
                    ? maximum.x
                    : minimum.x,
                (cornerIndex & 2u)
                    ? maximum.y
                    : minimum.y,
                (cornerIndex & 4u)
                    ? maximum.z
                    : minimum.z
            };

            const glm::vec4 clip =
                viewProjection *
                model *
                glm::vec4(localCorner, 1.0f);

            outsideLeft &=
                clip.x < -clip.w;

            outsideRight &=
                clip.x > clip.w;

            outsideBottom &=
                clip.y < -clip.w;

            outsideTop &=
                clip.y > clip.w;

            // Vulkan uses a zero-to-one depth clip range.
            outsideNear &=
                clip.z < 0.0f;

            outsideFar &=
                clip.z > clip.w;
        }

        return
            outsideLeft ||
            outsideRight ||
            outsideBottom ||
            outsideTop ||
            outsideNear ||
            outsideFar;
    }
}

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

void DeferredRenderer::Prepare(
    const RenderQueue& renderQueue,
    const Camera& camera
) {
    BuildBatches(renderQueue, camera);
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

void DeferredRenderer::BuildBatches(
    const RenderQueue& renderQueue,
    const Camera& camera
) {
    const auto& commands =
        renderQueue.GetCommands();

    const glm::mat4 viewProjection =
        camera.GetProjectionMatrix() *
        camera.GetViewMatrix();

    mSortOrder.clear();
    mSortOrder.reserve(
        std::min(
            commands.size(),
            static_cast<std::size_t>(MAX_OBJECTS)
        )
    );

    mCulledObjectCount = 0;

    std::size_t visibleCount = 0;

    for (uint32_t index = 0;
         index < static_cast<uint32_t>(commands.size());
         ++index) {
        const RenderCommand& command =
            commands[index];

        if (!command.mesh) {
            continue;
        }

        if (IsOutsideFrustum(
                command.mesh->bounds,
                command.Transform,
                viewProjection)) {
            ++mCulledObjectCount;
            continue;
        }

        ++visibleCount;

        if (mSortOrder.size() < MAX_OBJECTS) {
            mSortOrder.push_back(index);
        }
    }

    if (visibleCount > MAX_OBJECTS) {
        Logger::Log(
            Logger::WARNING,
            "Render queue has {} visible objects after frustum culling, "
            "only the first {} will be drawn",
            visibleCount,
            MAX_OBJECTS
        );
    }

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

        if (mBatches.empty() ||
            mBatches.back().mesh != command.mesh) {
            mBatches.push_back({
                command.mesh,
                static_cast<uint32_t>(
                    mObjects.size()
                ),
                0
            });
        }

        mBatches.back().instanceCount++;

        mObjects.push_back({
            command.Transform,
            glm::mat4(
                glm::transpose(
                    glm::inverse(
                        glm::mat3(
                            command.Transform
                        )
                    )
                )
            ),
            command.materialIndex,
            0,
            0,
            0
        });
    }
}
