#include "Graphics/ShaderCache.h"
#include "Graphics/GraphicsContext.h"

#include <fstream>
#include <stdexcept>

using namespace SUN;

ShaderCache::ShaderCache(GraphicsContext* context) : mGraphicsContextPtr (context) {

}

const vk::raii::ShaderModule& ShaderCache::GetShader(const std::filesystem::path& path){
    if (mShaderModulesMap.contains(path)) {
        return mShaderModulesMap[path];
    }

    auto shaderCode = readFile(path);

    vk::ShaderModuleCreateInfo createInfo = {
        .codeSize = shaderCode.size() * sizeof(char),
        .pCode = reinterpret_cast<const uint32_t*>(shaderCode.data())
    };

    mShaderModulesMap[path] = {mGraphicsContextPtr->mDevice, createInfo};
    return mShaderModulesMap[path];
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