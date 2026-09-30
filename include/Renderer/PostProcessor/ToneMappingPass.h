#pragma once

#include <array>

#include "Graphics/GraphicsConfig.h"
#include "Graphics/RenderImage.h"
#include "Renderer/PostProcessor/PostProcessorPass.h"

namespace SUN {
    class ToneMappingPass : public PostProcessPass {
    public:
        ToneMappingPass(
            std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& bloomImages,
            std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& hdrImages,
            vk::raii::Sampler& sampler
        );

        void Execute(
            const PostProcessContext& context
        ) override;

        void Resize(
            vk::Extent2D newSize
        ) override;

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetOutputs();

    private:
        void CreateImages(vk::Extent2D size);
        void UpdateDescriptors();

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>&
            mBloomImages;

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>&
            mHDRImages;

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>
            mToneMappedImages;

        DescriptorResources mDescriptors;

        vk::raii::PipelineLayout mLayout = nullptr;
        vk::raii::Pipeline mPipeline = nullptr;
        vk::raii::Sampler& mSampler;
    };
}
