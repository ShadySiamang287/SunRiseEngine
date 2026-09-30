#include "Graphics/GraphicsCommands.h"
#include "Graphics/GraphicsContext.h"

#include "Graphics/vertex.h"
#include "Core/Window.h"

#include "Logger.h"
#include "Renderer/RenderingStructs.h"

#include <vector>

using namespace SUN;

void GraphicsCommands::RegisterContext(GraphicsContext* context) {
    mContextPtr = context;
}

bool GraphicsCommands::BeginFrame(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return false;
    }

    auto& frame = mContextPtr->mFrames[mContextPtr->mFrameIndex];

    auto fenceResult = mContextPtr->mDevice.waitForFences(*frame.inFlightFence, vk::True, UINT64_MAX);
    if (fenceResult != vk::Result::eSuccess) {
        throw std::runtime_error("failed to wait for fence!");
    } 
    
    auto [result, imageIndex] = mContextPtr->mSwapChain.acquireNextImage(UINT64_MAX, *frame.imageAvailable, nullptr);
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
    mContextPtr->mDevice.resetFences(*frame.inFlightFence);
    frame.commandBuffer.reset();
    return true;
}

void GraphicsCommands::EndFrame(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& frame = mContextPtr->mFrames[mContextPtr->mFrameIndex];
    auto& cmd = frame.commandBuffer;

    auto imageIndex = mContextPtr->mImageIndex;

    vk::SemaphoreSubmitInfo imageAvailable{
        .semaphore = frame.imageAvailable,
        .value = 0,
        .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .deviceIndex = 0
    };

    vk::CommandBufferSubmitInfo commandBufferInfo{
        .commandBuffer = *cmd,
        .deviceMask = 0
    };

    vk::SemaphoreSubmitInfo renderFinished{
        .semaphore = *mContextPtr->mRenderFinishedSemaphores[imageIndex],
        .value = 0,
        .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .deviceIndex = 0
    };

    vk::SubmitInfo2 submitInfo{
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &imageAvailable,

        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferInfo,

        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &renderFinished
    };

    mContextPtr->mGraphicsQueue.submit2(submitInfo, *frame.inFlightFence);

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

    mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer.begin({});
}

void GraphicsCommands::DrawIndexed(int indexCount, int instanceCount, int firstIndex, int vertexOffset, int firstInstance) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void GraphicsCommands::Draw(int vertexCount, int instanceCount, int firstVertex, int firstInstance) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void GraphicsCommands::DrawFullScreenTriangle() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.draw(3, 1, 0, 0);
}

void GraphicsCommands::EndDraw(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& frame = mContextPtr->mFrames[mContextPtr->mFrameIndex];
    auto& cmd = frame.commandBuffer;

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

void GraphicsCommands::PushBloomConstants(vk::raii::PipelineLayout& layout, bool horizontal){
    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;

    BloomPushConstants constants {horizontal};

    cmd.pushConstants(*layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(BloomPushConstants), &constants);

}

void GraphicsCommands::BeginRendering(vk::RenderingInfo& info) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.beginRendering(info);
}

void GraphicsCommands::EndRendering() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    
    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.endRendering();
}

void GraphicsCommands::BeginLabel(std::string labelName, std::array<float, 4> colours) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    vk::DebugUtilsLabelEXT label {
        .pLabelName = labelName.c_str(),
        .color = colours
    };
    
    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.beginDebugUtilsLabelEXT(label);
}
void GraphicsCommands::EndLabel() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    
    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.endDebugUtilsLabelEXT();
}

void GraphicsCommands::BindPipeline(vk::raii::Pipeline& pipeline) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    
    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

void GraphicsCommands::BindDescriptorSets(vk::raii::PipelineLayout& layout, const DescriptorResources& resources) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *layout, 0, *resources.sets[mContextPtr->mFrameIndex], {});
}

void GraphicsCommands::PushConstants(vk::raii::PipelineLayout& layout, vk::ShaderStageFlags flags, const SUN::PushConstants& constants) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.pushConstants(*layout, flags, 0, sizeof(SUN::PushConstants), &constants);
}

void GraphicsCommands::BindGeometryBuffer(const GeometryBuffer& buffer) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.bindVertexBuffers(0, buffer.GetHandle(), buffer.GetVertexOffset());
    cmd.bindIndexBuffer(buffer.GetHandle(), buffer.GetIndexOffset(), buffer.GetIndexType());
}

void GraphicsCommands::SetViewportAndScissor(vk::Extent2D extent) {
    if (!mContextPtr) {
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }

    auto& cmd =
        mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;

    cmd.setViewport(
        0,
        vk::Viewport(
            0.f,
            static_cast<float>(extent.height),
            static_cast<float>(extent.width),
            -static_cast<float>(extent.height),
            0.f,
            1.f
        )
    );

    cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
}

void GraphicsCommands::SetDepthTestEnable(bool state){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.setDepthTestEnable(state);
}

void GraphicsCommands::SetDepthWriteEnable(bool state){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    auto& cmd = mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer;
    cmd.setDepthWriteEnable(state);
}

void GraphicsCommands::WriteDescriptors(std::span<const vk::WriteDescriptorSet> writes) {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return;
    }
    
    mContextPtr->mDevice.updateDescriptorSets(writes, {});
}

vk::Format GraphicsCommands::GetSwapchainFormat() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return vk::Format::eUndefined;
    }

    return mContextPtr->mSwapChainSurfaceFormat.format;
}

vk::Extent2D GraphicsCommands::GetSwapchainExtent(){
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
        return vk::Extent2D{};
    }

    return mContextPtr->mSwapChainExtent;
}

vk::Image& GraphicsCommands::GetCurrentSwapchainImage() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
    }

    return mContextPtr->mSwapChainImages[mContextPtr->mImageIndex];
}

vk::raii::ImageView& GraphicsCommands::GetCurrentSwapchainImageView() {
    if (!mContextPtr){
        Logger::Log(Logger::ERROR, "Graphics commands not registered to context!");
    }

    return mContextPtr->mSwapChainImageViews[mContextPtr->mImageIndex];
}

void GraphicsCommands::TransitionImage(
    RenderImage& image,
    vk::ImageLayout newLayout,
    vk::AccessFlags2 newAccess,
    vk::PipelineStageFlags2 newStage) {

    const ImageTransition transition {
        .image = &image,
        .newLayout = newLayout,
        .newAccess = newAccess,
        .newStage = newStage
    };

    TransitionImages(std::span<const ImageTransition>(&transition, 1));
}

void GraphicsCommands::TransitionImages(std::span<const ImageTransition> transitions) {

    if (transitions.empty())
        return;

    std::vector<vk::ImageMemoryBarrier2> barriers;
    barriers.reserve(transitions.size());

    for (const ImageTransition& transition : transitions) {
        if (!transition.image || !transition.image->IsValid())
            continue;

        RenderImage& image = *transition.image;

        barriers.push_back(
            MakeImageBarrier(
                image.image.image,
                image.layout,
                transition.newLayout,
                image.access,
                transition.newAccess,
                image.stage,
                transition.newStage,
                image.aspect
            )
        );
    }

    ImageBarriers(barriers);

    for (const ImageTransition& transition : transitions) {
        if (!transition.image || !transition.image->IsValid())
            continue;

        transition.image->layout = transition.newLayout;
        transition.image->access = transition.newAccess;
        transition.image->stage = transition.newStage;
    }
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
    auto barrier = MakeImageBarrier(
        image,
        old_layout,
        new_layout,
        src_access_mask,
        dst_access_mask,
        src_stage_mask,
        dst_stage_mask,
        aspectMask
    );

    ImageBarriers(
        std::span<const vk::ImageMemoryBarrier2>{
            &barrier, 1
        }
    );
}

vk::ImageMemoryBarrier2 GraphicsCommands::MakeImageBarrier(
                vk::Image image,
                vk::ImageLayout oldLayout,
                vk::ImageLayout newLayout,
                vk::AccessFlags2 srcAccess,
                vk::AccessFlags2 dstAccess,
                vk::PipelineStageFlags2 srcStage,
                vk::PipelineStageFlags2 dstStage,
                vk::ImageAspectFlags aspect) {
    return {
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,

        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,

        .oldLayout = oldLayout,
        .newLayout = newLayout,

        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

        .image = image,

        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
}

void GraphicsCommands::ImageBarriers(std::span<const vk::ImageMemoryBarrier2> barriers){
    if (barriers.empty())
        return;

    vk::DependencyInfo dependencyInfo{
        .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),

        .pImageMemoryBarriers = barriers.data()
    };

    mContextPtr->mFrames[mContextPtr->mFrameIndex].commandBuffer.pipelineBarrier2(dependencyInfo);
}

GraphicsContext* GraphicsCommands::mContextPtr = nullptr;