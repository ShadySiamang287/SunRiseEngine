#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "AssetManagement/AssetManager.h"

#include "Graphics/ResourceFactory.h"
#include "Graphics/GraphicsCommands.h"
#include "Logger.h"

#include <memory>

using namespace SUN;

namespace {
    bool MaterialEquals(const GPUMaterial& lhs, const GPUMaterial& rhs) {
        return
            lhs.baseColorFactor.x == rhs.baseColorFactor.x &&
            lhs.baseColorFactor.y == rhs.baseColorFactor.y &&
            lhs.baseColorFactor.z == rhs.baseColorFactor.z &&
            lhs.baseColorFactor.w == rhs.baseColorFactor.w &&
            lhs.albedoTextureIndex == rhs.albedoTextureIndex &&
            lhs.normalTextureIndex == rhs.normalTextureIndex &&
            lhs.materialTextureIndex == rhs.materialTextureIndex &&
            lhs.metallicFactor == rhs.metallicFactor &&
            lhs.roughnessFactor == rhs.roughnessFactor;
    }
}

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
        .maxLod = VK_LOD_CLAMP_NONE
    };
    mTextureSampler = ResourceFactory::CreateSampler(samplerConfig);

    const std::array<uint8_t, 4> whitePixel{255, 255, 255, 255};
    mFallbackTexture = ResourceFactory::CreateTexture2D(whitePixel.data(), 1, 1, true);

    WriteTextureDescriptor(0, mFallbackTexture);
    WriteSamplerDescriptors();

    mGeometryBuffer.Init(
        GEOMETRY_VERTEX_BUFFER_SIZE,
        GEOMETRY_INDEX_BUFFER_SIZE,
        sizeof(Vertex),
        vk::IndexType::eUint32
    );

    mMaterialBuffer.Init(sizeof(GPUMaterial) * MAX_MATERIALS, true);
    mMaterials.reserve(MAX_MATERIALS);
    mMaterials.push_back(GPUMaterial{});

    for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex) {
        mMaterialBuffer.Upload(
            frameIndex,
            &mMaterials.front(),
            sizeof(GPUMaterial)
        );
    }
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

MaterialID AssetManager::GetOrCreateMaterial(const MaterialDescription& material) {
    const GPUMaterial gpuMaterial {
        .baseColorFactor = material.baseColorFactor,
        .albedoTextureIndex = GetTextureIndex(material.albedoTexture),
        .normalTextureIndex = GetTextureIndex(material.normalTexture),
        .materialTextureIndex = GetTextureIndex(material.materialTexture),
        .metallicFactor = material.metallicFactor,
        .roughnessFactor = material.roughnessFactor
    };

    for (MaterialID materialIndex = 0;
         materialIndex < static_cast<MaterialID>(mMaterials.size());
         ++materialIndex) {
        if (MaterialEquals(mMaterials[materialIndex], gpuMaterial)) {
            return materialIndex;
        }
    }

    if (mMaterials.size() >= MAX_MATERIALS) {
        Logger::Log(
            Logger::ERROR,
            "Material table is full ({} materials)",
            MAX_MATERIALS
        );
        return DEFAULT_MATERIAL_ID;
    }

    const MaterialID materialIndex =
        static_cast<MaterialID>(mMaterials.size());

    mMaterials.push_back(gpuMaterial);

    const std::size_t offset =
        static_cast<std::size_t>(materialIndex) * sizeof(GPUMaterial);

    for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex) {
        mMaterialBuffer.Upload(
            frameIndex,
            &mMaterials.back(),
            sizeof(GPUMaterial),
            offset
        );
    }

    return materialIndex;
}

vk::DeviceAddress AssetManager::GetMaterialBufferAddress() {
    return mMaterialBuffer.GetDeviceAddress();
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
