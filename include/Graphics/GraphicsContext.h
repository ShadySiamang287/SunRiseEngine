#pragma once
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

namespace SUN{
    class Window;

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


        vk::raii::Context mContext;
        vk::raii::Instance mInstance = nullptr;
        vk::raii::PhysicalDevice mPhysicalDevice = nullptr;
        vk::raii::Device mDevice = nullptr;
        vk::raii::Queue mGraphicsQueue = nullptr;
        vk::raii::SurfaceKHR mSurface = nullptr;

        vk::raii::DebugUtilsMessengerEXT mDebugMessenger = nullptr;
    };
}

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT       severity,
                                                      vk::DebugUtilsMessageTypeFlagsEXT              type,
                                                      const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData,
                                                      void *                                         pUserData);
