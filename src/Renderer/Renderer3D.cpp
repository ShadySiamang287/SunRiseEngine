#include "Renderer/Renderer3D.h"

#include "AssetManagement/AssetManager.h"
#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"

using namespace SUN;

Renderer3D::Renderer3D(
    AssetManager& assetManager)
    : mAssetManager(assetManager) {

    SamplerConfig gBufferSamplerConfig {
        .minFilter = vk::Filter::eNearest,
        .magFilter = vk::Filter::eNearest,
        .mipmapMode =
            vk::SamplerMipmapMode::eNearest,
        .addressModeU =
            vk::SamplerAddressMode::eClampToEdge,
        .addressModeV =
            vk::SamplerAddressMode::eClampToEdge,
        .addressModeW =
            vk::SamplerAddressMode::eClampToEdge,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .anisotropy = false,
        .compare = false
    };

    mSampler =
        ResourceFactory::CreateSampler(
            gBufferSamplerConfig
        );

    mDeferredRenderer =
        std::make_unique<DeferredRenderer>(
            mAssetManager
        );

    mSSAOPass =
        std::make_unique<SSAOPass>(
            mDeferredRenderer->GetGBuffers(),
            mSampler
        );

    mLightingPass =
        std::make_unique<LightingPass>(
            mDeferredRenderer->GetGBuffers(),
            mSSAOPass->GetOutputs(),
            mSampler
        );

    mBloomPass =
        std::make_unique<BloomPass>(
            mLightingPass->GetBrightnessImages(),
            mSampler
        );

    mToneMappingPass =
        std::make_unique<ToneMappingPass>(
            mBloomPass->GetOutputs(),
            mLightingPass->GetHDRImages(),
            mSampler
        );

    mFXAAPass =
        std::make_unique<FXAAPass>(
            mToneMappingPass->GetOutputs()
        );
}

void Renderer3D::BeginScene(
    Camera& camera) {

    mRenderQueue.Clear();
    mDirectionalLights.clear();
    mPointLights.clear();

    const vk::Extent2D viewport =
        GraphicsCommands::GetSwapchainExtent();

    camera.AspectRatio =
        static_cast<float>(viewport.width) /
        static_cast<float>(viewport.height);

    mCamera = &camera;
}

void Renderer3D::SubmitMesh(
    const Mesh& Mesh,
    const glm::mat4& Transform,
    AssetID albedoTexture,
    AssetID normalTexture,
    AssetID materialTexture,
    float metalicFactor,
    float roughnessFactor) {

    mRenderQueue.Submit({
        &Mesh,
        Transform,
        mAssetManager.GetTextureIndex(
            albedoTexture
        ),
        mAssetManager.GetTextureIndex(
            normalTexture
        ),
        mAssetManager.GetTextureIndex(
            materialTexture
        ),
        metalicFactor,
        roughnessFactor
    });
}

void Renderer3D::EndScene(
    RenderContext& context) {

    PostProcessContext postProcessContext {
        .frameIndex = context.frameIndex
    };

    mDeferredRenderer->Prepare(
        mRenderQueue
    );

    const PushConstants pushConstants =
        mFrameData.Prepare(
            *mCamera,
            mDeferredRenderer->GetObjects(),
            mDirectionalLights,
            mPointLights
        );

    GraphicsCommands::BeginDraw();

    mDeferredRenderer->Execute(
        context,
        pushConstants
    );

    mSSAOPass->Execute(
        context,
        pushConstants
    );

    mLightingPass->Execute(
        context,
        pushConstants
    );

    mBloomPass->Execute(
        postProcessContext
    );

    mToneMappingPass->Execute(
        postProcessContext
    );

    mFXAAPass->Execute(
        postProcessContext
    );

    GraphicsCommands::EndDraw();

    mCamera = nullptr;
}

void Renderer3D::Resize(
    vk::Extent2D newSize) {

    mDeferredRenderer->Resize(
        newSize
    );

    mSSAOPass->Resize(
        newSize
    );

    mLightingPass->Resize(
        newSize
    );

    mBloomPass->Resize(
        newSize
    );

    mToneMappingPass->Resize(
        newSize
    );

    mFXAAPass->Resize(
        newSize
    );
}

void Renderer3D::SubmitDirectionalLight(
    const GPUDirectionalLight& light) {

    mDirectionalLights.push_back(
        light
    );
}

void Renderer3D::SubmitPointLight(
    const GPUPointLight& light) {

    mPointLights.push_back(
        light
    );
}
