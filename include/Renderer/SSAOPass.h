#pragma once

#include <array>

#include "Graphics/GraphicsConfig.h"
#include "Graphics/RenderImage.h"
#include "Renderer/GBuffer.h"
#include "Renderer/RenderingStructs.h"

namespace SUN {
    class SSAOPass {
    public:
        SSAOPass(std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& gBuffers, vk::raii::Sampler& sampler);

        void Execute(const RenderContext& context, const PushConstants& pushConstants);

        void Resize(vk::Extent2D newSize);

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetOutputs();

    private:
        void CreateImages(vk::Extent2D sourceExtent);
        void UpdateDescriptors();

        std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& mGBuffers;
        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT> mAOImages;

        DescriptorResources mDescriptors;

        vk::raii::PipelineLayout mLayout = nullptr;
        vk::raii::Pipeline mPipeline = nullptr;

        vk::raii::Sampler& mSampler;
    };
}
