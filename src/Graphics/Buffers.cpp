#include "Graphics/Buffers.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

using namespace SUN;
GraphicsContext* Buffer::mContextPtr = nullptr;
GraphicsContext* ShaderBuffer::mContextPtr = nullptr;

void Buffer::RegisterContext(GraphicsContext* context) {
    mContextPtr = context;
    ShaderBuffer::mContextPtr = context;
}

Buffer::Buffer(Buffer&& other) {
    mBuffer      = other.mBuffer;
    mAllocation  = other.mAllocation;
    mSize        = other.mSize;
    mMappedData  = other.mMappedData;

    other.mBuffer     = nullptr;
    other.mAllocation = nullptr;
    other.mSize       = 0;
    other.mMappedData = nullptr;
}

Buffer& Buffer::operator=(Buffer&& other) {
    if (this != &other) {
        Destroy();

        mBuffer      = other.mBuffer;
        mAllocation  = other.mAllocation;
        mSize        = other.mSize;
        mMappedData  = other.mMappedData;

        other.mBuffer     = nullptr;
        other.mAllocation = nullptr;
        other.mSize       = 0;
        other.mMappedData = nullptr;
    }
    return *this;
}

void Buffer::Create(vk::DeviceSize size, vk::BufferUsageFlags usage,
                     VmaMemoryUsage memUsage, VmaAllocationCreateFlags allocFlags) {
    mSize = size;

    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memUsage;
    allocInfo.flags = allocFlags;

    VkBuffer rawBuffer = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo{};

    VkResult result = vmaCreateBuffer(
        mContextPtr->mAllocator,
        reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo),
        &allocInfo,
        &rawBuffer,
        &mAllocation,
        &allocationInfo);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Buffer::Create - vmaCreateBuffer failed");
    }

    mBuffer     = vk::Buffer(rawBuffer);
    mMappedData = allocationInfo.pMappedData; // non-null if MAPPED_BIT was set
}

void Buffer::Destroy() {
    if (mBuffer) {
        vmaDestroyBuffer(mContextPtr->mAllocator,
                          static_cast<VkBuffer>(mBuffer), mAllocation);
        mBuffer     = nullptr;
        mAllocation = nullptr;
        mMappedData = nullptr;
        mSize       = 0;
    }
}

void* Buffer::Map() {
    if (!mMappedData) {
        vmaMapMemory(mContextPtr->mAllocator, mAllocation, &mMappedData);
    }
    return mMappedData;
}

void Buffer::Unmap() {
    if (mMappedData) {
        vmaUnmapMemory(mContextPtr->mAllocator, mAllocation);
        mMappedData = nullptr;
    }
}

void Buffer::Upload(const void* data, size_t size, size_t offset) {
    if (mMappedData) {
        // Host-visible / persistently mapped path — direct memcpy
        std::memcpy(static_cast<uint8_t*>(mMappedData) + offset, data, size);
        vmaFlushAllocation(mContextPtr->mAllocator, mAllocation, offset, size);
        return;
    }

    // Device-local path — go through a staging buffer + one-shot command buffer
    vk::BufferCreateInfo stagingInfo{};
    stagingInfo.size        = size;
    stagingInfo.usage       = vk::BufferUsageFlagBits::eTransferSrc;
    stagingInfo.sharingMode = vk::SharingMode::eExclusive;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    VmaAllocationInfo stagingAllocationInfo{};

    vmaCreateBuffer(mContextPtr->mAllocator,
                     reinterpret_cast<const VkBufferCreateInfo*>(&stagingInfo),
                     &stagingAllocInfo, &stagingBuffer, &stagingAllocation,
                     &stagingAllocationInfo);

    std::memcpy(stagingAllocationInfo.pMappedData, data, size);

    mContextPtr->ImmediateSubmit([&](vk::raii::CommandBuffer& cmd) {
        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = offset;
        copyRegion.size      = size;
        cmd.copyBuffer(vk::Buffer(stagingBuffer), mBuffer, copyRegion);
    });

    vmaDestroyBuffer(mContextPtr->mAllocator, stagingBuffer, stagingAllocation);
}

vk::DeviceAddress Buffer::GetDeviceAddress() const {
    vk::BufferDeviceAddressInfo info{ .buffer = mBuffer};
    return mContextPtr->mDevice.getBufferAddress(info);
}

// ---------------- GeometryBuffer ----------------

namespace {
    vk::DeviceSize AlignUp(
        vk::DeviceSize value,
        vk::DeviceSize alignment
    ) {
        return (value + alignment - 1) / alignment * alignment;
    }

    vk::DeviceSize GrowCapacity(
        vk::DeviceSize current,
        vk::DeviceSize required
    ) {
        if (required <= current) {
            return current;
        }

        vk::DeviceSize capacity = current;

        while (capacity < required) {
            if (capacity >
                std::numeric_limits<vk::DeviceSize>::max() / 2) {
                return required;
            }

            capacity *= 2;
        }

        return capacity;
    }

    vk::BufferUsageFlags VertexBufferUsage() {
        return
            vk::BufferUsageFlagBits::eVertexBuffer |
            vk::BufferUsageFlagBits::eTransferSrc |
            vk::BufferUsageFlagBits::eTransferDst |
            vk::BufferUsageFlagBits::eShaderDeviceAddress;
    }

    vk::BufferUsageFlags IndexBufferUsage() {
        return
            vk::BufferUsageFlagBits::eIndexBuffer |
            vk::BufferUsageFlagBits::eTransferSrc |
            vk::BufferUsageFlagBits::eTransferDst |
            vk::BufferUsageFlagBits::eShaderDeviceAddress;
    }
}

void GeometryBuffer::Init(
    vk::DeviceSize vertexCapacity,
    vk::DeviceSize indexCapacity,
    vk::DeviceSize vertexStride,
    vk::IndexType indexType
) {
    if (vertexCapacity == 0 ||
        indexCapacity == 0 ||
        vertexStride == 0) {
        throw std::runtime_error(
            "GeometryBuffer::Init - capacities and stride must be non-zero"
        );
    }

    if (indexType != vk::IndexType::eUint16 &&
        indexType != vk::IndexType::eUint32) {
        throw std::runtime_error(
            "GeometryBuffer::Init - unsupported index type"
        );
    }

    mVertexCapacity = vertexCapacity;
    mIndexCapacity = indexCapacity;
    mStride = vertexStride;
    mIndexType = indexType;
    mVertexBytesUsed = 0;
    mIndexBytesUsed = 0;

    mVertexBuffer.Create(
        vertexCapacity,
        VertexBufferUsage(),
        VMA_MEMORY_USAGE_AUTO
    );

    mIndexBuffer.Create(
        indexCapacity,
        IndexBufferUsage(),
        VMA_MEMORY_USAGE_AUTO
    );
}

void GeometryBuffer::EnsureCapacity(
    vk::DeviceSize requiredVertexBytes,
    vk::DeviceSize requiredIndexBytes
) {
    const bool growVertex =
        requiredVertexBytes > mVertexCapacity;

    const bool growIndex =
        requiredIndexBytes > mIndexCapacity;

    if (!growVertex && !growIndex) {
        return;
    }

    const vk::DeviceSize newVertexCapacity =
        GrowCapacity(
            mVertexCapacity,
            requiredVertexBytes
        );

    const vk::DeviceSize newIndexCapacity =
        GrowCapacity(
            mIndexCapacity,
            requiredIndexBytes
        );

    Buffer newVertexBuffer;
    Buffer newIndexBuffer;

    if (growVertex) {
        newVertexBuffer.Create(
            newVertexCapacity,
            VertexBufferUsage(),
            VMA_MEMORY_USAGE_AUTO
        );
    }

    if (growIndex) {
        newIndexBuffer.Create(
            newIndexCapacity,
            IndexBufferUsage(),
            VMA_MEMORY_USAGE_AUTO
        );
    }

    Buffer::mContextPtr->ImmediateSubmit(
        [&](vk::raii::CommandBuffer& cmd) {
            if (growVertex && mVertexBytesUsed > 0) {
                const vk::BufferCopy copy {
                    .srcOffset = 0,
                    .dstOffset = 0,
                    .size = mVertexBytesUsed
                };

                cmd.copyBuffer(
                    mVertexBuffer.GetHandle(),
                    newVertexBuffer.GetHandle(),
                    copy
                );
            }

            if (growIndex && mIndexBytesUsed > 0) {
                const vk::BufferCopy copy {
                    .srcOffset = 0,
                    .dstOffset = 0,
                    .size = mIndexBytesUsed
                };

                cmd.copyBuffer(
                    mIndexBuffer.GetHandle(),
                    newIndexBuffer.GetHandle(),
                    copy
                );
            }
        }
    );

    if (growVertex) {
        mVertexBuffer = std::move(newVertexBuffer);
        mVertexCapacity = newVertexCapacity;
    }

    if (growIndex) {
        mIndexBuffer = std::move(newIndexBuffer);
        mIndexCapacity = newIndexCapacity;
    }
}

GeometryAllocation GeometryBuffer::UploadGeometry(
    const void* vertexData,
    size_t vertexDataSize,
    const void* indexData,
    size_t indexDataSize
) {
    const GeometryUpload upload {
        .vertexData = vertexData,
        .vertexDataSize = vertexDataSize,
        .indexData = indexData,
        .indexDataSize = indexDataSize
    };

    const auto allocations =
        UploadGeometryBatch(
            std::span<const GeometryUpload>(&upload, 1)
        );

    return allocations.front();
}

std::vector<GeometryAllocation>
GeometryBuffer::UploadGeometryBatch(
    std::span<const GeometryUpload> uploads
) {
    if (uploads.empty()) {
        return {};
    }

    const vk::DeviceSize indexElementSize =
        mIndexType == vk::IndexType::eUint16
            ? sizeof(uint16_t)
            : sizeof(uint32_t);

    struct PlannedUpload {
        vk::DeviceSize vertexDestination = 0;
        vk::DeviceSize indexDestination = 0;
        vk::DeviceSize vertexStaging = 0;
        vk::DeviceSize indexStaging = 0;
    };

    std::vector<GeometryAllocation> allocations;
    allocations.reserve(uploads.size());

    std::vector<PlannedUpload> planned;
    planned.reserve(uploads.size());

    vk::DeviceSize nextVertexBytes = mVertexBytesUsed;
    vk::DeviceSize nextIndexBytes = mIndexBytesUsed;
    vk::DeviceSize stagingBytes = 0;

    for (const GeometryUpload& upload : uploads) {
        if (!upload.vertexData ||
            !upload.indexData ||
            upload.vertexDataSize == 0 ||
            upload.indexDataSize == 0 ||
            upload.vertexDataSize % mStride != 0 ||
            upload.indexDataSize % indexElementSize != 0) {
            throw std::runtime_error(
                "GeometryBuffer::UploadGeometryBatch - invalid geometry"
            );
        }

        const vk::DeviceSize vertexDestination =
            AlignUp(nextVertexBytes, mStride);

        const vk::DeviceSize indexDestination =
            AlignUp(nextIndexBytes, indexElementSize);

        const vk::DeviceSize firstVertex =
            vertexDestination / mStride;

        const vk::DeviceSize firstIndex =
            indexDestination / indexElementSize;

        const vk::DeviceSize indexCount =
            upload.indexDataSize / indexElementSize;

        if (firstVertex >
                static_cast<vk::DeviceSize>(INT32_MAX) ||
            firstIndex >
                static_cast<vk::DeviceSize>(UINT32_MAX) ||
            indexCount >
                static_cast<vk::DeviceSize>(UINT32_MAX)) {
            throw std::runtime_error(
                "GeometryBuffer::UploadGeometryBatch - draw range exceeds Vulkan limits"
            );
        }

        const vk::DeviceSize vertexStaging =
            AlignUp(stagingBytes, 4);

        const vk::DeviceSize indexStaging =
            AlignUp(
                vertexStaging + upload.vertexDataSize,
                4
            );

        stagingBytes =
            indexStaging + upload.indexDataSize;

        planned.push_back({
            .vertexDestination = vertexDestination,
            .indexDestination = indexDestination,
            .vertexStaging = vertexStaging,
            .indexStaging = indexStaging
        });

        allocations.push_back({
            .firstIndex =
                static_cast<uint32_t>(firstIndex),
            .indexCount =
                static_cast<uint32_t>(indexCount),
            .vertexOffset =
                static_cast<int32_t>(firstVertex)
        });

        nextVertexBytes =
            vertexDestination + upload.vertexDataSize;

        nextIndexBytes =
            indexDestination + upload.indexDataSize;
    }

    EnsureCapacity(
        nextVertexBytes,
        nextIndexBytes
    );

    Buffer stagingBuffer;
    stagingBuffer.Create(
        stagingBytes,
        vk::BufferUsageFlagBits::eTransferSrc,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT |
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    for (std::size_t i = 0; i < uploads.size(); ++i) {
        const GeometryUpload& upload = uploads[i];
        const PlannedUpload& plan = planned[i];

        stagingBuffer.Upload(
            upload.vertexData,
            upload.vertexDataSize,
            plan.vertexStaging
        );

        stagingBuffer.Upload(
            upload.indexData,
            upload.indexDataSize,
            plan.indexStaging
        );
    }

    Buffer::mContextPtr->ImmediateSubmit(
        [&](vk::raii::CommandBuffer& cmd) {
            for (std::size_t i = 0;
                 i < uploads.size();
                 ++i) {
                const GeometryUpload& upload =
                    uploads[i];

                const PlannedUpload& plan =
                    planned[i];

                const vk::BufferCopy vertexCopy {
                    .srcOffset = plan.vertexStaging,
                    .dstOffset = plan.vertexDestination,
                    .size = upload.vertexDataSize
                };

                const vk::BufferCopy indexCopy {
                    .srcOffset = plan.indexStaging,
                    .dstOffset = plan.indexDestination,
                    .size = upload.indexDataSize
                };

                cmd.copyBuffer(
                    stagingBuffer.GetHandle(),
                    mVertexBuffer.GetHandle(),
                    vertexCopy
                );

                cmd.copyBuffer(
                    stagingBuffer.GetHandle(),
                    mIndexBuffer.GetHandle(),
                    indexCopy
                );
            }
        }
    );

    mVertexBytesUsed = nextVertexBytes;
    mIndexBytesUsed = nextIndexBytes;

    return allocations;
}

// ---------------- ShaderBuffer ----------------

void ShaderBuffer::Init(
    size_t elementSize,
    bool storageBuffer,
    vk::BufferUsageFlags additionalUsage
) {
    mIsStorage = storageBuffer;

    vk::BufferUsageFlags usage = storageBuffer
        ? vk::BufferUsageFlagBits::eStorageBuffer
        : vk::BufferUsageFlagBits::eUniformBuffer;

    usage |=
        vk::BufferUsageFlagBits::eShaderDeviceAddress |
        additionalUsage;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        mBuffers[i].Create(elementSize, usage, VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_MAPPED_BIT |
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        // Assumes GraphicsContext exposes a bindless registration function;
        // add this if it doesn't exist yet.
        // mBindlessIndices[i] = mContext->RegisterBindlessBuffer(
        //     mBuffers[i].GetHandle(), elementSize, storageBuffer);
    }
}

void ShaderBuffer::Destroy() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        //mContext->UnregisterBindlessBuffer(mBindlessIndices[i]);
        mBuffers[i].Destroy();
    }
}

void ShaderBuffer::Upload(const void* data, size_t size, size_t offset) {
    mBuffers[mContextPtr->mFrameIndex].Upload(data, size, offset);
}

void ShaderBuffer::Upload(uint32_t frameIndex, const void* data, size_t size, size_t offset) {
    mBuffers[frameIndex].Upload(data, size, offset);
}

uint32_t ShaderBuffer::GetBindlessIndex() const {
    return mBindlessIndices[mContextPtr->mFrameIndex];
}

vk::Buffer ShaderBuffer::GetHandle() const {
    return mBuffers[mContextPtr->mFrameIndex].GetHandle();
}

vk::Buffer ShaderBuffer::GetHandle(uint32_t frameIndex) const {
    return mBuffers[frameIndex].GetHandle();
}

vk::DeviceAddress ShaderBuffer::GetDeviceAddress() const {
    return mBuffers[mContextPtr->mFrameIndex].GetDeviceAddress();
}