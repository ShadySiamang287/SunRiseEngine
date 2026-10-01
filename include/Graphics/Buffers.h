#pragma once
#include <array>
#include <span>
#include <vector>

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
        friend class GeometryBuffer;
    };

    struct GeometryAllocation {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t vertexOffset = 0;
    };

    struct GeometryUpload {
        const void* vertexData = nullptr;
        size_t vertexDataSize = 0;
        const void* indexData = nullptr;
        size_t indexDataSize = 0;
    };

    class GeometryBuffer {
    public:
        void Init(
            vk::DeviceSize vertexCapacity,
            vk::DeviceSize indexCapacity,
            vk::DeviceSize vertexStride,
            vk::IndexType indexType
        );

        GeometryAllocation UploadGeometry(
            const void* vertexData,
            size_t vertexDataSize,
            const void* indexData,
            size_t indexDataSize
        );

        std::vector<GeometryAllocation> UploadGeometryBatch(
            std::span<const GeometryUpload> uploads
        );

        vk::Buffer GetVertexHandle() const { return mVertexBuffer.GetHandle(); }
        vk::Buffer GetIndexHandle() const { return mIndexBuffer.GetHandle(); }
        vk::DeviceSize GetVertexCapacity() const { return mVertexCapacity; }
        vk::DeviceSize GetIndexCapacity() const { return mIndexCapacity; }
        vk::DeviceSize GetVertexBytesUsed() const { return mVertexBytesUsed; }
        vk::DeviceSize GetIndexBytesUsed() const { return mIndexBytesUsed; }
        vk::DeviceSize GetStride() const { return mStride; }
        vk::IndexType GetIndexType() const { return mIndexType; }

    private:
        void EnsureCapacity(
            vk::DeviceSize requiredVertexBytes,
            vk::DeviceSize requiredIndexBytes
        );

        Buffer mVertexBuffer;
        Buffer mIndexBuffer;

        vk::DeviceSize mVertexCapacity = 0;
        vk::DeviceSize mIndexCapacity = 0;
        vk::DeviceSize mVertexBytesUsed = 0;
        vk::DeviceSize mIndexBytesUsed = 0;
        vk::DeviceSize mStride = 0;
        vk::IndexType mIndexType = vk::IndexType::eUint32;
    };

    class ShaderBuffer {
    public:
        void Init(
            size_t elementSize,
            bool storageBuffer = false,
            vk::BufferUsageFlags additionalUsage = {}
        );
        void Destroy();

        void Upload(const void* data, size_t size, size_t offset = 0);
        void Upload(uint32_t frameIndex, const void* data, size_t size, size_t offset = 0);

        uint32_t GetBindlessIndex() const;
        vk::Buffer GetHandle() const;
        vk::Buffer GetHandle(uint32_t frameIndex) const;
        vk::DeviceAddress GetDeviceAddress() const;
    private:
        std::array<Buffer, MAX_FRAMES_IN_FLIGHT> mBuffers;
        std::array<uint32_t, MAX_FRAMES_IN_FLIGHT> mBindlessIndices{};
        bool mIsStorage = false;

        static GraphicsContext* mContextPtr;

        friend Buffer;
    };
}