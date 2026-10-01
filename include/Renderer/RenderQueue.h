#pragma once

#include <vector>

#include "AssetManagement/Material.h"
#include "Renderer/RenderingStructs.h"

namespace SUN {
    struct RenderCommand {
        const Mesh* mesh;
        glm::mat4 Transform;
        MaterialID materialIndex = DEFAULT_MATERIAL_ID;
    };

    class RenderQueue {
    public:
        void Submit(const RenderCommand& command) {
            mCommands.push_back(command);
        }

        void Clear() {
            mCommands.clear();
        }

        const std::vector<RenderCommand>& GetCommands() const {
            return mCommands;
        }

    private:
        std::vector<RenderCommand> mCommands;
    };
}
