#pragma once

#include "Graphics/GraphicsConfig.h"
#include "Renderer/GBuffer.h"
#include "Renderer/RenderingStructs.h"

namespace SUN {
    class LightingPass {
    public:
        LightingPass(
            std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& gBuffers,
            std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& aoImages,
            vk::raii::Sampler& sampler
        );

        void Execute(const RenderContext& context, const PushConstants& pushConstants);

        void Resize(vk::Extent2D newSize);

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetHDRImages();
        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetBrightnessImages();

    private:
        void CreateImages(vk::Extent2D extent);
        void UpdateDescriptors();

        std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& mGBuffers;
        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& mAOImages;

        DescriptorResources mDescriptors;
        vk::raii::PipelineLayout mLayout = nullptr;
        vk::raii::Pipeline mPipeline = nullptr;

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT> mHDRImages;
        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT> mBrightnessImages;

        vk::raii::Sampler& mSampler;
    };
}
