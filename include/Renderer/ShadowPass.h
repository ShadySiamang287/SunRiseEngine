#pragma once

#include <array>
#include <span>
#include <vector>

#include "Graphics/Buffers.h"
#include "Graphics/RenderImage.h"
#include "Renderer/RenderingStructs.h"

namespace SUN {
    class AssetManager;

    inline constexpr uint32_t DIRECTIONAL_SHADOW_MAP_SIZE = 1024;
    inline constexpr uint32_t POINT_SHADOW_MAP_SIZE = 512;

    class ShadowPass {
    public:
        explicit ShadowPass(AssetManager& assetManager);

        void Prepare(
            std::span<const ObjectData> objects,
            const Camera& camera,
            std::span<GPUDirectionalLight> directionalLights,
            std::span<GPUPointLight> pointLights
        );

        void Execute(const PushConstants& pushConstants);

        const std::array<RenderImage, MAX_SHADOW_DIRECTIONAL_LIGHTS>&
            GetDirectionalShadowMaps() const {
            return mDirectionalShadowMaps;
        }

        const std::array<RenderImage, MAX_SHADOW_POINT_LIGHTS>&
            GetPointShadowMaps() const {
            return mPointShadowMaps;
        }

        vk::raii::Sampler& GetShadowSampler() {
            return mShadowSampler;
        }

    private:
        void CreateShadowMaps();

        void RenderShadowMap(
            vk::ImageView imageView,
            vk::Extent2D extent,
            const glm::mat4& lightViewProjection,
            vk::DeviceAddress objectDataAddress
        );

        static glm::mat4 BuildDirectionalMatrix(
            const Camera& camera,
            const glm::vec3& direction
        );

        static std::array<glm::mat4, 6>
            BuildPointMatrices(
                const glm::vec3& position,
                float range
            );

        AssetManager& mAssetManager;

        vk::raii::PipelineLayout mLayout = nullptr;
        vk::raii::Pipeline mPipeline = nullptr;
        vk::raii::Sampler mShadowSampler = nullptr;

        std::array<
            RenderImage,
            MAX_SHADOW_DIRECTIONAL_LIGHTS
        > mDirectionalShadowMaps;

        std::array<
            RenderImage,
            MAX_SHADOW_POINT_LIGHTS
        > mPointShadowMaps;

        std::array<
            std::array<vk::raii::ImageView, 6>,
            MAX_SHADOW_POINT_LIGHTS
        > mPointFaceViews;

        std::array<
            glm::mat4,
            MAX_SHADOW_DIRECTIONAL_LIGHTS
        > mDirectionalMatrices{};

        std::array<
            std::array<glm::mat4, 6>,
            MAX_SHADOW_POINT_LIGHTS
        > mPointMatrices{};

        ShaderBuffer mIndirectBuffer;
        std::vector<vk::DrawIndexedIndirectCommand>
            mIndirectCommands;

        uint32_t mObjectCount = 0;
        uint32_t mDirectionalShadowCount = 0;
        uint32_t mPointShadowCount = 0;
    };
}
