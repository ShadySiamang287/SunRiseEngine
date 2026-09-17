#pragma once
#include <vulkan/vulkan_raii.hpp>

#define VMA_VULKAN_VERSION 1004000 // Vulkan 1.4
#include "vk_mem_alloc.h"

#include "Graphics/vertex.h"

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

namespace SUN{
    class Window;
    class ResourceFactory;
    class ShaderCache;
    class GraphicsCommands;
    class Buffer;
    class ShaderBuffer;
    class Application;

    struct GBuffer{
        AllocatedImage Albedo;
        AllocatedImage Normal;
        AllocatedImage Depth;
    };

    class GraphicsContext{
    public:
        GraphicsContext() {}

        void Init(Window* window);
        void Shutdown();

    private:
        void CreateInstance();
        std::vector<const char*> getRequiredInstanceExtensions();

        void SetupDebugMessenger();

        void CreateSurface();

        void PickPhysicalDevice();
        bool isDeviceSuitable(vk::raii::PhysicalDevice const & physicalDevice);

        void CreateLogicalDevice();

        void CreateVMAAllocator();

        void CreateSwapchain();
        vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const &availableFormats);
        vk::PresentModeKHR ChooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes);
        vk::Extent2D ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities, const Window* window);
        uint32_t ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities);

        void RecreateSwapChain();
        void CleanupSwapChain();

        void CreateImageViews();

        void CreateCommandPool();

        void CreateCommandBuffers();

        void CreateSyncObjects();

        void CreateGBuffers();
        void DestroyGBuffers();

        void CreateDesciptorPool();

        void GraphicsContext::TransitionImageLayoutImmediate(
            vk::Image image,
            vk::ImageLayout old_layout,
            vk::ImageLayout new_layout,
            vk::AccessFlags2 src_access_mask,
            vk::AccessFlags2 dst_access_mask,
            vk::PipelineStageFlags2 src_stage_mask,
            vk::PipelineStageFlags2 dst_stage_mask,
            vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor
        );

        vk::raii::Context mContext;

        vk::raii::Instance mInstance = nullptr;

        vk::raii::PhysicalDevice mPhysicalDevice = nullptr;
        vk::raii::Device mDevice = nullptr;

        uint32_t mQueueIndex = ~0;
        vk::raii::Queue mGraphicsQueue = nullptr;

        vk::raii::SurfaceKHR mSurface = nullptr;

        Window* mWindowPtr = nullptr;
        vk::raii::SwapchainKHR mSwapChain = nullptr;
        std::vector<vk::Image> mSwapChainImages;
        std::vector<vk::raii::ImageView> mSwapChainImageViews;
        vk::SurfaceFormatKHR   mSwapChainSurfaceFormat;
        vk::Extent2D           mSwapChainExtent;
        
        vk::raii::CommandPool mCommandPool = nullptr;

        std::vector<vk::raii::CommandBuffer> mCommandBuffers;


        std::array<GBuffer, MAX_FRAMES_IN_FLIGHT> mGBuffers;
        vk::raii::DescriptorPool mDescriptorPool = nullptr;
        
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
