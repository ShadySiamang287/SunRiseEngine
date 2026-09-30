#include "Graphics/RenderImage.h"

using namespace SUN;

RenderImage::~RenderImage() {
    Destroy();
}

RenderImage::RenderImage(RenderImage&& other) noexcept {
    image.image = other.image.image;
    image.view = std::move(other.image.view);
    image.allocation = other.image.allocation;

    format = other.format;
    extent = other.extent;

    layout = other.layout;
    access = other.access;
    stage = other.stage;
    aspect = other.aspect;

    mAllocator = other.mAllocator;

    other.image.image = nullptr;
    other.image.allocation = VK_NULL_HANDLE;

    other.format = vk::Format::eUndefined;
    other.extent = {};
    other.layout = vk::ImageLayout::eUndefined;
    other.access = {};
    other.stage = vk::PipelineStageFlagBits2::eNone;
    other.aspect = vk::ImageAspectFlagBits::eColor;
    other.mAllocator = nullptr;
}

RenderImage& RenderImage::operator=(RenderImage&& other) noexcept {
    if (this == &other)
        return *this;

    Destroy();

    image.image = other.image.image;
    image.view = std::move(other.image.view);
    image.allocation = other.image.allocation;

    format = other.format;
    extent = other.extent;

    layout = other.layout;
    access = other.access;
    stage = other.stage;
    aspect = other.aspect;

    mAllocator = other.mAllocator;

    other.image.image = nullptr;
    other.image.allocation = VK_NULL_HANDLE;

    other.format = vk::Format::eUndefined;
    other.extent = {};
    other.layout = vk::ImageLayout::eUndefined;
    other.access = {};
    other.stage = vk::PipelineStageFlagBits2::eNone;
    other.aspect = vk::ImageAspectFlagBits::eColor;
    other.mAllocator = nullptr;

    return *this;
}

void RenderImage::Destroy() {
    if (!mAllocator || !image.image)
        return;

    image.view = nullptr;

    vmaDestroyImage(
        mAllocator,
        static_cast<VkImage>(image.image),
        image.allocation
    );

    image.image = nullptr;
    image.allocation = VK_NULL_HANDLE;

    format = vk::Format::eUndefined;
    extent = {};
    layout = vk::ImageLayout::eUndefined;
    access = {};
    stage = vk::PipelineStageFlagBits2::eNone;
    aspect = vk::ImageAspectFlagBits::eColor;
    mAllocator = nullptr;
}
