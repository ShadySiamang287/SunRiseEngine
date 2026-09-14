#pragma once
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3
#include "vk_mem_alloc.h"

namespace SUN{
    constexpr int MAX_FRAMES_IN_FLIGHT = 3;

    class Window;
    class ResourceFactory;
    class ShaderCache;

    class GraphicsContext{
    public:
        GraphicsContext() {}

        void Init(const Window* window);
        void Shutdown();

    private:
        void CreateInstance();
        std::vector<const char*> getRequiredInstanceExtensions();

        void SetupDebugMessenger();

        void CreateSurface(const Window* window);

        void PickPhysicalDevice();
        bool isDeviceSuitable(vk::raii::PhysicalDevice const & physicalDevice);

        void CreateLogicalDevice();

        void CreateVMAAllocator();

        void CreateSwapchain(const Window* window);
        vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const &availableFormats);
        vk::PresentModeKHR ChooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes);
        vk::Extent2D ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities, const Window* window);
        uint32_t ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities);

        void CreateImageViews();

        vk::raii::Context mContext;
        vk::raii::Instance mInstance = nullptr;
        vk::raii::PhysicalDevice mPhysicalDevice = nullptr;
        vk::raii::Device mDevice = nullptr;
        vk::raii::Queue mGraphicsQueue = nullptr;
        vk::raii::SurfaceKHR mSurface = nullptr;

        vk::raii::SwapchainKHR mSwapChain = nullptr;
        std::vector<vk::Image> mSwapChainImages;
        std::vector<vk::raii::ImageView> mSwapChainImageViews;
        vk::SurfaceFormatKHR   mSwapChainSurfaceFormat;
        vk::Extent2D           mSwapChainExtent;

        vk::raii::DebugUtilsMessengerEXT mDebugMessenger = nullptr;

        VmaAllocator mAllocator;

        friend ResourceFactory;
        friend ShaderCache;
    };
}

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT       severity,
                                                      vk::DebugUtilsMessageTypeFlagsEXT              type,
                                                      const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData,
                                                      void *                                         pUserData);
