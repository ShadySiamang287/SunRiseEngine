#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <array>

#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>

namespace SUN{
    struct Vertex {
        glm::vec2 pos;
        glm::vec3 colour;

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            return {.binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex};
        }

        static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
        {
        return {{{.location = 0, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(Vertex, pos)},
                {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, colour)}}};
        }
    };

    struct PushConstants {
        vk::DeviceAddress frameDataAddress;
    };

    struct AllocatedImage {
        vk::Image image{nullptr};
        vk::raii::ImageView view{nullptr};
        VmaAllocation allocation{VK_NULL_HANDLE};
    };

    struct DescriptorResources {
        vk::raii::DescriptorSetLayout setLayout{nullptr};
        std::vector<vk::raii::DescriptorSet> sets;
    };
}