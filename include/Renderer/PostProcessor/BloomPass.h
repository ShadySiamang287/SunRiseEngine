#pragma once

#include <array>

#include "Graphics/GraphicsConfig.h"
#include "Graphics/RenderImage.h"
#include "Renderer/PostProcessor/PostProcessorPass.h"

namespace SUN {
    class BloomPass : public PostProcessPass {
    public:
        BloomPass(std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& brightnessImages, vk::raii::Sampler& sampler);

        void Execute(const PostProcessContext& context) override;

        void Resize(vk::Extent2D newSize) override;

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetOutputs();

    private:
        void CreateImages(vk::Extent2D size);
        void UpdateDescriptors();

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>&
            mBrightnessImages;

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT> mBlurHorizontal;

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT> mBlurVertical;

        DescriptorResources mHorizontalDescriptors;
        DescriptorResources mVerticalDescriptors;

        vk::raii::PipelineLayout mLayout = nullptr;
        vk::raii::Pipeline mPipeline = nullptr;
        vk::raii::Sampler& mSampler;
    };
}
