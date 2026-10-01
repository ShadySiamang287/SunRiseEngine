#pragma once

#include <span>
#include <vulkan/vulkan_raii.hpp>

#include "Graphics/Buffers.h"
#include "Graphics/RenderImage.h"
#include "Graphics/vertex.h"

namespace SUN {
    class GraphicsContext;

    struct ImageTransition {
        RenderImage* image;
        vk::ImageLayout newLayout;
        vk::AccessFlags2 newAccess;
        vk::PipelineStageFlags2 newStage;
    };

    class GraphicsCommands {
    public:
        static void RegisterContext(GraphicsContext* context);

        static bool BeginFrame();
        static void EndFrame();

        static void BeginDraw();
        static void DrawIndexed(int indexCount, int instanceCount, int firstIndex, int vertexOffset, int firstInstance);
        static void DrawIndexedIndirect(
            vk::Buffer buffer,
            vk::DeviceSize offset,
            uint32_t drawCount,
            uint32_t stride
        );
        static void DrawIndexedIndirectCount(
            vk::Buffer buffer,
            vk::DeviceSize offset,
            vk::Buffer countBuffer,
            vk::DeviceSize countBufferOffset,
            uint32_t maxDrawCount,
            uint32_t stride
        );
        static void Draw(int vertexCount, int instanceCount, int firstVertex, int firstInstance);
        static void Dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1);
        static void DrawFullScreenTriangle();
        static void EndDraw();

        static void PushBloomConstants(vk::raii::PipelineLayout& layout, bool horizontal);

        static void BeginRendering(vk::RenderingInfo& info);
        static void EndRendering();

        static void BeginLabel(std::string labelName, std::array<float, 4> colours);
        static void EndLabel();

        static void BindPipeline(vk::raii::Pipeline& pipeline);
        static void BindComputePipeline(vk::raii::Pipeline& pipeline);
        static void BindGeometryBuffer(const GeometryBuffer& buffer);
        static void BindDescriptorSets(vk::raii::PipelineLayout& layout, const DescriptorResources& resources);
        static void PushConstants(
            vk::raii::PipelineLayout& layout,
            vk::ShaderStageFlags flags,
            const PushConstants& constants
        );

        static void PushCullConstants(
            vk::raii::PipelineLayout& layout,
            const CullPushConstants& constants
        );

        static void PushShadowConstants(
            vk::raii::PipelineLayout& layout,
            const ShadowPushConstants& constants
        );

        static void SetViewportAndScissor(vk::Extent2D extent);
        static void SetDepthTestEnable(bool state);
        static void SetDepthWriteEnable(bool state);
        static void SetDepthBias(
            float constantFactor,
            float clamp,
            float slopeFactor
        );

        static void WriteDescriptors(std::span<const vk::WriteDescriptorSet> writes);

        static vk::Format GetSwapchainFormat();
        static vk::Extent2D GetSwapchainExtent();
        static vk::Image& GetCurrentSwapchainImage();
        static vk::raii::ImageView& GetCurrentSwapchainImageView();

        static void TransitionImage(
            RenderImage& image,
            vk::ImageLayout newLayout,
            vk::AccessFlags2 newAccess,
            vk::PipelineStageFlags2 newStage
        );

        static void TransitionImages(std::span<const ImageTransition> transitions);

        // Raw image transition for resources not represented by RenderImage,
        // such as swapchain images.
        static void TransitionImageLayout(
            vk::Image image,
            vk::ImageLayout oldLayout,
            vk::ImageLayout newLayout,
            vk::AccessFlags2 srcAccess,
            vk::AccessFlags2 dstAccess,
            vk::PipelineStageFlags2 srcStage,
            vk::PipelineStageFlags2 dstStage,
            vk::ImageAspectFlags aspectMask =
                vk::ImageAspectFlagBits::eColor
        );

        static vk::ImageMemoryBarrier2 MakeImageBarrier(
            vk::Image image,
            vk::ImageLayout oldLayout,
            vk::ImageLayout newLayout,
            vk::AccessFlags2 srcAccess,
            vk::AccessFlags2 dstAccess,
            vk::PipelineStageFlags2 srcStage,
            vk::PipelineStageFlags2 dstStage,
            vk::ImageAspectFlags aspect =
                vk::ImageAspectFlagBits::eColor,
            uint32_t layerCount = 1
        );

        static void ImageBarriers(std::span<const vk::ImageMemoryBarrier2> barriers);

        static void BufferBarrier(
            vk::Buffer buffer,
            vk::DeviceSize size,
            vk::AccessFlags2 srcAccess,
            vk::AccessFlags2 dstAccess,
            vk::PipelineStageFlags2 srcStage,
            vk::PipelineStageFlags2 dstStage
        );

        static GraphicsContext* mContextPtr;
    };
}
