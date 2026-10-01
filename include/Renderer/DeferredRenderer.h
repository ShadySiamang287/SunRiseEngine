#pragma once

#include <span>

#include "Graphics/GraphicsConfig.h"
#include "Renderer/GBuffer.h"
#include "Renderer/RenderingStructs.h"
#include "Renderer/RenderQueue.h"

namespace SUN {
    class AssetManager;

    class DeferredRenderer {
    public:
        explicit DeferredRenderer(AssetManager& assetManager);

        void Prepare(const RenderQueue& renderQueue);

        void Execute(const RenderContext& context, const PushConstants& pushConstants);

        void Resize(vk::Extent2D newSize);

        std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& GetGBuffers();
        std::span<const ObjectData> GetObjects() const;

    private:
        void CreateGBuffers(vk::Extent2D extent);
        void BuildObjects(const RenderQueue& renderQueue);

        vk::raii::Pipeline mPipeline = nullptr;
        vk::raii::PipelineLayout mPipelineLayout = nullptr;

        vk::raii::Pipeline mCullPipeline = nullptr;
        vk::raii::PipelineLayout mCullPipelineLayout = nullptr;

        std::array<GBuffer, MAX_FRAMES_IN_FLIGHT> mGBuffers;

        AssetManager& mAssetManager;

        std::vector<ObjectData> mObjects;
        ShaderBuffer mIndirectBuffer;
        uint32_t mDrawCount = 0;
    };
}
