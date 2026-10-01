#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "AssetManagement/Material.h"
#include "Graphics/Buffers.h"

namespace SUN {
    class Renderer3D;

    class Camera {
    public:
        glm::vec3 Position;
        glm::quat Rotation;

        float FOV = 60.f;
        float NearClip = 0.1f;
        float FarClip = 1000.f;
        float AspectRatio = 16.f / 9.f;

        glm::mat4 GetViewMatrix() const {
            return glm::inverse(
                glm::translate(glm::mat4(1.f), Position) *
                glm::mat4_cast(Rotation)
            );
        }

        glm::mat4 GetProjectionMatrix() const {
            return glm::perspective(glm::radians(FOV), AspectRatio, NearClip, FarClip);
        }
    };

    struct BoundingBox {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};

        glm::vec3 Center() const {
            return (min + max) * 0.5f;
        }

        glm::vec3 Extents() const {
            return (max - min) * 0.5f;
        }
    };

    struct Mesh {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t vertexOffset = 0;
        BoundingBox bounds;
    };

    struct RenderContext {
        Renderer3D* renderer;
        uint32_t frameIndex;
    };

    struct FrameData {
        glm::mat4 view;
        glm::mat4 proj;
        glm::mat4 inverseView;
        glm::mat4 inverseProjection;
        glm::vec4 cameraPosition;
        uint32_t directionalLightCount;
        uint32_t pointLightCount;
        uint32_t padding0;
        uint32_t padding1;
    };

    constexpr uint32_t MAX_OBJECTS = 16384;

    struct ObjectData {
        glm::mat4 model;
        glm::mat4 normal;
        MaterialID materialIndex = DEFAULT_MATERIAL_ID;
        uint32_t padding0 = 0;
        uint32_t padding1 = 0;
        uint32_t padding2 = 0;
    };

    struct DrawBatch {
        const Mesh* mesh;
        uint32_t firstInstance;
        uint32_t instanceCount;
    };

    constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 32;

    struct GPUDirectionalLight {
        glm::vec4 directionIntensity;
        glm::vec4 color;
    };

    constexpr uint32_t MAX_POINT_LIGHTS = 32;

    struct GPUPointLight {
        glm::vec4 positionRange;
        glm::vec4 colorIntensity;
    };

    struct BloomPushConstants {
        bool horizontal;
        bool padding0;
        bool padding1;
        bool padding2;
    };
}
