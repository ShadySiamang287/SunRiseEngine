#pragma once

#include <map>
#include <filesystem>
#include <memory>

#include <vulkan/vulkan_raii.hpp>

namespace SUN {
    class GraphicsContext;

    class ShaderCache{
    public:
        ShaderCache(GraphicsContext* context);

        const vk::raii::ShaderModule& GetShader(const std::filesystem::path& path);
    private:
        static std::vector<char> readFile(const std::filesystem::path& path);

        std::map<std::filesystem::path, vk::raii::ShaderModule> mShaderModulesMap;
        GraphicsContext* mGraphicsContextPtr;
    };
}