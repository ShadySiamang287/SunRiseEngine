#pragma once

#include "Renderer/GBuffer.h"
#include "Renderer/RenderingStructs.h"

namespace SUN {
    class LightingPass {
    public:
        LightingPass(
            std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& gBuffers,
            vk::raii::Sampler& sampler
        );
        ~LightingPass();

        void Execute(
            const RenderContext& context,
            const PushConstants& pushConstants
        );

        void Resize(vk::Extent2D newSize);

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetHDRImages();
        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetBrightnessImages();

    private:
        void CreateImages(vk::Extent2D extent);
        void DestroyImages();

        std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& mGBuffers;

        DescriptorResources mDescriptors;
        vk::raii::PipelineLayout mLayout = nullptr;
        vk::raii::Pipeline mPipeline = nullptr;

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT> mHDRImages;
        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT> mBrightnessImages;

        vk::raii::Sampler& mSampler;
    };
}
