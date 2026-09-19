#include "Renderer/Renderer3D.h"

#include <iostream>

using namespace SUN;

void Renderer3D::BeginScene(const Camera& camera) {
    mRenderQueue.Clear();
    mDirectionalLights.clear();
    mPointLights.clear();

    mCamera = &camera;
}

void Renderer3D::SubmitMesh(const Mesh& Mesh, const glm::mat4& Transform) {
    mRenderQueue.Submit({
        &Mesh,
        Transform
    });
}

void Renderer3D::EndScene(RenderContext& context) {
    mDeferredRenderer.Render(context, mRenderQueue, mCamera, mDirectionalLights, mPointLights);
    mCamera = nullptr;
}

void Renderer3D::SubmitDirectionalLight(
    const GPUDirectionalLight& light
) {
    mDirectionalLights.push_back(light);
}

void Renderer3D::SubmitPointLight(
    const GPUPointLight& light
) {
    mPointLights.push_back(light);
}