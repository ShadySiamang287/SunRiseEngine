#include "Graphics/ResourceFactory.h"

#include "Graphics/GraphicsContext.h"
#include "Graphics/ShaderCache.h"

#include "Logger.h"
#include "Graphics/vertex.h"

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

vk::raii::PipelineLayout ResourceFactory::CreatePipelineLayout(DescriptorResources* resources){
    if (!mInstancePtr) {
        Logger::Log(Logger::ERROR, "No reasource factory created!");
        return nullptr;
    }

    vk::PushConstantRange pushRange {
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(PushConstants)
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

    if (!config.inputAttachmentIndices.empty()) {
        vk::RenderingInputAttachmentIndexInfo inputInfo{
            .colorAttachmentCount =
                static_cast<uint32_t>(config.inputAttachmentIndices.size()),
            .pColorAttachmentInputIndices =
                config.inputAttachmentIndices.data()
        };

        vk::StructureChain<
            vk::GraphicsPipelineCreateInfo,
            vk::PipelineRenderingCreateInfo,
            vk::RenderingAttachmentLocationInfo,
            vk::RenderingInputAttachmentIndexInfo
        > chain{
            graphicsInfo,
            renderingInfo,
            locationInfo,
            inputInfo
        };

        pipeline = vk::raii::Pipeline(
            mInstancePtr->mGraphicsContextPtr->mDevice,
            nullptr,
            chain.get<vk::GraphicsPipelineCreateInfo>());
    } else {
        vk::StructureChain<
            vk::GraphicsPipelineCreateInfo,
            vk::PipelineRenderingCreateInfo,
            vk::RenderingAttachmentLocationInfo
        > chain{
            graphicsInfo,
            renderingInfo,
            locationInfo
        };

        pipeline = vk::raii::Pipeline(
            mInstancePtr->mGraphicsContextPtr->mDevice,
            nullptr,
            chain.get<vk::GraphicsPipelineCreateInfo>());
    }

    vk::DebugUtilsObjectNameInfoEXT nameInfo {
        .objectType = vk::ObjectType::ePipeline,
        .objectHandle = reinterpret_cast<uint64_t>(static_cast<VkPipeline>(*pipeline)),
        .pObjectName = debugName.c_str()
    };
    mInstancePtr->mGraphicsContextPtr->mDevice.setDebugUtilsObjectNameEXT(nameInfo);

    return std::move(pipeline);
}

ResourceFactory* ResourceFactory::mInstancePtr = nullptr;