#pragma once

#include "Renderer/RenderingStructs.h"
#include "Renderer/RenderQueue.h"

namespace SUN {
    class DeferredRenderer {
    public:
        DeferredRenderer();

        void Render(RenderContext& context, const RenderQueue& renderQueue, const Camera* cam);

    private:
        void BuildBatches(const RenderQueue& renderQueue);

        DescriptorResources mLightingDescriptors; 
        vk::raii::Pipeline mGbufferPipeline = nullptr;
        vk::raii::Pipeline mLightingPipeline = nullptr;
        vk::raii::PipelineLayout mPipelineLayout = nullptr;
        vk::raii::PipelineLayout mLightingLayout = nullptr;

        ShaderBuffer mFrameDataBuffer;
        ShaderBuffer mObjectDataBuffer;

        std::vector<uint32_t> mSortOrder;
        std::vector<ObjectData> mObjects;
        std::vector<DrawBatch> mBatches;

        FrameData mFrameData;
    };
}