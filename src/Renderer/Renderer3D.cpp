#include "Renderer/Renderer3D.h"

#include <iostream>

using namespace SUN;

void Renderer3D::BeginScene(const Camera& camera) {
    mRenderQueue.Clear();
    mCamera = &camera;
}

void Renderer3D::SubmitMesh(const Mesh& Mesh, const glm::mat4& Transform) {
    mRenderQueue.Submit({
        &Mesh,
        Transform
    });
}

void Renderer3D::EndScene(RenderContext& context) {
    mDeferredRenderer.Render(context, mRenderQueue, mCamera);
    mCamera = nullptr;
}