#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_map>

#include "Graphics/Texture2D.h"

namespace SUN {
    using AssetID = uint64_t;

    inline constexpr AssetID INVALID_ASSET_ID = 0;

    class AssetManager {
    public:
        AssetID LoadTexture(const std::filesystem::path& path, bool srgb = true);

        Texture2D* GetTexture(AssetID id);
        const Texture2D* GetTexture(AssetID id) const;

        bool IsTextureLoaded(const std::filesystem::path& path) const;

    private:
        static std::filesystem::path NormalizePath(const std::filesystem::path& path);

        AssetID mNextAssetID = 1;

        std::unordered_map<AssetID, Texture2D> mTextures;
        std::unordered_map<std::filesystem::path, AssetID> mTexturePaths;
    };
}
