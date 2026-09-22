#include "Graphics/ResourceFactory.h"

#include "Graphics/GraphicsContext.h"
#include "Graphics/ShaderCache.h"

#include "Logger.h"
#include "Graphics/vertex.h"

#include "Graphics/GraphicsCommands.h"

using namespace SUN;

ResourceFactory::ResourceFactory(GraphicsContext* context) : mGraphicsContextPtr(context) {
    mShaderCachePtr = std::make_unique<ShaderCache>(context);
    mInstancePtr = this;
}

ResourceFactory::~ResourceFactory() = default;

DescriptorResources ResourceFactory::CreateDescriptorResources(std::span<vk::DescriptorSetLayoutBinding> bindings) {
    if (!mInstancePtr) {
        Logger::Log(Logger::ERROR, "No reasource factory created!");
        return {};
    }
    DescriptorResources temp {};

    vk::DescriptorSetLayoutCreateInfo layoutInfo {
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()
    };
    temp.setLayout = vk::raii::DescriptorSetLayout(mInstancePtr->mGraphicsContextPtr->mDevice, layoutInfo);

    std::array<vk::DescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts{
        *temp.setLayout,
        *temp.setLayout
    };


    vk::DescriptorSetAllocateInfo allocInfo = {
        .descriptorPool = mInstancePtr->mGraphicsContextPtr->mDescriptorPool,
        .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts = layouts.data()
    };
    temp.sets = mInstancePtr->mGraphicsContextPtr->mDevice.allocateDescriptorSets(allocInfo);
    return std::move(temp);
}

vk::raii::PipelineLayout ResourceFactory::CreatePipelineLayout(vk::ShaderStageFlags flags, uint32_t pushConstantsSize, DescriptorResources* resources){
    if (!mInstancePtr) {
        Logger::Log(Logger::ERROR, "No reasource factory created!");
        return nullptr;
    }

    vk::PushConstantRange pushRange {
        .stageFlags = flags,
        .offset = 0,
        .size = pushConstantsSize
    };

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };

    if (resources) {
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &*resources->setLayout;
    } else {
        pipelineLayoutInfo.setLayoutCount = 0;
    }

    return vk::raii::PipelineLayout(mInstancePtr->mGraphicsContextPtr->mDevice, pipelineLayoutInfo);
}

vk::raii::Pipeline ResourceFactory::CreatePipeline(const PipelineConfig& config, vk::raii::PipelineLayout& layout, std::string debugName) {
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo = {
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = mInstancePtr->mShaderCachePtr->GetShader(config.vertexFile),
        .pName = config.vertexName.c_str()
    };
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo = {
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = mInstancePtr->mShaderCachePtr->GetShader(config.fragFile),
        .pName = config.fragName.c_str()
    };

    vk::PipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo, fragShaderStageInfo
    };

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

    vk::VertexInputBindingDescription bindingDescription;
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    if (config.useVertexInput)
    {
        bindingDescription = Vertex::getBindingDescription();

        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;

        vertexInputInfo.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions =
            attributeDescriptions.data();
    }

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = config.primitiveTopology};
    vk::PipelineViewportStateCreateInfo      viewportState{.viewportCount = 1, .scissorCount = 1};

    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable       = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode             = vk::PolygonMode::eFill,
        .cullMode                = vk::CullModeFlagBits::eBack,
        .frontFace               = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable         = vk::False,
        .lineWidth               = 1.0f
    };

    vk::PipelineMultisampleStateCreateInfo multisampling{.rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False};

    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable = vk::False,
        .depthWriteEnable = vk::False,
        .depthCompareOp = vk::CompareOp::eLess,
    };

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable    = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};


    std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachements(
        config.colorAttachmentFormats.size(),
        colorBlendAttachment
    );

    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False, .logicOp = vk::LogicOp::eCopy, .attachmentCount = static_cast<uint32_t>(colorBlendAttachements.size()), .pAttachments = colorBlendAttachements.data()};

    std::vector<vk::DynamicState>      dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eDepthTestEnable, vk::DynamicState::eDepthWriteEnable};
    vk::PipelineDynamicStateCreateInfo dynamicState{.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data()};
    
    vk::PipelineRenderingCreateInfo renderingInfo {
        .colorAttachmentCount = static_cast<uint32_t>(config.colorAttachmentFormats.size()),
        .pColorAttachmentFormats = config.colorAttachmentFormats.data(),
        .depthAttachmentFormat = config.depthAttachmentFormat,
        .stencilAttachmentFormat = config.stencilAttachmentFormat
    };

    vk::RenderingAttachmentLocationInfo locationInfo{
        .colorAttachmentCount =
            static_cast<uint32_t>(config.colorAttachmentLocations.size()),
        .pColorAttachmentLocations =
            config.colorAttachmentLocations.data()
    };

    vk::GraphicsPipelineCreateInfo graphicsInfo{
        .stageCount = 2,
        .pStages = shaderStages,

        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,

        .layout = layout,
        .renderPass = nullptr
    };

    vk::raii::Pipeline pipeline = nullptr;

    vk::StructureChain<
        vk::GraphicsPipelineCreateInfo,
        vk::RenderingAttachmentLocationInfo,
        vk::PipelineRenderingCreateInfo
        > chain{
        graphicsInfo,
        locationInfo,
        renderingInfo
    };

    pipeline = vk::raii::Pipeline(
        mInstancePtr->mGraphicsContextPtr->mDevice,
        nullptr,
        chain.get<vk::GraphicsPipelineCreateInfo>());
    

    vk::DebugUtilsObjectNameInfoEXT nameInfo {
        .objectType = vk::ObjectType::ePipeline,
        .objectHandle = reinterpret_cast<uint64_t>(static_cast<VkPipeline>(*pipeline)),
        .pObjectName = debugName.c_str()
    };
    mInstancePtr->mGraphicsContextPtr->mDevice.setDebugUtilsObjectNameEXT(nameInfo);

    return std::move(pipeline);
}

Texture2D ResourceFactory::CreateTexture2D(const void* pixels, uint32_t width, uint32_t height, vk::Format format) {
    auto* context = mInstancePtr->mGraphicsContextPtr;
    
    Texture2D texture;
    texture.mContext = context;
    texture.mFormat = format;
    texture.mExtent = {
        width,
        height
    };
    texture.mMipLevels = 1;

    const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(width) * static_cast<vk::DeviceSize>(height) * 4;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo stagingAllocationInfo{};


    vk::BufferCreateInfo stagingInfo{
        .size = imageSize,
        .usage = vk::BufferUsageFlagBits::eTransferSrc,
        .sharingMode = vk::SharingMode::eExclusive
    };

    VmaAllocationCreateInfo stagingAllocInfo{
        .flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT,

        .usage = VMA_MEMORY_USAGE_AUTO
    };

    VkResult result = vmaCreateBuffer(
        context->mAllocator,
        reinterpret_cast<const VkBufferCreateInfo*>(
            &stagingInfo
        ),
        &stagingAllocInfo,
        &stagingBuffer,
        &stagingAllocation,
        &stagingAllocationInfo
    );

    std::memcpy(
        stagingAllocationInfo.pMappedData,
        pixels,
        static_cast<size_t>(imageSize)
    );
vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = format,

        .extent = {
            width,
            height,
            1
        },

        .mipLevels = 1,
        .arrayLayers = 1,

        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,

        .usage =
            vk::ImageUsageFlagBits::eTransferDst |
            vk::ImageUsageFlagBits::eSampled,

        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo imageAllocInfo{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    };

    VkImage rawImage = VK_NULL_HANDLE;

    result = vmaCreateImage(
        context->mAllocator,
        reinterpret_cast<const VkImageCreateInfo*>(
            &imageInfo
        ),
        &imageAllocInfo,
        &rawImage,
        &texture.mImage.allocation,
        nullptr
    );

    if (result != VK_SUCCESS)
    {
        vmaDestroyBuffer(
            context->mAllocator,
            stagingBuffer,
            stagingAllocation
        );

        throw std::runtime_error(
            "Failed to create Texture2D image"
        );
    }

    texture.mImage.image = rawImage;

        context->ImmediateSubmit(
        [&](vk::raii::CommandBuffer& cmd)
        {
            vk::ImageMemoryBarrier2 toTransfer{
                .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                .srcAccessMask = {},

                .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                .dstAccessMask =vk::AccessFlagBits2::eTransferWrite,

                .oldLayout = vk::ImageLayout::eUndefined,

                .newLayout = vk::ImageLayout::eTransferDstOptimal,

                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

                .image = texture.mImage.image,

                .subresourceRange = {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            vk::DependencyInfo dependency{
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &toTransfer
            };

            cmd.pipelineBarrier2(dependency);

            vk::BufferImageCopy copyRegion{
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,

                .imageSubresource = {
                    .aspectMask =
                        vk::ImageAspectFlagBits::eColor,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                },

                .imageOffset = { 0, 0, 0 },

                .imageExtent = {
                    width,
                    height,
                    1
                }
            };

            cmd.copyBufferToImage(
                stagingBuffer,
                texture.mImage.image,
                vk::ImageLayout::eTransferDstOptimal,
                copyRegion
            );

            vk::ImageMemoryBarrier2 toShaderRead{
                .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,

                .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderRead,

                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,

                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

                .image = texture.mImage.image,

                .subresourceRange = {
                    .aspectMask =
                        vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            dependency.pImageMemoryBarriers =
                &toShaderRead;

            cmd.pipelineBarrier2(dependency);
        }
    );
}

vk::raii::Sampler ResourceFactory::CreateSampler(const SamplerConfig& config) {
    if (!mInstancePtr) {
        Logger::Log(
            Logger::ERROR,
            "No resource factory created!"
        );

        return nullptr;
    }

    vk::SamplerCreateInfo samplerInfo{
        .magFilter = config.magFilter,
        .minFilter = config.minFilter,

        .mipmapMode = config.mipmapMode,

        .addressModeU = config.addressModeU,
        .addressModeV = config.addressModeV,
        .addressModeW = config.addressModeW,

        .mipLodBias = config.mipLodBias,

        .anisotropyEnable = config.anisotropy,
        .maxAnisotropy = config.maxAnisotropy,

        .compareEnable = config.compare,
        .compareOp = config.compareOp,

        .minLod = config.minLod,
        .maxLod = config.maxLod,

        .borderColor = config.borderColor,

        .unnormalizedCoordinates = vk::False
    };

    return vk::raii::Sampler(
        mInstancePtr->mGraphicsContextPtr->mDevice,
        samplerInfo
    );
}

RenderImage ResourceFactory::CreateRenderImage(vk::Format format, vk::Extent2D extent, vk::ImageLayout layout, vk::ImageUsageFlags usageFlags, vk::ImageAspectFlags aspectFlags){
    RenderImage tempRenderImage;
    tempRenderImage.format = format;
    tempRenderImage.extent = extent;
    tempRenderImage.layout = layout;

    const vk::ImageCreateInfo createInfo {
        .sType = vk::StructureType::eImageCreateInfo,
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = usageFlags
    };

    VmaAllocationCreateInfo allocInfo {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    };

    VkImage tempImage;
    vmaCreateImage(mInstancePtr->mGraphicsContextPtr->mAllocator, reinterpret_cast<const VkImageCreateInfo*>(&createInfo), &allocInfo, &tempImage, &tempRenderImage.image.allocation, nullptr);
    tempRenderImage.image.image = tempImage;

    mInstancePtr->mGraphicsContextPtr->TransitionImageLayoutImmediate(
        tempRenderImage.image.image,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        {},
        vk::AccessFlagBits2::eShaderRead,
        vk::PipelineStageFlagBits2::eTopOfPipe,
        vk::PipelineStageFlagBits2::eFragmentShader
    );
    vk::ImageViewCreateInfo imageView {
        .image = tempRenderImage.image.image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspectFlags,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    tempRenderImage.image.view = vk::raii::ImageView(mInstancePtr->mGraphicsContextPtr->mDevice, imageView);
    return std::move(tempRenderImage);
}

ResourceFactory* ResourceFactory::mInstancePtr = nullptr;