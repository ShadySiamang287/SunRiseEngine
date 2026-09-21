#pragma once

#include "Renderer/RenderingStructs.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/PostProcessor/PostProcessorPass.h"

#include <span>

namespace SUN {
    class DeferredRenderer {
    public:
        DeferredRenderer(vk::raii::Sampler& sampler);
        ~DeferredRenderer();

        void Render(RenderContext& context, const RenderQueue& renderQueue, const Camera* ca,
            std::span<const GPUDirectionalLight> directionalLights,
            std::span<const GPUPointLight> pointLightsm);

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetBrightnessImages();
        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetHDRImages();

        private:
        void BuildBatches(const RenderQueue& renderQueue);

        DescriptorResources mLightingDescriptors; 

        vk::raii::Pipeline mGbufferPipeline = nullptr;
        vk::raii::Pipeline mLightingPipeline = nullptr;
        vk::raii::PipelineLayout mPipelineLayout = nullptr;
        vk::raii::PipelineLayout mLightingLayout = nullptr;

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT> mHDRImages;
        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT> mBrightnessImages;

        vk::raii::Sampler& mImageSampler;

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