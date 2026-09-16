#pragma once
#include <array>

#include "Graphics/GraphicsContext.h"

namespace SUN{
    class Buffer {
    public:
        Buffer() = default;
        static void RegisterContext(GraphicsContext* context);
        
        virtual ~Buffer() {
            Destroy();
        };

        // disable copy
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        // move only
        Buffer(Buffer&& other);
        Buffer& operator=(Buffer&& other);

        void Upload(const void* data, size_t size, size_t offset = 0);
        void* Map();
        void Unmap();
        VkDeviceAddress GetDeviceAddress() const;

        vk::Buffer GetHandle() const { return mBuffer; }
        vk::DeviceSize GetSize() const { return mSize; }

    protected:
        void Create(vk::DeviceSize size,
                    vk::BufferUsageFlags usage, VmaMemoryUsage memUsage,
                    VmaAllocationCreateFlags allocFlags = 0);
        void Destroy();

        vk::Buffer mBuffer = nullptr;
        VmaAllocation mAllocation = nullptr;
        vk::DeviceSize mSize = 0;
        void *mMappedData = nullptr;
        
        static GraphicsContext* mContextPtr;

        friend class ShaderBuffer;
    };

    class GeometryBuffer : public Buffer{
    public:
        void Init(const void* vertexData, size_t vertexDataSize, size_t vertexStride,
                  const void* indexData,  size_t indexDataSize,  vk::IndexType indexType);
        
        vk::DeviceSize GetVertexOffset() const { return mVertexOffset; }
        vk::DeviceSize GetIndexOffset()  const { return mIndexOffset;  }
        vk::DeviceSize GetStride()       const { return mStride; }
        uint32_t       GetIndexCount()   const { return mIndexCount; }
        vk::IndexType  GetIndexType()    const { return mIndexType; }

    private:
        vk::DeviceSize mVertexOffset = 0;
        vk::DeviceSize mIndexOffset  = 0;
        vk::DeviceSize mStride       = 0;
        uint32_t       mIndexCount   = 0;
        vk::IndexType  mIndexType    = vk::IndexType::eUint32;
    };

    class ShaderBuffer {
    public:
        void Init(size_t elementSize, bool storageBuffer = false);
        void Destroy();

        void Upload(const void* data, size_t size, size_t offset = 0);

        uint32_t GetBindlessIndex() const;
        vk::Buffer GetHandle() const;
        vk::DeviceAddress GetDeviceAddress() const;
    private:
        std::array<Buffer, MAX_FRAMES_IN_FLIGHT> mBuffers;
        std::array<uint32_t, MAX_FRAMES_IN_FLIGHT> mBindlessIndices{};
        bool mIsStorage = false;

        static GraphicsContext* mContextPtr;

        friend Buffer;
    };
}