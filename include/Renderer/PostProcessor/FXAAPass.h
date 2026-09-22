#pragma once

#include "Renderer/PostProcessor/PostProcessorPass.h"

namespace SUN{
    class FXAAPass : public PostProcessPass {
    public:
        FXAAPass(std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& toneMappingImages);

        void Execute(const PostProcessContext& context) override;
        void Resize(vk::Extent2D newSize) override;

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetOutput(uint32_t frameIndex) override;

    private:
        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& mToneMappingImages;

        DescriptorResources mDescriptors;
        vk::raii::PipelineLayout mLayout = nullptr;
        vk::raii::Pipeline mPipeline = nullptr;
        vk::raii::Sampler mSampler = nullptr;
    };
}