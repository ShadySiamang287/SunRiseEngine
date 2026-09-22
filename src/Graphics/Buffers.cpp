#include "Graphics/Buffers.h"

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

    // One-shot command buffer for the copy
    vk::CommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.commandPool        = mContextPtr->mCommandPool;
    cmdAllocInfo.level              = vk::CommandBufferLevel::ePrimary;
    cmdAllocInfo.commandBufferCount = 1;

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

// ---------------- VertexBuffer ----------------

void GeometryBuffer::Init(const void* vertexData, size_t vertexDataSize, size_t vertexStride,
                        const void* indexData,  size_t indexDataSize,  vk::IndexType indexType) {
    mStride = vertexStride;

    mIndexType = indexType;
    mIndexCount = static_cast<uint32_t>(
    indexDataSize / (indexType == vk::IndexType::eUint32 ? sizeof(uint32_t) : sizeof(uint16_t)));

    // Vertex data goes first, index data second, aligned to the index type's size
    // (Vulkan requires bindIndexBuffer's offset to be a multiple of the index type size)
    size_t indexAlignment = (indexType == vk::IndexType::eUint32) ? sizeof(uint32_t) : sizeof(uint16_t);
    size_t alignedVertexSize = (vertexDataSize + indexAlignment - 1) & ~(indexAlignment - 1);

    mVertexOffset = 0;
    mIndexOffset  = alignedVertexSize;
    size_t totalSize = alignedVertexSize + indexDataSize;

    Create(totalSize,
           vk::BufferUsageFlagBits::eVertexBuffer |
           vk::BufferUsageFlagBits::eIndexBuffer |
           vk::BufferUsageFlagBits::eTransferDst |
           vk::BufferUsageFlagBits::eShaderDeviceAddress,
           VMA_MEMORY_USAGE_AUTO,
           0); // device-local, no host mapping -> Upload() takes the staging path
    
    Upload(vertexData, vertexDataSize, mVertexOffset);
    Upload(indexData,  indexDataSize,  mIndexOffset);
}

// ---------------- ShaderBuffer ----------------

void ShaderBuffer::Init(size_t elementSize, bool storageBuffer) {
    mIsStorage = storageBuffer;

    vk::BufferUsageFlags usage = storageBuffer
        ? vk::BufferUsageFlagBits::eStorageBuffer
        : vk::BufferUsageFlagBits::eUniformBuffer;
    usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;

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

uint32_t ShaderBuffer::GetBindlessIndex() const {
    return mBindlessIndices[mContextPtr->mFrameIndex];
}

vk::Buffer ShaderBuffer::GetHandle() const {
    return mBuffers[mContextPtr->mFrameIndex].GetHandle();
}

vk::DeviceAddress ShaderBuffer::GetDeviceAddress() const {
    return mBuffers[mContextPtr->mFrameIndex].GetDeviceAddress();
}