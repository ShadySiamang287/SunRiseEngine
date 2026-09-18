#include "SceneManagement/BaseScene.h"
#include "SceneManagement/Entity.h"

#include "SceneManagement/Components.h"

using namespace SUN;

Entity BaseScene::CreateEntity(const std::string& name) {
    Entity entity(mRegistry.create(), this);

    entity.AddComponent<TagComponent>(name);
    entity.AddComponent<TransformComponent>();

    return entity;
}

void BaseScene::DestroyEntity(Entity entity) {
    mRegistry.destroy(entity.mHandle);
}