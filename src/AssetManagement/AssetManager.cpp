#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "AssetManagement/AssetManager.h"

#include "Graphics/ResourceFactory.h"
#include "Graphics/GraphicsCommands.h"
#include "Logger.h"

#include <memory>

using namespace SUN;

AssetManager::AssetManager() {
    mTextureDescriptors = ResourceFactory::CreateBindlessTextureResources(MAX_BINDLESS_TEXTURES);

    SamplerConfig samplerConfig{
        .minFilter = vk::Filter::eLinear,
        .magFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .minLod = 0.0f,
        .maxLod = 0.0f
    };
    mTextureSampler = ResourceFactory::CreateSampler(samplerConfig);

    const std::array<uint8_t, 4> whitePixel{255, 255, 255, 255};
    mFallbackTexture = ResourceFactory::CreateTexture2D(whitePixel.data(), 1, 1, true);

    WriteTextureDescriptor(0, mFallbackTexture);
    WriteSamplerDescriptors();
}

AssetID AssetManager::LoadTexture(const std::filesystem::path& path, bool srgb) {
    const std::filesystem::path normalizedPath = NormalizePath(path);

    if (const auto it = mTexturePaths.find(normalizedPath); it != mTexturePaths.end()) {
        return it->second;
    }

    if (!std::filesystem::exists(normalizedPath)) {
        Logger::Log(Logger::ERROR, "Texture not found: {}", normalizedPath.string());
        return INVALID_ASSET_ID;
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    using StbiPixels = std::unique_ptr<stbi_uc, void(*)(void*)>;
    StbiPixels pixels(
        stbi_load(
            normalizedPath.string().c_str(),
            &width,
            &height,
            &channels,
            STBI_rgb_alpha
        ),
        stbi_image_free
    );

    if (!pixels) {
        const char* reason = stbi_failure_reason();
        Logger::Log(
            Logger::ERROR,
            "Failed to load texture '{}': {}",
            normalizedPath.string(),
            reason ? reason : "unknown error"
        );
        return INVALID_ASSET_ID;
    }

    if (mNextTextureIndex >= MAX_BINDLESS_TEXTURES) {
        Logger::Log(Logger::ERROR, "Bindless texture table is full");
        return INVALID_ASSET_ID;
    }

    Texture2D texture = ResourceFactory::CreateTexture2D(
        pixels.get(),
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        srgb
    );

    const AssetID id = mNextAssetID++;
    const uint32_t bindlessIndex = mNextTextureIndex++;

    auto [it, inserted] = mTextures.emplace(
        id,
        TextureAsset{std::move(texture), bindlessIndex}
    );

    if (!inserted) return INVALID_ASSET_ID;

    WriteTextureDescriptor(bindlessIndex, it->second.texture);
    mTexturePaths.emplace(normalizedPath, id);

    return id;
}

Texture2D* AssetManager::GetTexture(AssetID id) {
    const auto it = mTextures.find(id);
    return it == mTextures.end() ? nullptr : &it->second.texture;
}

const Texture2D* AssetManager::GetTexture(AssetID id) const {
    const auto it = mTextures.find(id);
    return it == mTextures.end() ? nullptr : &it->second.texture;
}

uint32_t AssetManager::GetTextureIndex(AssetID id) const {
    const auto it = mTextures.find(id);
    return it == mTextures.end() ? 0u : it->second.bindlessIndex;
}

bool AssetManager::IsTextureLoaded(const std::filesystem::path& path) const {
    return mTexturePaths.contains(NormalizePath(path));
}

std::filesystem::path AssetManager::NormalizePath(const std::filesystem::path& path) {
    return path.lexically_normal();
}


void AssetManager::WriteTextureDescriptor(uint32_t index, const Texture2D& texture) {
    vk::DescriptorImageInfo imageInfo{
        .imageView = texture.GetView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    std::array<vk::WriteDescriptorSet, MAX_FRAMES_IN_FLIGHT> writes{};
    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
        writes[frame] = vk::WriteDescriptorSet{
            .dstSet = *mTextureDescriptors.sets[frame],
            .dstBinding = 0,
            .dstArrayElement = index,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &imageInfo
        };
    }
    GraphicsCommands::WriteDescriptors(writes);
}

void AssetManager::WriteSamplerDescriptors() {
    vk::DescriptorImageInfo samplerInfo{.sampler = *mTextureSampler};

    std::array<vk::WriteDescriptorSet, MAX_FRAMES_IN_FLIGHT> writes{};
    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
        writes[frame] = vk::WriteDescriptorSet{
            .dstSet = *mTextureDescriptors.sets[frame],
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplerInfo
        };
    }
    GraphicsCommands::WriteDescriptors(writes);
}
