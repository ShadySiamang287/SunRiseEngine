#pragma once

#include "Graphics/vertex.h"

namespace SUN {
    class ResourceFactory;

    class RenderImage {
    public:
        RenderImage() = default;
        ~RenderImage();

        RenderImage(const RenderImage&) = delete;
        RenderImage& operator=(const RenderImage&) = delete;

        RenderImage(RenderImage&& other) noexcept;
        RenderImage& operator=(RenderImage&& other) noexcept;

        bool IsValid() const {
            return static_cast<bool>(image.image);
        }

        AllocatedImage image;

        vk::Format format = vk::Format::eUndefined;
        vk::Extent2D extent{};

        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
        vk::AccessFlags2 access{};
        vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eNone;
        vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;

    private:
        void Destroy();

        VmaAllocator mAllocator = nullptr;

        friend class ResourceFactory;
    };
}
