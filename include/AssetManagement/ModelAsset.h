#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "AssetManagement/Asset.h"
#include "Renderer/RenderingStructs.h"

namespace SUN {
    struct ModelPrimitive {
        std::shared_ptr<Mesh> mesh;
        AssetID albedoTexture = INVALID_ASSET_ID;
        AssetID normalTexture = INVALID_ASSET_ID;
        AssetID materialTexture = INVALID_ASSET_ID;
        float metalicFactor = 1.f;
        float roughnessFactor = 1.f;
        glm::mat4 transform{1.0f};
        std::string name;
    };

    struct ModelAsset {
        std::vector<ModelPrimitive> primitives;
    };
}
