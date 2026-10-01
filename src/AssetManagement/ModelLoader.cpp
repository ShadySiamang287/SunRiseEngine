#include "AssetManagement/AssetManager.h"

#include "Graphics/vertex.h"
#include "Logger.h"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <glm/glm.hpp>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

using namespace SUN;

namespace {
    struct LoadedPrimitive {
        std::shared_ptr<Mesh> mesh;
        AssetID albedoTexture = INVALID_ASSET_ID;
        AssetID normalTexture = INVALID_ASSET_ID;
        AssetID materialTexture = INVALID_ASSET_ID;
        float metalicFactor = 1.f;
        float roughnessFactor = 1.f;
        std::string name;
    };

    struct CachedGeometry {
        std::shared_ptr<Mesh> mesh;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    struct GeometryDedupStats {
        std::size_t primitiveCount = 0;
        std::size_t uniqueGeometryCount = 0;
        std::size_t reusedGeometryCount = 0;
        std::size_t sourceBytes = 0;
        std::size_t uploadedBytes = 0;
    };

    constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
    constexpr uint64_t FNV_PRIME = 1099511628211ull;

    void HashByte(uint64_t& hash, uint8_t value) {
        hash ^= value;
        hash *= FNV_PRIME;
    }

    void HashUint32(uint64_t& hash, uint32_t value) {
        for (uint32_t byte = 0; byte < sizeof(value); ++byte) {
            HashByte(hash, static_cast<uint8_t>((value >> (byte * 8u)) & 0xffu));
        }
    }

    void HashUint64(uint64_t& hash, uint64_t value) {
        for (uint32_t byte = 0; byte < sizeof(value); ++byte) {
            HashByte(hash, static_cast<uint8_t>((value >> (byte * 8u)) & 0xffu));
        }
    }

    void HashFloat(uint64_t& hash, float value) {
        HashUint32(hash, std::bit_cast<uint32_t>(value));
    }

    void HashVertex(uint64_t& hash, const Vertex& vertex) {
        HashFloat(hash, vertex.pos.x);
        HashFloat(hash, vertex.pos.y);
        HashFloat(hash, vertex.pos.z);

        HashFloat(hash, vertex.normal.x);
        HashFloat(hash, vertex.normal.y);
        HashFloat(hash, vertex.normal.z);

        HashFloat(hash, vertex.colour.x);
        HashFloat(hash, vertex.colour.y);
        HashFloat(hash, vertex.colour.z);

        HashFloat(hash, vertex.uv.x);
        HashFloat(hash, vertex.uv.y);

        HashFloat(hash, vertex.tangent.x);
        HashFloat(hash, vertex.tangent.y);
        HashFloat(hash, vertex.tangent.z);
        HashFloat(hash, vertex.tangent.w);
    }

    uint64_t HashGeometry(
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices
    ) {
        uint64_t hash = FNV_OFFSET_BASIS;

        HashUint64(hash, static_cast<uint64_t>(vertices.size()));
        HashUint64(hash, static_cast<uint64_t>(indices.size()));

        for (const Vertex& vertex : vertices) {
            HashVertex(hash, vertex);
        }

        for (uint32_t index : indices) {
            HashUint32(hash, index);
        }

        return hash;
    }

    bool FloatBitsEqual(float lhs, float rhs) {
        return std::bit_cast<uint32_t>(lhs) == std::bit_cast<uint32_t>(rhs);
    }

    bool VerticesEqual(const Vertex& lhs, const Vertex& rhs) {
        return
            FloatBitsEqual(lhs.pos.x, rhs.pos.x) &&
            FloatBitsEqual(lhs.pos.y, rhs.pos.y) &&
            FloatBitsEqual(lhs.pos.z, rhs.pos.z) &&
            FloatBitsEqual(lhs.normal.x, rhs.normal.x) &&
            FloatBitsEqual(lhs.normal.y, rhs.normal.y) &&
            FloatBitsEqual(lhs.normal.z, rhs.normal.z) &&
            FloatBitsEqual(lhs.colour.x, rhs.colour.x) &&
            FloatBitsEqual(lhs.colour.y, rhs.colour.y) &&
            FloatBitsEqual(lhs.colour.z, rhs.colour.z) &&
            FloatBitsEqual(lhs.uv.x, rhs.uv.x) &&
            FloatBitsEqual(lhs.uv.y, rhs.uv.y) &&
            FloatBitsEqual(lhs.tangent.x, rhs.tangent.x) &&
            FloatBitsEqual(lhs.tangent.y, rhs.tangent.y) &&
            FloatBitsEqual(lhs.tangent.z, rhs.tangent.z) &&
            FloatBitsEqual(lhs.tangent.w, rhs.tangent.w);
    }

    bool GeometryMatches(
        const CachedGeometry& cached,
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices
    ) {
        if (cached.vertices.size() != vertices.size() ||
            cached.indices.size() != indices.size()) {
            return false;
        }

        for (std::size_t i = 0; i < vertices.size(); ++i) {
            if (!VerticesEqual(cached.vertices[i], vertices[i])) {
                return false;
            }
        }

        return cached.indices == indices;
    }

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
    std::unordered_map<uint64_t, std::vector<CachedGeometry>> geometryCache;
    GeometryDedupStats dedupStats;

    for (std::size_t meshIndex = 0; meshIndex < asset.meshes.size(); ++meshIndex) {
        const auto& gltfMesh = asset.meshes[meshIndex];
        auto& outputMesh = loadedMeshes[meshIndex];
        outputMesh.reserve(gltfMesh.primitives.size());

        for (std::size_t primitiveIndex = 0; primitiveIndex < gltfMesh.primitives.size(); ++primitiveIndex) {
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
                vertex.tangent = {0.f, 0.f, 0.f, 0.f};
            }

            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                asset,
                positionAccessor,
                [&](glm::vec3 position, std::size_t index) {
                    vertices[index].pos = position;
                }
            );

            if (const auto* normalAttribute = primitive.findAttribute("NORMAL"); normalAttribute != primitive.attributes.end()) {
                const auto& normalAccessor = asset.accessors[normalAttribute->accessorIndex];

                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    asset,
                    normalAccessor,
                    [&](glm::vec3 normal, std::size_t index) {
                        vertices[index].normal = normal;
                    }
                );
            }

            if (const auto* tangentAttribute = primitive.findAttribute("TANGENT"); tangentAttribute != primitive.attributes.end()) {
                const auto& tangentAccessor = asset.accessors[tangentAttribute->accessorIndex];

                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    asset,
                    tangentAccessor,
                    [&](glm::vec4 tangent, std::size_t index) {
                        vertices[index].tangent = tangent;
                    }
                );
            }

            AssetID albedoTexture = INVALID_ASSET_ID;
            AssetID normalTexture = INVALID_ASSET_ID;
            AssetID materialTexture = INVALID_ASSET_ID;
            float metallicFactor = 0.f;
            float roughnessFactor = 0.f;
            std::size_t texCoordIndex = 0;
            glm::vec4 baseColorFactor{1.0f};

            if (primitive.materialIndex.has_value()) {
                const auto& material = asset.materials[primitive.materialIndex.value()];

                const auto& factor = material.pbrData.baseColorFactor;
                baseColorFactor = {
                    factor[0],
                    factor[1],
                    factor[2],
                    factor[3]
                };

                metallicFactor = glm::clamp(
                    material.pbrData.metallicFactor,
                    0.0f,
                    1.0f
                );

                roughnessFactor = glm::clamp(
                    material.pbrData.roughnessFactor,
                    0.0f,
                    1.0f
                );

                if (material.pbrData.baseColorTexture.has_value()) {
                    const auto& textureInfo = material.pbrData.baseColorTexture.value();

                    texCoordIndex = textureInfo.texCoordIndex;

                    const auto& texture =
                        asset.textures[textureInfo.textureIndex];

                    if (texture.imageIndex.has_value()) {
                        const auto& image = asset.images[texture.imageIndex.value()];

                        std::visit(
                            fastgltf::visitor{
                                [&](const fastgltf::sources::URI& source) {
                                    if (!source.uri.isLocalPath()) {
                                        Logger::Log(Logger::WARNING,"Skipping non-local glTF image URI");
                                        return;
                                    }

                                    const auto imagePath = ResolveImagePath(normalizedPath.parent_path(), source
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
                if (material.normalTexture.has_value()) {
                    const auto& normalInfo = material.normalTexture.value();

                    const auto& texture = asset.textures[normalInfo.textureIndex];

                    if (texture.imageIndex.has_value()) {
                        const auto& image = asset.images[texture.imageIndex.value()];

                        std::visit(
                            fastgltf::visitor{
                                [&](const fastgltf::sources::URI& source)
                                {
                                    if (!source.uri.isLocalPath()) {
                                        return;
                                    }

                                    auto imagePath = ResolveImagePath(normalizedPath.parent_path(), source );

                                    normalTexture = LoadTexture(imagePath, false);
                                },

                                [&](const auto&)
                                {
                                    Logger::Log(Logger::WARNING, "Embedded normal maps not supported yet");
                                }
                            },
                            image.data
                        );
                    }
                }
                if (material.pbrData.metallicRoughnessTexture.has_value()) {
                    const auto& materialInfo = material.pbrData.metallicRoughnessTexture.value();

                    const auto& texture = asset.textures[materialInfo.textureIndex];

                    if (texture.imageIndex.has_value()) {
                        const auto& image = asset.images[texture.imageIndex.value()];

                        std::visit(
                            fastgltf::visitor{
                                [&](const fastgltf::sources::URI& source)
                                {
                                    if (!source.uri.isLocalPath()) {
                                        return;
                                    }

                                    auto imagePath = ResolveImagePath(
                                        normalizedPath.parent_path(),
                                        source
                                    );

                                    materialTexture = LoadTexture(imagePath, false);
                                },

                                [&](const auto&)
                                {
                                    Logger::Log(
                                        Logger::WARNING,
                                        "Embedded normal maps not supported yet"
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

            const auto& indexAccessor = asset.accessors[primitive.indicesAccessor.value()];

            std::vector<uint32_t> indices(indexAccessor.count);
            fastgltf::iterateAccessorWithIndex<uint32_t>(
                asset,
                indexAccessor,
                [&](uint32_t value, std::size_t index) {
                    indices[index] = value;
                }
            );

            const std::size_t geometryBytes =
                vertices.size() * sizeof(Vertex) +
                indices.size() * sizeof(uint32_t);

            ++dedupStats.primitiveCount;
            dedupStats.sourceBytes += geometryBytes;

            const uint64_t geometryHash = HashGeometry(vertices, indices);
            auto& candidates = geometryCache[geometryHash];

            std::shared_ptr<Mesh> mesh;
            for (const CachedGeometry& candidate : candidates) {
                if (GeometryMatches(candidate, vertices, indices)) {
                    mesh = candidate.mesh;
                    ++dedupStats.reusedGeometryCount;
                    break;
                }
            }

            if (!mesh) {
                mesh = std::make_shared<Mesh>();
                mesh->buffer.Init(
                    vertices.data(),
                    vertices.size() * sizeof(Vertex),
                    sizeof(Vertex),
                    indices.data(),
                    indices.size() * sizeof(uint32_t),
                    vk::IndexType::eUint32
                );

                ++dedupStats.uniqueGeometryCount;
                dedupStats.uploadedBytes += geometryBytes;

                candidates.push_back({
                    .mesh = mesh,
                    .vertices = std::move(vertices),
                    .indices = std::move(indices)
                });
            }

            std::string primitiveName(gltfMesh.name.data(), gltfMesh.name.size());
            if (primitiveName.empty()) {
                primitiveName = "Mesh " + std::to_string(meshIndex);
            }
            primitiveName += " Primitive " + std::to_string(primitiveIndex);

            outputMesh.push_back({
                .mesh = std::move(mesh),
                .albedoTexture = albedoTexture,
                .normalTexture = normalTexture,
                .materialTexture = materialTexture,
                .metalicFactor = metallicFactor,
                .roughnessFactor = roughnessFactor,
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
                std::string name(
                    node.name.data(),
                    node.name.size()
                );
                if (name.empty()) {
                    name = primitive.name;
                } else {
                    name += " / " + primitive.name;
                }

                model->primitives.push_back({
                    .mesh = primitive.mesh,
                    .albedoTexture = primitive.albedoTexture,
                    .normalTexture = primitive.normalTexture,
                    .materialTexture = primitive.materialTexture,
                    .metalicFactor = primitive.metalicFactor,
                    .roughnessFactor = primitive.roughnessFactor,
                    .transform = nodeTransform,
                    .name = std::move(name)
                });
            }
        }
    );

    const double primitiveReduction =
        dedupStats.primitiveCount == 0
            ? 0.0
            : (static_cast<double>(dedupStats.reusedGeometryCount) /
               static_cast<double>(dedupStats.primitiveCount)) * 100.0;

    const double uploadReduction =
        dedupStats.sourceBytes == 0
            ? 0.0
            : (1.0 -
               static_cast<double>(dedupStats.uploadedBytes) /
               static_cast<double>(dedupStats.sourceBytes)) * 100.0;

    constexpr double bytesPerMiB = 1024.0 * 1024.0;

    Logger::Log(
        Logger::LOG,
        "Geometry dedup '{}': {} glTF primitives -> {} unique GPU meshes "
        "({} reused, {:.1f}% reduction), upload data {:.2f} MiB -> {:.2f} MiB "
        "({:.1f}% reduction)",
        normalizedPath.string(),
        dedupStats.primitiveCount,
        dedupStats.uniqueGeometryCount,
        dedupStats.reusedGeometryCount,
        primitiveReduction,
        static_cast<double>(dedupStats.sourceBytes) / bytesPerMiB,
        static_cast<double>(dedupStats.uploadedBytes) / bytesPerMiB,
        uploadReduction
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

