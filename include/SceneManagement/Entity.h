#pragma once

#include <entt/entt.hpp>

namespace SUN{
    class BaseScene;

    class Entity {
    public:
        Entity() = default;
        Entity(entt::entity handle, BaseScene* scene) : mHandle(handle), mScene(scene) {}

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            return mScene->mRegistry.emplace<T>(
                mHandle,
                std::forward<Args>(args)...);
        }

        template<typename T>
        T& GetComponent()
        {
            return mScene->mRegistry.get<T>(mHandle);
        }

        template<typename T>
        bool HasComponent() const
        {
            return mScene->mRegistry.all_of<T>(mHandle);
        }
    private:
        entt::entity mHandle {entt::null};
        BaseScene* mScene = nullptr;
        friend class BaseScene;
    };
}