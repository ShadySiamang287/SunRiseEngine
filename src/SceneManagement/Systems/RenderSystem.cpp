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

        const auto* material =
            scene.Registry().try_get<MaterialComponent>(entity);

        renderer->SubmitMesh(
            *mesh.mesh,
            mesh.GetWorldTransform(transform),
            mesh.GetWorldNormalMatrix(transform),
            material ? material->material : DEFAULT_MATERIAL_ID
        );
    }

    auto directionalLightView = scene.Registry().view<TransformComponent, DirectionalLightComponent>();
    for (auto entity : directionalLightView) {
        auto& transform = directionalLightView.get<TransformComponent>(entity);
        auto& light = directionalLightView.get<DirectionalLightComponent>(entity);

        glm::vec3 direction = transform.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);

        renderer->SubmitDirectionalLight({
            .directionIntensity = {
                direction,
                light.intensity
            },
            .color = {
                light.Colour,
                1.f
            },
            .castsShadows =
                light.CastShadows ? 1u : 0u
        });
    }

    auto pointLightView = scene.Registry().view<TransformComponent, PointLightComponent>();
    for (auto entity : pointLightView) {
        auto& transform = pointLightView.get<TransformComponent>(entity);
        auto& light = pointLightView.get<PointLightComponent>(entity);

        renderer->SubmitPointLight({
            .positionRange = {
                transform.Position,
                light.range
            },
            .colorIntensity = {
                light.Colour,
                light.intensity
            },
            .castsShadows =
                light.CastShadows ? 1u : 0u
        });
    }

}