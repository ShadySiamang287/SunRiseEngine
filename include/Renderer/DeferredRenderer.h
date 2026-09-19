#pragma once

#include "Renderer/RenderingStructs.h"
#include "Renderer/RenderQueue.h"

#include <span>

namespace SUN {
    class DeferredRenderer {
    public:
        DeferredRenderer();

        void Render(RenderContext& context, const RenderQueue& renderQueue, const Camera* ca,
            std::span<const GPUDirectionalLight> directionalLights,
            std::span<const GPUPointLight> pointLightsm);

    private:
        void BuildBatches(const RenderQueue& renderQueue);

        DescriptorResources mLightingDescriptors; 
        DescriptorResources mBloomHorizontalDescriptors;
        DescriptorResources mBloomVerticalDescriptors;
        DescriptorResources mToneMappingDescriptors;
        vk::raii::Pipeline mGbufferPipeline = nullptr;
        vk::raii::Pipeline mLightingPipeline = nullptr;
        vk::raii::Pipeline mBloomPipeline = nullptr;
        vk::raii::Pipeline mToneMappingPipeline = nullptr;
        vk::raii::PipelineLayout mPipelineLayout = nullptr;
        vk::raii::PipelineLayout mLightingLayout = nullptr;
        vk::raii::PipelineLayout mBloomLayout = nullptr;
        vk::raii::PipelineLayout mToneMappingLayout = nullptr;

        vk::raii::Sampler mImageSampler = nullptr;

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