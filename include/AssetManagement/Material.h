#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "AssetManagement/Asset.h"

namespace SUN {
    using MaterialID = uint32_t;

    inline constexpr MaterialID DEFAULT_MATERIAL_ID = 0;
    inline constexpr MaterialID INVALID_MATERIAL_ID = UINT32_MAX;

    struct MaterialDescription {
        AssetID albedoTexture = INVALID_ASSET_ID;
        AssetID normalTexture = INVALID_ASSET_ID;
        AssetID materialTexture = INVALID_ASSET_ID;

        glm::vec4 baseColorFactor{1.0f};

        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
    };

    struct alignas(16) GPUMaterial {
        glm::vec4 baseColorFactor{1.0f};

        uint32_t albedoTextureIndex = 0;
        uint32_t normalTextureIndex = 0;
        uint32_t materialTextureIndex = 0;
        float metallicFactor = 0.0f;

        float roughnessFactor = 1.0f;
        uint32_t padding0 = 0;
        uint32_t padding1 = 0;
        uint32_t padding2 = 0;
    };

    static_assert(sizeof(GPUMaterial) == 48);
}
