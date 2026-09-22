#include "Graphics/Texture2D.h"
#include "Graphics/GraphicsContext.h"

using namespace SUN;

Texture2D::Texture2D(Texture2D&& other) noexcept
{
    mImage.image = other.mImage.image;
    mImage.view = std::move(other.mImage.view);
    mImage.allocation = other.mImage.allocation;

    mFormat = other.mFormat;
    mExtent = other.mExtent;
    mMipLevels = other.mMipLevels;

    mContext = other.mContext;

    other.mImage.image = nullptr;
    other.mImage.allocation = VK_NULL_HANDLE;

    other.mFormat = vk::Format::eUndefined;
    other.mExtent = {};
    other.mMipLevels = 1;

    other.mContext = nullptr;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept
{
    if (this == &other)
        return *this;

    Destroy();

    mImage.image = other.mImage.image;
    mImage.view = std::move(other.mImage.view);
    mImage.allocation = other.mImage.allocation;

    mFormat = other.mFormat;
    mExtent = other.mExtent;
    mMipLevels = other.mMipLevels;

    mContext = other.mContext;

    other.mImage.image = nullptr;
    other.mImage.allocation = VK_NULL_HANDLE;

    other.mFormat = vk::Format::eUndefined;
    other.mExtent = {};
    other.mMipLevels = 1;

    other.mContext = nullptr;

    return *this;
}

void Texture2D::Destroy()
{
    if (!mContext || !mImage.image)
        return;

    mImage.view = nullptr;

    vmaDestroyImage(
        mContext->mAllocator,
        static_cast<VkImage>(mImage.image),
        mImage.allocation
    );

    mImage.image = nullptr;
    mImage.allocation = VK_NULL_HANDLE;

    mFormat = vk::Format::eUndefined;
    mExtent = {};
    mMipLevels = 1;
}