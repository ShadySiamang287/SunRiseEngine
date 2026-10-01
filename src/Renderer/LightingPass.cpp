#include "Renderer/LightingPass.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"

using namespace SUN;

LightingPass::LightingPass(
    std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& gBuffers,
    std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& aoImages,
    vk::raii::Sampler& sampler,
    ShadowPass& shadowPass)
    : mGBuffers(gBuffers),
      mAOImages(aoImages),
      mSampler(sampler),
      mShadowPass(shadowPass) {

    std::array<vk::DescriptorSetLayoutBinding, 8> bindings;

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
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    bindings[3] = {
        .binding = 3,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    bindings[4] = {
        .binding = 4,
        .descriptorType = vk::DescriptorType::eSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    bindings[5] = {
        .binding = 5,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = MAX_SHADOW_DIRECTIONAL_LIGHTS,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    bindings[6] = {
        .binding = 6,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = MAX_SHADOW_POINT_LIGHTS,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    bindings[7] = {
        .binding = 7,
        .descriptorType = vk::DescriptorType::eSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };



    mDescriptors =
        ResourceFactory::CreateDescriptorResources(bindings);

    mLayout =
        ResourceFactory::CreatePipelineLayout(vk::ShaderStageFlagBits::eFragment, sizeof(PushConstants), &mDescriptors);

    PipelineConfig lightingConfig {
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

    mPipeline =
        ResourceFactory::CreatePipeline(lightingConfig, mLayout, "Lighting Pipeline");

    CreateImages(GraphicsCommands::GetSwapchainExtent());

    UpdateDescriptors();
}

void LightingPass::Execute(const RenderContext& context, const PushConstants& pushConstants) {

    const uint32_t frameIndex =
        context.frameIndex;

    GraphicsCommands::BeginLabel(
        "Lighting pass",
        {0.76F, 0.32F, 0.32F, 1.F}
    );

    const std::array<ImageTransition, 2>
        transitions {
            ImageTransition {
                &mHDRImages[frameIndex],
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput
            },
            ImageTransition {
                &mBrightnessImages[frameIndex],
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput
            }
        };

    GraphicsCommands::TransitionImages(transitions);

    vk::RenderingAttachmentInfo hdrAttachment {
        .imageView = mHDRImages[frameIndex].image.view,
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

    vk::RenderingAttachmentInfo brightnessAttachment {
        .imageView = mBrightnessImages[frameIndex].image.view,
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

    std::array<vk::RenderingAttachmentInfo, 2>
        attachments {
            hdrAttachment,
            brightnessAttachment
        };

    vk::RenderingInfo renderingInfo {
        .renderArea = {
            .offset = {0, 0},
            .extent = mHDRImages[frameIndex].extent
        },
        .layerCount = 1,
        .colorAttachmentCount = static_cast<uint32_t>(attachments.size()),
        .pColorAttachments = attachments.data()
    };

    GraphicsCommands::BeginRendering(renderingInfo);

    GraphicsCommands::SetViewportAndScissor(mHDRImages[frameIndex].extent);

    GraphicsCommands::SetDepthTestEnable(false);
    GraphicsCommands::SetDepthWriteEnable(false);

    GraphicsCommands::BindPipeline(mPipeline);

    GraphicsCommands::BindDescriptorSets(mLayout, mDescriptors);

    GraphicsCommands::PushConstants(mLayout, vk::ShaderStageFlagBits::eFragment, pushConstants);

    GraphicsCommands::DrawFullScreenTriangle();

    GraphicsCommands::EndRendering();

    GraphicsCommands::TransitionImage(
        mBrightnessImages[frameIndex],
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::AccessFlagBits2::eShaderRead,
        vk::PipelineStageFlagBits2::eFragmentShader
    );

    GraphicsCommands::EndLabel();
}

void LightingPass::Resize(vk::Extent2D newSize) {

    CreateImages(newSize);
    UpdateDescriptors();
}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& LightingPass::GetHDRImages() {
    return mHDRImages;
}

std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& LightingPass::GetBrightnessImages() {
    return mBrightnessImages;
}

void LightingPass::CreateImages(vk::Extent2D extent) {

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        mHDRImages[i] =
            ResourceFactory::CreateRenderImage(
                vk::Format::eR16G16B16A16Sfloat,
                extent,
                vk::ImageLayout::eUndefined,
                vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageAspectFlagBits::eColor
            );

        mBrightnessImages[i] =
            ResourceFactory::CreateRenderImage(
                vk::Format::eR16G16B16A16Sfloat,
                extent,
                vk::ImageLayout::eUndefined,
                vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageAspectFlagBits::eColor
            );
    }
}

void LightingPass::UpdateDescriptors() {
    vk::DescriptorImageInfo samplerInfo {
        .sampler = *mSampler
    };

    vk::DescriptorImageInfo shadowSamplerInfo {
        .sampler = *mShadowPass.GetShadowSampler()
    };

    std::array<
        vk::DescriptorImageInfo,
        MAX_SHADOW_DIRECTIONAL_LIGHTS
    > directionalShadowInfos{};

    const auto& directionalShadowMaps =
        mShadowPass.GetDirectionalShadowMaps();

    for (uint32_t i = 0;
         i < MAX_SHADOW_DIRECTIONAL_LIGHTS;
         ++i) {
        directionalShadowInfos[i] = {
            .imageView =
                *directionalShadowMaps[i].image.view,
            .imageLayout =
                vk::ImageLayout::eShaderReadOnlyOptimal
        };
    }

    std::array<
        vk::DescriptorImageInfo,
        MAX_SHADOW_POINT_LIGHTS
    > pointShadowInfos{};

    const auto& pointShadowMaps =
        mShadowPass.GetPointShadowMaps();

    for (uint32_t i = 0;
         i < MAX_SHADOW_POINT_LIGHTS;
         ++i) {
        pointShadowInfos[i] = {
            .imageView =
                *pointShadowMaps[i].image.view,
            .imageLayout =
                vk::ImageLayout::eShaderReadOnlyOptimal
        };
    }

    for (uint32_t frameIndex = 0;
         frameIndex < MAX_FRAMES_IN_FLIGHT;
         ++frameIndex) {
        const GBuffer& gbuffer =
            mGBuffers[frameIndex];

        vk::DescriptorImageInfo albedoInfo {
            .imageView =
                *gbuffer.albedo.image.view,
            .imageLayout =
                vk::ImageLayout::eShaderReadOnlyOptimal
        };

        vk::DescriptorImageInfo normalInfo {
            .imageView =
                *gbuffer.normal.image.view,
            .imageLayout =
                vk::ImageLayout::eShaderReadOnlyOptimal
        };

        vk::DescriptorImageInfo depthInfo {
            .imageView =
                *gbuffer.depth.image.view,
            .imageLayout =
                vk::ImageLayout::eShaderReadOnlyOptimal
        };

        vk::DescriptorImageInfo aoInfo {
            .imageView =
                *mAOImages[frameIndex].image.view,
            .imageLayout =
                vk::ImageLayout::eShaderReadOnlyOptimal
        };

        std::array<vk::WriteDescriptorSet, 8>
            writes {
                vk::WriteDescriptorSet {
                    .dstSet =
                        *mDescriptors.sets[frameIndex],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType =
                        vk::DescriptorType::eSampledImage,
                    .pImageInfo = &albedoInfo
                },
                vk::WriteDescriptorSet {
                    .dstSet =
                        *mDescriptors.sets[frameIndex],
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType =
                        vk::DescriptorType::eSampledImage,
                    .pImageInfo = &normalInfo
                },
                vk::WriteDescriptorSet {
                    .dstSet =
                        *mDescriptors.sets[frameIndex],
                    .dstBinding = 2,
                    .descriptorCount = 1,
                    .descriptorType =
                        vk::DescriptorType::eSampledImage,
                    .pImageInfo = &depthInfo
                },
                vk::WriteDescriptorSet {
                    .dstSet =
                        *mDescriptors.sets[frameIndex],
                    .dstBinding = 3,
                    .descriptorCount = 1,
                    .descriptorType =
                        vk::DescriptorType::eSampledImage,
                    .pImageInfo = &aoInfo
                },
                vk::WriteDescriptorSet {
                    .dstSet =
                        *mDescriptors.sets[frameIndex],
                    .dstBinding = 4,
                    .descriptorCount = 1,
                    .descriptorType =
                        vk::DescriptorType::eSampler,
                    .pImageInfo = &samplerInfo
                },
                vk::WriteDescriptorSet {
                    .dstSet =
                        *mDescriptors.sets[frameIndex],
                    .dstBinding = 5,
                    .descriptorCount =
                        MAX_SHADOW_DIRECTIONAL_LIGHTS,
                    .descriptorType =
                        vk::DescriptorType::eSampledImage,
                    .pImageInfo =
                        directionalShadowInfos.data()
                },
                vk::WriteDescriptorSet {
                    .dstSet =
                        *mDescriptors.sets[frameIndex],
                    .dstBinding = 6,
                    .descriptorCount =
                        MAX_SHADOW_POINT_LIGHTS,
                    .descriptorType =
                        vk::DescriptorType::eSampledImage,
                    .pImageInfo =
                        pointShadowInfos.data()
                },
                vk::WriteDescriptorSet {
                    .dstSet =
                        *mDescriptors.sets[frameIndex],
                    .dstBinding = 7,
                    .descriptorCount = 1,
                    .descriptorType =
                        vk::DescriptorType::eSampler,
                    .pImageInfo = &shadowSamplerInfo
                }
            };

        GraphicsCommands::WriteDescriptors(
            writes
        );
    }
}
