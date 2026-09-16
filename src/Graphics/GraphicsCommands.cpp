#include "Graphics/GraphicsCommands.h"
#include "Graphics/GraphicsContext.h"

#include "Graphics/vertex.h"

#include "Logger.h"

using namespace SUN;

void GraphicsCommands::RegisterContext(GraphicsContext* context) {
    mContextPtr = context;
}

void GraphicsCommands::BeginFrame(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto fenceResult = mContextPtr->mDevice.waitForFences(*mContextPtr->mInFlightFences[mContextPtr->mFrameIndex], vk::True, UINT64_MAX);
    if (fenceResult != vk::Result::eSuccess) {
        throw std::runtime_error("failed to wait for fence!");
    } 
    mContextPtr->mDevice.resetFences(*mContextPtr->mInFlightFences[mContextPtr->mFrameIndex]);

    auto [result, imageIndex] = mContextPtr->mSwapChain.acquireNextImage(UINT64_MAX, *mContextPtr->mPresentCompleteSemaphores[mContextPtr->mFrameIndex], nullptr);
    mContextPtr->mImageIndex = imageIndex;
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
    mContextPtr->mGraphicsQueue.presentKHR(presentInfoKHR);

    mContextPtr->mFrameIndex = (mContextPtr->mFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void GraphicsCommands::BeginDraw(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    
    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].begin({});
    TransitionImageLayout(
        mContextPtr->mImageIndex,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        {},
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput  
    );

    vk::ClearValue              clearColor     = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo = {
        .imageView   = mContextPtr->mSwapChainImageViews[mContextPtr->mImageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eClear,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = clearColor
    };

    vk::RenderingInfo renderingInfo = {
        .renderArea           = {.offset = {0, 0}, .extent = mContextPtr->mSwapChainExtent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &attachmentInfo
    };

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].beginRendering(renderingInfo);
}

void GraphicsCommands::Draw(int indexCount, int instanceCount, int firstIndex, int vertexOffset, int firstInstance) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void GraphicsCommands::EndDraw(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].endRendering();

    TransitionImageLayout(
        mContextPtr->mImageIndex,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite,             // srcAccessMask
        {},                                                     // dstAccessMask
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,     // srcStage
        vk::PipelineStageFlagBits2::eBottomOfPipe               // dstStage
    );
    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].end();
}

void GraphicsCommands::BindPipeline(vk::raii::Pipeline& pipeline) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
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
    mContextPtr->mCommandBuffers[mContextPtr->mFrameIndex].bindIndexBuffer(buffer.GetHandle(), buffer.GetIndexOffset(), vk::IndexType::eUint32);
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
            -static_cast<float>(mContextPtr->mSwapChainExtent.height)
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

void GraphicsCommands::TransitionImageLayout(	    
    uint32_t                imageIndex,
    vk::ImageLayout         old_layout,
    vk::ImageLayout         new_layout,
    vk::AccessFlags2        src_access_mask,
    vk::AccessFlags2        dst_access_mask,
    vk::PipelineStageFlags2 src_stage_mask,
    vk::PipelineStageFlags2 dst_stage_mask
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
        .image               = mContextPtr->mSwapChainImages[imageIndex],
        .subresourceRange    = {
            .aspectMask     = vk::ImageAspectFlagBits::eColor,
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