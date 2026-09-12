#include "Graphics/GraphicsContext.h"
#include "Logger.h"

#include <vulkan/vulkan_to_string.hpp>

#include <GLFW/glfw3.h>
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

void GraphicsContext::Init(){
    CreateInstance();
    SetupDebugMessenger();
    PickPhysicalDevice();
}

void GraphicsContext::Shutdown(){
    
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