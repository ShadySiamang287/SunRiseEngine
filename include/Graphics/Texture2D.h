#pragma once

#include "Graphics/vertex.h"

namespace SUN {
    class GraphicsContext;

    class Texture2D {
    public:
        Texture2D() = default;
        ~Texture2D() {
            Destroy();
        }

        Texture2D(const Texture2D&) = delete;
        Texture2D& operator=(const Texture2D&) = delete;

        Texture2D(Texture2D&& other) noexcept;
        Texture2D& operator=(Texture2D&& other) noexcept;

        vk::Image GetImage() const {
            return mImage.image;
        }

        vk::ImageView GetView() const {
            return *mImage.view;
        }

        vk::Format GetFormat() const {
            return mFormat;
        }

        vk::Extent2D GetExtent() const {
            return mExtent;
        }

        uint32_t GetMipLevels() const {
            return mMipLevels;
        }

        bool IsValid() const {
            return static_cast<bool>(mImage.image);
        }
    private:
        void Destroy();

        AllocatedImage mImage;

        vk::Format mFormat = vk::Format::eUndefined;
        vk::Extent2D mExtent{};

        uint32_t mMipLevels = 1;

        GraphicsContext* mContext = nullptr;

        friend class ResourceFactory;
    };
}