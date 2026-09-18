#include "SceneManagement/Systems/RenderSystem.h"
#include "SceneManagement/Components.h"

#include "Renderer/Renderer3D.h"


using namespace SUN;

void RenderSystem::Render(BaseScene& scene, Renderer3D* renderer) {
    auto view = scene.Registry().view<TransformComponent, MeshComponent>();

    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& mesh = view.get<MeshComponent>(entity);

        renderer->SubmitMesh(mesh.mesh, transform.GetTransform());
    }
}