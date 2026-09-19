#include "Graphics/GraphicsCommands.h"
#include "Graphics/GraphicsContext.h"

#include "Graphics/vertex.h"
#include "Core/Window.h"

#include "Logger.h"
#include "Renderer/RenderingStructs.h"

using namespace SUN;

void GraphicsCommands::RegisterContext(GraphicsContext* context) {
    mContextPtr = context;
}

bool GraphicsCommands::BeginFrame(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return false;
    }

    auto fenceResult = mContextPtr->mDevice.waitForFences(*mContextPtr->mInFlightFences[mContextPtr->mFrameIndex], vk::True, UINT64_MAX);
    if (fenceResult != vk::Result::eSuccess) {
        throw std::runtime_error("failed to wait for fence!");
    } 
    
    auto [result, imageIndex] = mContextPtr->mSwapChain.acquireNextImage(UINT64_MAX, *mContextPtr->mPresentCompleteSemaphores[mContextPtr->mFrameIndex], nullptr);
    if (result == vk::Result::eErrorOutOfDateKHR){
        mContextPtr->RecreateSwapChain();
        return false;
    }
    
    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
    {
        assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    mContextPtr->mImageIndex = imageIndex;
    mContextPtr->mDevice.resetFences(*mContextPtr->mInFlightFences[mContextPtr->mFrameIndex]);
    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].reset();
    return true;
}

void GraphicsCommands::EndFrame(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

vk::PipelineStageFlags waitDestinationStageMask( vk::PipelineStageFlagBits::eColorAttachmentOutput );
    const vk::SubmitInfo submitInfo{
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &*mContextPtr->mPresentCompleteSemaphores[mContextPtr->mFrameIndex],
        .pWaitDstStageMask    = &waitDestinationStageMask,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &*mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &*mContextPtr->mRenderFinishedSemaphores[mContextPtr->mImageIndex]
    };

    mContextPtr->mGraphicsQueue.submit(submitInfo, *mContextPtr->mInFlightFences[mContextPtr->mFrameIndex]);

    const vk::PresentInfoKHR presentInfoKHR {
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &*mContextPtr->mRenderFinishedSemaphores[mContextPtr->mImageIndex],
        .swapchainCount     = 1,
        .pSwapchains        = &*mContextPtr->mSwapChain,
        .pImageIndices      = &mContextPtr->mImageIndex,
    };

    vk::Result result = vk::Result::eSuccess;
    try {
        result = mContextPtr->mGraphicsQueue.presentKHR(presentInfoKHR);
    } catch (const vk::OutOfDateKHRError&){
        result = vk::Result::eErrorOutOfDateKHR;
    }

    if ((result == vk::Result::eSuboptimalKHR) || (result == vk::Result::eErrorOutOfDateKHR) || mContextPtr->mWindowPtr->mResized) {
        mContextPtr->mWindowPtr->mResized = false;
        mContextPtr->RecreateSwapChain();
    } else {
        assert(result == vk::Result::eSuccess);
    }

    mContextPtr->mFrameIndex = (mContextPtr->mFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void GraphicsCommands::BeginDraw(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].begin({});
}

void GraphicsCommands::DrawIndexed(int indexCount, int instanceCount, int firstIndex, int vertexOffset, int firstInstance) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void GraphicsCommands::Draw(int vertexCount, int instanceCount, int firstVertex, int firstInstance) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void GraphicsCommands::EndDraw(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    
    auto& cmd = mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex];

    TransitionImageLayout(
        mContextPtr->mSwapChainImages[mContextPtr->mImageIndex],
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,

        vk::AccessFlagBits2::eColorAttachmentWrite,
        {},

        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eBottomOfPipe
    );
    cmd.end();
}

void GraphicsCommands::BeginGBufferPass() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& cmd = mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex];
    auto& gbuffer = mContextPtr->mGBuffers[mContextPtr->mFrameIndex];
    vk::DebugUtilsLabelEXT label {
        .pLabelName = "Deffered pass",
    };
    label.setColor({0.5F, 0.76F, .32F, 1.F});
    cmd.beginDebugUtilsLabelEXT(label);

    TransitionImageLayout(
        gbuffer.Albedo.image,

        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eColorAttachmentOptimal,

        vk::AccessFlagBits2::eShaderRead,
        vk::AccessFlagBits2::eColorAttachmentWrite,

        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    TransitionImageLayout(
        gbuffer.Normal.image,

        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eColorAttachmentOptimal,

        vk::AccessFlagBits2::eShaderRead,
        vk::AccessFlagBits2::eColorAttachmentWrite,

        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    TransitionImageLayout(
        gbuffer.Depth.image,

        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eDepthAttachmentOptimal,

        vk::AccessFlagBits2::eShaderRead,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,

        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::PipelineStageFlagBits2::eEarlyFragmentTests |
            vk::PipelineStageFlagBits2::eLateFragmentTests,

        vk::ImageAspectFlagBits::eDepth
    );

    vk::RenderingAttachmentInfo gbuffer0{
        .imageView = gbuffer.Albedo.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingAttachmentInfo gbuffer1{
        .imageView = gbuffer.Normal.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingAttachmentInfo depth{
        .imageView = gbuffer.Depth.view,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearDepthStencilValue{1.0f, 0}
        }
    };

    std::array<vk::RenderingAttachmentInfo, 2> colors{
        gbuffer0,
        gbuffer1
    };


    vk::RenderingInfo renderingInfo{
        .renderArea = {
            .offset = {0, 0},
            .extent = mContextPtr->mSwapChainExtent
        },
        .layerCount = 1,
        .colorAttachmentCount = 2,
        .pColorAttachments = colors.data(),
        .pDepthAttachment = &depth
    };

    cmd.beginRendering(renderingInfo);
}

void GraphicsCommands::EndGBufferPass() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& cmd = mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex];
    auto& gbuffer = mContextPtr->mGBuffers[mContextPtr->mFrameIndex];

    cmd.endRendering();

    TransitionImageLayout(
        gbuffer.Albedo.image,

        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,

        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::AccessFlagBits2::eShaderRead,

        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eFragmentShader
    );

    TransitionImageLayout(
        gbuffer.Normal.image,

        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,

        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::AccessFlagBits2::eShaderRead,

        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eFragmentShader
    );

    TransitionImageLayout(
        gbuffer.Depth.image,

        vk::ImageLayout::eDepthAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,

        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::AccessFlagBits2::eShaderRead,

        vk::PipelineStageFlagBits2::eEarlyFragmentTests |
            vk::PipelineStageFlagBits2::eLateFragmentTests,

        vk::PipelineStageFlagBits2::eFragmentShader,

        vk::ImageAspectFlagBits::eDepth
    );

    cmd.endDebugUtilsLabelEXT();
}

void GraphicsCommands::BeginLightingPass() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    auto& cmd = mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex];

    vk::DebugUtilsLabelEXT label {
        .pLabelName = "Lighting pass",
    };
    label.setColor({0.76F, 0.32F, .32F, 1.F});
    cmd.beginDebugUtilsLabelEXT(label);
    
    TransitionImageLayout(
        mContextPtr->mHDRTargets[mContextPtr->mFrameIndex].image,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eColorAttachmentOptimal,

        {}, // src access
        vk::AccessFlagBits2::eColorAttachmentWrite,

        vk::PipelineStageFlagBits2::eTopOfPipe,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    TransitionImageLayout(
        mContextPtr->mBloomTargets[mContextPtr->mFrameIndex].brightness.image,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eColorAttachmentOptimal,

        {}, // src access
        vk::AccessFlagBits2::eColorAttachmentWrite,

        vk::PipelineStageFlagBits2::eTopOfPipe,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    vk::RenderingAttachmentInfo brightAttachment {
        .imageView = mContextPtr->mBloomTargets[mContextPtr->mFrameIndex].brightness.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingAttachmentInfo hdrAttachment{
        .imageView = mContextPtr->mHDRTargets[mContextPtr->mFrameIndex].view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    std::array<vk::RenderingAttachmentInfo, 2> attachments {
        hdrAttachment,
        brightAttachment
    };

    vk::RenderingInfo renderingInfo{
        .renderArea = {
            .offset = {0, 0},
            .extent = mContextPtr->mSwapChainExtent
        },
        .layerCount = 1,          // Required when viewMask == 0
        .colorAttachmentCount = 2,
        .pColorAttachments = attachments.data(),
    };

    cmd.beginRendering(renderingInfo);
}

void GraphicsCommands::EndLightingPass() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& cmd = mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex];

    cmd.endRendering();

    TransitionImageLayout(
        mContextPtr->mHDRTargets[mContextPtr->mFrameIndex].image,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,

        {}, // src access
        vk::AccessFlagBits2::eColorAttachmentWrite,

        vk::PipelineStageFlagBits2::eTopOfPipe,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    TransitionImageLayout(
        mContextPtr->mBloomTargets[mContextPtr->mFrameIndex].brightness.image,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,

        {}, // src access
        vk::AccessFlagBits2::eColorAttachmentWrite,

        vk::PipelineStageFlagBits2::eTopOfPipe,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    cmd.endDebugUtilsLabelEXT();
}

void GraphicsCommands::BeginBloom() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& cmd = mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex];
    
    vk::DebugUtilsLabelEXT label {
        .pLabelName = "Bloom Pass",
    };
    label.setColor({0.32F, 0.76F, .76F, 1.F});
    cmd.beginDebugUtilsLabelEXT(label);

    TransitionImageLayout(
        mContextPtr->mBloomTargets[mContextPtr->mFrameIndex].ping.image,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eColorAttachmentOptimal,

        {}, // src access
        vk::AccessFlagBits2::eColorAttachmentWrite,

        vk::PipelineStageFlagBits2::eTopOfPipe,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    vk::RenderingAttachmentInfo attachment{
        .imageView = mContextPtr->mBloomTargets[mContextPtr->mFrameIndex].ping.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingInfo renderingInfo{
        .renderArea = {
            .offset = {0, 0},
            .extent = mContextPtr->mSwapChainExtent
        },
        .layerCount = 1,          // Required when viewMask == 0
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    };
    cmd.beginRendering(renderingInfo);
}

void GraphicsCommands::PushBloomConstants(vk::raii::PipelineLayout& layout, bool horizontal){
    auto& cmd = mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex];

    BloomPushConstants constants {horizontal};

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].pushConstants(*layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(BloomPushConstants), &constants);

}

void GraphicsCommands::TransitionBloomDirection(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    
    auto& cmd = mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex];
    cmd.endRendering();

    TransitionImageLayout(
        mContextPtr->mBloomTargets[mContextPtr->mFrameIndex].ping.image,

        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,

        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::AccessFlagBits2::eShaderRead,

        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eFragmentShader
    );

    // Vertical pass is about to write pong.
    TransitionImageLayout(
        mContextPtr->mBloomTargets[mContextPtr->mFrameIndex].pong.image,

        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eColorAttachmentOptimal,

        vk::AccessFlagBits2::eShaderRead,
        vk::AccessFlagBits2::eColorAttachmentWrite,

        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    vk::RenderingAttachmentInfo attachment{
        .imageView = mContextPtr->mBloomTargets[mContextPtr->mFrameIndex].pong.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingInfo renderingInfo{
        .renderArea = {
            .offset = {0, 0},
            .extent = mContextPtr->mSwapChainExtent
        },
        .layerCount = 1,          // Required when viewMask == 0
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    };
    cmd.beginRendering(renderingInfo);
}

void GraphicsCommands::EndBloom() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    
    auto& cmd = mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex];
    cmd.endRendering();

    TransitionImageLayout(
        mContextPtr->mBloomTargets[mContextPtr->mFrameIndex].pong.image,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,

        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::AccessFlagBits2::eShaderRead,

        vk::PipelineStageFlagBits2::eTopOfPipe,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );


    cmd.endDebugUtilsLabelEXT();
}

void GraphicsCommands::BeginToneMapping() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& cmd = mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex];

    vk::DebugUtilsLabelEXT label {
        .pLabelName = "Tone Mapping",
    };
    label.setColor({0.32F, 0.32F, .76F, 1.F});
    cmd.beginDebugUtilsLabelEXT(label);

    TransitionImageLayout(
        mContextPtr->mSwapChainImages[mContextPtr->mImageIndex],
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,

        {}, // src access
        vk::AccessFlagBits2::eColorAttachmentWrite,

        vk::PipelineStageFlagBits2::eTopOfPipe,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    vk::RenderingAttachmentInfo swapchain{
        .imageView = mContextPtr->mSwapChainImageViews[mContextPtr->mImageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}
        }
    };

    vk::RenderingInfo renderingInfo{
        .renderArea = {
            .offset = {0, 0},
            .extent = mContextPtr->mSwapChainExtent
        },
        .layerCount = 1,          // Required when viewMask == 0
        .colorAttachmentCount = 1,
        .pColorAttachments = &swapchain,
    };

    cmd.beginRendering(renderingInfo);

}

void GraphicsCommands::EndToneMapping() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& cmd = mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex];
    cmd.endRendering();

    cmd.endDebugUtilsLabelEXT();
}

void GraphicsCommands::BindPipeline(vk::raii::Pipeline& pipeline) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

void GraphicsCommands::BindDescriptorSets(vk::raii::PipelineLayout& layout, const DescriptorResources& resources) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *layout,
        0,
        *resources.sets[mContextPtr->mFrameIndex],
        {}
    );
}

void GraphicsCommands::PushConstants(vk::raii::PipelineLayout& layout, vk::ShaderStageFlags flags, const SUN::PushConstants& constants) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].pushConstants(*layout, flags, 0, sizeof(SUN::PushConstants), &constants);
}

void GraphicsCommands::BindGeometryBuffer(const GeometryBuffer& buffer) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].bindVertexBuffers(0, buffer.GetHandle(), buffer.GetVertexOffset());
    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].bindIndexBuffer(buffer.GetHandle(), buffer.GetIndexOffset(), buffer.GetIndexType());
}

void GraphicsCommands::SetViewport(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].setViewport(
        0,
        vk::Viewport(
            0.f, 
            static_cast<float>(mContextPtr->mSwapChainExtent.height),
            static_cast<float>(mContextPtr->mSwapChainExtent.width), 
            -static_cast<float>(mContextPtr->mSwapChainExtent.height),
            0.f,
            1.f
        )
    );
}

void GraphicsCommands::SetScissor() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].setScissor(
        0, vk::Rect2D(vk::Offset2D(0, 0), mContextPtr->mSwapChainExtent)
    );
}

void GraphicsCommands::SetDepthTestEnable(bool state){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].setDepthTestEnable(state);
}

void GraphicsCommands::SetDepthWriteEnable(bool state){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].setDepthWriteEnable(state);
}

void GraphicsCommands::WriteLightingDescriptorSets(const DescriptorResources& resources, vk::raii::Sampler& sampler)  {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    const auto& gbuffer = mContextPtr->mGBuffers[mContextPtr->mFrameIndex];

    vk::DescriptorImageInfo albedoInfo{
        .sampler = nullptr,
        .imageView = *gbuffer.Albedo.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    vk::DescriptorImageInfo normalInfo{
        .sampler = nullptr,
        .imageView = *gbuffer.Normal.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    vk::DescriptorImageInfo depthInfo{
        .sampler = nullptr,
        .imageView = *gbuffer.Depth.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    vk::DescriptorImageInfo samplerInfo{
        .sampler = *sampler
    };

    std::array<vk::WriteDescriptorSet, 4> writes{
        vk::WriteDescriptorSet{
            .dstSet =
                *resources.sets[mContextPtr->mFrameIndex],

            .dstBinding = 0,
            .descriptorCount = 1,

            .descriptorType =
                vk::DescriptorType::eSampledImage,

            .pImageInfo = &albedoInfo
        },

        vk::WriteDescriptorSet{
            .dstSet =
                *resources.sets[mContextPtr->mFrameIndex],

            .dstBinding = 1,
            .descriptorCount = 1,

            .descriptorType =
                vk::DescriptorType::eSampledImage,

            .pImageInfo = &normalInfo
        },

        vk::WriteDescriptorSet{
            .dstSet =
                *resources.sets[mContextPtr->mFrameIndex],

            .dstBinding = 2,
            .descriptorCount = 1,

            .descriptorType = vk::DescriptorType::eSampledImage,

            .pImageInfo = &depthInfo
        },

        vk::WriteDescriptorSet{
            .dstSet = *resources.sets[mContextPtr->mFrameIndex],
            .dstBinding = 3,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplerInfo
        }
    };

    mContextPtr->mDevice.updateDescriptorSets(writes, {});
}

void GraphicsCommands::WriteToneMappingDescriptorSets(const DescriptorResources& resources, vk::raii::Sampler& sampler)  {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    const auto& hdr = mContextPtr->mHDRTargets[mContextPtr->mFrameIndex];

    vk::DescriptorImageInfo hdrInfo{
        .sampler = nullptr,
        .imageView = *hdr.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    vk::DescriptorImageInfo samplerInfo{
        .sampler = *sampler
    };

    std::array<vk::WriteDescriptorSet, 2> writes{
        vk::WriteDescriptorSet{
            .dstSet =
                *resources.sets[mContextPtr->mFrameIndex],

            .dstBinding = 0,
            .descriptorCount = 1,

            .descriptorType =
                vk::DescriptorType::eSampledImage,

            .pImageInfo = &hdrInfo
        },

        vk::WriteDescriptorSet{
            .dstSet = *resources.sets[mContextPtr->mFrameIndex],
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplerInfo
        }
    };

    mContextPtr->mDevice.updateDescriptorSets(writes, {});
}

void GraphicsCommands::WriteBloomDescriptorSets(const DescriptorResources& resources, vk::raii::Sampler& sampler, bool horizontal)  {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    const auto& bloom = mContextPtr->mBloomTargets[mContextPtr->mFrameIndex];

    vk::DescriptorImageInfo bloomInfo{
        .sampler = nullptr,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    if (horizontal) {
        bloomInfo.imageView = bloom.brightness.view;
    } else {
        bloomInfo.imageView = bloom.ping.view;
    }

    vk::DescriptorImageInfo samplerInfo{
        .sampler = *sampler
    };

    std::array<vk::WriteDescriptorSet, 2> writes{
        vk::WriteDescriptorSet{
            .dstSet =
                *resources.sets[mContextPtr->mFrameIndex],

            .dstBinding = 0,
            .descriptorCount = 1,

            .descriptorType =
                vk::DescriptorType::eSampledImage,

            .pImageInfo = &bloomInfo
        },

        vk::WriteDescriptorSet{
            .dstSet = *resources.sets[mContextPtr->mFrameIndex],
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplerInfo
        }
    };

    mContextPtr->mDevice.updateDescriptorSets(writes, {});
}


vk::Format GraphicsCommands::GetSwapchainFormat() {
    return mContextPtr->mSwapChainSurfaceFormat.format;
}

void GraphicsCommands::TransitionImageLayout(	    
    vk::Image                image,
    vk::ImageLayout         old_layout,
    vk::ImageLayout         new_layout,
    vk::AccessFlags2        src_access_mask,
    vk::AccessFlags2        dst_access_mask,
    vk::PipelineStageFlags2 src_stage_mask,
    vk::PipelineStageFlags2 dst_stage_mask,
    vk::ImageAspectFlags aspectMask
) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask        = dst_stage_mask,
        .dstAccessMask       = dst_access_mask,
        .oldLayout           = old_layout,
        .newLayout           = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = {
            .aspectMask     = aspectMask,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        }
    };

    vk::DependencyInfo dependency_info = {
        .dependencyFlags         = {},
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier
    };

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].pipelineBarrier2(dependency_info);
}

GraphicsContext* GraphicsCommands::mContextPtr = nullptr;