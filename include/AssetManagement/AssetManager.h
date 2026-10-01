#pragma once

#include <filesystem>
#include <unordered_map>
#include <vector>

#include "AssetManagement/Asset.h"
#include "AssetManagement/Material.h"
#include "AssetManagement/ModelAsset.h"
#include "Graphics/Buffers.h"
#include "Graphics/Texture2D.h"
#include "Graphics/vertex.h"

namespace SUN {
    inline constexpr uint32_t MAX_BINDLESS_TEXTURES = 1024;
    inline constexpr uint32_t MAX_MATERIALS = 4096;
    inline constexpr std::size_t INITIAL_GEOMETRY_VERTEX_BUFFER_SIZE =
        64ull * 1024ull * 1024ull;
    inline constexpr std::size_t INITIAL_GEOMETRY_INDEX_BUFFER_SIZE =
        32ull * 1024ull * 1024ull;

    class AssetManager {
    public:
        AssetManager();

        AssetID LoadTexture(const std::filesystem::path& path, bool srgb = true);
        std::shared_ptr<ModelAsset> LoadModel(const std::filesystem::path& path);

        Texture2D* GetTexture(AssetID id);
        const Texture2D* GetTexture(AssetID id) const;

        uint32_t GetTextureIndex(AssetID id) const;
        bool IsTextureLoaded(const std::filesystem::path& path) const;

        MaterialID GetOrCreateMaterial(const MaterialDescription& material);
        vk::DeviceAddress GetMaterialBufferAddress();
        std::size_t GetMaterialCount() const { return mMaterials.size(); }

        GeometryBuffer& GetGeometryBuffer() { return mGeometryBuffer; }
        const GeometryBuffer& GetGeometryBuffer() const { return mGeometryBuffer; }

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
        std::unordered_map<std::filesystem::path, std::shared_ptr<ModelAsset>> mModels;

        GeometryBuffer mGeometryBuffer;
        ShaderBuffer mMaterialBuffer;
        std::vector<GPUMaterial> mMaterials;
    };
}
