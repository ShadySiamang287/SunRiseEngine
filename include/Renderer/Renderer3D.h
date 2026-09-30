#pragma once

#include "Renderer/RenderingStructs.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/DeferredRenderer.h"

#include "Renderer/PostProcessor/BloomPass.h"
#include "Renderer/PostProcessor/ToneMappingPass.h"
#include "Renderer/PostProcessor/FXAAPass.h"
#include "AssetManagement/Asset.h"

namespace SUN {

    class AssetManager;

    class Renderer3D{
    public:
        explicit Renderer3D(AssetManager& assetManager);

        void BeginScene(Camera& camera);

        void SubmitMesh(
            const Mesh& Mesh,
            const glm::mat4& Transform,
            AssetID albedoTexture = INVALID_ASSET_ID
        );

        void SubmitDirectionalLight(
            const GPUDirectionalLight& light
        );

        void SubmitPointLight(
            const GPUPointLight& light
        );

        void EndScene(RenderContext& context);
        void Resize(vk::Extent2D newSize);
    private:
        AssetManager& mAssetManager;

        std::unique_ptr<BloomPass> mBloomPass;
        std::unique_ptr<ToneMappingPass> mToneMappingPass;
        std::unique_ptr<FXAAPass> mFXAAPass;

        RenderQueue mRenderQueue;
        const Camera* mCamera = nullptr;
        std::unique_ptr<DeferredRenderer> mDeferredRenderer;

        std::vector<GPUDirectionalLight> mDirectionalLights;
        std::vector<GPUPointLight> mPointLights;
        vk::raii::Sampler mSampler = nullptr;
    };
}