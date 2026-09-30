#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "AssetManagement/AssetManager.h"

#include "Graphics/ResourceFactory.h"
#include "Logger.h"

#include <memory>

using namespace SUN;

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

    Texture2D texture = ResourceFactory::CreateTexture2D(
        pixels.get(),
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        srgb
    );

    const AssetID id = mNextAssetID++;

    mTextures.emplace(id, std::move(texture));
    mTexturePaths.emplace(normalizedPath, id);

    return id;
}

Texture2D* AssetManager::GetTexture(AssetID id) {
    const auto it = mTextures.find(id);
    return it == mTextures.end() ? nullptr : &it->second;
}

const Texture2D* AssetManager::GetTexture(AssetID id) const {
    const auto it = mTextures.find(id);
    return it == mTextures.end() ? nullptr : &it->second;
}

bool AssetManager::IsTextureLoaded(const std::filesystem::path& path) const {
    return mTexturePaths.contains(NormalizePath(path));
}

std::filesystem::path AssetManager::NormalizePath(const std::filesystem::path& path) {
    return path.lexically_normal();
}
