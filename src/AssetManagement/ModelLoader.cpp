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

    struct DiagnosticCorner {
        std::array<uint32_t, 3> position{};
        std::array<uint32_t, 3> normal{};
        std::array<uint32_t, 2> uv{};
        std::array<uint32_t, 4> tangent{};
    };

    struct DiagnosticTriangle {
        std::array<DiagnosticCorner, 3> corners{};
    };

    struct PrimitiveDiagnostic {
        std::string name;
        std::size_t vertexCount = 0;
        std::size_t indexCount = 0;
        uint64_t positionTopologyHash = 0;
        std::vector<DiagnosticTriangle> triangles;
    };

    struct AttributeComparison {
        bool positionTopologySame = true;
        bool normalsSame = true;
        bool uvsSame = true;
        bool tangentsSame = true;

        std::size_t normalTriangle = 0;
        std::size_t normalCorner = 0;
        std::size_t uvTriangle = 0;
        std::size_t uvCorner = 0;
        std::size_t tangentTriangle = 0;
        std::size_t tangentCorner = 0;
    };

    struct RigidShapeDiagnostic {
        std::string name;
        std::size_t vertexCount = 0;
        std::size_t indexCount = 0;
        uint64_t positionTopologyHash = 0;
        uint64_t shapeHash = 0;
        std::vector<std::array<uint64_t, 3>> triangleEdges;
    };

    struct ScaledShapeDiagnostic {
        std::string name;
        std::size_t vertexCount = 0;
        std::size_t indexCount = 0;
        uint64_t normalizedShapeHash = 0;
        double referenceEdgeLength = 0.0;
        std::vector<std::array<uint64_t, 3>> normalizedTriangleEdges;
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

    struct GeometryDedupStats {
        std::size_t primitiveCount = 0;
        std::size_t uniqueGeometryCount = 0;
        std::size_t reusedGeometryCount = 0;
        std::size_t translatedReuseCount = 0;
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

    bool PositionLess(
        const DiagnosticCorner& lhs,
        const DiagnosticCorner& rhs
    ) {
        return lhs.position < rhs.position;
    }

    bool TrianglePositionLess(
        const DiagnosticTriangle& lhs,
        const DiagnosticTriangle& rhs
    ) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            if (lhs.corners[corner].position <
                rhs.corners[corner].position) {
                return true;
            }

            if (rhs.corners[corner].position <
                lhs.corners[corner].position) {
                return false;
            }
        }

        return false;
    }

    DiagnosticTriangle RotateDiagnosticTriangle(
        const DiagnosticTriangle& triangle,
        std::size_t rotation
    ) {
        DiagnosticTriangle result;

        for (std::size_t corner = 0; corner < 3; ++corner) {
            result.corners[corner] =
                triangle.corners[(corner + rotation) % 3];
        }

        return result;
    }

    PrimitiveDiagnostic BuildPrimitiveDiagnostic(
        std::string name,
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices
    ) {
        PrimitiveDiagnostic diagnostic;
        diagnostic.name = std::move(name);
        diagnostic.vertexCount = vertices.size();
        diagnostic.indexCount = indices.size();
        diagnostic.triangles.reserve(indices.size() / 3);

        const glm::vec3 origin =
            FindGeometryOrigin(vertices);

        auto makeCorner =
            [&](uint32_t vertexIndex) {
                const Vertex& vertex =
                    vertices[vertexIndex];

                const glm::vec3 relativePosition =
                    vertex.pos - origin;

                return DiagnosticCorner {
                    .position = {
                        CanonicalFloatBits(relativePosition.x),
                        CanonicalFloatBits(relativePosition.y),
                        CanonicalFloatBits(relativePosition.z)
                    },
                    .normal = {
                        CanonicalFloatBits(vertex.normal.x),
                        CanonicalFloatBits(vertex.normal.y),
                        CanonicalFloatBits(vertex.normal.z)
                    },
                    .uv = {
                        CanonicalFloatBits(vertex.uv.x),
                        CanonicalFloatBits(vertex.uv.y)
                    },
                    .tangent = {
                        CanonicalFloatBits(vertex.tangent.x),
                        CanonicalFloatBits(vertex.tangent.y),
                        CanonicalFloatBits(vertex.tangent.z),
                        CanonicalFloatBits(vertex.tangent.w)
                    }
                };
            };

        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            DiagnosticTriangle triangle {
                .corners = {
                    makeCorner(indices[i]),
                    makeCorner(indices[i + 1]),
                    makeCorner(indices[i + 2])
                }
            };

            DiagnosticTriangle best = triangle;

            for (std::size_t rotation = 1; rotation < 3; ++rotation) {
                DiagnosticTriangle candidate =
                    RotateDiagnosticTriangle(
                        triangle,
                        rotation
                    );

                if (TrianglePositionLess(candidate, best)) {
                    best = std::move(candidate);
                }
            }

            diagnostic.triangles.push_back(
                std::move(best)
            );
        }

        std::sort(
            diagnostic.triangles.begin(),
            diagnostic.triangles.end(),
            TrianglePositionLess
        );

        uint64_t hash = FNV_OFFSET_BASIS;

        HashUint64(
            hash,
            static_cast<uint64_t>(diagnostic.vertexCount)
        );

        HashUint64(
            hash,
            static_cast<uint64_t>(diagnostic.indexCount)
        );

        for (const DiagnosticTriangle& triangle :
             diagnostic.triangles) {
            for (const DiagnosticCorner& corner :
                 triangle.corners) {
                for (uint32_t value : corner.position) {
                    HashUint32(hash, value);
                }
            }
        }

        diagnostic.positionTopologyHash = hash;
        return diagnostic;
    }

    AttributeComparison ComparePrimitiveAttributes(
        const PrimitiveDiagnostic& lhs,
        const PrimitiveDiagnostic& rhs
    ) {
        AttributeComparison comparison;

        if (lhs.vertexCount != rhs.vertexCount ||
            lhs.indexCount != rhs.indexCount ||
            lhs.triangles.size() != rhs.triangles.size()) {
            comparison.positionTopologySame = false;
            return comparison;
        }

        for (std::size_t triangle = 0;
             triangle < lhs.triangles.size();
             ++triangle) {
            for (std::size_t corner = 0;
                 corner < 3;
                 ++corner) {
                const DiagnosticCorner& a =
                    lhs.triangles[triangle].corners[corner];

                const DiagnosticCorner& b =
                    rhs.triangles[triangle].corners[corner];

                if (a.position != b.position) {
                    comparison.positionTopologySame = false;
                    return comparison;
                }

                if (comparison.normalsSame &&
                    a.normal != b.normal) {
                    comparison.normalsSame = false;
                    comparison.normalTriangle = triangle;
                    comparison.normalCorner = corner;
                }

                if (comparison.uvsSame &&
                    a.uv != b.uv) {
                    comparison.uvsSame = false;
                    comparison.uvTriangle = triangle;
                    comparison.uvCorner = corner;
                }

                if (comparison.tangentsSame &&
                    a.tangent != b.tangent) {
                    comparison.tangentsSame = false;
                    comparison.tangentTriangle = triangle;
                    comparison.tangentCorner = corner;
                }
            }
        }

        return comparison;
    }

    float DiagnosticFloat(uint32_t bits) {
        return std::bit_cast<float>(bits);
    }

    void LogAttributeDifference(
        const PrimitiveDiagnostic& lhs,
        const PrimitiveDiagnostic& rhs,
        const AttributeComparison& comparison
    ) {
        Logger::Log(
            Logger::LOG,
            "Primitive attribute candidate '{}' vs '{}': "
            "position/topology=same, normals={}, uvs={}, tangents={}",
            lhs.name,
            rhs.name,
            comparison.normalsSame ? "same" : "DIFFERENT",
            comparison.uvsSame ? "same" : "DIFFERENT",
            comparison.tangentsSame ? "same" : "DIFFERENT"
        );

        if (!comparison.normalsSame) {
            const DiagnosticCorner& a =
                lhs.triangles[comparison.normalTriangle]
                    .corners[comparison.normalCorner];

            const DiagnosticCorner& b =
                rhs.triangles[comparison.normalTriangle]
                    .corners[comparison.normalCorner];

            Logger::Log(
                Logger::LOG,
                "  first normal difference at triangle {}, corner {}: "
                "({}, {}, {}) vs ({}, {}, {})",
                comparison.normalTriangle,
                comparison.normalCorner,
                DiagnosticFloat(a.normal[0]),
                DiagnosticFloat(a.normal[1]),
                DiagnosticFloat(a.normal[2]),
                DiagnosticFloat(b.normal[0]),
                DiagnosticFloat(b.normal[1]),
                DiagnosticFloat(b.normal[2])
            );
        }

        if (!comparison.uvsSame) {
            const DiagnosticCorner& a =
                lhs.triangles[comparison.uvTriangle]
                    .corners[comparison.uvCorner];

            const DiagnosticCorner& b =
                rhs.triangles[comparison.uvTriangle]
                    .corners[comparison.uvCorner];

            Logger::Log(
                Logger::LOG,
                "  first UV difference at triangle {}, corner {}: "
                "({}, {}) vs ({}, {})",
                comparison.uvTriangle,
                comparison.uvCorner,
                DiagnosticFloat(a.uv[0]),
                DiagnosticFloat(a.uv[1]),
                DiagnosticFloat(b.uv[0]),
                DiagnosticFloat(b.uv[1])
            );
        }

        if (!comparison.tangentsSame) {
            const DiagnosticCorner& a =
                lhs.triangles[comparison.tangentTriangle]
                    .corners[comparison.tangentCorner];

            const DiagnosticCorner& b =
                rhs.triangles[comparison.tangentTriangle]
                    .corners[comparison.tangentCorner];

            Logger::Log(
                Logger::LOG,
                "  first tangent difference at triangle {}, corner {}: "
                "({}, {}, {}, {}) vs ({}, {}, {}, {})",
                comparison.tangentTriangle,
                comparison.tangentCorner,
                DiagnosticFloat(a.tangent[0]),
                DiagnosticFloat(a.tangent[1]),
                DiagnosticFloat(a.tangent[2]),
                DiagnosticFloat(a.tangent[3]),
                DiagnosticFloat(b.tangent[0]),
                DiagnosticFloat(b.tangent[1]),
                DiagnosticFloat(b.tangent[2]),
                DiagnosticFloat(b.tangent[3])
            );
        }
    }

    constexpr double RIGID_EDGE_QUANTIZATION = 100000.0;

    uint64_t QuantizeEdgeLength(
        const glm::vec3& a,
        const glm::vec3& b
    ) {
        const double length =
            static_cast<double>(glm::length(b - a));

        return static_cast<uint64_t>(
            std::llround(length * RIGID_EDGE_QUANTIZATION)
        );
    }

    RigidShapeDiagnostic BuildRigidShapeDiagnostic(
        std::string name,
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        uint64_t positionTopologyHash
    ) {
        RigidShapeDiagnostic diagnostic;
        diagnostic.name = std::move(name);
        diagnostic.vertexCount = vertices.size();
        diagnostic.indexCount = indices.size();
        diagnostic.positionTopologyHash =
            positionTopologyHash;

        diagnostic.triangleEdges.reserve(
            indices.size() / 3
        );

        for (std::size_t i = 0;
             i + 2 < indices.size();
             i += 3) {
            const glm::vec3& p0 =
                vertices[indices[i]].pos;

            const glm::vec3& p1 =
                vertices[indices[i + 1]].pos;

            const glm::vec3& p2 =
                vertices[indices[i + 2]].pos;

            std::array<uint64_t, 3> edges {
                QuantizeEdgeLength(p0, p1),
                QuantizeEdgeLength(p1, p2),
                QuantizeEdgeLength(p2, p0)
            };

            std::sort(edges.begin(), edges.end());
            diagnostic.triangleEdges.push_back(edges);
        }

        std::sort(
            diagnostic.triangleEdges.begin(),
            diagnostic.triangleEdges.end()
        );

        uint64_t hash = FNV_OFFSET_BASIS;

        HashUint64(
            hash,
            static_cast<uint64_t>(diagnostic.vertexCount)
        );

        HashUint64(
            hash,
            static_cast<uint64_t>(diagnostic.indexCount)
        );

        for (const auto& triangle :
             diagnostic.triangleEdges) {
            HashUint64(hash, triangle[0]);
            HashUint64(hash, triangle[1]);
            HashUint64(hash, triangle[2]);
        }

        diagnostic.shapeHash = hash;
        return diagnostic;
    }

    constexpr double SCALE_EDGE_QUANTIZATION = 1000000.0;

    ScaledShapeDiagnostic BuildScaledShapeDiagnostic(
        std::string name,
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices
    ) {
        ScaledShapeDiagnostic diagnostic;
        diagnostic.name = std::move(name);
        diagnostic.vertexCount = vertices.size();
        diagnostic.indexCount = indices.size();

        std::vector<std::array<double, 3>> triangleEdges;
        triangleEdges.reserve(indices.size() / 3);

        double longestEdge = 0.0;

        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            const glm::vec3& p0 = vertices[indices[i]].pos;
            const glm::vec3& p1 = vertices[indices[i + 1]].pos;
            const glm::vec3& p2 = vertices[indices[i + 2]].pos;

            std::array<double, 3> edges {
                static_cast<double>(glm::length(p1 - p0)),
                static_cast<double>(glm::length(p2 - p1)),
                static_cast<double>(glm::length(p0 - p2))
            };

            std::sort(edges.begin(), edges.end());
            longestEdge = std::max(longestEdge, edges[2]);
            triangleEdges.push_back(edges);
        }

        diagnostic.referenceEdgeLength = longestEdge;

        if (longestEdge <= 0.0) {
            return diagnostic;
        }

        diagnostic.normalizedTriangleEdges.reserve(
            triangleEdges.size()
        );

        for (const auto& edges : triangleEdges) {
            std::array<uint64_t, 3> normalized {
                static_cast<uint64_t>(std::llround(
                    (edges[0] / longestEdge) *
                    SCALE_EDGE_QUANTIZATION
                )),
                static_cast<uint64_t>(std::llround(
                    (edges[1] / longestEdge) *
                    SCALE_EDGE_QUANTIZATION
                )),
                static_cast<uint64_t>(std::llround(
                    (edges[2] / longestEdge) *
                    SCALE_EDGE_QUANTIZATION
                ))
            };

            diagnostic.normalizedTriangleEdges.push_back(
                normalized
            );
        }

        std::sort(
            diagnostic.normalizedTriangleEdges.begin(),
            diagnostic.normalizedTriangleEdges.end()
        );

        uint64_t hash = FNV_OFFSET_BASIS;

        HashUint64(
            hash,
            static_cast<uint64_t>(diagnostic.vertexCount)
        );

        HashUint64(
            hash,
            static_cast<uint64_t>(diagnostic.indexCount)
        );

        for (const auto& triangle :
             diagnostic.normalizedTriangleEdges) {
            HashUint64(hash, triangle[0]);
            HashUint64(hash, triangle[1]);
            HashUint64(hash, triangle[2]);
        }

        diagnostic.normalizedShapeHash = hash;
        return diagnostic;
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
    std::unordered_map<uint64_t, std::vector<PrimitiveDiagnostic>>
        attributeDiagnosticCache;
    std::unordered_map<uint64_t, std::vector<RigidShapeDiagnostic>>
        rigidShapeDiagnosticCache;
    std::unordered_map<uint64_t, std::vector<ScaledShapeDiagnostic>>
        scaledShapeDiagnosticCache;
    std::unordered_map<uint64_t, std::vector<AffineDiagnostic>>
        affineDiagnosticCache;
    std::unordered_set<MaterialID> modelMaterialIDs;
    GeometryDedupStats dedupStats;

    std::size_t attributeDifferencePairs = 0;
    std::size_t rigidShapePairs = 0;
    std::size_t uniformScalePairs = 0;
    std::size_t affinePairs = 0;

    constexpr std::size_t MAX_ATTRIBUTE_DIAGNOSTIC_LOGS = 32;
    constexpr std::size_t MAX_RIGID_SHAPE_DIAGNOSTIC_LOGS = 32;
    constexpr std::size_t MAX_SCALE_DIAGNOSTIC_LOGS = 32;
    constexpr std::size_t MAX_AFFINE_DIAGNOSTIC_LOGS = 32;

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

            PrimitiveDiagnostic diagnostic =
                BuildPrimitiveDiagnostic(
                    primitiveName,
                    vertices,
                    indices
                );

            auto& diagnosticCandidates =
                attributeDiagnosticCache[
                    diagnostic.positionTopologyHash
                ];

            for (const PrimitiveDiagnostic& candidate :
                 diagnosticCandidates) {
                const AttributeComparison comparison =
                    ComparePrimitiveAttributes(
                        candidate,
                        diagnostic
                    );

                if (!comparison.positionTopologySame ||
                    (comparison.normalsSame &&
                     comparison.uvsSame &&
                     comparison.tangentsSame)) {
                    continue;
                }

                ++attributeDifferencePairs;

                if (attributeDifferencePairs <=
                    MAX_ATTRIBUTE_DIAGNOSTIC_LOGS) {
                    LogAttributeDifference(
                        candidate,
                        diagnostic,
                        comparison
                    );
                }
            }

            RigidShapeDiagnostic rigidDiagnostic =
                BuildRigidShapeDiagnostic(
                    primitiveName,
                    vertices,
                    indices,
                    diagnostic.positionTopologyHash
                );

            auto& rigidCandidates =
                rigidShapeDiagnosticCache[
                    rigidDiagnostic.shapeHash
                ];

            for (const RigidShapeDiagnostic& candidate :
                 rigidCandidates) {
                if (candidate.triangleEdges !=
                    rigidDiagnostic.triangleEdges) {
                    continue;
                }

                // Same translated position/topology was already handled by
                // the existing diagnostics/dedup path. A different position
                // signature with the same edge lengths is the interesting
                // rotation/reflection case.
                if (candidate.positionTopologyHash ==
                    rigidDiagnostic.positionTopologyHash) {
                    continue;
                }

                ++rigidShapePairs;

                if (rigidShapePairs <=
                    MAX_RIGID_SHAPE_DIAGNOSTIC_LOGS) {
                    Logger::Log(
                        Logger::LOG,
                        "Rigid-shape candidate '{}' vs '{}': same quantized "
                        "triangle edge-length signature but different "
                        "position/topology; likely baked rotation/reflection "
                        "(diagnostic only)",
                        candidate.name,
                        rigidDiagnostic.name
                    );
                }
            }

            rigidCandidates.push_back(
                std::move(rigidDiagnostic)
            );

            ScaledShapeDiagnostic scaledDiagnostic =
                BuildScaledShapeDiagnostic(
                    primitiveName,
                    vertices,
                    indices
                );

            if (scaledDiagnostic.referenceEdgeLength > 0.0) {
                auto& scaledCandidates =
                    scaledShapeDiagnosticCache[
                        scaledDiagnostic.normalizedShapeHash
                    ];

                for (const ScaledShapeDiagnostic& candidate :
                     scaledCandidates) {
                    if (candidate.normalizedTriangleEdges !=
                        scaledDiagnostic.normalizedTriangleEdges) {
                        continue;
                    }

                    const double scaleRatio =
                        scaledDiagnostic.referenceEdgeLength /
                        candidate.referenceEdgeLength;

                    if (std::abs(scaleRatio - 1.0) < 1e-5) {
                        continue;
                    }

                    ++uniformScalePairs;

                    if (uniformScalePairs <=
                        MAX_SCALE_DIAGNOSTIC_LOGS) {
                        Logger::Log(
                            Logger::LOG,
                            "Uniform-scale candidate '{}' vs '{}': same "
                            "normalized triangle edge signature, estimated "
                            "scale ratio {:.6f} (diagnostic only)",
                            candidate.name,
                            scaledDiagnostic.name,
                            scaleRatio
                        );
                    }
                }

                scaledCandidates.push_back(
                    std::move(scaledDiagnostic)
                );
            }

            AffineDiagnostic affineDiagnostic;
            affineDiagnostic.name = primitiveName;
            affineDiagnostic.indices = indices;
            affineDiagnostic.positions.reserve(vertices.size());

            for (const Vertex& vertex : vertices) {
                affineDiagnostic.positions.push_back(vertex.pos);
            }

            affineDiagnostic.topologyHash =
                HashRawTopology(
                    affineDiagnostic.positions.size(),
                    affineDiagnostic.indices
                );

            auto& affineCandidates =
                affineDiagnosticCache[
                    affineDiagnostic.topologyHash
                ];

            for (const AffineDiagnostic& candidate :
                 affineCandidates) {
                AffineMatch match;

                if (!RecoverAffineTransform(
                        candidate,
                        affineDiagnostic,
                        match)) {
                    continue;
                }

                const glm::vec3 scale =
                    AffineColumnScale(match.linear);

                const float uniformity =
                    std::max({
                        std::abs(scale.x - scale.y),
                        std::abs(scale.x - scale.z),
                        std::abs(scale.y - scale.z)
                    });

                // Rigid/uniform-scale cases are already covered by the
                // preceding diagnostics. Keep this log focused on the
                // remaining non-uniform/sheared affine transforms.
                if (uniformity <= 1e-4f) {
                    continue;
                }

                ++affinePairs;

                if (affinePairs <=
                    MAX_AFFINE_DIAGNOSTIC_LOGS) {
                    Logger::Log(
                        Logger::LOG,
                        "Affine-shape candidate '{}' vs '{}': exact raw "
                        "topology/order and all positions match after an "
                        "affine transform. column scales=({}, {}, {}), "
                        "translation=({}, {}, {}), max error={}. "
                        "Likely baked non-uniform scale/shear "
                        "(diagnostic only)",
                        candidate.name,
                        affineDiagnostic.name,
                        scale.x,
                        scale.y,
                        scale.z,
                        match.translation.x,
                        match.translation.y,
                        match.translation.z,
                        match.maxError
                    );
                }
            }

            affineCandidates.push_back(
                std::move(affineDiagnostic)
            );

            diagnosticCandidates.push_back(
                std::move(diagnostic)
            );

            const MaterialID materialID =
                GetOrCreateMaterial(materialDescription);

            modelMaterialIDs.insert(materialID);

            const std::size_t geometryBytes =
                vertices.size() * sizeof(Vertex) +
                indices.size() * sizeof(uint32_t);

            ++dedupStats.primitiveCount;
            dedupStats.sourceBytes += geometryBytes;

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
                    .geometry = std::move(canonicalGeometry),
                    .origin = geometryOrigin
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

    if (attributeDifferencePairs > 0) {
        Logger::Log(
            Logger::LOG,
            "Primitive attribute diagnostics '{}': found {} pair(s) with "
            "matching translated position/topology but differing vertex "
            "attributes; logged first {}",
            normalizedPath.string(),
            attributeDifferencePairs,
            std::min(
                attributeDifferencePairs,
                MAX_ATTRIBUTE_DIAGNOSTIC_LOGS
            )
        );
    } else {
        Logger::Log(
            Logger::LOG,
            "Primitive attribute diagnostics '{}': no primitives shared "
            "translated position/topology while differing only in normal/UV/"
            "tangent attributes",
            normalizedPath.string()
        );
    }

    if (rigidShapePairs > 0) {
        Logger::Log(
            Logger::LOG,
            "Rigid-shape diagnostics '{}': found {} pair(s) with matching "
            "triangle edge-length signatures but different position/topology; "
            "logged first {}. These are candidates for baked "
            "rotation/reflection, not automatic deduplication yet.",
            normalizedPath.string(),
            rigidShapePairs,
            std::min(
                rigidShapePairs,
                MAX_RIGID_SHAPE_DIAGNOSTIC_LOGS
            )
        );
    } else {
        Logger::Log(
            Logger::LOG,
            "Rigid-shape diagnostics '{}': no rotation/reflection candidates "
            "found from triangle edge-length signatures",
            normalizedPath.string()
        );
    }

    if (uniformScalePairs > 0) {
        Logger::Log(
            Logger::LOG,
            "Uniform-scale diagnostics '{}': found {} pair(s) matching after "
            "uniform scale normalization; logged first {}",
            normalizedPath.string(),
            uniformScalePairs,
            std::min(
                uniformScalePairs,
                MAX_SCALE_DIAGNOSTIC_LOGS
            )
        );
    } else {
        Logger::Log(
            Logger::LOG,
            "Uniform-scale diagnostics '{}': no additional uniform-scale "
            "duplicates found",
            normalizedPath.string()
        );
    }

    if (affinePairs > 0) {
        Logger::Log(
            Logger::LOG,
            "Affine-shape diagnostics '{}': found {} pair(s) whose positions "
            "are reproduced by a verified non-uniform affine transform with "
            "matching raw topology/order; logged first {}",
            normalizedPath.string(),
            affinePairs,
            std::min(
                affinePairs,
                MAX_AFFINE_DIAGNOSTIC_LOGS
            )
        );
    } else {
        Logger::Log(
            Logger::LOG,
            "Affine-shape diagnostics '{}': no verified non-uniform affine "
            "duplicates found among primitives with matching raw topology/order",
            normalizedPath.string()
        );
    }

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
        "({} reused, {} via baked translation, {:.1f}% reduction), "
        "upload data {:.2f} MiB -> {:.2f} MiB ({:.1f}% reduction)",
        normalizedPath.string(),
        dedupStats.primitiveCount,
        dedupStats.uniqueGeometryCount,
        dedupStats.reusedGeometryCount,
        dedupStats.translatedReuseCount,
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
        "Material sharing '{}': {} primitive references -> {} unique materials "
        "({:.1f}% reuse)",
        normalizedPath.string(),
        dedupStats.primitiveCount,
        modelMaterialIDs.size(),
        materialReuse
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

