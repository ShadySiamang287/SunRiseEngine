#include "Renderer/Renderer3D.h"

#include "AssetManagement/AssetManager.h"
#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"

using namespace SUN;

Renderer3D::Renderer3D(AssetManager& assetManager)
    : mAssetManager(assetManager) {

    SamplerConfig gBufferSamplerConfig {
        .minFilter = vk::Filter::eNearest,
        .magFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .anisotropy = false,
        .compare = false
    };

    mSampler =
        ResourceFactory::CreateSampler(gBufferSamplerConfig);

    mDeferredRenderer =
        std::make_unique<DeferredRenderer>(mAssetManager);

    mShadowPass =
        std::make_unique<ShadowPass>(mAssetManager);

    mSSAOPass = std::make_unique<SSAOPass>(mDeferredRenderer->GetGBuffers(), mSampler);
    mSSAOBlurPass = std::make_unique<SSAOBlurPass>(mSSAOPass->GetOutputs(), mSampler);

    mLightingPass =
        std::make_unique<LightingPass>(
            mDeferredRenderer->GetGBuffers(),
            mSSAOBlurPass->GetOutputs(),
            mSampler,
            *mShadowPass
        );

    mBloomPass =
        std::make_unique<BloomPass>(mLightingPass->GetBrightnessImages(), mSampler);

    mToneMappingPass =
        std::make_unique<ToneMappingPass>(mBloomPass->GetOutputs(), mLightingPass->GetHDRImages(), mSampler);

    mFXAAPass =
        std::make_unique<FXAAPass>(mToneMappingPass->GetOutputs());
}

void Renderer3D::BeginScene(Camera& camera) {

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
    MaterialID material) {

    const glm::mat4 normalMatrix =
        glm::mat4(
            glm::transpose(
                glm::inverse(
                    glm::mat3(Transform)
                )
            )
        );

    SubmitMesh(
        Mesh,
        Transform,
        normalMatrix,
        material
    );
}

void Renderer3D::SubmitMesh(
    const Mesh& Mesh,
    const glm::mat4& Transform,
    const glm::mat4& NormalMatrix,
    MaterialID material) {

    mRenderQueue.Submit({
        .model = Transform,
        .normal = NormalMatrix,
        .boundsCenter = Mesh.bounds.center,
        .boundsExtents = Mesh.bounds.extents,
        .materialIndex = material,
        .firstIndex = Mesh.firstIndex,
        .indexCount = Mesh.indexCount,
        .vertexOffset = Mesh.vertexOffset
    });
}

void Renderer3D::EndScene(RenderContext& context) {

    PostProcessContext postProcessContext {
        .frameIndex = context.frameIndex
    };

    mDeferredRenderer->Prepare(mRenderQueue);

    mShadowPass->Prepare(
        mRenderQueue.GetObjects(),
        *mCamera,
        mDirectionalLights,
        mPointLights
    );

    const PushConstants pushConstants =
        mFrameData.Prepare(
            *mCamera,
            mDeferredRenderer->GetObjects(),
            mAssetManager.GetMaterialBufferAddress(),
            mDirectionalLights,
            mPointLights
        );

    GraphicsCommands::BeginDraw();

    mShadowPass->Execute(pushConstants);

    mDeferredRenderer->Execute(context, pushConstants);

    GraphicsCommands::BeginLabel("SSAO", {0.f, 0.5f, 0.75f, 1.f});
    mSSAOPass->Execute(context, pushConstants);
    mSSAOBlurPass->Execute(context);
    GraphicsCommands::EndLabel();


    mLightingPass->Execute(context, pushConstants);

    GraphicsCommands::BeginLabel("Post Processing", {1.f, 0.5f, 0.75f, 1.f});

    mBloomPass->Execute(postProcessContext);

    mToneMappingPass->Execute(postProcessContext);

    mFXAAPass->Execute(postProcessContext);

    GraphicsCommands::EndLabel();
    GraphicsCommands::EndDraw();

    mCamera = nullptr;
}

void Renderer3D::Resize(vk::Extent2D newSize) {

    mDeferredRenderer->Resize(newSize);

    mSSAOPass->Resize(newSize);
    mSSAOBlurPass->Resize(newSize);

    mLightingPass->Resize(newSize);

    mBloomPass->Resize(newSize);

    mToneMappingPass->Resize(newSize);

    mFXAAPass->Resize(newSize);
}

void Renderer3D::SubmitDirectionalLight(const GPUDirectionalLight& light) {

    mDirectionalLights.push_back(light);
}

void Renderer3D::SubmitPointLight(const GPUPointLight& light) {

    mPointLights.push_back(light);
}
