#pragma once

#include "Renderer/RenderingStructs.h"

namespace SUN {
    class Layer {
    public:
        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}

        virtual void OnUpdate(const float& dt) {}
        virtual void OnRender(RenderContext& context) {} 
    };
}
