#pragma once

#include <cstdint>
#include <vulkan/vulkan_raii.hpp>

namespace SUN {
    struct PostProcessContext {
        uint32_t frameIndex;
    };

    class PostProcessPass {
    public:
        virtual ~PostProcessPass() = default;

        virtual void Execute(const PostProcessContext& context) = 0;
        virtual void Resize(vk::Extent2D newSize) = 0;
    };
}
