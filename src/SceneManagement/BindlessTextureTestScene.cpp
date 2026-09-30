#include "SceneManagement/BindlessTextureTestScene.h"
#include "AssetManagement/AssetManager.h"
#include "Graphics/vertex.h"
#include "Renderer/Renderer3D.h"
#include "SceneManagement/Components.h"
#include "SceneManagement/Entity.h"
#include "SceneManagement/Systems/RenderSystem.h"

#include <array>
#include <memory>

using namespace SUN;

BindlessTextureTestScene::BindlessTextureTestScene(AssetManager& assetManager)
    : mAssetManager(assetManager) {}

void BindlessTextureTestScene::OnEnter() {
    mCheckerTexture = mAssetManager.LoadTexture("assets/textures/bindless_test_checker.png", true);

    const std::array<Vertex, 4> vertices{
        Vertex{{-1.f,-1.f,0.f},{0.f,0.f,1.f},{1.f,1.f,1.f},{0.f,1.f}},
        Vertex{{ 1.f,-1.f,0.f},{0.f,0.f,1.f},{1.f,1.f,1.f},{1.f,1.f}},
        Vertex{{ 1.f, 1.f,0.f},{0.f,0.f,1.f},{1.f,1.f,1.f},{1.f,0.f}},
        Vertex{{-1.f, 1.f,0.f},{0.f,0.f,1.f},{1.f,1.f,1.f},{0.f,0.f}}
    };

    const std::array<uint32_t, 12> indices{
        0,1,2, 2,3,0,
        2,1,0, 0,3,2
    };

    auto mesh = std::make_shared<Mesh>();
    mesh->buffer.Init(vertices.data(), sizeof(vertices), sizeof(Vertex),
                      indices.data(), sizeof(indices), vk::IndexType::eUint32);

    Entity quad = CreateEntity("Bindless Texture Quad");
    quad.AddComponent<MeshComponent>(MeshComponent{.mesh = mesh});
    quad.AddComponent<MaterialComponent>(MaterialComponent{.AlbedoTexture = mCheckerTexture});

    Entity pointLight = CreateEntity("Test Point Light");
    pointLight.GetComponent<TransformComponent>().Position = {0.f, 0.f, 2.f};
    pointLight.AddComponent<PointLightComponent>(PointLightComponent{
        .Colour = {1.f,1.f,1.f},
        .intensity = 8.f,
        .range = 10.f
    });

    mCamera.Position = {0.f,0.f,2.5f};
    mCamera.Rotation = glm::quat{1.f,0.f,0.f,0.f};
}

void BindlessTextureTestScene::Render(RenderContext& context) {
    context.renderer->BeginScene(mCamera);
    RenderSystem::Render(*this, context.renderer);
    context.renderer->EndScene(context);
}
