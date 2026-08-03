#include "mesh_ops.h"
#include "mesh_spatial_index.h"

#include <manifold/cross_section.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxCoordinateMagnitude = 1'000'000'000.0;
constexpr size_t kMaxManifoldVertices = 10'000'000;
constexpr size_t kMaxManifoldTriangles = 20'000'000;
constexpr size_t kMaxCutPoints = 1'000;
constexpr size_t kMaxConnectorPlacements = 100;
constexpr double kMaxConnectorDimension = 1'000'000.0;

struct RepairEdgeKey {
    uint32_t a = 0;
    uint32_t b = 0;
    bool operator==(const RepairEdgeKey&) const = default;
};

struct RepairEdgeHash {
    size_t operator()(const RepairEdgeKey& edge) const noexcept {
        return static_cast<size_t>((static_cast<uint64_t>(edge.a) << 32) ^ edge.b);
    }
};

struct RepairOccurrence {
    size_t triangle = 0;
    int corner = 0;
    uint32_t from = 0;
    uint32_t to = 0;
};

using RepairEdgeMap = std::unordered_map<RepairEdgeKey, std::vector<RepairOccurrence>, RepairEdgeHash>;

RepairEdgeMap repairEdges(const TriangleMesh& mesh) {
    RepairEdgeMap edges;
    edges.reserve(mesh.triangles.size() * 3);
    for (size_t triangleIndex = 0; triangleIndex < mesh.triangles.size(); ++triangleIndex) {
        const auto& triangle = mesh.triangles[triangleIndex];
        for (int corner = 0; corner < 3; ++corner) {
            const uint32_t from = triangle[static_cast<size_t>(corner)];
            const uint32_t to = triangle[static_cast<size_t>((corner + 1) % 3)];
            if (from >= mesh.vertices.size() || to >= mesh.vertices.size()) continue;
            edges[{std::min(from, to), std::max(from, to)}].push_back(
                {triangleIndex, corner, from, to});
        }
    }
    return edges;
}

std::vector<double> connectorPositions(double lineLength, const DovetailSettings& s) {
    if (!std::isfinite(lineLength) || lineLength <= 0.0) return {};
    const double edge = std::max(s.endMargin, 0.5 * std::max(s.headWidth, s.neckWidth) + 0.5);
    const double usable = lineLength - 2.0 * edge;
    if (usable <= 0.0) return {};
    std::vector<double> pos;
    if (s.distribution == DovetailSettings::Distribution::Count) {
        const int count = std::clamp(s.count, 1, 100);
        pos.reserve(count);
        for (int i = 0; i < count; ++i)
            pos.push_back(edge + usable * (static_cast<double>(i) + 0.5) / count);
    } else {
        const double spacing = std::max(s.spacing, 1.0);
        const double rawCount = std::floor(usable / spacing) + 1.0;
        const int count = static_cast<int>(std::clamp(rawCount, 1.0, 100.0));
        const double occupied = (count - 1) * spacing;
        const double first = edge + 0.5 * (usable - occupied);
        pos.reserve(count);
        for (int i = 0; i < count; ++i) pos.push_back(first + i * spacing);
    }
    return pos;
}

double polylineLengthInternal(const std::vector<Vec2>& points) {
    double total = 0.0;
    for (size_t i = 1; i < points.size(); ++i) total += length(points[i] - points[i - 1]);
    return total;
}

struct PolylineSample {
    Vec2 center;
    Vec2 tangent;
    Vec2 left;
    size_t segmentIndex = 0;
};

PolylineSample samplePolyline(const std::vector<Vec2>& points, double position) {
    PolylineSample sample{};
    if (points.size() < 2) return sample;
    const double total = polylineLengthInternal(points);
    if (total < 1e-9) return sample;
    double target = std::clamp(position, 0.0, 1.0) * total;
    double travelled = 0.0;
    for (size_t i = 1; i < points.size(); ++i) {
        const Vec2 delta = points[i] - points[i - 1];
        const double segmentLength = length(delta);
        if (segmentLength < 1e-9) continue;
        if (target <= travelled + segmentLength || i + 1 == points.size()) {
            const double local = std::clamp((target - travelled) / segmentLength, 0.0, 1.0);
            sample.tangent = delta / segmentLength;
            sample.left = leftNormal(sample.tangent);
            sample.center = points[i - 1] + delta * local;
            sample.segmentIndex = i - 1;
            return sample;
        }
        travelled += segmentLength;
    }
    return sample;
}

double cross2(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

bool pointOnSegment(Vec2 p, Vec2 a, Vec2 b, double eps) {
    if (std::abs(cross2(b - a, p - a)) > eps) return false;
    return dot(p - a, p - b) <= eps;
}

bool segmentsIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d, double eps) {
    const double abC = cross2(b - a, c - a);
    const double abD = cross2(b - a, d - a);
    const double cdA = cross2(d - c, a - c);
    const double cdB = cross2(d - c, b - c);
    if (((abC > eps && abD < -eps) || (abC < -eps && abD > eps)) &&
        ((cdA > eps && cdB < -eps) || (cdA < -eps && cdB > eps))) return true;
    return (std::abs(abC) <= eps && pointOnSegment(c, a, b, eps)) ||
           (std::abs(abD) <= eps && pointOnSegment(d, a, b, eps)) ||
           (std::abs(cdA) <= eps && pointOnSegment(a, c, d, eps)) ||
           (std::abs(cdB) <= eps && pointOnSegment(b, c, d, eps));
}

Vec2 rayToRect(Vec2 origin, Vec2 direction, double minX, double maxX, double minY, double maxY) {
    constexpr double eps = 1e-12;
    double best = 1e300;
    if (direction.x > eps) best = std::min(best, (maxX - origin.x) / direction.x);
    if (direction.x < -eps) best = std::min(best, (minX - origin.x) / direction.x);
    if (direction.y > eps) best = std::min(best, (maxY - origin.y) / direction.y);
    if (direction.y < -eps) best = std::min(best, (minY - origin.y) / direction.y);
    return origin + direction * std::max(best, 0.0);
}

double rectParameter(Vec2 p, double minX, double maxX, double minY, double maxY) {
    const double w = maxX - minX;
    const double h = maxY - minY;
    const double dBottom = std::abs(p.y - minY);
    const double dRight = std::abs(p.x - maxX);
    const double dTop = std::abs(p.y - maxY);
    const double dLeft = std::abs(p.x - minX);
    const double nearest = std::min({dBottom, dRight, dTop, dLeft});
    if (nearest == dBottom) return std::clamp(p.x - minX, 0.0, w);
    if (nearest == dRight) return w + std::clamp(p.y - minY, 0.0, h);
    if (nearest == dTop) return w + h + std::clamp(maxX - p.x, 0.0, w);
    return 2.0 * w + h + std::clamp(maxY - p.y, 0.0, h);
}

Vec2 rectPoint(double parameter, double minX, double maxX, double minY, double maxY) {
    const double w = maxX - minX;
    const double h = maxY - minY;
    const double perimeter = 2.0 * (w + h);
    double s = std::fmod(parameter, perimeter);
    if (s < 0.0) s += perimeter;
    if (s <= w) return {minX + s, minY};
    s -= w;
    if (s <= h) return {maxX, minY + s};
    s -= h;
    if (s <= w) return {maxX - s, maxY};
    s -= w;
    return {minX, maxY - s};
}

std::vector<Vec2> leftRegionOutline(const TriangleMesh& source, const std::vector<Vec2>& points) {
    double minX = source.bounds.min.x;
    double maxX = source.bounds.max.x;
    double minY = source.bounds.min.y;
    double maxY = source.bounds.max.y;
    for (Vec2 p : points) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    const double margin = std::max({maxX - minX, maxY - minY, source.bounds.diagonal(), 10.0}) * 2.0;
    minX -= margin; maxX += margin; minY -= margin; maxY += margin;

    const Vec2 firstTangent = normalized(points[1] - points[0]);
    const Vec2 lastTangent = normalized(points.back() - points[points.size() - 2]);
    const Vec2 start = rayToRect(points.front(), firstTangent * -1.0, minX, maxX, minY, maxY);
    const Vec2 end = rayToRect(points.back(), lastTangent, minX, maxX, minY, maxY);

    std::vector<Vec2> outline;
    outline.reserve(points.size() + 6);
    outline.push_back(start);
    outline.insert(outline.end(), points.begin(), points.end());
    outline.push_back(end);

    const double w = maxX - minX;
    const double h = maxY - minY;
    const double perimeter = 2.0 * (w + h);
    const double endParameter = rectParameter(end, minX, maxX, minY, maxY);
    double startParameter = rectParameter(start, minX, maxX, minY, maxY);
    while (startParameter <= endParameter + 1e-9) startParameter += perimeter;
    const double corners[] = {0.0, w, w + h, 2.0 * w + h};
    for (int cycle = 0; cycle < 2; ++cycle) {
        for (double corner : corners) {
            const double value = corner + cycle * perimeter;
            if (value > endParameter + 1e-9 && value < startParameter - 1e-9)
                outline.push_back(rectPoint(value, minX, maxX, minY, maxY));
        }
    }
    return outline;
}

std::vector<Vec2> visibleConnectorOutline(ConnectorType type, Vec2 center, Vec2 tangent,
                                          Vec2 ownerNormal, double neck, double head, double depth,
                                          double chamferWidth = 0.0, double chamferAngle = 45.0,
                                          bool socketEntry = false) {
    std::vector<Vec2> local;
    if (type == ConnectorType::Dovetail) {
        local = {{-neck * 0.5, 0.0}, {-head * 0.5, depth},
                 { head * 0.5, depth}, { neck * 0.5, 0.0}};
    } else if (type == ConnectorType::StraightTab) {
        local = {{-neck * 0.5, 0.0}, {-neck * 0.5, depth}, {neck * 0.5, depth}, {neck * 0.5, 0.0}};
    } else if (type == ConnectorType::RoundPin) {
        const double radius = std::max(neck * 0.5, 0.25);
        const double centerDepth = std::max(depth - radius, radius * 0.4);
        local.push_back({-radius, 0.0});
        local.push_back({-radius, centerDepth});
        constexpr int steps = 10;
        for (int i = 0; i <= steps; ++i) {
            const double angle = kPi - kPi * static_cast<double>(i) / steps;
            local.push_back({std::cos(angle) * radius, centerDepth + std::sin(angle) * radius});
        }
        local.push_back({radius, 0.0});
    } else {
        const double stem = std::max(neck, 0.5);
        const double bulb = std::max(head, stem + 0.2);
        local = {{-stem * 0.5, 0.0}, {-stem * 0.5, depth * 0.38},
                 {-bulb * 0.5, depth * 0.48}, {-bulb * 0.5, depth * 0.78},
                 {-bulb * 0.28, depth}, {bulb * 0.28, depth},
                 {bulb * 0.5, depth * 0.78}, {bulb * 0.5, depth * 0.48},
                 {stem * 0.5, depth * 0.38}, {stem * 0.5, 0.0}};
    }

    const double width = std::clamp(chamferWidth, 0.0, std::max(0.0, std::min(neck, head) * 0.45));
    const double angle = std::clamp(chamferAngle, 15.0, 75.0) * kPi / 180.0;
    const double run = std::clamp(width / std::max(std::tan(angle), 1e-6), 0.0, depth * 0.45);
    if (width > 1e-6 && run > 1e-6 && local.size() >= 4) {
        if (socketEntry) {
            // Flare only the mouth. The nominal contour is restored after the
            // chamfer run, so the calibrated fit is unchanged when fully seated.
            const Vec2 left = local.front();
            const Vec2 leftNext = local[1];
            const Vec2 rightPrevious = local[local.size() - 2];
            const Vec2 right = local.back();
            auto atDepth = [&](Vec2 from, Vec2 to) {
                const double denominator = to.y - from.y;
                const double t = std::abs(denominator) < 1e-9 ? 0.0
                    : std::clamp((run - from.y) / denominator, 0.0, 1.0);
                return from + (to - from) * t;
            };
            std::vector<Vec2> flared;
            flared.reserve(local.size() + 2);
            flared.push_back({left.x - width, left.y});
            flared.push_back(atDepth(left, leftNext));
            flared.insert(flared.end(), local.begin() + 1, local.end() - 1);
            flared.push_back(atDepth(right, rightPrevious));
            flared.push_back({right.x + width, right.y});
            local = std::move(flared);
        } else if (type == ConnectorType::Dovetail || type == ConnectorType::StraightTab) {
            // Break the two leading nose corners of the male connector.
            const double halfNeck = neck * 0.5;
            const double halfHead = (type == ConnectorType::Dovetail ? head : neck) * 0.5;
            const double sideAtRun = halfNeck + (halfHead - halfNeck) * ((depth - run) / depth);
            local = {{-halfNeck, 0.0}, {-sideAtRun, depth - run},
                     {-halfHead + width, depth}, {halfHead - width, depth},
                     {sideAtRun, depth - run}, {halfNeck, 0.0}};
        }
    }
    std::vector<Vec2> outline;
    outline.reserve(local.size());
    for (Vec2 p : local) outline.push_back(center + tangent * p.x - ownerNormal * p.y);
    if (signedArea(outline) < 0.0) std::reverse(outline.begin(), outline.end());
    return outline;
}

DovetailSettings effectiveSettings(const DovetailSettings& defaults, const ConnectorPlacement& placement) {
    DovetailSettings result = defaults;
    result.type = placement.type;
    if (placement.neckWidth > 0.0) result.neckWidth = placement.neckWidth;
    if (placement.headWidth > 0.0) result.headWidth = placement.headWidth;
    if (placement.depth > 0.0) result.depth = placement.depth;
    if (placement.embed > 0.0) result.embed = placement.embed;
    if (placement.clearance >= 0.0) result.clearance = placement.clearance;
    if (placement.leadChamfer >= 0) result.leadChamfer = placement.leadChamfer != 0;
    if (placement.chamferWidth >= 0.0) result.chamferWidth = placement.chamferWidth;
    if (placement.chamferAngle >= 0.0) result.chamferAngle = placement.chamferAngle;
    if (result.type == ConnectorType::StraightTab || result.type == ConnectorType::RoundPin)
        result.headWidth = result.neckWidth;
    return result;
}

std::vector<Vec2> connectorToolOutline(ConnectorType type, Vec2 center, Vec2 tangent, Vec2 ownerNormal,
                                       double neck, double head, double depth, double embed,
                                       double chamferWidth = 0.0, double chamferAngle = 45.0,
                                       bool socketEntry = false) {
    std::vector<Vec2> visible = visibleConnectorOutline(
        type, center, tangent, ownerNormal, neck, head, depth,
        chamferWidth, chamferAngle, socketEntry);
    if (visible.size() < 3) return visible;
    std::vector<Vec2> tool;
    tool.reserve(visible.size() + 2);
    tool.push_back(visible.front() + ownerNormal * embed);
    tool.insert(tool.end(), visible.begin(), visible.end());
    tool.push_back(visible.back() + ownerNormal * embed);
    if (signedArea(tool) < 0.0) std::reverse(tool.begin(), tool.end());
    return tool;
}

// Boolean tool: same visible contour plus a hidden rectangular anchor inside
// the owning half. This keeps the cut line as the visible connector end while
// still producing a robust union with the owner part.
std::vector<Vec2> maleToolOutline(Vec2 center, Vec2 tangent, Vec2 ownerNormal,
                                  double neck, double head, double depth, double embed) {
    std::vector<Vec2> p{
        center + tangent * (-neck * 0.5) + ownerNormal * embed,
        center + tangent * (-neck * 0.5),
        center + tangent * (-head * 0.5) - ownerNormal * depth,
        center + tangent * ( head * 0.5) - ownerNormal * depth,
        center + tangent * ( neck * 0.5),
        center + tangent * ( neck * 0.5) + ownerNormal * embed
    };
    if (signedArea(p) < 0.0) std::reverse(p.begin(), p.end());
    return p;
}

std::vector<Vec2> socketToolOutline(Vec2 center, Vec2 tangent, Vec2 ownerNormal,
                                    ConnectorType type, double neck, double head, double depth,
                                    double clearance, double embed,
                                    double chamferWidth = 0.0, double chamferAngle = 45.0) {
    const double c = std::max(clearance, 0.0);
    const double back = std::max(embed, std::max(c, 0.05));
    return connectorToolOutline(type, center, tangent, ownerNormal,
                                neck + 2.0 * c, head + 2.0 * c, depth + c, back,
                                chamferWidth, chamferAngle, true);
}

manifold::Manifold prismFromOutline(const std::vector<Vec2>& outline, double zMin, double zMax,
                                    bool extendPastBounds);

bool meshCoordinatesAndIndicesValid(const TriangleMesh& mesh) {
    if (mesh.vertices.empty() || mesh.triangles.empty() ||
        mesh.vertices.size() > kMaxManifoldVertices ||
        mesh.triangles.size() > kMaxManifoldTriangles ||
        mesh.vertices.size() > std::numeric_limits<uint32_t>::max() ||
        mesh.defaultMaterial == 0 ||
        (!mesh.triangleMaterials.empty() &&
         mesh.triangleMaterials.size() != mesh.triangles.size())) return false;
    for (const Vec3& vertex : mesh.vertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z) ||
            std::abs(vertex.x) > kMaxCoordinateMagnitude ||
            std::abs(vertex.y) > kMaxCoordinateMagnitude ||
            std::abs(vertex.z) > kMaxCoordinateMagnitude ||
            std::abs(vertex.x) > std::numeric_limits<float>::max() ||
            std::abs(vertex.y) > std::numeric_limits<float>::max() ||
            std::abs(vertex.z) > std::numeric_limits<float>::max()) return false;
    }
    for (const auto& triangle : mesh.triangles) {
        if (triangle[0] >= mesh.vertices.size() || triangle[1] >= mesh.vertices.size() ||
            triangle[2] >= mesh.vertices.size() || triangle[0] == triangle[1] ||
            triangle[1] == triangle[2] || triangle[2] == triangle[0]) return false;
    }
    return true;
}

struct ManifoldMaterialRelation {
    uint32_t defaultMaterial = 1;
    std::unordered_map<uint32_t, uint32_t> originalToMaterial;
};

uint32_t triangleMaterial(const TriangleMesh& mesh, size_t triangleIndex) {
    if (mesh.triangleMaterials.empty()) return std::max(mesh.defaultMaterial, 1u);
    const uint32_t material = mesh.triangleMaterials[triangleIndex];
    return material == 0 ? std::max(mesh.defaultMaterial, 1u) : material;
}

manifold::Manifold meshToManifoldWithMaterials(const TriangleMesh& mesh,
                                                ManifoldMaterialRelation* relation) {
    if (!meshCoordinatesAndIndicesValid(mesh)) {
        manifold::MeshGL invalid;
        invalid.numProp = 3;
        invalid.vertProperties = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
        return manifold::Manifold(invalid);
    }
    manifold::MeshGL gl;
    gl.numProp = 3;
    gl.vertProperties.reserve(mesh.vertices.size() * 3);
    for (const Vec3& vertex : mesh.vertices) {
        gl.vertProperties.push_back(static_cast<float>(vertex.x));
        gl.vertProperties.push_back(static_cast<float>(vertex.y));
        gl.vertProperties.push_back(static_cast<float>(vertex.z));
    }

    std::set<uint32_t> usedMaterials;
    for (size_t index = 0; index < mesh.triangles.size(); ++index)
        usedMaterials.insert(triangleMaterial(mesh, index));
    const bool needsRuns = relation != nullptr &&
        (usedMaterials.size() > 1 ||
         (!usedMaterials.empty() && *usedMaterials.begin() != std::max(mesh.defaultMaterial, 1u)));
    if (!needsRuns) {
        gl.triVerts.reserve(mesh.triangles.size() * 3);
        for (const auto& triangle : mesh.triangles) {
            gl.triVerts.push_back(triangle[0]);
            gl.triVerts.push_back(triangle[1]);
            gl.triVerts.push_back(triangle[2]);
        }
        if (relation) relation->defaultMaterial = std::max(mesh.defaultMaterial, 1u);
        return manifold::Manifold(gl);
    }

    relation->defaultMaterial = std::max(mesh.defaultMaterial, 1u);
    const uint32_t firstOriginal = manifold::Manifold::ReserveIDs(
        static_cast<uint32_t>(usedMaterials.size()));
    std::unordered_map<uint32_t, uint32_t> materialToOriginal;
    uint32_t ordinal = 0;
    for (const uint32_t material : usedMaterials) {
        const uint32_t original = firstOriginal + ordinal++;
        materialToOriginal.emplace(material, original);
        relation->originalToMaterial.emplace(original, material);
    }

    gl.triVerts.reserve(mesh.triangles.size() * 3);
    gl.runIndex.push_back(0);
    for (const uint32_t material : usedMaterials) {
        gl.runOriginalID.push_back(materialToOriginal.at(material));
        for (size_t index = 0; index < mesh.triangles.size(); ++index) {
            if (triangleMaterial(mesh, index) != material) continue;
            const auto& triangle = mesh.triangles[index];
            gl.triVerts.push_back(triangle[0]);
            gl.triVerts.push_back(triangle[1]);
            gl.triVerts.push_back(triangle[2]);
        }
        gl.runIndex.push_back(static_cast<uint32_t>(gl.triVerts.size()));
    }
    return manifold::Manifold(gl);
}

TriangleMesh manifoldToMeshWithMaterials(const manifold::Manifold& solid,
                                         const ManifoldMaterialRelation* relation,
                                         const TriangleMesh* sourceForGeneratedFaces = nullptr,
                                         const MeshSpatialIndex* sourceIndex = nullptr) {
    const manifold::MeshGL gl = solid.GetMeshGL();
    TriangleMesh mesh;
    mesh.defaultMaterial = relation ? relation->defaultMaterial : 1;
    if (gl.numProp < 3) return mesh;
    const size_t propertyCount = static_cast<size_t>(gl.numProp);
    if (gl.vertProperties.size() % propertyCount != 0 || gl.triVerts.size() % 3 != 0) return mesh;
    const size_t vertexCount = gl.vertProperties.size() / propertyCount;
    const size_t triangleCount = gl.triVerts.size() / 3;
    if (vertexCount == 0 || triangleCount == 0 || vertexCount > kMaxManifoldVertices ||
        triangleCount > kMaxManifoldTriangles || vertexCount > std::numeric_limits<uint32_t>::max()) return mesh;
    mesh.vertices.reserve(vertexCount);
    for (size_t index = 0; index < vertexCount; ++index) {
        Vec3 vertex{gl.vertProperties[index * propertyCount],
                    gl.vertProperties[index * propertyCount + 1],
                    gl.vertProperties[index * propertyCount + 2]};
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z) ||
            std::abs(vertex.x) > kMaxCoordinateMagnitude ||
            std::abs(vertex.y) > kMaxCoordinateMagnitude ||
            std::abs(vertex.z) > kMaxCoordinateMagnitude) return {};
        mesh.vertices.push_back(vertex);
        mesh.bounds.expand(vertex);
    }
    mesh.triangles.reserve(triangleCount);
    for (size_t index = 0; index < gl.triVerts.size(); index += 3) {
        const std::array<uint32_t, 3> triangle{
            gl.triVerts[index], gl.triVerts[index + 1], gl.triVerts[index + 2]};
        if (triangle[0] >= vertexCount || triangle[1] >= vertexCount ||
            triangle[2] >= vertexCount || triangle[0] == triangle[1] ||
            triangle[1] == triangle[2] || triangle[2] == triangle[0]) return {};
        mesh.triangles.push_back(triangle);
    }

    if (relation && !relation->originalToMaterial.empty() &&
        gl.runIndex.size() == gl.runOriginalID.size() + 1) {
        mesh.triangleMaterials.assign(triangleCount, mesh.defaultMaterial);
        for (size_t run = 0; run < gl.runOriginalID.size(); ++run) {
            const size_t begin = std::min<size_t>(gl.runIndex[run] / 3, triangleCount);
            const size_t end = std::min<size_t>(gl.runIndex[run + 1] / 3, triangleCount);
            const auto found = relation->originalToMaterial.find(gl.runOriginalID[run]);
            if (found != relation->originalToMaterial.end()) {
                std::fill(mesh.triangleMaterials.begin() + static_cast<std::ptrdiff_t>(begin),
                          mesh.triangleMaterials.begin() + static_cast<std::ptrdiff_t>(end),
                          found->second);
                continue;
            }
            // Manifold assigns a new provenance ID to faces created by the
            // cutting region or connector tools. Inherit the nearest source
            // surface's slot instead of turning every such face into the
            // object's default color.
            for (size_t triangleIndex = begin; triangleIndex < end; ++triangleIndex) {
                uint32_t material = mesh.defaultMaterial;
                if (sourceForGeneratedFaces && sourceIndex) {
                    const auto& triangle = mesh.triangles[triangleIndex];
                    const Vec3 center = (mesh.vertices[triangle[0]] + mesh.vertices[triangle[1]] +
                                         mesh.vertices[triangle[2]]) / 3.0;
                    const size_t nearest = sourceIndex->closestTriangle(center);
                    if (nearest < sourceForGeneratedFaces->triangles.size())
                        material = triangleMaterial(*sourceForGeneratedFaces, nearest);
                }
                mesh.triangleMaterials[triangleIndex] = material;
            }
        }
        if (std::all_of(mesh.triangleMaterials.begin(), mesh.triangleMaterials.end(),
                        [&](uint32_t material) { return material == mesh.defaultMaterial; }))
            mesh.triangleMaterials.clear();
    }
    return mesh;
}

bool connectorHasMaterial(const TriangleMesh& mesh,
                          const MeshSpatialIndex* spatialIndex,
                          const manifold::Manifold* leftRegion,
                          const manifold::Manifold* leftSolid,
                          const manifold::Manifold* rightSolid,
                          Vec2 center, Vec2 tangent, Vec2 ownerNormal,
                          bool maleOnLeft, const DovetailSettings& s) {
    const std::vector<Vec2> male = connectorToolOutline(
        s.type, center, tangent, ownerNormal,
        s.neckWidth, s.headWidth, s.depth, s.embed,
        s.leadChamfer ? s.chamferWidth : 0.0, s.chamferAngle);
    const std::vector<Vec2> socket = socketToolOutline(
        center, tangent, ownerNormal, s.type,
        s.neckWidth, s.headWidth, s.depth, s.clearance, s.embed,
        s.leadChamfer ? s.chamferWidth : 0.0, s.chamferAngle);

    if (leftRegion != nullptr && leftSolid != nullptr && rightSolid != nullptr) {
        const manifold::Manifold maleTool = prismFromOutline(
            male, mesh.bounds.min.z, mesh.bounds.max.z, false);
        const manifold::Manifold socketTool = prismFromOutline(
            socket, mesh.bounds.min.z, mesh.bounds.max.z, false);
        if (maleTool.Status() != manifold::Manifold::Error::NoError ||
            socketTool.Status() != manifold::Manifold::Error::NoError) return false;

        // Validate against the two *actual* cut halves. Looking only at the
        // complete source mesh lets material on the wrong side compensate for a
        // hole and can therefore mark an unusable connector green.
        const manifold::Manifold maleLeft = maleTool ^ *leftRegion;
        const manifold::Manifold maleRight = maleTool - *leftRegion;
        const manifold::Manifold socketLeft = socketTool ^ *leftRegion;
        const manifold::Manifold socketRight = socketTool - *leftRegion;
        const manifold::Manifold& ownerNominal = maleOnLeft ? maleLeft : maleRight;
        const manifold::Manifold& receiverNominal = maleOnLeft ? maleRight : maleLeft;
        const manifold::Manifold& socketNominal = maleOnLeft ? socketRight : socketLeft;
        const manifold::Manifold& ownerSolid = maleOnLeft ? *leftSolid : *rightSolid;
        const manifold::Manifold& receiverSolid = maleOnLeft ? *rightSolid : *leftSolid;

        auto sufficientlyCovered = [](const manifold::Manifold& nominal,
                                      const manifold::Manifold& solid) {
            if (nominal.Status() != manifold::Manifold::Error::NoError || nominal.IsEmpty())
                return false;
            const double nominalVolume = std::abs(nominal.Volume());
            if (!std::isfinite(nominalVolume) || nominalVolume <= 1e-9) return false;
            const manifold::Manifold covered = nominal ^ solid;
            if (covered.Status() != manifold::Manifold::Error::NoError) return false;
            const double coveredVolume = std::abs(covered.Volume());
            // Allow a small numerical/surface tolerance, but reject connectors
            // that overlap holes, notches or the outside of either cut part.
            constexpr double minimumMaterialFraction = 0.92;
            return std::isfinite(coveredVolume) &&
                   coveredVolume + 1e-7 >= nominalVolume * minimumMaterialFraction;
        };

        return sufficientlyCovered(ownerNominal, ownerSolid) &&
               sufficientlyCovered(receiverNominal, receiverSolid) &&
               sufficientlyCovered(socketNominal, receiverSolid);
    }

    // Conservative fallback for meshes Manifold cannot consume: validate at
    // several heights instead of only the middle slice.
    const double ownerV = std::max(0.25, std::min(s.embed * 0.5, 1.0));
    const double receiverV = std::max(0.5, s.depth * 0.55);
    const double neckU = std::max(0.0, s.neckWidth * 0.40);
    const double headU = std::max(0.0, s.headWidth * 0.40);
    const Vec2 samples[] = {
        center + ownerNormal * ownerV,
        center + tangent * neckU + ownerNormal * ownerV,
        center - tangent * neckU + ownerNormal * ownerV,
        center - ownerNormal * receiverV,
        center + tangent * headU - ownerNormal * receiverV,
        center - tangent * headU - ownerNormal * receiverV,
    };
    int validLayers = 0;
    constexpr int layerCount = 7;
    for (int layer = 0; layer < layerCount; ++layer) {
        const double fraction = (static_cast<double>(layer) + 0.5) / layerCount;
        const double z = mesh.bounds.min.z + (mesh.bounds.max.z - mesh.bounds.min.z) * fraction;
        bool complete = true;
        for (Vec2 point : samples) {
            if (spatialIndex == nullptr ||
                !spatialIndex->pointInsideVolume({point.x, point.y, z})) {
                complete = false;
                break;
            }
        }
        if (complete) ++validLayers;
    }
    return validLayers >= 5;
}

bool pointInPolygon(Vec2 point, const std::vector<Vec2>& polygon, double eps) {
    if (polygon.size() < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2 a = polygon[j];
        const Vec2 b = polygon[i];
        if (pointOnSegment(point, a, b, eps)) return true;
        const bool crosses = ((a.y > point.y) != (b.y > point.y));
        if (crosses) {
            const double x = a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (x >= point.x - eps) inside = !inside;
        }
    }
    return inside;
}

bool polygonsOverlap(const std::vector<Vec2>& first,
                     const std::vector<Vec2>& second,
                     double eps) {
    if (first.size() < 3 || second.size() < 3) return false;
    for (size_t i = 0; i < first.size(); ++i) {
        const Vec2 a = first[i];
        const Vec2 b = first[(i + 1) % first.size()];
        for (size_t j = 0; j < second.size(); ++j) {
            const Vec2 c = second[j];
            const Vec2 d = second[(j + 1) % second.size()];
            if (segmentsIntersect(a, b, c, d, eps)) return true;
        }
    }
    return pointInPolygon(first.front(), second, eps) ||
           pointInPolygon(second.front(), first, eps);
}

bool segmentIntersectsPolygon(Vec2 a, Vec2 b,
                              const std::vector<Vec2>& polygon,
                              double eps) {
    if (polygon.size() < 3) return false;
    if (pointInPolygon(a, polygon, eps) || pointInPolygon(b, polygon, eps)) return true;
    for (size_t index = 0; index < polygon.size(); ++index) {
        if (segmentsIntersect(a, b, polygon[index],
                              polygon[(index + 1) % polygon.size()], eps)) return true;
    }
    return false;
}

manifold::Manifold prismFromOutline(const std::vector<Vec2>& outline, double zMin, double zMax,
                                    bool extendPastBounds = true) {
    manifold::SimplePolygon contour;
    contour.reserve(outline.size());
    for (const auto& p : outline) contour.push_back({p.x, p.y});
    manifold::Polygons polygons{contour};
    const double margin = extendPastBounds
        ? std::max(0.05, (zMax - zMin) * 0.02)
        : 0.0;
    return manifold::Manifold::Extrude(polygons, (zMax - zMin) + 2.0 * margin)
        .Translate({0.0, 0.0, zMin - margin});
}

std::vector<Vec2> markRectangle(Vec2 center, Vec2 tangent, Vec2 left,
                                double localX, double localY,
                                double width, double height) {
    std::vector<Vec2> outline;
    outline.reserve(4);
    for (const Vec2 corner : std::array<Vec2, 4>{{
             {-0.5 * width, -0.5 * height}, {0.5 * width, -0.5 * height},
             {0.5 * width, 0.5 * height}, {-0.5 * width, 0.5 * height}}}) {
        outline.push_back(center + tangent * (localX + corner.x) +
                          left * (localY + corner.y));
    }
    return outline;
}

AssemblyMarkPreview assemblyMarkPreviewInternal(const std::vector<Vec2>& cutPoints,
                                                const DovetailSettings& settings,
                                                int requestedCode) {
    AssemblyMarkPreview preview;
    if (cutPoints.size() < 2 || polylineLengthInternal(cutPoints) < 1e-6) return preview;
    const double height = std::clamp(settings.assemblyMarkSize, 2.0, 30.0);
    const double digitWidth = height * 0.58;
    const double stroke = std::max(height * 0.12, 0.35);
    const double gap = height * 0.18;
    preview.code = std::clamp(requestedCode, 1, 9999);
    const std::string digits = std::to_string(preview.code);
    const double totalWidth = digitWidth * digits.size() + gap * (digits.size() - 1);
    const PolylineSample sample = samplePolyline(
        cutPoints, std::clamp(settings.assemblyMarkPosition, 0.02, 0.98));
    const double sideOffset = height * 0.72 + 0.5;
    preview.leftCenter = sample.center + sample.left * sideOffset;
    preview.rightCenter = sample.center - sample.left * sideOffset;
    if (settings.assemblyMarkPositionsCustom &&
        std::isfinite(settings.assemblyMarkLeftX) &&
        std::isfinite(settings.assemblyMarkLeftY) &&
        std::isfinite(settings.assemblyMarkRightX) &&
        std::isfinite(settings.assemblyMarkRightY) &&
        std::abs(settings.assemblyMarkLeftX) <= kMaxCoordinateMagnitude &&
        std::abs(settings.assemblyMarkLeftY) <= kMaxCoordinateMagnitude &&
        std::abs(settings.assemblyMarkRightX) <= kMaxCoordinateMagnitude &&
        std::abs(settings.assemblyMarkRightY) <= kMaxCoordinateMagnitude) {
        preview.leftCenter = {settings.assemblyMarkLeftX, settings.assemblyMarkLeftY};
        preview.rightCenter = {settings.assemblyMarkRightX, settings.assemblyMarkRightY};
    }
    preview.outlines.reserve(digits.size() * 14);

    // Seven-segment masks: a,b,c,d,e,f,g in bits 0..6.
    static constexpr std::array<unsigned char, 10> masks{{
        0x3F, 0x06, 0x5B, 0x4F, 0x66,
        0x6D, 0x7D, 0x07, 0x7F, 0x6F}};
    auto addDigit = [&](Vec2 center, size_t digitIndex, int digit) {
        const double x = -0.5 * totalWidth + 0.5 * digitWidth +
                         digitIndex * (digitWidth + gap);
        const double horizontalLength = std::max(digitWidth - stroke * 0.35, stroke);
        const double verticalLength = std::max(height * 0.5 - stroke * 0.65, stroke);
        const double xSide = 0.5 * digitWidth - 0.5 * stroke;
        const double yEnd = 0.5 * height - 0.5 * stroke;
        const double ySide = 0.25 * height;
        const unsigned char mask = masks[static_cast<size_t>(digit)];
        auto add = [&](int bit, double sx, double sy, double width, double segmentHeight) {
            if ((mask & (1u << bit)) != 0)
                preview.outlines.push_back(markRectangle(
                    center, sample.tangent, sample.left, x + sx, sy, width, segmentHeight));
        };
        add(0, 0.0, yEnd, horizontalLength, stroke);       // a
        add(1, xSide, ySide, stroke, verticalLength);      // b
        add(2, xSide, -ySide, stroke, verticalLength);     // c
        add(3, 0.0, -yEnd, horizontalLength, stroke);      // d
        add(4, -xSide, -ySide, stroke, verticalLength);    // e
        add(5, -xSide, ySide, stroke, verticalLength);     // f
        add(6, 0.0, 0.0, horizontalLength, stroke);        // g
    };
    for (size_t index = 0; index < digits.size(); ++index) {
        const int digit = digits[index] - '0';
        addDigit(preview.leftCenter, index, digit);
        addDigit(preview.rightCenter, index, digit);
    }
    preview.valid = !preview.outlines.empty();
    return preview;
}

manifold::Manifold assemblyMarkTool(const TriangleMesh& source,
                                    const std::vector<Vec2>& cutPoints,
                                    const DovetailSettings& settings) {
    const double thickness = source.bounds.size().z;
    if (thickness < 0.15) return {};
    const AssemblyMarkPreview preview = assemblyMarkPreviewInternal(
        cutPoints, settings, settings.assemblyMarkCode);
    if (!preview.valid) return {};
    const double depth = std::clamp(settings.assemblyMarkDepth, 0.05,
                                    std::max(0.05, thickness * 0.45));
    manifold::Manifold marks;
    bool hasMark = false;
    for (const std::vector<Vec2>& outline : preview.outlines) {
        manifold::Manifold segment = prismFromOutline(
            outline, source.bounds.max.z - depth, source.bounds.max.z + 0.05, false);
        if (segment.Status() != manifold::Manifold::Error::NoError || segment.IsEmpty()) continue;
        marks = hasMark ? marks + segment : std::move(segment);
        hasMark = true;
    }
    return marks;
}

} // namespace

AssemblyMarkPreview makeAssemblyMarkPreview(const std::vector<Vec2>& cutPoints,
                                            const DovetailSettings& settings,
                                            int code) {
    return assemblyMarkPreviewInternal(cutPoints, settings, code);
}

std::string manifoldErrorText(manifold::Manifold::Error e) {
    using E = manifold::Manifold::Error;
    switch (e) {
        case E::NoError: return "kein Fehler";
        case E::NonFiniteVertex: return "nicht endlicher Vertex";
        case E::NotManifold: return "STL ist nicht wasserdicht/orientiert-manifold";
        case E::VertexOutOfBounds: return "Vertexindex außerhalb des Bereichs";
        case E::PropertiesWrongLength: return "ungültige Vertexdatenlänge";
        case E::MissingPositionProperties: return "Positionsdaten fehlen";
        case E::MergeVectorsDifferentLengths: return "inkonsistente Verschmelzungsdaten";
        case E::MergeIndexOutOfBounds: return "Verschmelzungsindex außerhalb des Bereichs";
        case E::TransformWrongLength: return "ungültige Transformationsdaten";
        case E::RunIndexWrongLength: return "ungültige Run-Indizes";
        case E::FaceIDWrongLength: return "ungültige Face-IDs";
        case E::InvalidConstruction: return "ungültige Geometriekonstruktion";
        case E::ResultTooLarge: return "Ergebnis ist zu groß";
        case E::InvalidTangents: return "ungültige Tangenten";
        case E::Cancelled: return "Vorgang abgebrochen";
    }
    return "unbekannter Fehler";
}

manifold::Manifold meshToManifold(const TriangleMesh& mesh) {
    return meshToManifoldWithMaterials(mesh, nullptr);
}

TriangleMesh manifoldToMesh(const manifold::Manifold& solid) {
    return manifoldToMeshWithMaterials(solid, nullptr);
}

MeshDiagnostics analyzeMesh(const TriangleMesh& mesh) {
    MeshDiagnostics diagnostics;
    const double epsilon = std::max(mesh.bounds.diagonal() * 1e-10, 1e-10);
    for (const auto& triangle : mesh.triangles) {
        if (triangle[0] >= mesh.vertices.size() || triangle[1] >= mesh.vertices.size() ||
            triangle[2] >= mesh.vertices.size() || triangle[0] == triangle[1] ||
            triangle[1] == triangle[2] || triangle[2] == triangle[0]) {
            ++diagnostics.degenerateTriangles;
            continue;
        }
        if (length(cross(mesh.vertices[triangle[1]] - mesh.vertices[triangle[0]],
                         mesh.vertices[triangle[2]] - mesh.vertices[triangle[0]])) <= epsilon * epsilon)
            ++diagnostics.degenerateTriangles;
    }

    const RepairEdgeMap edges = repairEdges(mesh);
    diagnostics.edges.reserve(edges.size() / 20 + 1);
    for (const auto& [key, occurrences] : edges) {
        MeshIssueKind kind;
        bool issue = true;
        if (occurrences.size() == 1) {
            ++diagnostics.openEdges;
            kind = MeshIssueKind::OpenEdge;
        } else if (occurrences.size() > 2) {
            ++diagnostics.nonManifoldEdges;
            kind = MeshIssueKind::NonManifoldEdge;
        } else if (occurrences.size() == 2 &&
                   occurrences[0].from == occurrences[1].from &&
                   occurrences[0].to == occurrences[1].to) {
            ++diagnostics.orientationConflicts;
            kind = MeshIssueKind::OrientationConflict;
        } else {
            issue = false;
            kind = MeshIssueKind::OpenEdge;
        }
        if (issue && key.a < mesh.vertices.size() && key.b < mesh.vertices.size())
            diagnostics.edges.push_back({mesh.vertices[key.a], mesh.vertices[key.b], kind});
    }
    return diagnostics;
}

std::vector<ConnectorCalibrationSample> createConnectorCalibrationSamples(
    const DovetailSettings& settings, double startClearance, double step, int count,
    std::string& error) {
    std::vector<ConnectorCalibrationSample> samples;
    DovetailSettings local = settings;
    local.neckWidth = std::max(local.neckWidth, 0.5);
    local.depth = std::max(local.depth, 0.5);
    local.embed = std::max(local.embed, 0.2);
    if (local.type == ConnectorType::StraightTab || local.type == ConnectorType::RoundPin)
        local.headWidth = local.neckWidth;
    else
        local.headWidth = std::max(local.headWidth, local.neckWidth + 0.2);
    startClearance = std::max(startClearance, 0.0);
    step = std::max(step, 0.01);
    count = std::clamp(count, 2, 12);

    // Stiff calibration coupons: generous side/back walls keep the socket from
    // flexing during the fit test, otherwise the perceived clearance is too large.
    const double blockWidth = std::max({local.headWidth + 16.0, local.neckWidth + 16.0, 32.0});
    const double blockDepth = std::max(local.depth + 10.0, 20.0);
    const double height = 6.0;
    const double columnPitch = blockWidth + 7.0;
    const double maleOffsetY = blockDepth + local.depth + 7.0;
    const Vec2 tangent{1.0, 0.0};
    const Vec2 ownerNormal{0.0, 1.0};
    samples.reserve(static_cast<size_t>(count));

    for (int index = 0; index < count; ++index) {
        const double clearance = startClearance + step * index;
        const double x = columnPitch * index;
        const Vec2 center{x, 0.0};
        manifold::Manifold maleBase = manifold::Manifold::Cube({blockWidth, blockDepth, height})
            .Translate({x - blockWidth * 0.5, 0.0, 0.0});
        manifold::Manifold socketBase = manifold::Manifold::Cube({blockWidth, blockDepth, height})
            .Translate({x - blockWidth * 0.5, -blockDepth, 0.0});
        // One to twelve small through-holes identify the sample physically after printing.
        for (int marker = 0; marker <= index; ++marker) {
            const int column = marker % 6;
            const int row = marker / 6;
            const double markerX = x + (column - 2.5) * 2.4;
            const manifold::Manifold maleMarker = manifold::Manifold::Cube({1.1, 1.6, height + 2.0})
                .Translate({markerX - 0.55, blockDepth - 2.3 - row * 2.2, -1.0});
            const manifold::Manifold socketMarker = manifold::Manifold::Cube({1.1, 1.6, height + 2.0})
                .Translate({markerX - 0.55, -blockDepth + 0.7 + row * 2.2, -1.0});
            maleBase = maleBase - maleMarker;
            socketBase = socketBase - socketMarker;
        }
        const std::vector<Vec2> maleOutline = connectorToolOutline(
            local.type, center, tangent, ownerNormal,
            local.neckWidth, local.headWidth, local.depth, local.embed,
            local.leadChamfer ? local.chamferWidth : 0.0, local.chamferAngle);
        // The additive tab must end exactly at the coupon surfaces. The default
        // through-cut margin is only appropriate for subtractive operations.
        manifold::Manifold male = maleBase + prismFromOutline(maleOutline, 0.0, height, false);
        male = male.Translate({0.0, maleOffsetY, 0.0});

        const std::vector<Vec2> socketOutline = socketToolOutline(
            center, tangent, ownerNormal, local.type,
            local.neckWidth, local.headWidth, local.depth, clearance, local.embed,
            local.leadChamfer ? local.chamferWidth : 0.0, local.chamferAngle);
        manifold::Manifold socket = socketBase - prismFromOutline(socketOutline, 0.0, height);
        if (male.Status() != manifold::Manifold::Error::NoError ||
            socket.Status() != manifold::Manifold::Error::NoError ||
            male.IsEmpty() || socket.IsEmpty()) {
            error = "Kalibrierkörper konnte für Spiel " + std::to_string(clearance) + " mm nicht erzeugt werden.";
            return {};
        }
        ConnectorCalibrationSample sample;
        sample.clearance = clearance;
        sample.male = manifoldToMesh(male.Simplify());
        sample.socket = manifoldToMesh(socket.Simplify());
        if (sample.male.triangles.empty() || sample.socket.triangles.empty()) {
            error = "Kalibrierkörper enthält keine verwendbare Geometrie.";
            return {};
        }
        samples.push_back(std::move(sample));
    }
    error.clear();
    return samples;
}

bool arrangeConnectorCalibrationSamples(std::vector<ConnectorCalibrationSample>& samples,
                                        double bedWidth, double bedDepth,
                                        double margin, double gap,
                                        std::string& error) {
    if (samples.empty()) {
        error = "Es sind keine Kalibrierkörper zum Anordnen vorhanden.";
        return false;
    }
    bedWidth = std::max(1.0, bedWidth);
    bedDepth = std::max(1.0, bedDepth);
    margin = std::max(0.0, margin);
    gap = std::max(0.0, gap);
    const double usableWidth = bedWidth - 2.0 * margin;
    const double usableDepth = bedDepth - 2.0 * margin;
    if (usableWidth <= 0.0 || usableDepth <= 0.0) {
        error = "Der Sicherheitsrand ist größer als das gewählte Druckbett.";
        return false;
    }

    std::vector<TriangleMesh*> meshes;
    meshes.reserve(samples.size() * 2);
    double cellWidth = 0.0;
    double cellDepth = 0.0;
    for (ConnectorCalibrationSample& sample : samples) {
        meshes.push_back(&sample.male);
        meshes.push_back(&sample.socket);
        for (TriangleMesh* mesh : {&sample.male, &sample.socket}) {
            if (!mesh->bounds.valid()) {
                error = "Ein Kalibrierkörper besitzt keine gültigen Abmessungen.";
                return false;
            }
            cellWidth = std::max(cellWidth, mesh->bounds.size().x);
            cellDepth = std::max(cellDepth, mesh->bounds.size().y);
        }
    }

    const int objectCount = static_cast<int>(meshes.size());
    auto fits = [&](int columns, double candidateGap) {
        const int rows = (objectCount + columns - 1) / columns;
        const double width = columns * cellWidth + (columns - 1) * candidateGap;
        const double depth = rows * cellDepth + (rows - 1) * candidateGap;
        return width <= usableWidth + 1e-9 && depth <= usableDepth + 1e-9;
    };
    auto findColumns = [&](double candidateGap) {
        // An even column count keeps each male/socket pair in the same row.
        for (int candidate = objectCount; candidate >= 1; --candidate) {
            if (candidate % 2 == 0 && fits(candidate, candidateGap)) return candidate;
        }
        for (int candidate = objectCount; candidate >= 1; --candidate)
            if (fits(candidate, candidateGap)) return candidate;
        return 0;
    };
    int columns = findColumns(gap);
    if (columns == 0 && gap > 1.0) {
        // Dense test series may need less spacing on the small A1-mini bed.
        gap = 1.0;
        columns = findColumns(gap);
    }
    if (columns == 0) {
        std::ostringstream message;
        message << "Die gewählte Testreihe passt mit " << objectCount
                << " Körpern nicht auf das Druckbett "
                << std::fixed << std::setprecision(0) << bedWidth << " × " << bedDepth
                << " mm. Anzahl oder Verbindermaße verkleinern.";
        error = message.str();
        return false;
    }

    const int rows = (objectCount + columns - 1) / columns;
    const double layoutWidth = columns * cellWidth + (columns - 1) * gap;
    const double layoutDepth = rows * cellDepth + (rows - 1) * gap;
    const double startX = 0.5 * (bedWidth - layoutWidth);
    const double startY = 0.5 * (bedDepth - layoutDepth);
    for (int index = 0; index < objectCount; ++index) {
        TriangleMesh& mesh = *meshes[static_cast<size_t>(index)];
        const int column = index % columns;
        const int row = index / columns;
        const double targetMinX = startX + column * (cellWidth + gap) +
                                  0.5 * (cellWidth - mesh.bounds.size().x);
        const double targetMinY = startY + row * (cellDepth + gap) +
                                  0.5 * (cellDepth - mesh.bounds.size().y);
        const Vec3 translation{targetMinX - mesh.bounds.min.x,
                               targetMinY - mesh.bounds.min.y, 0.0};
        mesh.bounds = AABB{};
        for (Vec3& vertex : mesh.vertices) {
            vertex = vertex + translation;
            mesh.bounds.expand(vertex);
        }
    }
    error.clear();
    return true;
}

std::vector<TriangleMesh> decomposeMesh(const TriangleMesh& mesh) {
    std::vector<TriangleMesh> result;
    const manifold::Manifold solid = meshToManifold(mesh);
    if (solid.Status() != manifold::Manifold::Error::NoError) return result;
    const std::vector<manifold::Manifold> components = solid.Decompose();
    result.reserve(components.size());
    for (const auto& component : components) {
        TriangleMesh converted = manifoldToMesh(component);
        if (!converted.triangles.empty()) result.push_back(std::move(converted));
    }
    return result;
}

double polylineLength(const std::vector<Vec2>& cutPoints) {
    return polylineLengthInternal(cutPoints);
}

double closestPositionOnPolyline(const std::vector<Vec2>& cutPoints, Vec2 point) {
    const double total = polylineLengthInternal(cutPoints);
    if (cutPoints.size() < 2 || total < 1e-9) return 0.0;
    double travelled = 0.0;
    double bestDistance2 = 1e300;
    double bestAlong = 0.0;
    for (size_t i = 1; i < cutPoints.size(); ++i) {
        const Vec2 delta = cutPoints[i] - cutPoints[i - 1];
        const double len2 = dot(delta, delta);
        if (len2 < 1e-12) continue;
        const double segmentLength = std::sqrt(len2);
        const double local = std::clamp(dot(point - cutPoints[i - 1], delta) / len2, 0.0, 1.0);
        const Vec2 projected = cutPoints[i - 1] + delta * local;
        const double distance2 = dot(point - projected, point - projected);
        if (distance2 < bestDistance2) {
            bestDistance2 = distance2;
            bestAlong = travelled + local * segmentLength;
        }
        travelled += segmentLength;
    }
    return std::clamp(bestAlong / total, 0.0, 1.0);
}

bool validateCutPolyline(const std::vector<Vec2>& cutPoints, std::string& message) {
    if (cutPoints.size() < 2) {
        message = "Der Schnitt benötigt mindestens zwei Punkte.";
        return false;
    }
    if (cutPoints.size() > kMaxCutPoints ||
        std::any_of(cutPoints.begin(), cutPoints.end(), [](Vec2 point) {
            return !std::isfinite(point.x) || !std::isfinite(point.y) ||
                   std::abs(point.x) > kMaxCoordinateMagnitude ||
                   std::abs(point.y) > kMaxCoordinateMagnitude;
        })) {
        message = "Der Schnitt enthält zu viele oder ungültige Punkte.";
        return false;
    }
    const double scale = std::max(polylineLengthInternal(cutPoints), 1.0);
    const double eps = scale * 1e-8;
    for (size_t i = 1; i < cutPoints.size(); ++i) {
        if (length(cutPoints[i] - cutPoints[i - 1]) <= eps) {
            message = "Zwei aufeinanderfolgende Schnittpunkte liegen zu dicht beieinander.";
            return false;
        }
    }
    for (size_t i = 1; i < cutPoints.size(); ++i) {
        for (size_t j = i + 2; j < cutPoints.size(); ++j) {
            if (segmentsIntersect(cutPoints[i - 1], cutPoints[i], cutPoints[j - 1], cutPoints[j], eps)) {
                message = "Der Schnittlinienzug überschneidet sich selbst.";
                return false;
            }
        }
    }
    message.clear();
    return true;
}

std::vector<ConnectorPlacement> makeDefaultConnectorPlacements(const std::vector<Vec2>& cutPoints,
                                                                const DovetailSettings& s) {
    std::vector<ConnectorPlacement> out;
    const double len = polylineLengthInternal(cutPoints);
    if (len < 1e-6) return out;
    const std::vector<double> positions = connectorPositions(len, s);
    out.reserve(positions.size());
    for (double d : positions) {
        ConnectorPlacement placement;
        placement.position = std::clamp(d / len, 0.0, 1.0);
        placement.maleOnLeft = s.maleOnLeft;
        placement.type = s.type;
        out.push_back(placement);
    }
    return out;
}

std::vector<ConnectorPreview> makeConnectorPreview(const TriangleMesh& source,
                                                    const std::vector<Vec2>& cutPoints,
                                                    const DovetailSettings& s,
                                                    const std::vector<ConnectorPlacement>& placements,
                                                    const MeshSpatialIndex* preparedValidationIndex,
                                                    bool exactMaterialValidation) {
    std::vector<ConnectorPreview> out;
    if (polylineLengthInternal(cutPoints) < 1e-6) return out;
    std::optional<manifold::Manifold> materialSolid;
    std::optional<manifold::Manifold> materialLeftRegion;
    std::optional<manifold::Manifold> materialLeftSolid;
    std::optional<manifold::Manifold> materialRightSolid;
    std::optional<MeshSpatialIndex> spatialIndex;
    if (s.validateInsideModel && exactMaterialValidation &&
        meshCoordinatesAndIndicesValid(source)) {
        manifold::Manifold solid = meshToManifold(source);
        if (solid.Status() == manifold::Manifold::Error::NoError && !solid.IsEmpty()) {
            materialSolid = std::move(solid);
            const std::vector<Vec2> leftOutline = leftRegionOutline(source, cutPoints);
            if (leftOutline.size() >= 3 && signedArea(leftOutline) > 1e-8) {
                manifold::Manifold region = prismFromOutline(
                    leftOutline, source.bounds.min.z, source.bounds.max.z);
                manifold::Manifold left = *materialSolid ^ region;
                manifold::Manifold right = *materialSolid - region;
                if (region.Status() == manifold::Manifold::Error::NoError &&
                    left.Status() == manifold::Manifold::Error::NoError &&
                    right.Status() == manifold::Manifold::Error::NoError &&
                    !left.IsEmpty() && !right.IsEmpty()) {
                    materialLeftRegion = std::move(region);
                    materialLeftSolid = std::move(left);
                    materialRightSolid = std::move(right);
                }
            }
        }
    }
    const MeshSpatialIndex* validationIndex = preparedValidationIndex;
    if (s.validateInsideModel &&
        (!materialLeftRegion || !materialLeftSolid || !materialRightSolid) &&
        validationIndex == nullptr) {
        spatialIndex.emplace(source);
        validationIndex = &*spatialIndex;
    }
    out.reserve(placements.size());
    std::vector<std::pair<std::vector<Vec2>, std::vector<Vec2>>> collisionOutlines;
    collisionOutlines.reserve(placements.size());
    for (const ConnectorPlacement& placement : placements) {
        const DovetailSettings local = effectiveSettings(s, placement);
        const PolylineSample sample = samplePolyline(cutPoints, placement.position);
        const Vec2 ownerNormal = placement.maleOnLeft ? sample.left : sample.left * -1.0;
        ConnectorPreview p;
        p.center = sample.center;
        p.tangent = sample.tangent;
        p.left = sample.left;
        p.maleOnLeft = placement.maleOnLeft;
        p.maleOutline = visibleConnectorOutline(local.type, sample.center, sample.tangent, ownerNormal,
                                                local.neckWidth, local.headWidth, local.depth,
                                                local.leadChamfer ? local.chamferWidth : 0.0,
                                                local.chamferAngle, false);
        p.socketOutline = visibleConnectorOutline(local.type, sample.center, sample.tangent, ownerNormal,
                                                  local.neckWidth + 2.0 * local.clearance,
                                                  local.headWidth + 2.0 * local.clearance,
                                                  local.depth + local.clearance,
                                                  local.leadChamfer ? local.chamferWidth : 0.0,
                                                  local.chamferAngle, true);
        std::vector<Vec2> maleCollisionOutline = connectorToolOutline(
            local.type, sample.center, sample.tangent, ownerNormal,
            local.neckWidth, local.headWidth, local.depth, local.embed,
            local.leadChamfer ? local.chamferWidth : 0.0, local.chamferAngle);
        std::vector<Vec2> socketCollisionOutline = socketToolOutline(
            sample.center, sample.tangent, ownerNormal, local.type,
            local.neckWidth, local.headWidth, local.depth, local.clearance,
            local.embed, local.leadChamfer ? local.chamferWidth : 0.0,
            local.chamferAngle);
        p.valid = !local.validateInsideModel || connectorHasMaterial(
            source,
            validationIndex,
            materialLeftRegion ? &*materialLeftRegion : nullptr,
            materialLeftSolid ? &*materialLeftSolid : nullptr,
            materialRightSolid ? &*materialRightSolid : nullptr,
            sample.center, sample.tangent, ownerNormal, placement.maleOnLeft, local);
        const double cutConflictEpsilon = std::max(source.bounds.diagonal() * 1e-9, 1e-7);
        for (size_t segmentIndex = 0; segmentIndex + 1 < cutPoints.size(); ++segmentIndex) {
            if (segmentIndex == sample.segmentIndex) continue;
            const bool adjacent = segmentIndex + 1 == sample.segmentIndex ||
                                  sample.segmentIndex + 1 == segmentIndex;
            if (adjacent) {
                const Vec2 sampledDirection = normalized(
                    cutPoints[sample.segmentIndex + 1] - cutPoints[sample.segmentIndex]);
                const Vec2 otherDirection = normalized(
                    cutPoints[segmentIndex + 1] - cutPoints[segmentIndex]);
                // A redundant point on an otherwise straight segment is not a
                // bend and must not create a false collision warning.
                if (dot(sampledDirection, otherDirection) > 0.9998 &&
                    std::abs(cross2(sampledDirection, otherDirection)) < 0.02)
                    continue;
            }
            if (segmentIntersectsPolygon(cutPoints[segmentIndex], cutPoints[segmentIndex + 1],
                                         maleCollisionOutline, cutConflictEpsilon) ||
                segmentIntersectsPolygon(cutPoints[segmentIndex], cutPoints[segmentIndex + 1],
                                         socketCollisionOutline, cutConflictEpsilon)) {
                p.crossesCutCorner = true;
                p.valid = false;
                break;
            }
        }
        if (p.valid && local.validateInsideModel && local.validateWallThickness &&
            local.minimumWall > 0.0) {
            DovetailSettings reserve = local;
            const double wall = std::clamp(local.minimumWall, 0.0, 20.0);
            reserve.neckWidth += 2.0 * wall;
            reserve.headWidth += 2.0 * wall;
            reserve.depth += wall;
            reserve.embed += wall;
            p.thinWall = !connectorHasMaterial(
                source,
                validationIndex,
                materialLeftRegion ? &*materialLeftRegion : nullptr,
                materialLeftSolid ? &*materialLeftSolid : nullptr,
                materialRightSolid ? &*materialRightSolid : nullptr,
                sample.center, sample.tangent, ownerNormal, placement.maleOnLeft, reserve);
        }
        collisionOutlines.emplace_back(std::move(maleCollisionOutline),
                                       std::move(socketCollisionOutline));
        out.push_back(std::move(p));
    }

    // Overlapping connector tools produce ambiguous or fragile booleans. Mark
    // both placements invalid before any geometry is committed.
    const double overlapEpsilon = std::max(source.bounds.diagonal() * 1e-9, 1e-7);
    for (size_t i = 0; i < out.size(); ++i) {
        for (size_t j = i + 1; j < out.size(); ++j) {
            const auto& [maleI, socketI] = collisionOutlines[i];
            const auto& [maleJ, socketJ] = collisionOutlines[j];
            if (polygonsOverlap(maleI, maleJ, overlapEpsilon) ||
                polygonsOverlap(maleI, socketJ, overlapEpsilon) ||
                polygonsOverlap(socketI, maleJ, overlapEpsilon) ||
                polygonsOverlap(socketI, socketJ, overlapEpsilon)) {
                out[i].valid = false;
                out[j].valid = false;
                out[i].overlap = true;
                out[j].overlap = true;
            }
        }
    }
    return out;
}

DovetailResult createDovetailSplit(const TriangleMesh& source,
                                   const std::vector<Vec2>& cutPoints,
                                   const DovetailSettings& s,
                                   const std::vector<ConnectorPlacement>& placements,
                                   bool requireConnector) {
    DovetailResult r;
    if (!meshCoordinatesAndIndicesValid(source)) {
        r.message = "Quellmesh enthält ungültige, degenerierte oder extrem große Geometrie.";
        return r;
    }
    if (!validateCutPolyline(cutPoints, r.message)) return r;
    if (placements.empty() && requireConnector) { r.message = "Es sind keine Verbinder vorhanden."; return r; }
    if (placements.size() > kMaxConnectorPlacements) {
        r.message = "Ein Schnitt darf höchstens 100 Verbinder enthalten.";
        return r;
    }
    auto validDimension = [](double value, bool allowZero = false) {
        return std::isfinite(value) && (allowZero ? value >= 0.0 : value > 0.0) &&
               value <= kMaxConnectorDimension;
    };
    if (!validDimension(s.neckWidth) || !validDimension(s.headWidth) ||
        !validDimension(s.depth) || !validDimension(s.embed) ||
        !validDimension(s.clearance, true) || s.clearance > 1000.0 ||
        !validDimension(s.chamferWidth, true) || !std::isfinite(s.chamferAngle) ||
        s.chamferAngle < 0.0 || s.chamferAngle > 90.0 ||
        !std::isfinite(s.minimumWall) || s.minimumWall < 0.0 || s.minimumWall > 20.0 ||
        s.assemblyMarkCode < 1 || s.assemblyMarkCode > 1'000'000'000 ||
        !std::isfinite(s.assemblyMarkPosition) || s.assemblyMarkPosition < 0.0 ||
        s.assemblyMarkPosition > 1.0 || !std::isfinite(s.assemblyMarkSize) ||
        s.assemblyMarkSize < 2.0 || s.assemblyMarkSize > 30.0 ||
        !std::isfinite(s.assemblyMarkDepth) || s.assemblyMarkDepth < 0.05 ||
        s.assemblyMarkDepth > 5.0 || !std::isfinite(s.assemblyMarkLeftX) ||
        !std::isfinite(s.assemblyMarkLeftY) || !std::isfinite(s.assemblyMarkRightX) ||
        !std::isfinite(s.assemblyMarkRightY) ||
        std::abs(s.assemblyMarkLeftX) > kMaxCoordinateMagnitude ||
        std::abs(s.assemblyMarkLeftY) > kMaxCoordinateMagnitude ||
        std::abs(s.assemblyMarkRightX) > kMaxCoordinateMagnitude ||
        std::abs(s.assemblyMarkRightY) > kMaxCoordinateMagnitude ||
        ((s.type == ConnectorType::Dovetail || s.type == ConnectorType::Puzzle) && s.headWidth <= s.neckWidth)) {
        r.message = "Ungültige Verbindermaße."; return r;
    }
    for (const auto& placement : placements) {
        const DovetailSettings local = effectiveSettings(s, placement);
        if (!std::isfinite(placement.position) || placement.position < 0.0 || placement.position > 1.0 ||
            !validDimension(local.neckWidth) || !validDimension(local.headWidth) ||
            !validDimension(local.depth) || !validDimension(local.embed) ||
            !validDimension(local.clearance, true) || local.clearance > 1000.0 ||
            !validDimension(local.chamferWidth, true) || !std::isfinite(local.chamferAngle) ||
            local.chamferAngle < 0.0 || local.chamferAngle > 90.0 ||
            ((local.type == ConnectorType::Dovetail || local.type == ConnectorType::Puzzle) &&
             local.headWidth <= local.neckWidth)) {
            r.message = "Mindestens ein Verbinder besitzt ungültige individuelle Maße.";
            return r;
        }
    }
    ManifoldMaterialRelation materialRelation;
    manifold::Manifold sourceSolid = meshToManifoldWithMaterials(source, &materialRelation);
    if (sourceSolid.Status() != manifold::Manifold::Error::NoError) {
        r.message = "STL kann nicht als Volumenkörper verarbeitet werden: " + manifoldErrorText(sourceSolid.Status());
        return r;
    }
    const std::vector<Vec2> leftOutline = leftRegionOutline(source, cutPoints);
    if (leftOutline.size() < 3 || signedArea(leftOutline) <= 1e-8) {
        r.message = "Aus dem Schnittlinienzug konnte kein gültiger linker Bereich gebildet werden.";
        return r;
    }
    const manifold::Manifold leftRegion = prismFromOutline(leftOutline, source.bounds.min.z, source.bounds.max.z);
    manifold::Manifold partA = sourceSolid ^ leftRegion;
    manifold::Manifold partB = sourceSolid - leftRegion;
    if (partA.Status() != manifold::Manifold::Error::NoError ||
        partB.Status() != manifold::Manifold::Error::NoError ||
        partA.IsEmpty() || partB.IsEmpty()) {
        r.message = "Der Linienzug teilt das Modell nicht in zwei gültige Volumenkörper. Start- und Endsegment müssen das Modell vollständig durchqueren.";
        return r;
    }

    // Part A is always the local left side of the directed polyline; part B is right.
    r.connectors = makeConnectorPreview(source, cutPoints, s, placements);
    if (std::any_of(r.connectors.begin(), r.connectors.end(),
                    [](const ConnectorPreview& connector) { return connector.overlap; })) {
        r.message = "Mindestens zwei Verbinder überlappen sich. Position, Größe oder Anzahl ändern.";
        return r;
    }
    int used = 0;
    int skipped = 0;
    for (size_t connectorIndex = 0; connectorIndex < r.connectors.size(); ++connectorIndex) {
        const auto& connector = r.connectors[connectorIndex];
        if (!connector.valid) { ++skipped; continue; }
        const DovetailSettings local = effectiveSettings(s, placements[connectorIndex]);
        const Vec2 ownerNormal = connector.maleOnLeft ? connector.left : connector.left * -1.0;
        const std::vector<Vec2> maleToolShape = connectorToolOutline(local.type, connector.center, connector.tangent, ownerNormal,
                                                                     local.neckWidth, local.headWidth, local.depth, local.embed,
                                                                     local.leadChamfer ? local.chamferWidth : 0.0,
                                                                     local.chamferAngle);
        const std::vector<Vec2> socketToolShape = socketToolOutline(connector.center, connector.tangent, ownerNormal,
                                                                    local.type, local.neckWidth, local.headWidth, local.depth,
                                                                    local.clearance, local.embed,
                                                                    local.leadChamfer ? local.chamferWidth : 0.0,
                                                                    local.chamferAngle);
        // A cutting tool must pass slightly through the model to avoid
        // coplanar boolean ambiguities.  The additive male connector must
        // not use that margin: otherwise it remains visible above and below
        // thin models (for example 0.072 mm on a 3.6 mm part).
        const manifold::Manifold nominalMaleTool = prismFromOutline(
            maleToolShape, source.bounds.min.z, source.bounds.max.z, false);
        const manifold::Manifold socketTool = prismFromOutline(
            socketToolShape, source.bounds.min.z, source.bounds.max.z);
        // Clip the additive connector to the actual source volume. This keeps
        // curved, tapered and sloped models from receiving material above or
        // below their real surface while preserving the intended split shape.
        const manifold::Manifold maleTool = nominalMaleTool ^ sourceSolid;
        if (maleTool.Status() != manifold::Manifold::Error::NoError || maleTool.IsEmpty()) {
            ++skipped;
            continue;
        }
        if (connector.maleOnLeft) {
            partA = partA + maleTool;
            partB = partB - socketTool;
        } else {
            partB = partB + maleTool;
            partA = partA - socketTool;
        }
        ++used;
    }
    if (s.assemblyMarks) {
        const manifold::Manifold marks = assemblyMarkTool(source, cutPoints, s);
        if (marks.Status() == manifold::Manifold::Error::NoError && !marks.IsEmpty()) {
            const manifold::Manifold markedA = partA - marks;
            const manifold::Manifold markedB = partB - marks;
            // Markings are an optional aid, never a reason to lose an
            // otherwise valid split on a curved or very thin surface.
            if (markedA.Status() == manifold::Manifold::Error::NoError &&
                markedB.Status() == manifold::Manifold::Error::NoError &&
                !markedA.IsEmpty() && !markedB.IsEmpty()) {
                partA = markedA;
                partB = markedB;
            }
        }
    }
    if (used == 0 && requireConnector) {
        r.message = "Kein Verbinder liegt vollständig im Modell. Position oder Verbindermaße ändern.";
        return r;
    }
    const auto statusA = partA.Status();
    const auto statusB = partB.Status();
    if (statusA != manifold::Manifold::Error::NoError || statusB != manifold::Manifold::Error::NoError ||
        partA.IsEmpty() || partB.IsEmpty()) {
        r.message = "Boolesche Operation fehlgeschlagen. Teil A: " + manifoldErrorText(statusA) +
                    ", Teil B: " + manifoldErrorText(statusB);
        return r;
    }
    std::optional<MeshSpatialIndex> materialIndex;
    if (!source.triangleMaterials.empty()) materialIndex.emplace(source);
    r.partA = manifoldToMeshWithMaterials(partA.Simplify(), &materialRelation, &source,
                                          materialIndex ? &*materialIndex : nullptr);
    r.partB = manifoldToMeshWithMaterials(partB.Simplify(), &materialRelation, &source,
                                          materialIndex ? &*materialIndex : nullptr);
    if (r.partA.triangles.empty() || r.partB.triangles.empty()) {
        r.message = "Das Ergebnis konnte nicht in STL-Dreiecke umgewandelt werden.";
        return r;
    }
    std::ostringstream msg;
    msg << used << " Verbinder erzeugt";
    if (skipped) msg << ", " << skipped << " wegen fehlendem Material übersprungen";
    msg << ". Teil A: " << r.partA.triangles.size() << " Dreiecke, Teil B: " << r.partB.triangles.size() << " Dreiecke.";
    r.message = msg.str();
    r.ok = true;
    return r;
}
