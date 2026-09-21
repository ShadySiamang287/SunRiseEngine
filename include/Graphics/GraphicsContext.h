#pragma once
#include <vulkan/vulkan_raii.hpp>

#define VMA_VULKAN_VERSION 1004000 // Vulkan 1.4
#include <vk_mem_alloc.h>

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

    struct BloomBuffers{
        AllocatedImage brightness;
        AllocatedImage ping;
        AllocatedImage pong;
    };

    struct FrameResources{
        vk::raii::CommandBuffer commandBuffer = nullptr;
        GBuffer gbuffer;
        AllocatedImage hdrTarget;
        BloomBuffers bloomTargets;

        vk::raii::Semaphore imageAvailable = nullptr;
        vk::raii::Fence inFlightFence = nullptr;
    };

    class GraphicsContext{
    public:
        GraphicsContext() {}

        void Init(Window* window);
        void Shutdown();

        uint32_t GetFrameIndex() {return mFrameIndex;} 

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

        void CreateFrameSyncObjects();
        void CreateSwapchainSyncObjects();

        void CreateGBuffers();
        void DestroyGBuffers();

        void CreateHDRS();
        void DestroyHDRS();

        void CreateBloomTargets();
        void DestroyBloomTargets();

        void CreateDesciptorPool();

        void TransitionImageLayoutImmediate(
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

        std::array<FrameResources, MAX_FRAMES_IN_FLIGHT> mFrames;

        vk::raii::DescriptorPool mDescriptorPool = nullptr;

        std::vector<vk::raii::Semaphore> mRenderFinishedSemaphores;
        
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
