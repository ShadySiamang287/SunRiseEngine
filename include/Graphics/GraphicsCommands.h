#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <span>

#include "Graphics/Buffers.h"
#include "Graphics/vertex.h"
#include "Renderer/PostProcessor/PostProcessorPass.h"

namespace SUN {
    class GraphicsContext;

    class GraphicsCommands {
    public:
        static void RegisterContext(GraphicsContext* context);

        static bool BeginFrame();
        static void EndFrame();

        static void BeginDraw();
        static void DrawIndexed(int indexCount, int instanceCount, int firstIndex, int vertexOffset, int firstInstance);
        static void Draw(int vertexCount, int instanceCount, int firstVertex, int firstInstance);
        static void DrawFullScreenTriangle();
        static void EndDraw();

        static void BeginGBufferPass();
        static void EndGBufferPass();

        static void PushBloomConstants(vk::raii::PipelineLayout& layout, bool horizontal);

        static void BeginRendering(vk::RenderingInfo& info);
        static void EndRendering();

        static void BeginLabel(std::string labelName, std::array<float, 4> colours);
        static void EndLabel();

        static void BindPipeline(vk::raii::Pipeline& pipeline);
        static void BindGeometryBuffer(const GeometryBuffer& buffer);
        static void BindDescriptorSets(vk::raii::PipelineLayout& layout, const DescriptorResources& resources);
        static void PushConstants(vk::raii::PipelineLayout& layout, vk::ShaderStageFlags flags, const PushConstants& constants);

        static void SetViewport();
        static void SetScissor();
        static void SetDepthTestEnable(bool state);
        static void SetDepthWriteEnable(bool state);

        static void WriteLightingDescriptorSets(const DescriptorResources& resources, vk::raii::Sampler& sampler);
        static void WriteDescriptors(const std::span<vk::WriteDescriptorSet> writes);

        static vk::Format GetSwapchainFormat();
        static vk::Extent2D GetSwapchainExtent();
        static vk::Image& GetCurrentSwapchainImage();
        static vk::raii::ImageView& GetCurrentSwapchainImageView();

        static void DestroyRenderImage(RenderImage& image);

        static void TransitionImageLayout(	    
            vk::Image               image,
            vk::ImageLayout         old_layout,
            vk::ImageLayout         new_layout,
            vk::AccessFlags2        src_access_mask,
            vk::AccessFlags2        dst_access_mask,
            vk::PipelineStageFlags2 src_stage_mask,
            vk::PipelineStageFlags2 dst_stage_mask,
            vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor
        );

        static vk::ImageMemoryBarrier2 MakeImageBarrier(
                vk::Image image,
                vk::ImageLayout oldLayout,
                vk::ImageLayout newLayout,
                vk::AccessFlags2 srcAccess,
                vk::AccessFlags2 dstAccess,
                vk::PipelineStageFlags2 srcStage,
                vk::PipelineStageFlags2 dstStage,
                vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor); 

        static void ImageBarriers(std::span<const vk::ImageMemoryBarrier2> barriers);

    public:
        static GraphicsContext* mContextPtr;
    };
}