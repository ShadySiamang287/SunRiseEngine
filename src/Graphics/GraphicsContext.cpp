#include "Graphics/GraphicsContext.h"
#include "Core/Window.h"
#include "Logger.h"

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_to_string.hpp>

#include <GLFW/glfw3.h>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <limits>
#include <algorithm>

const std::vector<char const*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

std::vector<const char*> requiredDeviceExtension = {vk::KHRSwapchainExtensionName};


#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

using namespace SUN;

void GraphicsContext::Init(Window* window){
    mWindowPtr = window;
    CreateInstance();
    SetupDebugMessenger();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateVMAAllocator();
    CreateSwapchain();
    CreateImageViews();
    CreateCommandPool();
    CreateCommandBuffers();
    CreateSyncObjects();
    CreateGBuffers();
    CreateBloomTargets();
    CreateHDRS();
    CreateDesciptorPool();
}

void GraphicsContext::Shutdown(){
    mDevice.waitIdle();
    CleanupSwapChain();
    vmaDestroyAllocator(mAllocator);
}

void GraphicsContext::CreateInstance(){
    vk::ApplicationInfo appInfo {
        .pApplicationName = "GameApp",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = vk::ApiVersion14
    };

    std::vector<char const*> requiredLayers;
    if (enableValidationLayers)
        {
            requiredLayers.assign(validationLayers.begin(), validationLayers.end());
        }

    // Check if the required layers are supported by the Vulkan implementation.
    auto layerProperties = mContext.enumerateInstanceLayerProperties();
    auto unsupportedLayerIt = std::ranges::find_if(requiredLayers,
                                                   [&layerProperties](auto const &requiredLayer) {
                                                       return std::ranges::none_of(layerProperties,
                                                                                   [requiredLayer](auto const &layerProperty) { return strcmp(layerProperty.layerName, requiredLayer) == 0; });
                                                   });
    if (unsupportedLayerIt != requiredLayers.end())
    {
        throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));
    }

    // Get the required extensions.
    auto requiredExtensions = getRequiredInstanceExtensions();

    // Check if the required extensions are supported by the Vulkan implementation.
    auto extensionProperties = mContext.enumerateInstanceExtensionProperties();
		auto unsupportedPropertyIt =
		    std::ranges::find_if(requiredExtensions,
		                         [&extensionProperties](auto const &requiredExtension) {
			                         return std::ranges::none_of(extensionProperties,
			                                                     [requiredExtension](auto const &extensionProperty) { return strcmp(extensionProperty.extensionName, requiredExtension) == 0; });
		                         });
		if (unsupportedPropertyIt != requiredExtensions.end())
		{
			throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));
		}

    vk::InstanceCreateInfo createInfo {
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
        .ppEnabledLayerNames = requiredLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
        .ppEnabledExtensionNames = requiredExtensions.data()
    };

    mInstance = vk::raii::Instance(mContext, createInfo);
}

std::vector<const char*> GraphicsContext::getRequiredInstanceExtensions(){
    uint32_t glfwExtensionCount = 0;
    auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (enableValidationLayers)
    {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
    }

    return extensions;
}

void GraphicsContext::SetupDebugMessenger() {
    if (!enableValidationLayers) return;
    vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                                        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    vk::DebugUtilsMessageTypeFlagsEXT     messageTypeFlags(
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
    vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{.messageSeverity = severityFlags,
                                                                          .messageType     = messageTypeFlags,
                                                                          .pfnUserCallback = &debugCallback};
    mDebugMessenger = mInstance.createDebugUtilsMessengerEXT( debugUtilsMessengerCreateInfoEXT );
}

void GraphicsContext::CreateSurface() {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(*mInstance, mWindowPtr->mWindowPtr, nullptr, &surface) != 0) {
        throw std::runtime_error("Failed to create window surface");
    }
    mSurface = vk::raii::SurfaceKHR(mInstance, surface);
}

void GraphicsContext::PickPhysicalDevice() {
    std::vector<vk::raii::PhysicalDevice> physicalDevices = mInstance.enumeratePhysicalDevices();
    auto const devIter = std::ranges::find_if( physicalDevices, [&]( auto const & physicalDevice ) { return isDeviceSuitable( physicalDevice ); } );
    if ( devIter == physicalDevices.end() )
    {
    throw std::runtime_error( "failed to find a suitable GPU!" );
    }
    mPhysicalDevice = *devIter;
}

bool GraphicsContext::isDeviceSuitable(vk::raii::PhysicalDevice const & physicalDevice){
    bool supportsVulkan14 = physicalDevice.getProperties2().properties.apiVersion >= vk::ApiVersion14;
    auto queueFamiles = physicalDevice.getQueueFamilyProperties();
    bool supportsGraphics = std::ranges::any_of(queueFamiles, [](auto const& qfp) {
        return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
    });

    auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    bool supportsAllRequiredExtensions =
    std::ranges::all_of( requiredDeviceExtension,
        [&availableDeviceExtensions]( auto const & requiredDeviceExtension )
        {
            return std::ranges::any_of( availableDeviceExtensions,
                                        [requiredDeviceExtension]( auto const & availableDeviceExtension )
                                        { return strcmp( availableDeviceExtension.extensionName, requiredDeviceExtension ) == 0; } );
        } );

    auto features = physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2,
                                                        vk::PhysicalDeviceVulkan11Features,
                                                        vk::PhysicalDeviceVulkan12Features,
                                                        vk::PhysicalDeviceVulkan13Features>();
    bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
                            features.template get<vk::PhysicalDeviceVulkan12Features>().descriptorIndexing &&
                            features.template get<vk::PhysicalDeviceVulkan12Features>().shaderSampledImageArrayNonUniformIndexing &&
                            features.template get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingUpdateUnusedWhilePending &&
                            features.template get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingPartiallyBound&&
                            features.template get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingVariableDescriptorCount &&
                            features.template get<vk::PhysicalDeviceVulkan12Features>().runtimeDescriptorArray &&
                            features.template get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress &&
                            features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
                            features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2;

    return supportsVulkan14 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
}

void GraphicsContext::CreateLogicalDevice(){
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties = mPhysicalDevice.getQueueFamilyProperties();

    for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
    {
        if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
            mPhysicalDevice.getSurfaceSupportKHR(qfpIndex, *mSurface))
        {
            // found a queue family that supports both graphics and present
            mQueueIndex = qfpIndex;
            break;
        }
    }
    if (mQueueIndex == ~0)
    {
        throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
    }

    vk::StructureChain<vk::PhysicalDeviceFeatures2,
                    vk::PhysicalDeviceVulkan11Features,
                    vk::PhysicalDeviceVulkan12Features,
                    vk::PhysicalDeviceVulkan13Features>
        featureChain = {
            {},                                    // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters = true},        // vk::PhysicalDeviceVulkan11Features
            {
                .descriptorIndexing = true,
                .shaderSampledImageArrayNonUniformIndexing = true,
                .descriptorBindingUpdateUnusedWhilePending = true,
                .descriptorBindingPartiallyBound = true,
                .descriptorBindingVariableDescriptorCount = true,
                .runtimeDescriptorArray = true,
                .bufferDeviceAddress = true,
            },                                     
            {
                .synchronization2 = true,
                .dynamicRendering = true,
            },            // vk::PhysicalDeviceVulkan13Features
        };
    
    float                     queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{.queueFamilyIndex = mQueueIndex, .queueCount = 1, .pQueuePriorities = &queuePriority};
    vk::DeviceCreateInfo      deviceCreateInfo{.pNext                   = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
                                               .queueCreateInfoCount    = 1,
                                               .pQueueCreateInfos       = &deviceQueueCreateInfo,
                                               .enabledExtensionCount   = static_cast<uint32_t>(requiredDeviceExtension.size()),
                                               .ppEnabledExtensionNames = requiredDeviceExtension.data()}; 

    mDevice = vk::raii::Device(mPhysicalDevice, deviceCreateInfo);
    mGraphicsQueue = vk::raii::Queue(mDevice, mQueueIndex, 0);
}

void GraphicsContext::CreateVMAAllocator(){
    VmaVulkanFunctions vulkanFunctions = {
        .vkGetInstanceProcAddr = mInstance.getDispatcher()->vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = mDevice.getDispatcher()->vkGetDeviceProcAddr,
    };

    VmaAllocatorCreateInfo allocatorCreateInfo = {
        .flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = *mPhysicalDevice,
        .device = *mDevice,
        .pVulkanFunctions = &vulkanFunctions,
        .instance = *mInstance,
        .vulkanApiVersion = VK_API_VERSION_1_4,
    };

    vmaCreateAllocator(&allocatorCreateInfo, &mAllocator);
}

void GraphicsContext::CreateSwapchain(){
    vk::SurfaceCapabilitiesKHR surfaceCapabilities = mPhysicalDevice.getSurfaceCapabilitiesKHR( *mSurface );
    mSwapChainExtent                               = ChooseSwapExtent(surfaceCapabilities, mWindowPtr);
    uint32_t minImageCount                         = ChooseSwapMinImageCount(surfaceCapabilities);

    std::vector<vk::SurfaceFormatKHR> availableFormats = mPhysicalDevice.getSurfaceFormatsKHR(*mSurface);
    mSwapChainSurfaceFormat                            = ChooseSwapSurfaceFormat(availableFormats);

    std::vector<vk::PresentModeKHR> availablePresentModes = mPhysicalDevice.getSurfacePresentModesKHR(*mSurface);

    vk::SwapchainCreateInfoKHR swapChainCreateInfo{
                                               .surface          = *mSurface,
                                               .minImageCount    = minImageCount,
                                               .imageFormat      = mSwapChainSurfaceFormat.format,
                                               .imageColorSpace  = mSwapChainSurfaceFormat.colorSpace,
                                               .imageExtent      = mSwapChainExtent,
                                               .imageArrayLayers = 1,
                                               .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
                                               .imageSharingMode = vk::SharingMode::eExclusive,
                                               .preTransform     = surfaceCapabilities.currentTransform,
                                               .compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                               .presentMode      = ChooseSwapPresentMode(availablePresentModes),
                                               .clipped          = true
    };

    mSwapChain = vk::raii::SwapchainKHR(mDevice, swapChainCreateInfo);
    mSwapChainImages = mSwapChain.getImages();
}

vk::SurfaceFormatKHR GraphicsContext::ChooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const &availableFormats) {
    const auto formatIt = std::ranges::find_if(
        availableFormats,
        [](const auto &format) { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}

vk::PresentModeKHR GraphicsContext::ChooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes) {
    assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
    return std::ranges::any_of(availablePresentModes,
                               [](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ?
               vk::PresentModeKHR::eMailbox :
               vk::PresentModeKHR::eFifo;
}

vk::Extent2D GraphicsContext::ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities, const Window* window) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }
    int width, height;
    glfwGetFramebufferSize(window->mWindowPtr, &width, &height);

    return {
        std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
    };
}

uint32_t GraphicsContext::ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities)
{
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
    {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}

void GraphicsContext::RecreateSwapChain() {
    mDevice.waitIdle();
    CleanupSwapChain();

    CreateSwapchain();
    CreateImageViews();
    CreateGBuffers();
    CreateBloomTargets();
    CreateHDRS();
}

void GraphicsContext::CleanupSwapChain() {
    DestroyHDRS();
    DestroyBloomTargets();
    DestroyGBuffers();
    mSwapChainImageViews.clear();
    mSwapChain = nullptr;
}

void GraphicsContext::CreateImageViews() {
    assert(mSwapChainImageViews.empty());

    vk::ImageViewCreateInfo imageViewCreateInfo{ 
        .viewType         = vk::ImageViewType::e2D,
        .format           = mSwapChainSurfaceFormat.format,
        .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
    };
    imageViewCreateInfo.components = {
        vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity
    };
    imageViewCreateInfo.subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1};        
    
    for (const auto &image : mSwapChainImages)
    {
        imageViewCreateInfo.image = image;
        mSwapChainImageViews.emplace_back( mDevice, imageViewCreateInfo );
    }
}

void GraphicsContext::CreateCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{
        .flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = mQueueIndex
    };

    mCommandPool = vk::raii::CommandPool(mDevice, poolInfo);
}

void GraphicsContext::CreateCommandBuffers() {
    vk::CommandBufferAllocateInfo allocInfo{ 
        .commandPool = mCommandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT
    };

    mCommandBuffers = vk::raii::CommandBuffers(mDevice, allocInfo);
}

void GraphicsContext::CreateSyncObjects() {
    assert(mPresentCompleteSemaphores.empty() && mRenderFinishedSemaphores.empty() && mInFlightFences.empty());

    for (int i = 0 ; i < mSwapChainImages.size(); i++){ 
        mRenderFinishedSemaphores.emplace_back(mDevice, vk::SemaphoreCreateInfo());
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        mPresentCompleteSemaphores.emplace_back(mDevice, vk::SemaphoreCreateInfo());
        mInFlightFences.emplace_back(mDevice, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    } 
}

void GraphicsContext::CreateGBuffers() {
    const vk::ImageCreateInfo albedo {
        .sType = vk::StructureType::eImageCreateInfo,
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent = {mSwapChainExtent.width, mSwapChainExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
    };

    const vk::ImageCreateInfo normal {
        .sType = vk::StructureType::eImageCreateInfo,
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent = {mSwapChainExtent.width, mSwapChainExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
    };

    const vk::ImageCreateInfo depth {
        .sType = vk::StructureType::eImageCreateInfo,
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eD32Sfloat,
        .extent = {mSwapChainExtent.width, mSwapChainExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled
    };

    VmaAllocationCreateInfo allocInfo {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    };

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        GBuffer& buffer = mGBuffers[i];
        VkImage tempAlbedo;
        vmaCreateImage(mAllocator, reinterpret_cast<const VkImageCreateInfo*>(&albedo), &allocInfo, &tempAlbedo, &buffer.Albedo.allocation, nullptr);
        buffer.Albedo.image = tempAlbedo;

        TransitionImageLayoutImmediate(
            buffer.Albedo.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            {},                                            // no prior access to wait on
            vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eFragmentShader
        );

        vk::ImageViewCreateInfo albedoView{
            .image = buffer.Albedo.image,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR16G16B16A16Sfloat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        buffer.Albedo.view = vk::raii::ImageView(mDevice, albedoView);

        VkImage tempNormal;
        vmaCreateImage(mAllocator, reinterpret_cast<const VkImageCreateInfo*>(&normal), &allocInfo, &tempNormal, &buffer.Normal.allocation, nullptr);
        buffer.Normal.image = tempNormal;

        TransitionImageLayoutImmediate(
            buffer.Normal.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            {},                                            // no prior access to wait on
            vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eFragmentShader
        );

        vk::ImageViewCreateInfo normalView{
            .image = buffer.Normal.image,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR16G16B16A16Sfloat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        buffer.Normal.view = vk::raii::ImageView(mDevice, normalView);

        VkImage tempDepth;
        vmaCreateImage(mAllocator, reinterpret_cast<const VkImageCreateInfo*>(&depth), &allocInfo, &tempDepth, &buffer.Depth.allocation, nullptr);
        buffer.Depth.image = tempDepth;

        TransitionImageLayoutImmediate(
            buffer.Depth.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            {},                                            // no prior access to wait on
            vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::ImageAspectFlagBits::eDepth
        );


        vk::ImageViewCreateInfo depthView{
            .image = buffer.Depth.image,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eD32Sfloat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        buffer.Depth.view = vk::raii::ImageView(mDevice, depthView);
        
    }
}

void GraphicsContext::DestroyGBuffers() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){
        GBuffer& buffer = mGBuffers[i];
        buffer.Albedo.view = nullptr;
        buffer.Normal.view = nullptr;
        buffer.Depth.view = nullptr;
        vmaDestroyImage(mAllocator, buffer.Albedo.image, buffer.Albedo.allocation);
        vmaDestroyImage(mAllocator, buffer.Normal.image, buffer.Normal.allocation);
        vmaDestroyImage(mAllocator, buffer.Depth.image, buffer.Depth.allocation);
    }
}

void GraphicsContext::CreateHDRS() {
    const vk::ImageCreateInfo hdrInfo {
        .sType = vk::StructureType::eImageCreateInfo,
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent = {mSwapChainExtent.width, mSwapChainExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
    };

    VmaAllocationCreateInfo allocInfo {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    };

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        AllocatedImage& hdr = mHDRTargets[i];
        VkImage tempHDR;
        vmaCreateImage(mAllocator, reinterpret_cast<const VkImageCreateInfo*>(&hdrInfo), &allocInfo, &tempHDR, &hdr.allocation, nullptr);
        hdr.image = tempHDR;

        TransitionImageLayoutImmediate(
            hdr.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            {},                                            // no prior access to wait on
            vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eFragmentShader
        );

        vk::ImageViewCreateInfo hdrView{
            .image = hdr.image,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR16G16B16A16Sfloat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        hdr.view = vk::raii::ImageView(mDevice, hdrView);
    }
}

void GraphicsContext::DestroyHDRS() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){
        AllocatedImage& hdrImage = mHDRTargets[i];
        hdrImage.view = nullptr;
        vmaDestroyImage(mAllocator, hdrImage.image, hdrImage.allocation);
    }
}

void GraphicsContext::CreateBloomTargets(){
    const vk::ImageCreateInfo targetInfo {
        .sType = vk::StructureType::eImageCreateInfo,
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent = {mSwapChainExtent.width, mSwapChainExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
    };

    VmaAllocationCreateInfo allocInfo {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    };

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){ 
        BloomBuffers& bloomBuffer = mBloomTargets[i];
        VkImage tempBright;
        vmaCreateImage(mAllocator, reinterpret_cast<const VkImageCreateInfo*>(&targetInfo), &allocInfo, &tempBright, &bloomBuffer.brightness.allocation, nullptr);
        bloomBuffer.brightness.image = tempBright;

        TransitionImageLayoutImmediate(
            bloomBuffer.brightness.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            {},                                            // no prior access to wait on
            vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eFragmentShader
        );

        vk::ImageViewCreateInfo bloomView{
            .image = bloomBuffer.brightness.image,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR16G16B16A16Sfloat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        bloomBuffer.brightness.view = vk::raii::ImageView(mDevice, bloomView);

        VkImage tempPing;
        vmaCreateImage(mAllocator, reinterpret_cast<const VkImageCreateInfo*>(&targetInfo), &allocInfo, &tempPing, &bloomBuffer.ping.allocation, nullptr);
        bloomBuffer.ping.image = tempPing;

        TransitionImageLayoutImmediate(
            bloomBuffer.ping.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            {},                                            // no prior access to wait on
            vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eFragmentShader
        );

        vk::ImageViewCreateInfo pingView{
            .image = bloomBuffer.ping.image,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR16G16B16A16Sfloat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        bloomBuffer.ping.view = vk::raii::ImageView(mDevice, pingView);

        VkImage tempPong;
        vmaCreateImage(mAllocator, reinterpret_cast<const VkImageCreateInfo*>(&targetInfo), &allocInfo, &tempPong, &bloomBuffer.pong.allocation, nullptr);
        bloomBuffer.pong.image = tempPong;

        TransitionImageLayoutImmediate(
            bloomBuffer.pong.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            {},                                            // no prior access to wait on
            vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eFragmentShader
        );

        vk::ImageViewCreateInfo pongView{
            .image = bloomBuffer.pong.image,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR16G16B16A16Sfloat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        bloomBuffer.pong.view = vk::raii::ImageView(mDevice, pongView);
    }
}

void GraphicsContext::DestroyBloomTargets() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){
        BloomBuffers& buffer = mBloomTargets[i];
        buffer.brightness.view = nullptr;
        buffer.ping.view = nullptr;
        buffer.pong.view = nullptr;
        vmaDestroyImage(mAllocator, buffer.brightness.image, buffer.brightness.allocation);
        vmaDestroyImage(mAllocator, buffer.ping.image, buffer.ping.allocation);
        vmaDestroyImage(mAllocator, buffer.pong.image, buffer.pong.allocation);
    }
}

void GraphicsContext::CreateDesciptorPool(){
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        { vk::DescriptorType::eSampledImage, 64 },
        { vk::DescriptorType::eSampler,      16 }
    };

    vk::DescriptorPoolCreateInfo poolInfo {
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 64,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };
    mDescriptorPool = vk::raii::DescriptorPool(mDevice, poolInfo);
}

void GraphicsContext::TransitionImageLayoutImmediate(
    vk::Image image,
    vk::ImageLayout old_layout,
    vk::ImageLayout new_layout,
    vk::AccessFlags2 src_access_mask,
    vk::AccessFlags2 dst_access_mask,
    vk::PipelineStageFlags2 src_stage_mask,
    vk::PipelineStageFlags2 dst_stage_mask,
    vk::ImageAspectFlags aspectMask
) {
    // 1. Allocate a temporary command buffer from your existing pool
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *mCommandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };
    vk::raii::CommandBuffers tempBuffers(mDevice, allocInfo);
    vk::raii::CommandBuffer& cmd = tempBuffers.front();

    // 2. Begin, one-time-submit
    cmd.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });

    // 3. Record the barrier
    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspectMask,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    vk::DependencyInfo dependency{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };
    cmd.pipelineBarrier2(dependency);

    // 4. End and submit
    cmd.end();

    vk::SubmitInfo submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &*cmd
    };
    mGraphicsQueue.submit(submitInfo, nullptr);

    // 5. Wait for it to finish before returning — this is a one-shot setup call,
    //    so blocking here is fine (don't do this per-frame!)
    mGraphicsQueue.waitIdle();

    // tempBuffers goes out of scope here and frees itself
}

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT       severity,
                                                      vk::DebugUtilsMessageTypeFlagsEXT              type,
                                                      const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData,
                                                      void *                                         pUserData)
{
    switch (severity) {
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
            Logger::Log(Logger::ERROR, " - {}, validation layer: {}, message name: {}, message: {}",pCallbackData->messageIdNumber,  vk::to_string(type), pCallbackData->pMessageIdName, pCallbackData->pMessage);
            break;
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
            Logger::Log(Logger::WARNING, " - {}, validation layer: {}, message name: {}, message: {}",pCallbackData->messageIdNumber,  vk::to_string(type), pCallbackData->pMessageIdName, pCallbackData->pMessage);
            break;
        default:
            return vk::False;
    }
  return vk::False;
}