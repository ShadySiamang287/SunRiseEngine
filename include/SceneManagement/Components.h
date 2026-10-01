#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "AssetManagement/Material.h"
#include "Renderer/RenderingStructs.h"

namespace SUN {
    struct TagComponent {
        std::string Tag;
    };

    struct TransformComponent {
        glm::vec3 Position{0.f};
        glm::quat Rotation{1.f, 0.f, 0.f, 0.f};
        glm::vec3 Scale{1.0f};

        const glm::mat4& GetTransform() const {
            UpdateCache();
            return mCachedTransform;
        }

        const glm::mat4& GetNormalMatrix() const {
            UpdateCache();
            return mCachedNormalMatrix;
        }

        uint64_t GetTransformVersion() const {
            UpdateCache();
            return mTransformVersion;
        }

    private:
        bool TransformChanged() const {
            return
                !mCacheValid ||
                Position.x != mCachedPosition.x ||
                Position.y != mCachedPosition.y ||
                Position.z != mCachedPosition.z ||
                Rotation.w != mCachedRotation.w ||
                Rotation.x != mCachedRotation.x ||
                Rotation.y != mCachedRotation.y ||
                Rotation.z != mCachedRotation.z ||
                Scale.x != mCachedScale.x ||
                Scale.y != mCachedScale.y ||
                Scale.z != mCachedScale.z;
        }

        void UpdateCache() const {
            if (!TransformChanged()) {
                return;
            }

            mCachedPosition = Position;
            mCachedRotation = Rotation;
            mCachedScale = Scale;

            mCachedTransform =
                glm::translate(
                    glm::mat4(1.0f),
                    Position
                ) *
                glm::mat4_cast(Rotation) *
                glm::scale(
                    glm::mat4(1.0f),
                    Scale
                );

            mCachedNormalMatrix = glm::mat4(
                glm::transpose(
                    glm::inverse(
                        glm::mat3(mCachedTransform)
                    )
                )
            );

            mCacheValid = true;
            ++mTransformVersion;
        }

        mutable bool mCacheValid = false;
        mutable uint64_t mTransformVersion = 0;

        mutable glm::vec3 mCachedPosition{0.0f};
        mutable glm::quat mCachedRotation{
            1.0f,
            0.0f,
            0.0f,
            0.0f
        };
        mutable glm::vec3 mCachedScale{1.0f};

        mutable glm::mat4 mCachedTransform{1.0f};
        mutable glm::mat4 mCachedNormalMatrix{1.0f};
    };

    struct CameraComponent {
        Camera Camera;
        bool Primary = true;
    };

    struct MeshComponent {
        std::shared_ptr<Mesh> mesh;
        glm::mat4 LocalTransform{1.0f};

        const glm::mat4& GetWorldTransform(
            const TransformComponent& transform
        ) const {
            UpdateTransformCache(transform);
            return mCachedWorldTransform;
        }

        const glm::mat4& GetWorldNormalMatrix(
            const TransformComponent& transform
        ) const {
            UpdateTransformCache(transform);
            return mCachedWorldNormalMatrix;
        }

    private:
        static bool MatricesEqual(
            const glm::mat4& lhs,
            const glm::mat4& rhs
        ) {
            for (uint32_t column = 0; column < 4; ++column) {
                for (uint32_t row = 0; row < 4; ++row) {
                    if (lhs[column][row] != rhs[column][row]) {
                        return false;
                    }
                }
            }

            return true;
        }

        void UpdateTransformCache(
            const TransformComponent& transform
        ) const {
            const uint64_t transformVersion =
                transform.GetTransformVersion();

            const bool localChanged =
                !mTransformCacheValid ||
                !MatricesEqual(
                    LocalTransform,
                    mCachedLocalTransform
                );

            if (
                mTransformCacheValid &&
                !localChanged &&
                transformVersion ==
                    mCachedTransformVersion
            ) {
                return;
            }

            if (localChanged) {
                mCachedLocalTransform =
                    LocalTransform;

                mCachedLocalNormalMatrix =
                    glm::mat4(
                        glm::transpose(
                            glm::inverse(
                                glm::mat3(LocalTransform)
                            )
                        )
                    );
            }

            mCachedTransformVersion =
                transformVersion;

            mCachedWorldTransform =
                transform.GetTransform() *
                mCachedLocalTransform;

            mCachedWorldNormalMatrix =
                transform.GetNormalMatrix() *
                mCachedLocalNormalMatrix;

            mTransformCacheValid = true;
        }

        mutable bool mTransformCacheValid = false;
        mutable uint64_t mCachedTransformVersion = 0;

        mutable glm::mat4 mCachedLocalTransform{1.0f};
        mutable glm::mat4 mCachedLocalNormalMatrix{1.0f};

        mutable glm::mat4 mCachedWorldTransform{1.0f};
        mutable glm::mat4 mCachedWorldNormalMatrix{1.0f};
    };

    struct MaterialComponent {
        MaterialID material = DEFAULT_MATERIAL_ID;
    };

    struct DirectionalLightComponent {
        glm::vec3 Colour{1.f};
        float intensity;
        bool CastShadows = true;
    };

    struct PointLightComponent {
        glm::vec3 Colour{1.f};
        float intensity;
        float range = 10.f;
        bool CastShadows = false;
    };
}
