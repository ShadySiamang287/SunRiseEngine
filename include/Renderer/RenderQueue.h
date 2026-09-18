#pragma once
#include "Renderer/RenderingStructs.h"

#include <vector>
#include <iostream>

namespace SUN {
    struct RenderCommand {
        const Mesh* mesh;
        glm::mat4 Transform;
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