#pragma once

#include "Graphics/vertex.h"
#include "Graphics/GraphicsContext.h"
#include <vulkan/vulkan_raii.hpp>

namespace SUN {
    struct RenderImage{
        AllocatedImage image;

        vk::Format format;
        vk::Extent2D extent;

        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
    };

    struct PostProcessContext {
        // RenderImage& input;
        // RenderImage& output;

        uint32_t frameIndex;
    };

    class PostProcessPass{
    public:
        virtual ~PostProcessPass() = default;
        virtual void Execute(const PostProcessContext& context) = 0;
        virtual void Resize(vk::Extent2D newSize) = 0;
        virtual std::array<RenderImage, MAX_FRAMES_IN_FLIGHT>& GetOutput(uint32_t frameIndex) = 0;
    };
}