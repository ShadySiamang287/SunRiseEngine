#pragma once

#include <array>

#include "Graphics/GraphicsConfig.h"
#include "Graphics/RenderImage.h"
#include "Renderer/RenderingStructs.h"

namespace SUN {
    class SSAOBlurPass {
    public:
        SSAOBlurPass(std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& ssaoImages, vk::raii::Sampler& sampler);

        void Execute(const RenderContext& context);
        void Resize(vk::Extent2D newSize);

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetOutputs();

    private:
        void CreateImages(vk::Extent2D extent);
        void UpdateDescriptors();

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& mSSAOImages;
        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT> mBlurredAOImages;

        DescriptorResources mDescriptors;
        vk::raii::PipelineLayout mLayout = nullptr;
        vk::raii::Pipeline mPipeline = nullptr;

        vk::raii::Sampler& mSampler;
    };
}
