#include "Graphics/ShaderCache.h"
#include "Graphics/GraphicsContext.h"

#include <fstream>
#include <stdexcept>

using namespace SUN;

ShaderCache::ShaderCache(GraphicsContext* context) : mGraphicsContextPtr (context) {

}

const vk::raii::ShaderModule& ShaderCache::GetShader(const std::filesystem::path& path){
    if (auto it = mShaderModulesMap.find(path); it != mShaderModulesMap.end()) {
        return it->second;
    }


    auto shaderCode = readFile(path);

    vk::ShaderModuleCreateInfo createInfo = {
        .codeSize = shaderCode.size() * sizeof(char),
        .pCode = reinterpret_cast<const uint32_t*>(shaderCode.data())
    };

    auto [it, inserted] = mShaderModulesMap.try_emplace(
        path, mGraphicsContextPtr->mDevice, createInfo);

    std::string name = "Shader modules for: " + path.string();

    vk::DebugUtilsObjectNameInfoEXT name_info = {
        .objectType = vk::ObjectType::eShaderModule,
        .objectHandle = reinterpret_cast<uint64_t>(static_cast<VkShaderModule>(*it->second)),
        .pObjectName = name.c_str(),
    };
    mGraphicsContextPtr->mDevice.setDebugUtilsObjectNameEXT(name_info);
    return it->second;
}

std::vector<char> ShaderCache::readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file!");
    }

    std::vector<char> buffer(file.tellg());

    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    
    file.close();

    return buffer;
}