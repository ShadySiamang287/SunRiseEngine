#pragma once

#include <entt/entt.hpp>
#include <string>

#include "Renderer/RenderingStructs.h"

namespace SUN{
    class Entity;

    class BaseScene{
    public:
        virtual ~BaseScene() = default;

        virtual void OnEnter() {};
        virtual void OnExit() {};

        virtual void HandleInput()  {};
        virtual void Update(const float& dt) {};
        virtual void Render(RenderContext& context) {};

        Entity CreateEntity(const std::string& name = "");
        void DestroyEntity(Entity entity);
        entt::registry& Registry() {return mRegistry;}

    protected:
        entt::registry mRegistry;
        friend class Entity;
    };
}