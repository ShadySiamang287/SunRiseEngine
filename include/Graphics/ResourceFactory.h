#pragma once
#include <memory>
#include <filesystem>

#include <vulkan/vulkan_raii.hpp>

#include <span>

#include "Graphics/vertex.h"

namespace SUN {
    struct PipelineConfig {
        std::filesystem::path vertexFile;
        std::string vertexName;

        std::filesystem::path fragFile;
        std::string fragName;

        vk::PrimitiveTopology primitiveTopology;

        std::vector<vk::Format> colorAttachmentFormats;
        std::vector<uint32_t> colorAttachmentLocations;

        vk::Format depthAttachmentFormat = vk::Format::eUndefined;
        vk::Format stencilAttachmentFormat = vk::Format::eUndefined;

        bool useVertexInput = true;
    };

    struct SamplerConfig {
        vk::Filter minFilter = vk::Filter::eLinear;
        vk::Filter magFilter = vk::Filter::eLinear;

        vk::SamplerMipmapMode mipmapMode =
            vk::SamplerMipmapMode::eLinear;

        vk::SamplerAddressMode addressModeU =
            vk::SamplerAddressMode::eRepeat;

        vk::SamplerAddressMode addressModeV =
            vk::SamplerAddressMode::eRepeat;

        vk::SamplerAddressMode addressModeW =
            vk::SamplerAddressMode::eRepeat;

        float minLod = 0.0f;
        float maxLod = 0.0f;
        float mipLodBias = 0.0f;

        bool anisotropy = false;
        float maxAnisotropy = 1.0f;

        bool compare = false;
        vk::CompareOp compareOp = vk::CompareOp::eAlways;

        vk::BorderColor borderColor =
            vk::BorderColor::eFloatOpaqueBlack;
    };

    class GraphicsContext;
    class ShaderCache;
    
    class ResourceFactory {
    public:
        ResourceFactory(GraphicsContext* context);
        ~ResourceFactory();

        static DescriptorResources CreateDescriptorResources(std::span<vk::DescriptorSetLayoutBinding> bindings);
        static vk::raii::PipelineLayout CreatePipelineLayout(vk::ShaderStageFlags flags, uint32_t pushConstantsSize, DescriptorResources* resources = nullptr);
        static vk::raii::Pipeline CreatePipeline(const PipelineConfig& config, vk::raii::PipelineLayout& layout, std::string debugName);

        static vk::raii::Sampler CreateSampler(const SamplerConfig& config);

    private:
        GraphicsContext* mGraphicsContextPtr;
        std::unique_ptr<ShaderCache> mShaderCachePtr;

        static ResourceFactory*  mInstancePtr;
    };
}