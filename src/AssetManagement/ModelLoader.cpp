#include "AssetManagement/AssetManager.h"

#include "Graphics/vertex.h"
#include "Logger.h"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace SUN;

namespace {
    struct LoadedPrimitive {
        std::shared_ptr<Mesh> mesh;
        MaterialID material = DEFAULT_MATERIAL_ID;
        glm::mat4 geometryTransform{1.0f};
        std::string name;
    };

    struct CanonicalVertex {
        std::array<uint32_t, 12> values{};

        bool operator==(const CanonicalVertex&) const = default;

        bool operator<(const CanonicalVertex& other) const {
            return values < other.values;
        }
    };

    struct CanonicalGeometry {
        std::vector<CanonicalVertex> vertices;
        std::vector<std::array<uint32_t, 3>> triangles;

        bool operator==(const CanonicalGeometry&) const = default;
    };

    struct AffineDiagnostic {
        std::string name;
        uint64_t topologyHash = 0;
        std::vector<glm::vec3> positions;
        std::vector<uint32_t> indices;
    };

    struct AffineMatch {
        glm::mat3 linear{1.0f};
        glm::vec3 translation{0.0f};
        float maxError = 0.0f;
    };

    struct CachedGeometry {
        std::shared_ptr<Mesh> mesh;
        CanonicalGeometry geometry;
        glm::vec3 origin{0.0f};
    };

    struct AffineCachedGeometry {
        std::shared_ptr<Mesh> mesh;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::string name;
    };

    struct PendingGeometryUpload {
        std::shared_ptr<Mesh> mesh;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    struct VertexDedupStats {
        std::size_t inputVertices = 0;
        std::size_t outputVertices = 0;
    };

    struct GeometryDedupStats {
        std::size_t primitiveCount = 0;
        std::size_t uniqueGeometryCount = 0;
        std::size_t reusedGeometryCount = 0;
        std::size_t translatedReuseCount = 0;
        std::size_t affineReuseCount = 0;
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
            HashByte(
                hash,
                static_cast<uint8_t>((value >> (byte * 8u)) & 0xffu)
            );
        }
    }

    void HashUint64(uint64_t& hash, uint64_t value) {
        for (uint32_t byte = 0; byte < sizeof(value); ++byte) {
            HashByte(
                hash,
                static_cast<uint8_t>((value >> (byte * 8u)) & 0xffu)
            );
        }
    }

    uint32_t CanonicalFloatBits(float value) {
        if (value == 0.0f) {
            return 0;
        }
        return std::bit_cast<uint32_t>(value);
    }

    BoundingBox CalculateBounds(
        const std::vector<Vertex>& vertices
    ) {
        BoundingBox bounds;

        if (vertices.empty()) {
            return bounds;
        }

        bounds.min = vertices.front().pos;
        bounds.max = vertices.front().pos;

        for (const Vertex& vertex : vertices) {
            bounds.min = glm::min(bounds.min, vertex.pos);
            bounds.max = glm::max(bounds.max, vertex.pos);
        }

        return bounds;
    }

    glm::vec3 FindGeometryOrigin(const std::vector<Vertex>& vertices) {
        if (vertices.empty()) {
            return glm::vec3(0.0f);
        }

        glm::vec3 origin = vertices.front().pos;

        for (const Vertex& vertex : vertices) {
            const glm::vec3& position = vertex.pos;

            if (position.x < origin.x ||
                (position.x == origin.x && position.y < origin.y) ||
                (position.x == origin.x &&
                 position.y == origin.y &&
                 position.z < origin.z)) {
                origin = position;
            }
        }

        return origin;
    }

    CanonicalVertex MakeCanonicalVertex(
        const Vertex& vertex,
        const glm::vec3& origin
    ) {
        const glm::vec3 relativePosition =
            vertex.pos - origin;

        return {{
            CanonicalFloatBits(relativePosition.x),
            CanonicalFloatBits(relativePosition.y),
            CanonicalFloatBits(relativePosition.z),
            CanonicalFloatBits(vertex.normal.x),
            CanonicalFloatBits(vertex.normal.y),
            CanonicalFloatBits(vertex.normal.z),
            CanonicalFloatBits(vertex.uv.x),
            CanonicalFloatBits(vertex.uv.y),
            CanonicalFloatBits(vertex.tangent.x),
            CanonicalFloatBits(vertex.tangent.y),
            CanonicalFloatBits(vertex.tangent.z),
            CanonicalFloatBits(vertex.tangent.w)
        }};
    }

    CanonicalGeometry BuildCanonicalGeometry(
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        glm::vec3& origin
    ) {
        CanonicalGeometry canonical;
        origin = FindGeometryOrigin(vertices);
        canonical.vertices.reserve(vertices.size());
        canonical.triangles.reserve(indices.size() / 3);

        std::vector<std::pair<CanonicalVertex, uint32_t>> sortedVertices;
        sortedVertices.reserve(vertices.size());

        for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i) {
            sortedVertices.emplace_back(
                MakeCanonicalVertex(vertices[i], origin),
                i
            );
        }

        std::sort(
            sortedVertices.begin(),
            sortedVertices.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
            }
        );

        std::vector<uint32_t> remap(vertices.size());

        for (const auto& [vertex, originalIndex] : sortedVertices) {
            if (canonical.vertices.empty() ||
                !(canonical.vertices.back() == vertex)) {
                canonical.vertices.push_back(vertex);
            }

            remap[originalIndex] =
                static_cast<uint32_t>(canonical.vertices.size() - 1);
        }

        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            std::array<uint32_t, 3> triangle {
                remap[indices[i]],
                remap[indices[i + 1]],
                remap[indices[i + 2]]
            };

            const std::array<uint32_t, 3> rotate1 {
                triangle[1],
                triangle[2],
                triangle[0]
            };

            const std::array<uint32_t, 3> rotate2 {
                triangle[2],
                triangle[0],
                triangle[1]
            };

            triangle = std::min(triangle, std::min(rotate1, rotate2));
            canonical.triangles.push_back(triangle);
        }

        std::sort(
            canonical.triangles.begin(),
            canonical.triangles.end()
        );

        return canonical;
    }

    uint64_t HashVertex(const Vertex& vertex) {
        uint64_t hash = FNV_OFFSET_BASIS;

        const std::array<float, 12> values {
            vertex.pos.x,
            vertex.pos.y,
            vertex.pos.z,
            vertex.normal.x,
            vertex.normal.y,
            vertex.normal.z,
            vertex.uv.x,
            vertex.uv.y,
            vertex.tangent.x,
            vertex.tangent.y,
            vertex.tangent.z,
            vertex.tangent.w
        };

        for (float value : values) {
            HashUint32(hash, CanonicalFloatBits(value));
        }

        return hash;
    }

    bool VerticesEqual(
        const Vertex& lhs,
        const Vertex& rhs
    ) {
        return
            CanonicalFloatBits(lhs.pos.x) ==
                CanonicalFloatBits(rhs.pos.x) &&
            CanonicalFloatBits(lhs.pos.y) ==
                CanonicalFloatBits(rhs.pos.y) &&
            CanonicalFloatBits(lhs.pos.z) ==
                CanonicalFloatBits(rhs.pos.z) &&
            CanonicalFloatBits(lhs.normal.x) ==
                CanonicalFloatBits(rhs.normal.x) &&
            CanonicalFloatBits(lhs.normal.y) ==
                CanonicalFloatBits(rhs.normal.y) &&
            CanonicalFloatBits(lhs.normal.z) ==
                CanonicalFloatBits(rhs.normal.z) &&
            CanonicalFloatBits(lhs.uv.x) ==
                CanonicalFloatBits(rhs.uv.x) &&
            CanonicalFloatBits(lhs.uv.y) ==
                CanonicalFloatBits(rhs.uv.y) &&
            CanonicalFloatBits(lhs.tangent.x) ==
                CanonicalFloatBits(rhs.tangent.x) &&
            CanonicalFloatBits(lhs.tangent.y) ==
                CanonicalFloatBits(rhs.tangent.y) &&
            CanonicalFloatBits(lhs.tangent.z) ==
                CanonicalFloatBits(rhs.tangent.z) &&
            CanonicalFloatBits(lhs.tangent.w) ==
                CanonicalFloatBits(rhs.tangent.w);
    }

    void DeduplicateVertices(
        std::vector<Vertex>& vertices,
        std::vector<uint32_t>& indices
    ) {
        if (vertices.empty()) {
            return;
        }

        std::vector<Vertex> uniqueVertices;
        uniqueVertices.reserve(vertices.size());

        std::vector<uint32_t> remap(vertices.size());

        std::unordered_map<uint64_t, std::vector<uint32_t>>
            vertexBuckets;

        vertexBuckets.reserve(vertices.size());

        for (uint32_t sourceIndex = 0;
             sourceIndex < static_cast<uint32_t>(vertices.size());
             ++sourceIndex) {
            const Vertex& vertex = vertices[sourceIndex];
            const uint64_t hash = HashVertex(vertex);

            auto& bucket = vertexBuckets[hash];

            uint32_t uniqueIndex = UINT32_MAX;

            for (uint32_t candidateIndex : bucket) {
                if (VerticesEqual(
                        uniqueVertices[candidateIndex],
                        vertex)) {
                    uniqueIndex = candidateIndex;
                    break;
                }
            }

            if (uniqueIndex == UINT32_MAX) {
                uniqueIndex =
                    static_cast<uint32_t>(
                        uniqueVertices.size()
                    );

                uniqueVertices.push_back(vertex);
                bucket.push_back(uniqueIndex);
            }

            remap[sourceIndex] = uniqueIndex;
        }

        for (uint32_t& index : indices) {
            index = remap[index];
        }

        vertices = std::move(uniqueVertices);
    }

    uint64_t HashRawTopology(
        std::size_t vertexCount,
        const std::vector<uint32_t>& indices
    ) {
        uint64_t hash = FNV_OFFSET_BASIS;

        HashUint64(hash, static_cast<uint64_t>(vertexCount));
        HashUint64(hash, static_cast<uint64_t>(indices.size()));

        for (uint32_t index : indices) {
            HashUint32(hash, index);
        }

        return hash;
    }

    float GeometryScale(
        const std::vector<glm::vec3>& positions
    ) {
        if (positions.empty()) {
            return 1.0f;
        }

        glm::vec3 minimum = positions.front();
        glm::vec3 maximum = positions.front();

        for (const glm::vec3& position : positions) {
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }

        return std::max(glm::length(maximum - minimum), 1.0f);
    }

    bool RecoverAffineTransform(
        const AffineDiagnostic& source,
        const AffineDiagnostic& target,
        AffineMatch& match
    ) {
        if (source.positions.size() != target.positions.size() ||
            source.indices != target.indices ||
            source.positions.size() < 3) {
            return false;
        }

        const float sourceScale =
            GeometryScale(source.positions);

        const float basisEpsilon =
            sourceScale * 1e-6f;

        const std::size_t p0Index = 0;
        std::size_t p1Index = source.positions.size();
        std::size_t p2Index = source.positions.size();
        std::size_t p3Index = source.positions.size();

        const glm::vec3 p0 =
            source.positions[p0Index];

        for (std::size_t i = 1;
             i < source.positions.size();
             ++i) {
            if (glm::length(source.positions[i] - p0) >
                basisEpsilon) {
                p1Index = i;
                break;
            }
        }

        if (p1Index == source.positions.size()) {
            return false;
        }

        const glm::vec3 e1 =
            source.positions[p1Index] - p0;

        for (std::size_t i = 1;
             i < source.positions.size();
             ++i) {
            if (i == p1Index) {
                continue;
            }

            const glm::vec3 candidate =
                source.positions[i] - p0;

            if (glm::length(glm::cross(e1, candidate)) >
                basisEpsilon * basisEpsilon) {
                p2Index = i;
                break;
            }
        }

        if (p2Index == source.positions.size()) {
            return false;
        }

        const glm::vec3 e2 =
            source.positions[p2Index] - p0;

        const glm::vec3 sourceNormal =
            glm::cross(e1, e2);

        for (std::size_t i = 1;
             i < source.positions.size();
             ++i) {
            if (i == p1Index || i == p2Index) {
                continue;
            }

            const glm::vec3 candidate =
                source.positions[i] - p0;

            if (std::abs(glm::dot(sourceNormal, candidate)) >
                basisEpsilon * basisEpsilon) {
                p3Index = i;
                break;
            }
        }

        const glm::vec3 q0 =
            target.positions[p0Index];

        const glm::vec3 f1 =
            target.positions[p1Index] - q0;

        const glm::vec3 f2 =
            target.positions[p2Index] - q0;

        glm::vec3 e3;
        glm::vec3 f3;

        if (p3Index != source.positions.size()) {
            e3 = source.positions[p3Index] - p0;
            f3 = target.positions[p3Index] - q0;
        } else {
            e3 = glm::normalize(sourceNormal);
            f3 = glm::normalize(glm::cross(f1, f2));
        }

        const glm::mat3 sourceBasis(
            e1,
            e2,
            e3
        );

        const float determinant =
            glm::determinant(sourceBasis);

        if (std::abs(determinant) <= 1e-8f) {
            return false;
        }

        const glm::mat3 targetBasis(
            f1,
            f2,
            f3
        );

        match.linear =
            targetBasis * glm::inverse(sourceBasis);

        match.translation =
            q0 - match.linear * p0;

        const float targetScale =
            GeometryScale(target.positions);

        const float verifyEpsilon =
            targetScale * 1e-4f;

        match.maxError = 0.0f;

        for (std::size_t i = 0;
             i < source.positions.size();
             ++i) {
            const glm::vec3 transformed =
                match.linear * source.positions[i] +
                match.translation;

            const float error =
                glm::length(
                    transformed - target.positions[i]
                );

            match.maxError =
                std::max(match.maxError, error);

            if (error > verifyEpsilon) {
                return false;
            }
        }

        return true;
    }

    glm::vec3 AffineColumnScale(
        const glm::mat3& linear
    ) {
        return {
            glm::length(linear[0]),
            glm::length(linear[1]),
            glm::length(linear[2])
        };
    }

    glm::mat4 ToAffineMatrix(
        const AffineMatch& match
    ) {
        glm::mat4 transform{1.0f};

        transform[0] = glm::vec4(match.linear[0], 0.0f);
        transform[1] = glm::vec4(match.linear[1], 0.0f);
        transform[2] = glm::vec4(match.linear[2], 0.0f);
        transform[3] = glm::vec4(match.translation, 1.0f);

        return transform;
    }

    bool NearlyEqual(
        const glm::vec2& lhs,
        const glm::vec2& rhs,
        float epsilon
    ) {
        return glm::length(lhs - rhs) <= epsilon;
    }

    bool DirectionsMatch(
        const glm::vec3& lhs,
        const glm::vec3& rhs,
        float minimumDot = 0.9999f
    ) {
        const float lhsLength = glm::length(lhs);
        const float rhsLength = glm::length(rhs);

        if (lhsLength <= 1e-6f || rhsLength <= 1e-6f) {
            return lhsLength <= 1e-6f &&
                   rhsLength <= 1e-6f;
        }

        return glm::dot(
            lhs / lhsLength,
            rhs / rhsLength
        ) >= minimumDot;
    }

    bool VerifyAffineAttributes(
        const std::vector<Vertex>& source,
        const std::vector<Vertex>& target,
        const AffineMatch& match
    ) {
        if (source.size() != target.size()) {
            return false;
        }

        const float determinant =
            glm::determinant(match.linear);

        if (std::abs(determinant) <= 1e-8f) {
            return false;
        }

        const glm::mat3 normalMatrix =
            glm::transpose(glm::inverse(match.linear));

        constexpr float UV_EPSILON = 1e-5f;
        constexpr float TANGENT_W_EPSILON = 1e-5f;

        for (std::size_t i = 0; i < source.size(); ++i) {
            const Vertex& sourceVertex = source[i];
            const Vertex& targetVertex = target[i];

            if (!NearlyEqual(
                    sourceVertex.uv,
                    targetVertex.uv,
                    UV_EPSILON)) {
                return false;
            }

            const glm::vec3 transformedNormal =
                normalMatrix * sourceVertex.normal;

            if (!DirectionsMatch(
                    transformedNormal,
                    targetVertex.normal)) {
                return false;
            }

            const float sourceTangentLength =
                glm::length(glm::vec3(sourceVertex.tangent));

            const float targetTangentLength =
                glm::length(glm::vec3(targetVertex.tangent));

            if (sourceTangentLength <= 1e-6f ||
                targetTangentLength <= 1e-6f) {
                if (!(sourceTangentLength <= 1e-6f &&
                      targetTangentLength <= 1e-6f)) {
                    return false;
                }
            } else {
                glm::vec3 N =
                    glm::normalize(transformedNormal);

                glm::vec3 transformedTangent =
                    glm::normalize(
                        match.linear *
                        glm::vec3(sourceVertex.tangent)
                    );

                transformedTangent =
                    transformedTangent -
                    N * glm::dot(N, transformedTangent);

                if (glm::length(transformedTangent) <= 1e-6f) {
                    return false;
                }

                transformedTangent =
                    glm::normalize(transformedTangent);

                glm::vec3 targetTangent =
                    glm::vec3(targetVertex.tangent);

                targetTangent =
                    targetTangent -
                    glm::normalize(targetVertex.normal) *
                    glm::dot(
                        glm::normalize(targetVertex.normal),
                        targetTangent
                    );

                if (glm::length(targetTangent) <= 1e-6f) {
                    return false;
                }

                targetTangent =
                    glm::normalize(targetTangent);

                if (!DirectionsMatch(
                        transformedTangent,
                        targetTangent)) {
                    return false;
                }
            }

            if (std::abs(
                    sourceVertex.tangent.w -
                    targetVertex.tangent.w) >
                TANGENT_W_EPSILON) {
                return false;
            }
        }

        return true;
    }

    uint64_t HashGeometry(const CanonicalGeometry& geometry) {
        uint64_t hash = FNV_OFFSET_BASIS;

        HashUint64(hash, static_cast<uint64_t>(geometry.vertices.size()));
        HashUint64(hash, static_cast<uint64_t>(geometry.triangles.size()));

        for (const CanonicalVertex& vertex : geometry.vertices) {
            for (uint32_t value : vertex.values) {
                HashUint32(hash, value);
            }
        }

        for (const auto& triangle : geometry.triangles) {
            HashUint32(hash, triangle[0]);
            HashUint32(hash, triangle[1]);
            HashUint32(hash, triangle[2]);
        }

        return hash;
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
    std::unordered_map<uint64_t, std::vector<AffineCachedGeometry>>
        affineGeometryCache;
    std::vector<PendingGeometryUpload> pendingGeometryUploads;
    std::unordered_set<MaterialID> modelMaterialIDs;

    VertexDedupStats vertexDedupStats;
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

            MaterialDescription materialDescription{};
            std::size_t texCoordIndex = 0;

            if (primitive.materialIndex.has_value()) {
                const auto& material = asset.materials[primitive.materialIndex.value()];

                const auto& factor = material.pbrData.baseColorFactor;
                materialDescription.baseColorFactor = {
                    factor[0],
                    factor[1],
                    factor[2],
                    factor[3]
                };

                materialDescription.metallicFactor = glm::clamp(
                    material.pbrData.metallicFactor,
                    0.0f,
                    1.0f
                );

                materialDescription.roughnessFactor = glm::clamp(
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

                                    materialDescription.albedoTexture = LoadTexture(imagePath, true);
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

                                    materialDescription.normalTexture = LoadTexture(imagePath, false);
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

                                    materialDescription.materialTexture = LoadTexture(imagePath, false);
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

            const std::size_t decodedVertexCount =
                vertices.size();

            const std::size_t decodedGeometryBytes =
                vertices.size() * sizeof(Vertex) +
                indices.size() * sizeof(uint32_t);

            DeduplicateVertices(vertices, indices);

            vertexDedupStats.inputVertices +=
                decodedVertexCount;

            vertexDedupStats.outputVertices +=
                vertices.size();

            std::string primitiveName(
                gltfMesh.name.data(),
                gltfMesh.name.size()
            );

            if (primitiveName.empty()) {
                primitiveName =
                    "Mesh " + std::to_string(meshIndex);
            }

            primitiveName +=
                " Primitive " +
                std::to_string(primitiveIndex);

            const MaterialID materialID =
                GetOrCreateMaterial(materialDescription);

            modelMaterialIDs.insert(materialID);

            const std::size_t geometryBytes =
                vertices.size() * sizeof(Vertex) +
                indices.size() * sizeof(uint32_t);

            ++dedupStats.primitiveCount;
            dedupStats.sourceBytes += decodedGeometryBytes;

            glm::vec3 geometryOrigin{0.0f};

            CanonicalGeometry canonicalGeometry =
                BuildCanonicalGeometry(
                    vertices,
                    indices,
                    geometryOrigin
                );

            const uint64_t geometryHash =
                HashGeometry(canonicalGeometry);

            auto& candidates =
                geometryCache[geometryHash];

            std::shared_ptr<Mesh> mesh;
            glm::mat4 geometryTransform{1.0f};

            for (const CachedGeometry& candidate : candidates) {
                if (candidate.geometry == canonicalGeometry) {
                    mesh = candidate.mesh;
                    ++dedupStats.reusedGeometryCount;

                    const glm::vec3 translation =
                        geometryOrigin - candidate.origin;

                    if (translation != glm::vec3(0.0f)) {
                        geometryTransform =
                            glm::translate(
                                glm::mat4(1.0f),
                                translation
                            );

                        ++dedupStats.translatedReuseCount;
                    }

                    break;
                }
            }

            const uint64_t rawTopologyHash =
                HashRawTopology(
                    vertices.size(),
                    indices
                );

            if (!mesh) {
                auto& affineGeometryCandidates =
                    affineGeometryCache[rawTopologyHash];

                AffineDiagnostic targetAffine;
                targetAffine.name = primitiveName;
                targetAffine.topologyHash = rawTopologyHash;
                targetAffine.indices = indices;
                targetAffine.positions.reserve(vertices.size());

                for (const Vertex& vertex : vertices) {
                    targetAffine.positions.push_back(vertex.pos);
                }

                for (const AffineCachedGeometry& candidate :
                     affineGeometryCandidates) {
                    AffineDiagnostic sourceAffine;
                    sourceAffine.name = candidate.name;
                    sourceAffine.topologyHash = rawTopologyHash;
                    sourceAffine.indices = candidate.indices;
                    sourceAffine.positions.reserve(
                        candidate.vertices.size()
                    );

                    for (const Vertex& vertex :
                         candidate.vertices) {
                        sourceAffine.positions.push_back(
                            vertex.pos
                        );
                    }

                    AffineMatch match;

                    if (!RecoverAffineTransform(
                            sourceAffine,
                            targetAffine,
                            match)) {
                        continue;
                    }

                    if (!VerifyAffineAttributes(
                            candidate.vertices,
                            vertices,
                            match)) {
                        continue;
                    }

                    mesh = candidate.mesh;
                    geometryTransform =
                        ToAffineMatrix(match);

                    ++dedupStats.reusedGeometryCount;
                    ++dedupStats.affineReuseCount;

                    break;
                }
            }

            if (!mesh) {
                mesh = std::make_shared<Mesh>(Mesh {
                    .bounds = CalculateBounds(vertices)
                });

                ++dedupStats.uniqueGeometryCount;
                dedupStats.uploadedBytes += geometryBytes;

                candidates.push_back({
                    .mesh = mesh,
                    .geometry = std::move(canonicalGeometry),
                    .origin = geometryOrigin
                });

                affineGeometryCache[rawTopologyHash].push_back({
                    .mesh = mesh,
                    .vertices = vertices,
                    .indices = indices,
                    .name = primitiveName
                });

                pendingGeometryUploads.push_back({
                    .mesh = mesh,
                    .vertices = vertices,
                    .indices = indices
                });
            }

            outputMesh.push_back({
                .mesh = std::move(mesh),
                .material = materialID,
                .geometryTransform = geometryTransform,
                .name = std::move(primitiveName)
            });
        }
    }

    if (!pendingGeometryUploads.empty()) {
        std::vector<GeometryUpload> uploads;
        uploads.reserve(pendingGeometryUploads.size());

        for (const PendingGeometryUpload& pending :
             pendingGeometryUploads) {
            uploads.push_back({
                .vertexData = pending.vertices.data(),
                .vertexDataSize =
                    pending.vertices.size() * sizeof(Vertex),
                .indexData = pending.indices.data(),
                .indexDataSize =
                    pending.indices.size() * sizeof(uint32_t)
            });
        }

        const std::vector<GeometryAllocation> allocations =
            mGeometryBuffer.UploadGeometryBatch(uploads);

        for (std::size_t i = 0;
             i < pendingGeometryUploads.size();
             ++i) {
            Mesh& mesh = *pendingGeometryUploads[i].mesh;
            const GeometryAllocation& allocation =
                allocations[i];

            mesh.firstIndex = allocation.firstIndex;
            mesh.indexCount = allocation.indexCount;
            mesh.vertexOffset = allocation.vertexOffset;
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
                    .material = primitive.material,
                    .transform =
                        nodeTransform * primitive.geometryTransform,
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

    const std::size_t removedVertices =
        vertexDedupStats.inputVertices -
        vertexDedupStats.outputVertices;

    const double vertexReduction =
        vertexDedupStats.inputVertices == 0
            ? 0.0
            : (static_cast<double>(removedVertices) /
               static_cast<double>(
                   vertexDedupStats.inputVertices
               )) * 100.0;

    Logger::Log(
        Logger::LOG,
        "Mesh optimization '{}': vertices {} -> {} ({} removed, {:.1f}%); "
        "primitives {} -> {} unique GPU meshes ({} reused: {} translation, "
        "{} affine, {:.1f}%); geometry data {:.2f} MiB -> {:.2f} MiB "
        "({:.1f}% total reduction)",
        normalizedPath.string(),
        vertexDedupStats.inputVertices,
        vertexDedupStats.outputVertices,
        removedVertices,
        vertexReduction,
        dedupStats.primitiveCount,
        dedupStats.uniqueGeometryCount,
        dedupStats.reusedGeometryCount,
        dedupStats.translatedReuseCount,
        dedupStats.affineReuseCount,
        primitiveReduction,
        static_cast<double>(dedupStats.sourceBytes) / bytesPerMiB,
        static_cast<double>(dedupStats.uploadedBytes) / bytesPerMiB,
        uploadReduction
    );

    const double materialReuse =
        dedupStats.primitiveCount == 0
            ? 0.0
            : (1.0 -
               static_cast<double>(modelMaterialIDs.size()) /
               static_cast<double>(dedupStats.primitiveCount)) * 100.0;

    Logger::Log(
        Logger::LOG,
        "Materials '{}': {} glTF primitives -> {} unique materials "
        "({:.1f}% reuse)",
        normalizedPath.string(),
        dedupStats.primitiveCount,
        modelMaterialIDs.size(),
        materialReuse
    );

    Logger::Log(
        Logger::LOG,
        "Model loaded '{}': {} renderable instances, {} unique GPU meshes, "
        "{} unique materials",
        normalizedPath.string(),
        model->primitives.size(),
        dedupStats.uniqueGeometryCount,
        modelMaterialIDs.size()
    );

    mModels.emplace(normalizedPath, model);
    return model;
}

