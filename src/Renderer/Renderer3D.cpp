#include "Renderer/Renderer3D.h"
#include "Graphics/ResourceFactory.h"
#include "Graphics/GraphicsCommands.h"

#include <iostream>

using namespace SUN;

Renderer3D::Renderer3D() {
    SamplerConfig gBufferSamplerConfig{
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

    mSampler = ResourceFactory::CreateSampler(gBufferSamplerConfig);


    mDeferredRenderer = std::make_unique<DeferredRenderer>(mSampler);
    mBloomPass = std::make_unique<BloomPass>(mDeferredRenderer->GetBrightnessImages(), mSampler);
    mToneMappingPass = std::make_unique<ToneMappingPass>(mBloomPass->GetOutput(0), mDeferredRenderer->GetHDRImages(), mSampler);
    mFXAAPass = std::make_unique<FXAAPass>(mToneMappingPass->GetOutput(0));
}

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
    PostProcessContext postProcessContext {
        .frameIndex = context.frameIndex
    };
    GraphicsCommands::BeginDraw();
    mDeferredRenderer->Render(context, mRenderQueue, mCamera, mDirectionalLights, mPointLights);
   // GraphicsCommands::BeginLabel("Post Processing", {1.f, 0.5f, 0.75f, 1.f});
    mBloomPass->Execute(postProcessContext);
    mToneMappingPass->Execute(postProcessContext);
    mFXAAPass->Execute(postProcessContext);
    //GraphicsCommands::EndLabel();
    GraphicsCommands::EndDraw();
    mCamera = nullptr;
}

void Renderer3D::Resize(vk::Extent2D newSize) {
    mDeferredRenderer->Resize(newSize);
    mBloomPass->Resize(newSize);
    mToneMappingPass->Resize(newSize);
    mFXAAPass->Resize(newSize);
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