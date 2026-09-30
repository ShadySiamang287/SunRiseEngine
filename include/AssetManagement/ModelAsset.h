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
        glm::mat4 transform{1.0f};
        std::string name;
    };

    struct ModelAsset {
        std::vector<ModelPrimitive> primitives;
    };
}
