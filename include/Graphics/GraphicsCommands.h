#pragma once
#include <vulkan/vulkan_raii.hpp>

#include "Graphics/Buffers.h"
#include "Graphics/vertex.h"

namespace SUN {
    class GraphicsContext;

    class GraphicsCommands {
    public:
        static void RegisterContext(GraphicsContext* context);

        static void BeginFrame();
        static void EndFrame();

        static void BeginDraw();
        static void Draw(int indexCount, int instanceCount, int firstIndex, int vertexOffset, int firstInstance);
        static void EndDraw();

        static void BindPipeline(vk::raii::Pipeline& pipeline);
        static void BindGeometryBuffer(const GeometryBuffer& buffer);
        static void PushConstants(vk::raii::PipelineLayout& layout, vk::ShaderStageFlags flags, const PushConstants& constants);

        static void SetViewport();
        static void SetScissor();

    private:
        static void TransitionImageLayout(	    
            uint32_t                imageIndex,
            vk::ImageLayout         old_layout,
            vk::ImageLayout         new_layout,
            vk::AccessFlags2        src_access_mask,
            vk::AccessFlags2        dst_access_mask,
            vk::PipelineStageFlags2 src_stage_mask,
            vk::PipelineStageFlags2 dst_stage_mask
        );

        static GraphicsContext* mContextPtr;
    };
}