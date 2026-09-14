#include "Graphics/GraphicsContext.h"
#include "Graphics/Window.h"
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

void GraphicsContext::Init(const Window* window){
    CreateInstance();
    SetupDebugMessenger();
    CreateSurface(window);
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapchain(window);
    CreateImageViews();
}

void GraphicsContext::Shutdown(){
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

void GraphicsContext::CreateSurface(const Window* window) {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(*mInstance, window->mWindowPtr, nullptr, &surface) != 0) {
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
    bool supportsVulkan13 = physicalDevice.getProperties2().properties.apiVersion >= vk::ApiVersion13;
    auto queueFamiles = physicalDevice.getQueueFamilyProperties();
    bool supportsGraphics = std::ranges::any_of(queueFamiles, [](auto const& qfp) {
        return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
    });

    std::vector<const char*> requiredDeviceExtension = {vk::KHRSwapchainExtensionName};

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
                                                        vk::PhysicalDeviceVulkan13Features,
                                                        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
                            features.template get<vk::PhysicalDeviceVulkan12Features>().shaderSampledImageArrayNonUniformIndexing &&
                            features.template get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingPartiallyBound&&
                            features.template get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingVariableDescriptorCount &&
                            features.template get<vk::PhysicalDeviceVulkan12Features>().runtimeDescriptorArray &&
                            features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
                            features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
                            features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

    return supportsVulkan13 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
}

void GraphicsContext::CreateLogicalDevice(){
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties = mPhysicalDevice.getQueueFamilyProperties();

    uint32_t queueIndex = ~0;
    for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
    {
        if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
            mPhysicalDevice.getSurfaceSupportKHR(qfpIndex, *mSurface))
        {
            // found a queue family that supports both graphics and present
            queueIndex = qfpIndex;
            break;
        }
    }
    if (queueIndex == ~0)
    {
        throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
    }

    vk::StructureChain<vk::PhysicalDeviceFeatures2,
                    vk::PhysicalDeviceVulkan11Features,
                    vk::PhysicalDeviceVulkan12Features,
                    vk::PhysicalDeviceVulkan13Features,
                    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
        featureChain = {
            {},                                    // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters = true},        // vk::PhysicalDeviceVulkan11Features
            {
                .shaderSampledImageArrayNonUniformIndexing = true,
                .descriptorBindingPartiallyBound = true,
                .descriptorBindingVariableDescriptorCount = true,
                .runtimeDescriptorArray = true
            },                                     
            {
                .synchronization2 = true,
                .dynamicRendering = true,
            },            // vk::PhysicalDeviceVulkan13Features
            {.extendedDynamicState = true}         // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        };
    
    float                     queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{.queueFamilyIndex = queueIndex, .queueCount = 1, .pQueuePriorities = &queuePriority};
    vk::DeviceCreateInfo      deviceCreateInfo{.pNext                   = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
                                               .queueCreateInfoCount    = 1,
                                               .pQueueCreateInfos       = &deviceQueueCreateInfo,
                                               .enabledExtensionCount   = static_cast<uint32_t>(requiredDeviceExtension.size()),
                                               .ppEnabledExtensionNames = requiredDeviceExtension.data()}; 

    mDevice = vk::raii::Device(mPhysicalDevice, deviceCreateInfo);
    mGraphicsQueue = vk::raii::Queue(mDevice, queueIndex, 0);
}

void GraphicsContext::CreateVMAAllocator(){
    VmaVulkanFunctions vulkanFunctions = {
        .vkGetInstanceProcAddr = mInstance.getDispatcher()->vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = mDevice.getDispatcher()->vkGetDeviceProcAddr,
    };

    VmaAllocatorCreateInfo allocatorCreateInfo = {
        .flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT,
        .vulkanApiVersion = VK_API_VERSION_1_3,
        .physicalDevice = *mPhysicalDevice,
        .device = *mDevice,
        .instance = *mInstance,
        .pVulkanFunctions = &vulkanFunctions
    };

    vmaCreateAllocator(&allocatorCreateInfo, &mAllocator);
}

void GraphicsContext::CreateSwapchain(const Window* window){
    vk::SurfaceCapabilitiesKHR surfaceCapabilities = mPhysicalDevice.getSurfaceCapabilitiesKHR( *mSurface );
    mSwapChainExtent                               = ChooseSwapExtent(surfaceCapabilities, window);
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