#pragma once

#include "AssetManagement/Asset.h"
#include "AssetManagement/Material.h"
#include "Renderer/DeferredRenderer.h"
#include "Renderer/LightingPass.h"
#include "Renderer/RenderFrameData.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/RenderingStructs.h"
#include "Renderer/SSAOPass.h"
#include "Renderer/SSAOBlurPass.h"

#include "Renderer/PostProcessor/BloomPass.h"
#include "Renderer/PostProcessor/FXAAPass.h"
#include "Renderer/PostProcessor/ToneMappingPass.h"

namespace SUN {
    class AssetManager;

    class Renderer3D {
    public:
        explicit Renderer3D(AssetManager& assetManager);

        void BeginScene(Camera& camera);

        void SubmitMesh(
            const Mesh& Mesh,
            const glm::mat4& Transform,
            MaterialID material = DEFAULT_MATERIAL_ID
        );

        void SubmitMesh(
            const Mesh& Mesh,
            const glm::mat4& Transform,
            const glm::mat4& NormalMatrix,
            MaterialID material = DEFAULT_MATERIAL_ID
        );

        void SubmitDirectionalLight(const GPUDirectionalLight& light);
        void SubmitPointLight(const GPUPointLight& light);

        void EndScene(RenderContext& context);
        void Resize(vk::Extent2D newSize);

    private:
        AssetManager& mAssetManager;
        vk::raii::Sampler mSampler = nullptr;
        RenderFrameData mFrameData;
        std::unique_ptr<DeferredRenderer> mDeferredRenderer;
        std::unique_ptr<SSAOPass> mSSAOPass;
        std::unique_ptr<SSAOBlurPass> mSSAOBlurPass;
        std::unique_ptr<LightingPass> mLightingPass;
        std::unique_ptr<BloomPass> mBloomPass;
        std::unique_ptr<ToneMappingPass> mToneMappingPass;
        std::unique_ptr<FXAAPass> mFXAAPass;
        RenderQueue mRenderQueue;
        const Camera* mCamera = nullptr;
        std::vector<GPUDirectionalLight> mDirectionalLights;
        std::vector<GPUPointLight> mPointLights;
    };
}
