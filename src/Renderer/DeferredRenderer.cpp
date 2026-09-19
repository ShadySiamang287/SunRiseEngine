#include "Renderer/DeferredRenderer.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/ResourceFactory.h"
#include "Graphics/vertex.h"

#include "Logger.h"

#include <algorithm>
#include <iostream>
#include <numeric>

using namespace SUN;

DeferredRenderer::DeferredRenderer() {
    std::array<vk::DescriptorSetLayoutBinding, 4> lightingBindings;
    lightingBindings[0] = {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    lightingBindings[1] = {
        .binding = 1,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    lightingBindings[2] = {
        .binding = 2,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    lightingBindings[3] = {
        .binding = 3,
        .descriptorType = vk::DescriptorType::eSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

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

    mImageSampler = ResourceFactory::CreateSampler(gBufferSamplerConfig);

    mLightingDescriptors = ResourceFactory::CreateDescriptorResources(lightingBindings);
    mLightingLayout = ResourceFactory::CreatePipelineLayout(&mLightingDescriptors);

    std::array<vk::DescriptorSetLayoutBinding, 2> toneMappingBindings;
    toneMappingBindings[0] = {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    
    toneMappingBindings[1] = {
        .binding = 1,
        .descriptorType = vk::DescriptorType::eSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    mToneMappingDescriptors = ResourceFactory::CreateDescriptorResources(toneMappingBindings);
    mToneMappingLayout = ResourceFactory::CreatePipelineLayout(&mToneMappingDescriptors);

    mBloomDescriptors = ResourceFactory::CreateDescriptorResources(toneMappingBindings);
    mBloomLayout = ResourceFactory::CreatePipelineLayout(&mBloomDescriptors);

    mPipelineLayout = ResourceFactory::CreatePipelineLayout();

    PipelineConfig gBuffer = {
        .vertexFile = "./shaders/vertMain.spv",
        .vertexName = "vertMain",
        .fragFile =  "./shaders/gBufferFrag.spv",
        .fragName = "gBufferFrag",
        .primitiveTopology = vk::PrimitiveTopology::eTriangleList,
        .colorAttachmentFormats = {
            vk::Format::eR16G16B16A16Sfloat,
            vk::Format::eR16G16B16A16Sfloat
        },
        .colorAttachmentLocations = {
            0,
            1
        },
        .depthAttachmentFormat = vk::Format::eD32Sfloat,
        .useVertexInput = true
    };
    mGbufferPipeline = ResourceFactory::CreatePipeline(gBuffer, mPipelineLayout, "GBuffer pipeline");

    PipelineConfig lighting = {
        .vertexFile = "./shaders/lightVert.spv",
        .vertexName = "lightVert",
        .fragFile = "./shaders/lightFrag.spv",
        .fragName = "lightFrag",

        .primitiveTopology = vk::PrimitiveTopology::eTriangleList,

        .colorAttachmentFormats = {
            vk::Format::eR16G16B16A16Sfloat,
            vk::Format::eR16G16B16A16Sfloat
        },
        .colorAttachmentLocations = {
            0,
            1
        },
        .useVertexInput = false
    };
    mLightingPipeline = ResourceFactory::CreatePipeline(lighting, mLightingLayout, "Lighting Pipeline");

    PipelineConfig tonemapping = {
        .vertexFile = "./shaders/lightVert.spv",
        .vertexName = "lightVert",
        .fragFile = "./shaders/toneMappingFrag.spv",
        .fragName = "toneMappingFrag",

        .primitiveTopology = vk::PrimitiveTopology::eTriangleList,

        .colorAttachmentFormats = {
            GraphicsCommands::GetSwapchainFormat()
        },
        .colorAttachmentLocations = {
            0
        },
        .useVertexInput = false
    };
    mToneMappingPipeline = ResourceFactory::CreatePipeline(tonemapping, mToneMappingLayout, "Tone mapping Pipeline");

    PipelineConfig bloomConfig {
        .vertexFile = "./shaders/lightVert.spv",
        .vertexName = "lightVert",
        .fragFile = "./shaders/bloom.spv",
        .fragName = "bloom",

        .primitiveTopology = vk::PrimitiveTopology::eTriangleList,

        .colorAttachmentFormats = {
            vk::Format::eR16G16B16A16Sfloat
        },
        .colorAttachmentLocations = {
            0
        },
        .useVertexInput = false
    };
    mBloomPipeline = ResourceFactory::CreatePipeline(bloomConfig, mBloomLayout, "Bloom Pipeline");

    mFrameDataBuffer.Init(sizeof(FrameData));
    mObjectDataBuffer.Init(sizeof(ObjectData) * MAX_OBJECTS, true);
    mDirectionalLightDataBuffer.Init(sizeof(GPUDirectionalLight) * MAX_DIRECTIONAL_LIGHTS, true);
    mPointLightDataBuffer.Init(sizeof(GPUPointLight) * MAX_POINT_LIGHTS, true);


    mSortOrder.reserve(MAX_OBJECTS);
    mObjects.reserve(MAX_OBJECTS);
    mBatches.reserve(256);
}

void DeferredRenderer::Render(RenderContext& context, const RenderQueue& renderQueue, const Camera* cam,
    std::span<const GPUDirectionalLight> directionalLights,
    std::span<const GPUPointLight> pointLights) {
    mFrameData.view = cam->GetViewMatrix();
    mFrameData.inverseView = glm::inverse(mFrameData.view);
    mFrameData.proj = cam->GetProjectionMatrix();
    mFrameData.inverseProjection = glm::inverse(mFrameData.proj);
    mFrameData.cameraPosition = {cam->Position, 1.f};
    mFrameData.directionalLightCount = directionalLights.size();
    mFrameData.pointLightCount = pointLights.size();

    PushConstants pConstants {
        mFrameDataBuffer.GetDeviceAddress(),
        mObjectDataBuffer.GetDeviceAddress(),
        mDirectionalLightDataBuffer.GetDeviceAddress(),
        mPointLightDataBuffer.GetDeviceAddress()
    };
    mFrameDataBuffer.Upload(&mFrameData, sizeof(FrameData));
    mDirectionalLightDataBuffer.Upload(directionalLights.data(), sizeof(GPUDirectionalLight) * directionalLights.size());
    mPointLightDataBuffer.Upload(pointLights.data(), sizeof(GPUPointLight) * pointLights.size());

    BuildBatches(renderQueue);
    if (!mObjects.empty()) {
        mObjectDataBuffer.Upload(mObjects.data(), mObjects.size() * sizeof(ObjectData));
    }

    GraphicsCommands::BeginDraw();
    GraphicsCommands::WriteLightingDescriptorSets(mLightingDescriptors, mImageSampler);
    GraphicsCommands::WriteToneMappingDescriptorSets(mToneMappingDescriptors, mImageSampler);
    GraphicsCommands::BeginGBufferPass();

    GraphicsCommands::SetViewport();
    GraphicsCommands::SetScissor();

    GraphicsCommands::BindPipeline(mGbufferPipeline);

    GraphicsCommands::SetDepthTestEnable(true);
    GraphicsCommands::SetDepthWriteEnable(true);

    GraphicsCommands::PushConstants(mPipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,  pConstants);

    for(const auto& batch : mBatches) {
        GraphicsCommands::BindGeometryBuffer(batch.mesh->buffer);
        GraphicsCommands::DrawIndexed(batch.mesh->buffer.GetIndexCount(), batch.instanceCount, 0, 0, batch.firstInstance);
    }

    GraphicsCommands::EndGBufferPass();
    GraphicsCommands::BeginLightingPass();

    GraphicsCommands::SetDepthTestEnable(false);
    GraphicsCommands::SetDepthWriteEnable(false);

    GraphicsCommands::BindPipeline(mLightingPipeline);
    GraphicsCommands::BindDescriptorSets(mLightingLayout, mLightingDescriptors);
    GraphicsCommands::Draw(
        3,  // fullscreen triangle
        1,
        0,
        0
    );

    GraphicsCommands::EndLightingPass();
    GraphicsCommands::WriteBloomDescriptorSets(mBloomDescriptors, mImageSampler, true);
    GraphicsCommands::BeginBloom();
    GraphicsCommands::BindPipeline(mBloomPipeline);
    GraphicsCommands::PushBloomConstants(mBloomLayout, true);
    GraphicsCommands::BindDescriptorSets(mBloomLayout, mBloomDescriptors);
    GraphicsCommands::Draw(
        3,  // fullscreen triangle
        1,
        0,
        0
    );
    GraphicsCommands::TransitionBloomDirection();
    GraphicsCommands::WriteBloomDescriptorSets(mBloomDescriptors, mImageSampler, false);
    GraphicsCommands::PushBloomConstants(mBloomLayout, false);
    GraphicsCommands::Draw(
        3,  // fullscreen triangle
        1,
        0,
        0
    );
    GraphicsCommands::EndBloom();

    GraphicsCommands::BeginToneMapping();
    GraphicsCommands::PushConstants(mPipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,  pConstants);

    GraphicsCommands::BindPipeline(mToneMappingPipeline);
    GraphicsCommands::BindDescriptorSets(mToneMappingLayout, mToneMappingDescriptors);
    GraphicsCommands::Draw(
        3,  // fullscreen triangle
        1,
        0,
        0
    );
    GraphicsCommands::EndToneMapping();
    
    GraphicsCommands::EndDraw();
}

void DeferredRenderer::BuildBatches(const RenderQueue& renderQueue) {
    const auto& commands = renderQueue.GetCommands();
    size_t count = commands.size();
    if (count > MAX_OBJECTS) {
        Logger::Log(Logger::WARNING, "Render queue has {} objects, only the first {} will be drawn", count, MAX_OBJECTS);
        count = MAX_OBJECTS;
    }

    mSortOrder.resize(count);
    std::iota(mSortOrder.begin(), mSortOrder.end(), 0u);
    std::ranges::sort(mSortOrder, {}, [&](uint32_t i) { return commands[i].mesh; });

    mObjects.clear();
    mBatches.clear();

    for (uint32_t index : mSortOrder) {
        const RenderCommand& command = commands[index];

        // New mesh: start a batch whose first instance is where this object will land.
        if (mBatches.empty() || mBatches.back().mesh != command.mesh) {
            mBatches.push_back({command.mesh, static_cast<uint32_t>(mObjects.size()), 0});
        }
        mBatches.back().instanceCount++;

        mObjects.push_back({
            command.Transform,
            glm::mat4(glm::transpose(glm::inverse(glm::mat3(command.Transform))))
        });
    }
}