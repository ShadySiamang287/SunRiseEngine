#include "AssetManagement/AssetManager.h"

#include "Graphics/vertex.h"
#include "Logger.h"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace SUN;

namespace {
    struct LoadedPrimitive {
        std::shared_ptr<Mesh> mesh;
        AssetID albedoTexture = INVALID_ASSET_ID;
        std::string name;
    };

    glm::mat4 ToGlm(const fastgltf::math::fmat4x4& matrix) {
        glm::mat4 result{1.0f};
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t row = 0; row < 4; ++row) {
                result[column][row] = matrix[column][row];
            }
        }
        return result;
    }

    std::filesystem::path ResolveImagePath(
        const std::filesystem::path& modelDirectory,
        const fastgltf::sources::URI& source
    ) {
        std::string uriPath(source.uri.path().begin(), source.uri.path().end());
        std::filesystem::path imagePath(uriPath);

        if (imagePath.is_absolute() || std::filesystem::exists(imagePath)) {
            return imagePath.lexically_normal();
        }

        return (modelDirectory / imagePath).lexically_normal();
    }
}

std::shared_ptr<ModelAsset> AssetManager::LoadModel(const std::filesystem::path& path) {
    const std::filesystem::path normalizedPath = NormalizePath(path);

    if (const auto it = mModels.find(normalizedPath); it != mModels.end()) {
        return it->second;
    }

    if (!std::filesystem::exists(normalizedPath)) {
        Logger::Log(Logger::ERROR, "Model not found: {}", normalizedPath.string());
        return nullptr;
    }

    auto gltfData = fastgltf::GltfDataBuffer::FromPath(normalizedPath);
    if (gltfData.error() != fastgltf::Error::None) {
        Logger::Log(
            Logger::ERROR,
            "Failed to open glTF '{}': {}",
            normalizedPath.string(),
            std::string(fastgltf::getErrorMessage(gltfData.error()))
        );
        return nullptr;
    }

    fastgltf::Parser parser;

    constexpr auto options =
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::LoadGLBBuffers |
        fastgltf::Options::GenerateMeshIndices;

    auto parsed = parser.loadGltf(
        gltfData.get(),
        normalizedPath.parent_path(),
        options
    );

    if (parsed.error() != fastgltf::Error::None) {
        Logger::Log(
            Logger::ERROR,
            "Failed to parse glTF '{}': {}",
            normalizedPath.string(),
            std::string(fastgltf::getErrorMessage(parsed.error()))
        );
        return nullptr;
    }

    fastgltf::Asset asset = std::move(parsed.get());

    std::vector<std::vector<LoadedPrimitive>> loadedMeshes(asset.meshes.size());

    for (std::size_t meshIndex = 0; meshIndex < asset.meshes.size(); ++meshIndex) {
        const auto& gltfMesh = asset.meshes[meshIndex];
        auto& outputMesh = loadedMeshes[meshIndex];
        outputMesh.reserve(gltfMesh.primitives.size());

        for (std::size_t primitiveIndex = 0;
             primitiveIndex < gltfMesh.primitives.size();
             ++primitiveIndex) {
            const auto& primitive = gltfMesh.primitives[primitiveIndex];

            if (primitive.type != fastgltf::PrimitiveType::Triangles) {
                Logger::Log(
                    Logger::WARNING,
                    "Skipping non-triangle primitive {} in mesh '{}'",
                    primitiveIndex,
                    gltfMesh.name
                );
                continue;
            }

            const auto* positionAttribute = primitive.findAttribute("POSITION");
            if (positionAttribute == primitive.attributes.end()) {
                Logger::Log(Logger::WARNING, "Skipping glTF primitive without POSITION");
                continue;
            }

            const auto& positionAccessor =
                asset.accessors[positionAttribute->accessorIndex];

            std::vector<Vertex> vertices(positionAccessor.count);
            for (auto& vertex : vertices) {
                vertex.normal = {0.0f, 1.0f, 0.0f};
                vertex.colour = {1.0f, 1.0f, 1.0f};
                vertex.uv = {0.0f, 0.0f};
            }

            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                asset,
                positionAccessor,
                [&](glm::vec3 position, std::size_t index) {
                    vertices[index].pos = position;
                }
            );

            if (const auto* normalAttribute = primitive.findAttribute("NORMAL");
                normalAttribute != primitive.attributes.end()) {
                const auto& normalAccessor =
                    asset.accessors[normalAttribute->accessorIndex];

                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    asset,
                    normalAccessor,
                    [&](glm::vec3 normal, std::size_t index) {
                        vertices[index].normal = normal;
                    }
                );
            }

            AssetID albedoTexture = INVALID_ASSET_ID;
            std::size_t texCoordIndex = 0;
            glm::vec4 baseColorFactor{1.0f};

            if (primitive.materialIndex.has_value()) {
                const auto& material =
                    asset.materials[primitive.materialIndex.value()];

                const auto& factor = material.pbrData.baseColorFactor;
                baseColorFactor = {
                    factor[0],
                    factor[1],
                    factor[2],
                    factor[3]
                };

                if (material.pbrData.baseColorTexture.has_value()) {
                    const auto& textureInfo =
                        material.pbrData.baseColorTexture.value();

                    texCoordIndex = textureInfo.texCoordIndex;

                    const auto& texture =
                        asset.textures[textureInfo.textureIndex];

                    if (texture.imageIndex.has_value()) {
                        const auto& image =
                            asset.images[texture.imageIndex.value()];

                        std::visit(
                            fastgltf::visitor{
                                [&](const fastgltf::sources::URI& source) {
                                    if (!source.uri.isLocalPath()) {
                                        Logger::Log(
                                            Logger::WARNING,
                                            "Skipping non-local glTF image URI"
                                        );
                                        return;
                                    }

                                    const auto imagePath = ResolveImagePath(
                                        normalizedPath.parent_path(),
                                        source
                                    );

                                    albedoTexture = LoadTexture(imagePath, true);
                                },
                                [&](const auto&) {
                                    Logger::Log(
                                        Logger::WARNING,
                                        "Embedded glTF images are not supported yet; using fallback texture"
                                    );
                                }
                            },
                            image.data
                        );
                    }
                }
            }

            for (auto& vertex : vertices) {
                vertex.colour = glm::vec3(baseColorFactor);
            }

            const std::string texCoordName =
                "TEXCOORD_" + std::to_string(texCoordIndex);

            if (const auto* texCoordAttribute =
                    primitive.findAttribute(texCoordName);
                texCoordAttribute != primitive.attributes.end()) {
                const auto& texCoordAccessor =
                    asset.accessors[texCoordAttribute->accessorIndex];

                fastgltf::iterateAccessorWithIndex<glm::vec2>(
                    asset,
                    texCoordAccessor,
                    [&](glm::vec2 uv, std::size_t index) {
                        vertices[index].uv = uv;
                    }
                );
            }

            if (!primitive.indicesAccessor.has_value()) {
                Logger::Log(Logger::WARNING, "Skipping glTF primitive without indices");
                continue;
            }

            const auto& indexAccessor =
                asset.accessors[primitive.indicesAccessor.value()];

            std::vector<uint32_t> indices(indexAccessor.count);
            fastgltf::iterateAccessorWithIndex<uint32_t>(
                asset,
                indexAccessor,
                [&](uint32_t value, std::size_t index) {
                    indices[index] = value;
                }
            );

            auto mesh = std::make_shared<Mesh>();
            mesh->buffer.Init(
                vertices.data(),
                vertices.size() * sizeof(Vertex),
                sizeof(Vertex),
                indices.data(),
                indices.size() * sizeof(uint32_t),
                vk::IndexType::eUint32
            );

            std::string primitiveName = gltfMesh.name;
            if (primitiveName.empty()) {
                primitiveName = "Mesh " + std::to_string(meshIndex);
            }
            primitiveName += " Primitive " + std::to_string(primitiveIndex);

            outputMesh.push_back({
                .mesh = std::move(mesh),
                .albedoTexture = albedoTexture,
                .name = std::move(primitiveName)
            });
        }
    }

    auto model = std::make_shared<ModelAsset>();

    if (asset.scenes.empty()) {
        Logger::Log(
            Logger::ERROR,
            "glTF '{}' contains no scenes",
            normalizedPath.string()
        );
        return nullptr;
    }

    const std::size_t sceneIndex = asset.defaultScene.value_or(0);
    if (sceneIndex >= asset.scenes.size()) {
        Logger::Log(Logger::ERROR, "glTF default scene index is invalid");
        return nullptr;
    }

    fastgltf::iterateSceneNodes(
        asset,
        sceneIndex,
        fastgltf::math::fmat4x4(1.0f),
        [&](fastgltf::Node& node, fastgltf::math::fmat4x4 transform) {
            if (!node.meshIndex.has_value()) {
                return;
            }

            const auto meshIndex = node.meshIndex.value();
            if (meshIndex >= loadedMeshes.size()) {
                return;
            }

            const glm::mat4 nodeTransform = ToGlm(transform);

            for (const auto& primitive : loadedMeshes[meshIndex]) {
                std::string name = node.name;
                if (name.empty()) {
                    name = primitive.name;
                } else {
                    name += " / " + primitive.name;
                }

                model->primitives.push_back({
                    .mesh = primitive.mesh,
                    .albedoTexture = primitive.albedoTexture,
                    .transform = nodeTransform,
                    .name = std::move(name)
                });
            }
        }
    );

    Logger::Log(
        Logger::LOG,
        "Loaded glTF '{}' with {} renderable primitives",
        normalizedPath.string(),
        model->primitives.size()
    );

    mModels.emplace(normalizedPath, model);
    return model;
}
