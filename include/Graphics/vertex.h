#pragma once

#include <array>

#include <glm/glm.hpp>

#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>

namespace SUN {
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec2 uv{0.0f};
        glm::vec4 tangent;

        static vk::VertexInputBindingDescription getBindingDescription() {
            return {.binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex};
        }

        static std::array<vk::VertexInputAttributeDescription, 4> getAttributeDescriptions() {
            return {{
                {.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, pos)},
                {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, normal)},
                {.location = 2, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(Vertex, uv)},
                {.location = 3, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat, .offset = offsetof(Vertex, tangent)}
            }};
        }
    };

    struct PushConstants {
        vk::DeviceAddress frameDataAddress;
        vk::DeviceAddress objectDataAddress;
        vk::DeviceAddress materialDataAddress;
        vk::DeviceAddress directionalLightDataAddress;
        vk::DeviceAddress pointLightDataAddress;
    };

    struct CullPushConstants {
        vk::DeviceAddress frameDataAddress;
        vk::DeviceAddress objectDataAddress;
        vk::DeviceAddress indirectCommandAddress;
        uint32_t objectCount = 0;
        uint32_t padding = 0;
    };

    static_assert(sizeof(CullPushConstants) == 32);

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
