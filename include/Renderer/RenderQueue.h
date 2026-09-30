#pragma once
#include "Renderer/RenderingStructs.h"

#include <vector>
#include <iostream>

namespace SUN {
    struct RenderCommand {
        const Mesh* mesh;
        glm::mat4 Transform;
        uint32_t albedoTextureIndex = 0;
        uint32_t normalTextureIndex = 0;
        uint32_t materialTexturIndex = 0;
        float metalicFactor = 1.f;
        float roughnessFactor = 1.f;
    };

    class RenderQueue {
    public:
        void Submit(const RenderCommand& command){
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