#include "Renderer/ShadowPass.h"

#include "AssetManagement/AssetManager.h"
#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace SUN;

namespace {
    constexpr float DIRECTIONAL_SHADOW_HALF_EXTENT = 30.0f;
    constexpr float DIRECTIONAL_SHADOW_DISTANCE = 60.0f;
    constexpr float POINT_SHADOW_NEAR = 0.1f;
}

ShadowPass::ShadowPass(AssetManager& assetManager)
    : mAssetManager(assetManager) {

    mLayout =
        ResourceFactory::CreatePipelineLayout(
            vk::ShaderStageFlagBits::eVertex,
            sizeof(ShadowPushConstants)
        );

    PipelineConfig config {
        .vertexFile = "./shaders/shadowVert.spv",
        .vertexName = "shadowVert",
        .primitiveTopology =
            vk::PrimitiveTopology::eTriangleList,
        .depthAttachmentFormat =
            vk::Format::eD32Sfloat,
        .useVertexInput = true,
        .vertexAttributeCount = 1,
        .depthBiasEnable = true
    };

    mPipeline =
        ResourceFactory::CreatePipeline(
            config,
            mLayout,
            "Shadow depth pipeline"
        );

    SamplerConfig samplerConfig {
        .minFilter = vk::Filter::eNearest,
        .magFilter = vk::Filter::eNearest,
        .mipmapMode =
            vk::SamplerMipmapMode::eNearest,
        .addressModeU =
            vk::SamplerAddressMode::eClampToEdge,
        .addressModeV =
            vk::SamplerAddressMode::eClampToEdge,
        .addressModeW =
            vk::SamplerAddressMode::eClampToEdge,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .anisotropy = false,
        .compare = false
    };

    mShadowSampler =
        ResourceFactory::CreateSampler(
            samplerConfig
        );

    mIndirectBuffer.Init(
        sizeof(vk::DrawIndexedIndirectCommand) *
            MAX_OBJECTS,
        true,
        vk::BufferUsageFlagBits::eIndirectBuffer
    );

    mIndirectCommands.reserve(MAX_OBJECTS);

    mPointFaceViews.reserve(
        MAX_SHADOW_POINT_LIGHTS * 6
    );

    CreateShadowMaps();
}

void ShadowPass::CreateShadowMaps() {
    for (uint32_t i = 0;
         i < MAX_SHADOW_DIRECTIONAL_LIGHTS;
         ++i) {
        mDirectionalShadowMaps[i] =
            ResourceFactory::CreateRenderImage(
                vk::Format::eD32Sfloat,
                {
                    DIRECTIONAL_SHADOW_MAP_SIZE,
                    DIRECTIONAL_SHADOW_MAP_SIZE
                },
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageAspectFlagBits::eDepth
            );
    }

    for (uint32_t i = 0;
         i < MAX_SHADOW_POINT_LIGHTS;
         ++i) {
        mPointShadowMaps[i] =
            ResourceFactory::CreateRenderImage(
                vk::Format::eD32Sfloat,
                {
                    POINT_SHADOW_MAP_SIZE,
                    POINT_SHADOW_MAP_SIZE
                },
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageAspectFlagBits::eDepth,
                6,
                vk::ImageViewType::eCube,
                vk::ImageCreateFlagBits::eCubeCompatible
            );

        for (uint32_t face = 0;
             face < 6;
             ++face) {
            mPointFaceViews.emplace_back(
                ResourceFactory::CreateImageView(
                    mPointShadowMaps[i],
                    vk::ImageViewType::e2D,
                    face,
                    1
                )
            );
        }
    }
}

void ShadowPass::Prepare(
    std::span<const ObjectData> objects,
    const Camera& camera,
    std::span<GPUDirectionalLight> directionalLights,
    std::span<GPUPointLight> pointLights
) {
    const std::size_t objectCount =
        std::min(
            objects.size(),
            static_cast<std::size_t>(MAX_OBJECTS)
        );

    bool drawLayoutChanged =
        mIndirectCommands.size() != objectCount;

    if (!drawLayoutChanged) {
        for (uint32_t i = 0;
             i < static_cast<uint32_t>(objectCount);
             ++i) {
            const ObjectData& object = objects[i];
            const auto& command = mIndirectCommands[i];

            if (
                command.indexCount != object.indexCount ||
                command.firstIndex != object.firstIndex ||
                command.vertexOffset != object.vertexOffset ||
                command.firstInstance != i
            ) {
                drawLayoutChanged = true;
                break;
            }
        }
    }

    if (drawLayoutChanged) {
        mIndirectCommands.resize(objectCount);

        for (uint32_t i = 0;
             i < static_cast<uint32_t>(objectCount);
             ++i) {
            const ObjectData& object = objects[i];

            mIndirectCommands[i] = {
                .indexCount = object.indexCount,
                .instanceCount = 1,
                .firstIndex = object.firstIndex,
                .vertexOffset = object.vertexOffset,
                .firstInstance = i
            };
        }

        if (!mIndirectCommands.empty()) {
            const size_t uploadSize =
                mIndirectCommands.size() *
                sizeof(vk::DrawIndexedIndirectCommand);

            for (uint32_t frameIndex = 0;
                 frameIndex < MAX_FRAMES_IN_FLIGHT;
                 ++frameIndex) {
                mIndirectBuffer.Upload(
                    frameIndex,
                    mIndirectCommands.data(),
                    uploadSize
                );
            }
        }
    }

    mObjectCount =
        static_cast<uint32_t>(objectCount);

    mDirectionalShadowCount = 0;

    for (GPUDirectionalLight& light :
         directionalLights) {
        if (
            light.castsShadows == 0 ||
            mDirectionalShadowCount >=
                MAX_SHADOW_DIRECTIONAL_LIGHTS
        ) {
            light.castsShadows = 0;
            continue;
        }

        const uint32_t shadowIndex =
            mDirectionalShadowCount++;

        light.shadowIndex = shadowIndex;

        const glm::vec3 direction =
            glm::normalize(
                glm::vec3(
                    light.directionIntensity
                )
            );

        mDirectionalMatrices[shadowIndex] =
            BuildDirectionalMatrix(
                camera,
                direction
            );

        light.lightViewProjection =
            mDirectionalMatrices[shadowIndex];
    }

    mPointShadowCount = 0;

    for (GPUPointLight& light : pointLights) {
        if (
            light.castsShadows == 0 ||
            mPointShadowCount >=
                MAX_SHADOW_POINT_LIGHTS
        ) {
            light.castsShadows = 0;
            continue;
        }

        const uint32_t shadowIndex =
            mPointShadowCount++;

        light.shadowIndex = shadowIndex;

        mPointMatrices[shadowIndex] =
            BuildPointMatrices(
                glm::vec3(light.positionRange),
                std::max(
                    light.positionRange.w,
                    POINT_SHADOW_NEAR + 0.01f
                )
            );
    }
}

glm::mat4 ShadowPass::BuildDirectionalMatrix(
    const Camera& camera,
    const glm::vec3& direction
) {
    const glm::vec3 cameraForward =
        camera.Rotation *
        glm::vec3(0.0f, 0.0f, -1.0f);

    const glm::vec3 target =
        camera.Position +
        cameraForward *
            (DIRECTIONAL_SHADOW_DISTANCE * 0.35f);

    const glm::vec3 eye =
        target -
        direction *
            DIRECTIONAL_SHADOW_DISTANCE;

    glm::vec3 up(0.0f, 1.0f, 0.0f);

    if (
        std::abs(
            glm::dot(direction, up)
        ) > 0.95f
    ) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    const glm::mat4 view =
        glm::lookAtRH(
            eye,
            target,
            up
        );

    const glm::mat4 projection =
        glm::ortho(
            -DIRECTIONAL_SHADOW_HALF_EXTENT,
            DIRECTIONAL_SHADOW_HALF_EXTENT,
            -DIRECTIONAL_SHADOW_HALF_EXTENT,
            DIRECTIONAL_SHADOW_HALF_EXTENT,
            0.1f,
            DIRECTIONAL_SHADOW_DISTANCE * 2.0f
        );

    return projection * view;
}

std::array<glm::mat4, 6>
ShadowPass::BuildPointMatrices(
    const glm::vec3& position,
    float range
) {
    const glm::mat4 projection =
        glm::perspective(
            glm::radians(90.0f),
            1.0f,
            POINT_SHADOW_NEAR,
            range
        );

    const std::array<glm::vec3, 6> directions {
        glm::vec3( 1.0f,  0.0f,  0.0f),
        glm::vec3(-1.0f,  0.0f,  0.0f),
        glm::vec3( 0.0f,  1.0f,  0.0f),
        glm::vec3( 0.0f, -1.0f,  0.0f),
        glm::vec3( 0.0f,  0.0f,  1.0f),
        glm::vec3( 0.0f,  0.0f, -1.0f)
    };

    const std::array<glm::vec3, 6> up {
        glm::vec3(0.0f, -1.0f,  0.0f),
        glm::vec3(0.0f, -1.0f,  0.0f),
        glm::vec3(0.0f,  0.0f,  1.0f),
        glm::vec3(0.0f,  0.0f, -1.0f),
        glm::vec3(0.0f, -1.0f,  0.0f),
        glm::vec3(0.0f, -1.0f,  0.0f)
    };

    std::array<glm::mat4, 6> matrices{};

    for (uint32_t face = 0;
         face < 6;
         ++face) {
        matrices[face] =
            projection *
            glm::lookAtRH(
                position,
                position + directions[face],
                up[face]
            );
    }

    return matrices;
}

void ShadowPass::Execute(
    const PushConstants& pushConstants
) {
    if (
        mObjectCount == 0 ||
        (
            mDirectionalShadowCount == 0 &&
            mPointShadowCount == 0
        )
    ) {
        return;
    }

    GraphicsCommands::BeginLabel(
        "Shadow maps",
        {0.35f, 0.35f, 0.35f, 1.0f}
    );

    GraphicsCommands::BindPipeline(mPipeline);
    GraphicsCommands::BindGeometryBuffer(
        mAssetManager.GetGeometryBuffer()
    );

    GraphicsCommands::SetDepthTestEnable(true);
    GraphicsCommands::SetDepthWriteEnable(true);
    GraphicsCommands::SetDepthBias(
        1.25f,
        0.0f,
        1.75f
    );

    // All shadow maps are written before any of them are sampled,
    // so transition the active set together instead of issuing one
    // pipeline barrier per light.
    std::vector<ImageTransition> transitions;
    transitions.reserve(
        mDirectionalShadowCount +
        mPointShadowCount
    );

    for (uint32_t i = 0;
         i < mDirectionalShadowCount;
         ++i) {
        transitions.push_back({
            &mDirectionalShadowMaps[i],
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests
        });
    }

    for (uint32_t i = 0;
         i < mPointShadowCount;
         ++i) {
        transitions.push_back({
            &mPointShadowMaps[i],
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests
        });
    }

    GraphicsCommands::TransitionImages(transitions);

    const std::string drawLabel =
        "Depth indirect draw (" +
        std::to_string(mObjectCount) +
        " commands)";

    if (mDirectionalShadowCount > 0) {
        GraphicsCommands::BeginLabel(
            "Directional shadows (" +
                std::to_string(
                    mDirectionalShadowCount
                ) +
                ")",
            {0.45f, 0.45f, 0.75f, 1.0f}
        );

        GraphicsCommands::SetViewportAndScissor({
            DIRECTIONAL_SHADOW_MAP_SIZE,
            DIRECTIONAL_SHADOW_MAP_SIZE
        });

        for (uint32_t i = 0;
             i < mDirectionalShadowCount;
             ++i) {
            GraphicsCommands::BeginLabel(
                "Directional shadow " +
                    std::to_string(i),
                {0.55f, 0.55f, 0.85f, 1.0f}
            );

            RenderShadowMap(
                *mDirectionalShadowMaps[i].image.view,
                mDirectionalShadowMaps[i].extent,
                mDirectionalMatrices[i],
                pushConstants.objectDataAddress,
                drawLabel
            );

            GraphicsCommands::EndLabel();
        }

        GraphicsCommands::EndLabel();
    }

    if (mPointShadowCount > 0) {
        static constexpr std::array<
            const char*,
            6
        > FACE_NAMES {
            "+X",
            "-X",
            "+Y",
            "-Y",
            "+Z",
            "-Z"
        };

        GraphicsCommands::BeginLabel(
            "Point shadows (" +
                std::to_string(
                    mPointShadowCount
                ) +
                ")",
            {0.75f, 0.45f, 0.45f, 1.0f}
        );

        GraphicsCommands::SetViewportAndScissor({
            POINT_SHADOW_MAP_SIZE,
            POINT_SHADOW_MAP_SIZE
        });

        for (uint32_t i = 0;
             i < mPointShadowCount;
             ++i) {
            GraphicsCommands::BeginLabel(
                "Point shadow " +
                    std::to_string(i),
                {0.85f, 0.55f, 0.55f, 1.0f}
            );

            for (uint32_t face = 0;
                 face < 6;
                 ++face) {
                GraphicsCommands::BeginLabel(
                    std::string("Face ") +
                        FACE_NAMES[face],
                    {0.9f, 0.65f, 0.65f, 1.0f}
                );

                RenderShadowMap(
                    *mPointFaceViews[
                        i * 6 + face
                    ],
                    mPointShadowMaps[i].extent,
                    mPointMatrices[i][face],
                    pushConstants.objectDataAddress,
                    drawLabel
                );

                GraphicsCommands::EndLabel();
            }

            GraphicsCommands::EndLabel();
        }

        GraphicsCommands::EndLabel();
    }

    transitions.clear();

    for (uint32_t i = 0;
         i < mDirectionalShadowCount;
         ++i) {
        transitions.push_back({
            &mDirectionalShadowMaps[i],
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eFragmentShader
        });
    }

    for (uint32_t i = 0;
         i < mPointShadowCount;
         ++i) {
        transitions.push_back({
            &mPointShadowMaps[i],
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eFragmentShader
        });
    }

    GraphicsCommands::TransitionImages(transitions);

    GraphicsCommands::EndLabel();
}

void ShadowPass::RenderShadowMap(
    vk::ImageView imageView,
    vk::Extent2D extent,
    const glm::mat4& lightViewProjection,
    vk::DeviceAddress objectDataAddress,
    std::string_view drawLabel
) {
    const vk::RenderingAttachmentInfo depthAttachment {
        .imageView = imageView,
        .imageLayout =
            vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue {
            vk::ClearDepthStencilValue {
                1.0f,
                0
            }
        }
    };

    vk::RenderingInfo renderingInfo {
        .renderArea = {
            .offset = {0, 0},
            .extent = extent
        },
        .layerCount = 1,
        .pDepthAttachment = &depthAttachment
    };

    GraphicsCommands::BeginRendering(
        renderingInfo
    );

    const ShadowPushConstants constants {
        .objectDataAddress =
            objectDataAddress,
        .lightViewProjection =
            lightViewProjection
    };

    GraphicsCommands::PushShadowConstants(
        mLayout,
        constants
    );

    GraphicsCommands::BeginLabel(
        std::string(drawLabel),
        {0.8f, 0.8f, 0.8f, 1.0f}
    );

    GraphicsCommands::DrawIndexedIndirect(
        mIndirectBuffer.GetHandle(),
        0,
        mObjectCount,
        sizeof(vk::DrawIndexedIndirectCommand)
    );

    GraphicsCommands::EndLabel();

    GraphicsCommands::EndRendering();
}
