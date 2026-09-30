#pragma once

#include "Renderer/GBuffer.h"
#include "Renderer/RenderingStructs.h"
#include "Renderer/RenderQueue.h"

#include <span>

namespace SUN {
    class AssetManager;

    class DeferredRenderer {
    public:
        explicit DeferredRenderer(AssetManager& assetManager);
        ~DeferredRenderer();

        PushConstants PrepareFrame(
            const RenderQueue& renderQueue,
            const Camera* cam,
            std::span<const GPUDirectionalLight> directionalLights,
            std::span<const GPUPointLight> pointLights
        );

        void Execute(
            const RenderContext& context,
            const PushConstants& pushConstants
        );

        void Resize(vk::Extent2D newSize);

        std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& GetGBuffers();

    private:
        void CreateGBuffers(vk::Extent2D extent);
        void DestroyGBuffers();
        void BuildBatches(const RenderQueue& renderQueue);

        vk::raii::Pipeline mPipeline = nullptr;
        vk::raii::PipelineLayout mPipelineLayout = nullptr;

        std::array<GBuffer, MAX_FRAMES_IN_FLIGHT> mGBuffers;

        AssetManager& mAssetManager;

        ShaderBuffer mFrameDataBuffer;
        ShaderBuffer mObjectDataBuffer;
        ShaderBuffer mDirectionalLightDataBuffer;
        ShaderBuffer mPointLightDataBuffer;

        std::vector<uint32_t> mSortOrder;
        std::vector<ObjectData> mObjects;
        std::vector<DrawBatch> mBatches;

        FrameData mFrameData;
    };
}
