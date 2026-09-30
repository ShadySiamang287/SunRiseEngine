#pragma once

#include <filesystem>
#include <unordered_map>

#include "AssetManagement/Asset.h"
#include "Graphics/Texture2D.h"
#include "Graphics/vertex.h"

namespace SUN {
    inline constexpr uint32_t MAX_BINDLESS_TEXTURES = 16;

    class AssetManager {
    public:
        AssetManager();

        AssetID LoadTexture(const std::filesystem::path& path, bool srgb = true);

        Texture2D* GetTexture(AssetID id);
        const Texture2D* GetTexture(AssetID id) const;

        uint32_t GetTextureIndex(AssetID id) const;
        bool IsTextureLoaded(const std::filesystem::path& path) const;

        DescriptorResources& GetTextureDescriptors() { return mTextureDescriptors; }
        const DescriptorResources& GetTextureDescriptors() const { return mTextureDescriptors; }

    private:
        struct TextureAsset {
            Texture2D texture;
            uint32_t bindlessIndex = 0;
        };

        static std::filesystem::path NormalizePath(const std::filesystem::path& path);
        void WriteTextureDescriptor(uint32_t index, const Texture2D& texture);
        void WriteSamplerDescriptors();

        AssetID mNextAssetID = 1;
        uint32_t mNextTextureIndex = 1;

        DescriptorResources mTextureDescriptors;
        vk::raii::Sampler mTextureSampler = nullptr;
        Texture2D mFallbackTexture;

        std::unordered_map<AssetID, TextureAsset> mTextures;
        std::unordered_map<std::filesystem::path, AssetID> mTexturePaths;
    };
}
