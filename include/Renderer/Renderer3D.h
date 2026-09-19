#pragma once

#include "Renderer/RenderingStructs.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/DeferredRenderer.h"

namespace SUN {

    class Renderer3D{
    public:
        void BeginScene(const Camera& camera);

        void SubmitMesh(
            const Mesh& Mesh,
            const glm::mat4& Transform
        );

        void SubmitDirectionalLight(
            const GPUDirectionalLight& light
        );

        void SubmitPointLight(
            const GPUPointLight& light
        );

        void EndScene(RenderContext& context);
    private:
        RenderQueue mRenderQueue;
        const Camera* mCamera = nullptr;
        DeferredRenderer mDeferredRenderer;

        std::vector<GPUDirectionalLight> mDirectionalLights;
        std::vector<GPUPointLight> mPointLights;
    };
}