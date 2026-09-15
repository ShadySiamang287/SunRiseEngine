#pragma once
#include <memory>
#include <filesystem>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>



namespace SUN {
    struct PipelineConfig {
        std::filesystem::path vertexFile;
        std::string vertexName;

        std::filesystem::path fragFile;
        std::string fragName;

        vk::PrimitiveTopology primitiveTopology;
    };

    class GraphicsContext;
    class ShaderCache;
    
    class ResourceFactory {
    public:
        ResourceFactory(GraphicsContext* context);
        ~ResourceFactory();

        static vk::raii::PipelineLayout CreatePipelineLayout();
        static vk::raii::Pipeline CreatePipeline(const PipelineConfig& config, vk::raii::PipelineLayout& layout);

    private:
        GraphicsContext* mGraphicsContextPtr;
        std::unique_ptr<ShaderCache> mShaderCachePtr;

        static ResourceFactory*  mInstancePtr;
    };
}