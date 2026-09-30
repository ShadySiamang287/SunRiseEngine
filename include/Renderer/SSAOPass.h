#pragma once

#include <array>

#include "Graphics/Buffers.h"
#include "Graphics/GraphicsConfig.h"
#include "Graphics/RenderImage.h"
#include "Graphics/Texture2D.h"
#include "Renderer/GBuffer.h"
#include "Renderer/RenderingStructs.h"

namespace SUN {
    inline constexpr uint32_t SSAO_KERNEL_SIZE = 64;

    // Mirrors the LearnOpenGL samples[64], radius, bias and kernelSize uniforms.
    // Use float4 in the shader for the samples/settings to keep layout simple.
    struct SSAOData {
        std::array<glm::vec4, SSAO_KERNEL_SIZE> samples;
        glm::vec4 settings{0.5f, 0.025f, static_cast<float>(SSAO_KERNEL_SIZE), 1.0f};
        // settings = radius, bias, kernelSize, power
    };

    class SSAOPass {
    public:
        SSAOPass(std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& gBuffers, vk::raii::Sampler& gBufferSampler);

        void Execute(const RenderContext& context, const PushConstants& pushConstants);
        void Resize(vk::Extent2D newSize);

        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetOutputs();

    private:
        void GenerateKernel();
        void CreateNoiseTexture();
        void CreateImages(vk::Extent2D sourceExtent);
        void UpdateDescriptors();

        std::array<GBuffer, MAX_FRAMES_IN_FLIGHT>& mGBuffers;
        std::array<RenderImage, MAX_FRAMES_IN_FLIGHT> mAOImages;

        SSAOData mSSAOData;
        ShaderBuffer mSSAODataBuffer;

        Texture2D mNoiseTexture;
        vk::raii::Sampler mNoiseSampler = nullptr;
        vk::raii::Sampler& mGBufferSampler;

        DescriptorResources mDescriptors;
        vk::raii::PipelineLayout mLayout = nullptr;
        vk::raii::Pipeline mPipeline = nullptr;
    };
}
