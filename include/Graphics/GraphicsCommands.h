#pragma once;
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

namespace SUN {
    class GraphicsContext;

    class GraphicsCommands {
    public:
        static void RegisterContext(GraphicsContext* context);

        static void BeginFrame();
        static void EndFrame();

        static void BeginDraw();
        static void Draw(int vertexCount, int instanceCount, int firstVertex, int firstInstance);
        static void EndDraw();

        static void BindPipeline(vk::raii::Pipeline& pipeline);

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