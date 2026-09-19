#include "SceneManagement/Systems/RenderSystem.h"
#include "SceneManagement/Components.h"

#include "Renderer/Renderer3D.h"
#include "Renderer/RenderingStructs.h"

using namespace SUN;

void RenderSystem::Render(BaseScene& scene, Renderer3D* renderer) {
    auto meshView = scene.Registry().view<TransformComponent, MeshComponent>();

    for (auto entity : meshView) {
        auto& transform = meshView.get<TransformComponent>(entity);
        auto& mesh = meshView.get<MeshComponent>(entity);

        renderer->SubmitMesh(*mesh.mesh, transform.GetTransform());
    }

    auto directionalLightView = scene.Registry().view<TransformComponent, DirectionalLightComponent>();
    for (auto entity : directionalLightView) {
        auto& transform = directionalLightView.get<TransformComponent>(entity);
        auto& light = directionalLightView.get<DirectionalLightComponent>(entity);

        glm::vec3 direction = transform.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);

        renderer->SubmitDirectionalLight({
            {direction, light.intensity},
            {light.Colour, 1.f}
        });
    }

    auto pointLightView = scene.Registry().view<TransformComponent, PointLightComponent>();
    for (auto entity : pointLightView) {
        auto& transform = pointLightView.get<TransformComponent>(entity);
        auto& light = pointLightView.get<PointLightComponent>(entity);

        renderer->SubmitPointLight({
            {transform.Position, light.range},
            {light.Colour, light.intensity}
        });
    }

}