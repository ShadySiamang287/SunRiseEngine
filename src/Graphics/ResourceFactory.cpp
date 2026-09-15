#include "Graphics/ResourceFactory.h"

#include "Graphics/GraphicsContext.h"
#include "Graphics/ShaderCache.h"

#include "Logger.h"

using namespace SUN;

ResourceFactory::ResourceFactory(GraphicsContext* context) : mGraphicsContextPtr(context) {
    mShaderCachePtr = std::make_unique<ShaderCache>(context);
    mInstancePtr = this;
}

ResourceFactory::~ResourceFactory() = default;

vk::raii::PipelineLayout ResourceFactory::CreatePipelineLayout(){
    if (!mInstancePtr) {
        Logger::Log(Logger::ERROR, "No reasource factory created!");
        return nullptr;
    }

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo = {
        .setLayoutCount = 0,
        .pushConstantRangeCount = 0
    };

    return vk::raii::PipelineLayout(mInstancePtr->mGraphicsContextPtr->mDevice, pipelineLayoutInfo);
}

vk::raii::Pipeline ResourceFactory::CreatePipeline(const PipelineConfig& config, vk::raii::PipelineLayout& layout) {
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

    vk::PipelineVertexInputStateCreateInfo   vertexInputInfo;
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = config.primitiveTopology};
    vk::PipelineViewportStateCreateInfo      viewportState{.viewportCount = 1, .scissorCount = 1};

    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable       = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode             = vk::PolygonMode::eFill,
        .cullMode                = vk::CullModeFlagBits::eBack,
        .frontFace               = vk::FrontFace::eClockwise,
        .depthBiasEnable         = vk::False,
        .lineWidth               = 1.0f
    };

    vk::PipelineMultisampleStateCreateInfo multisampling{.rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False};

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable    = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False, .logicOp = vk::LogicOp::eCopy, .attachmentCount = 1, .pAttachments = &colorBlendAttachment};

    std::vector<vk::DynamicState>      dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data()};
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
		    {.stageCount          = 2,
		     .pStages             = shaderStages,
		     .pVertexInputState   = &vertexInputInfo,
		     .pInputAssemblyState = &inputAssembly,
		     .pViewportState      = &viewportState,
		     .pRasterizationState = &rasterizer,
		     .pMultisampleState   = &multisampling,
		     .pColorBlendState    = &colorBlending,
		     .pDynamicState       = &dynamicState,
		     .layout              = layout,
		     .renderPass          = nullptr},
		    {.colorAttachmentCount = 1, .pColorAttachmentFormats = &mInstancePtr->mGraphicsContextPtr->mSwapChainSurfaceFormat.format}};

    return vk::raii::Pipeline(mInstancePtr->mGraphicsContextPtr->mDevice, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

ResourceFactory* ResourceFactory::mInstancePtr = nullptr;