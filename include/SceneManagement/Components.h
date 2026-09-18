#pragma once

#include <string>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Renderer/RenderingStructs.h"

namespace SUN{
    struct TagComponent
    {
        std::string Tag;
    };

    struct TransformComponent
    {
        glm::vec3 Position{0.f};
        glm::quat Rotation{1.f, 0.f, 0.f, 0.f};
        glm::vec3 Scale{1.0f};

        glm::mat4 GetTransform() const {
            return glm::translate(glm::mat4(1.f), Position) 
                 * glm::mat4_cast(Rotation)
                 * glm::scale(glm::mat4(1.f), Scale);
        }
    };

    struct CameraComponent {
        Camera Camera;
        bool Primary = true;
    };

    struct MeshComponent {
        Mesh mesh;
    };
}