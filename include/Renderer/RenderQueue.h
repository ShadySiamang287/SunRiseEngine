#pragma once

#include <vector>

#include "Renderer/RenderingStructs.h"

namespace SUN {
    class RenderQueue {
    public:
        RenderQueue() {
            mObjects.reserve(MAX_OBJECTS);
        }

        void Submit(const ObjectData& object) {
            mObjects.push_back(object);
        }

        void Clear() {
            mObjects.clear();
        }

        const std::vector<ObjectData>& GetObjects() const {
            return mObjects;
        }

    private:
        std::vector<ObjectData> mObjects;
    };
}
