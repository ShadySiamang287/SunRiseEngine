#pragma once 

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Graphics/Buffers.h"

namespace SUN {
    class Renderer3D;

    class Camera{
    public:
        glm::vec3 Position;
        glm::quat Rotation;

        float FOV = 60.f;
        float NearClip = 0.1f;
        float FarClip = 1000.f;

        float AspectRatio = 16.f / 9.f;

        glm::mat4 GetViewMatrix() const{
            return glm::inverse(
                glm::translate(glm::mat4(1.f), Position) *
                glm::mat4_cast(Rotation)
            );
        }

        glm::mat4 GetProjectionMatrix() const{       
        return glm::perspective(
                glm::radians(FOV),
                AspectRatio,
                NearClip,
                FarClip
            );
        }
    };

    struct Mesh{
        GeometryBuffer buffer; 
    };

    struct RenderContext {
        Renderer3D* renderer;
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

    struct ObjectData{
        glm::mat4 model;
        glm::mat4 normal;
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
}