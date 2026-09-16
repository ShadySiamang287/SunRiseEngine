#pragma once
#include <vulkan/vulkan_raii.hpp>

#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3
#include "vk_mem_alloc.h"

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

namespace SUN{
    class Window;
    class ResourceFactory;
    class ShaderCache;
    class GraphicsCommands;
    class Buffer;
    class ShaderBuffer;
    class Application;

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

        void CreateCommandPool();

        void CreateCommandBuffers();

        void CreateSyncObjects();

        vk::raii::Context mContext;

        vk::raii::Instance mInstance = nullptr;

        vk::raii::PhysicalDevice mPhysicalDevice = nullptr;
        vk::raii::Device mDevice = nullptr;

        uint32_t mQueueIndex = ~0;
        vk::raii::Queue mGraphicsQueue = nullptr;

        vk::raii::SurfaceKHR mSurface = nullptr;

        vk::raii::SwapchainKHR mSwapChain = nullptr;
        std::vector<vk::Image> mSwapChainImages;
        std::vector<vk::raii::ImageView> mSwapChainImageViews;
        vk::SurfaceFormatKHR   mSwapChainSurfaceFormat;
        vk::Extent2D           mSwapChainExtent;
        
        vk::raii::CommandPool mCommandPool = nullptr;

        std::vector<vk::raii::CommandBuffer> mCommandBuffers;

        
        std::vector<vk::raii::Semaphore> mPresentCompleteSemaphores;
        std::vector<vk::raii::Semaphore> mRenderFinishedSemaphores;
        std::vector<vk::raii::Fence> mInFlightFences;
        
        vk::raii::DebugUtilsMessengerEXT mDebugMessenger = nullptr;
        VmaAllocator mAllocator;

        uint32_t mFrameIndex = 0;
        uint32_t mImageIndex;

        friend ResourceFactory;
        friend ShaderCache;
        friend GraphicsCommands;
        friend Buffer;
        friend ShaderBuffer;
        friend Application;
    };
}

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT       severity,
                                                      vk::DebugUtilsMessageTypeFlagsEXT              type,
                                                      const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData,
                                                      void *                                         pUserData);
