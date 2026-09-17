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

    class GraphicsContext;
    class ShaderCache;
    
    class ResourceFactory {
    public:
        ResourceFactory(GraphicsContext* context);
        ~ResourceFactory();

        static DescriptorResources CreateDescriptorResources(std::span<vk::DescriptorSetLayoutBinding> bindings);
        static vk::raii::PipelineLayout CreatePipelineLayout(DescriptorResources* resources = nullptr);
        static vk::raii::Pipeline CreatePipeline(const PipelineConfig& config, vk::raii::PipelineLayout& layout, std::string debugName);

    private:
        GraphicsContext* mGraphicsContextPtr;
        std::unique_ptr<ShaderCache> mShaderCachePtr;

        static ResourceFactory*  mInstancePtr;
    };
}