#include "mesh_spatial_index.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace {

constexpr uint32_t kLeafTriangleCount = 8;

bool validTriangle(const TriangleMesh& mesh, const std::array<uint32_t, 3>& triangle) {
    return triangle[0] < mesh.vertices.size() && triangle[1] < mesh.vertices.size() &&
           triangle[2] < mesh.vertices.size() && triangle[0] != triangle[1] &&
           triangle[1] != triangle[2] && triangle[0] != triangle[2];
}

uint64_t edgeKey(uint32_t first, uint32_t second) {
    const uint32_t low = std::min(first, second);
    const uint32_t high = std::max(first, second);
    return (static_cast<uint64_t>(low) << 32u) | high;
}

class NodeStack {
public:
    void push(uint32_t value) {
        if (size_ < fixed_.size()) fixed_[size_++] = value;
        else overflow_.push_back(value);
    }
    bool empty() const { return size_ == 0 && overflow_.empty(); }
    uint32_t pop() {
        if (!overflow_.empty()) {
            const uint32_t value = overflow_.back();
            overflow_.pop_back();
            return value;
        }
        return fixed_[--size_];
    }
private:
    std::array<uint32_t, 64> fixed_{};
    size_t size_ = 0;
    std::vector<uint32_t> overflow_;
};

class RayHits {
public:
    void push(double value) {
        if (overflow_.empty() && size_ < fixed_.size()) {
            fixed_[size_++] = value;
            return;
        }
        if (overflow_.empty()) {
            overflow_.reserve(fixed_.size() * 2);
            overflow_.insert(overflow_.end(), fixed_.begin(), fixed_.end());
        }
        overflow_.push_back(value);
    }
    bool empty() const { return size_ == 0 && overflow_.empty(); }
    size_t uniqueCount(double epsilon) {
        if (overflow_.empty()) {
            std::sort(fixed_.begin(), fixed_.begin() + static_cast<std::ptrdiff_t>(size_));
            return countUnique(fixed_.begin(), fixed_.begin() + static_cast<std::ptrdiff_t>(size_), epsilon);
        }
        std::sort(overflow_.begin(), overflow_.end());
        return countUnique(overflow_.begin(), overflow_.end(), epsilon);
    }
private:
    template <typename Iterator>
    static size_t countUnique(Iterator first, Iterator last, double epsilon) {
        size_t count = 0;
        double previous = -std::numeric_limits<double>::infinity();
        for (; first != last; ++first) {
            if (*first - previous > epsilon) {
                ++count;
                previous = *first;
            }
        }
        return count;
    }
    std::array<double, 64> fixed_{};
    size_t size_ = 0;
    std::vector<double> overflow_;
};

double component(Vec3 value, int axis) {
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}

double crossValue(Vec2 a, Vec2 b, Vec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointInTriangle(Vec2 point, Vec2 a, Vec2 b, Vec2 c) {
    const double area = crossValue(a, b, c);
    if (std::abs(area) < 1e-12) return false;
    const double u = crossValue(point, b, c) / area;
    const double v = crossValue(point, c, a) / area;
    const double w = 1.0 - u - v;
    return u >= -1e-9 && v >= -1e-9 && w >= -1e-9;
}

bool onSegment(Vec2 point, Vec2 a, Vec2 b, double epsilon) {
    return std::abs(crossValue(a, b, point)) <= epsilon &&
           point.x >= std::min(a.x, b.x) - epsilon &&
           point.x <= std::max(a.x, b.x) + epsilon &&
           point.y >= std::min(a.y, b.y) - epsilon &&
           point.y <= std::max(a.y, b.y) + epsilon;
}

bool segmentsIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d, double epsilon) {
    const double abC = crossValue(a, b, c);
    const double abD = crossValue(a, b, d);
    const double cdA = crossValue(c, d, a);
    const double cdB = crossValue(c, d, b);
    if (((abC > epsilon && abD < -epsilon) || (abC < -epsilon && abD > epsilon)) &&
        ((cdA > epsilon && cdB < -epsilon) || (cdA < -epsilon && cdB > epsilon))) return true;
    return onSegment(c, a, b, epsilon) || onSegment(d, a, b, epsilon) ||
           onSegment(a, c, d, epsilon) || onSegment(b, c, d, epsilon);
}

double pointBoundsDistance2(Vec2 point, const AABB& bounds) {
    const double dx = point.x < bounds.min.x ? bounds.min.x - point.x
                    : point.x > bounds.max.x ? point.x - bounds.max.x : 0.0;
    const double dy = point.y < bounds.min.y ? bounds.min.y - point.y
                    : point.y > bounds.max.y ? point.y - bounds.max.y : 0.0;
    return dx * dx + dy * dy;
}

double pointBoundsDistance2(Vec3 point, const AABB& bounds) {
    const double dx = point.x < bounds.min.x ? bounds.min.x - point.x
                    : point.x > bounds.max.x ? point.x - bounds.max.x : 0.0;
    const double dy = point.y < bounds.min.y ? bounds.min.y - point.y
                    : point.y > bounds.max.y ? point.y - bounds.max.y : 0.0;
    const double dz = point.z < bounds.min.z ? bounds.min.z - point.z
                    : point.z > bounds.max.z ? point.z - bounds.max.z : 0.0;
    return dx * dx + dy * dy + dz * dz;
}

double pointTriangleDistance2(Vec3 point, Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = point - a;
    const double d1 = dot(ab, ap);
    const double d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return dot(ap, ap);

    const Vec3 bp = point - b;
    const double d3 = dot(ab, bp);
    const double d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return dot(bp, bp);

    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        const double v = d1 / (d1 - d3);
        const Vec3 delta = point - (a + ab * v);
        return dot(delta, delta);
    }

    const Vec3 cp = point - c;
    const double d5 = dot(ab, cp);
    const double d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return dot(cp, cp);

    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        const double w = d2 / (d2 - d6);
        const Vec3 delta = point - (a + ac * w);
        return dot(delta, delta);
    }

    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        const Vec3 bc = c - b;
        const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        const Vec3 delta = point - (b + bc * w);
        return dot(delta, delta);
    }

    const double denominator = va + vb + vc;
    if (std::abs(denominator) < 1e-30) {
        return std::min({dot(ap, ap), dot(bp, bp), dot(cp, cp)});
    }
    const double inverse = 1.0 / denominator;
    const double v = vb * inverse;
    const double w = vc * inverse;
    const Vec3 delta = point - (a + ab * v + ac * w);
    return dot(delta, delta);
}

bool segmentOverlapsBounds(Vec2 a, Vec2 b, const AABB& bounds, double epsilon) {
    const double minX = std::min(a.x, b.x) - epsilon;
    const double maxX = std::max(a.x, b.x) + epsilon;
    const double minY = std::min(a.y, b.y) - epsilon;
    const double maxY = std::max(a.y, b.y) + epsilon;
    return maxX >= bounds.min.x && minX <= bounds.max.x &&
           maxY >= bounds.min.y && minY <= bounds.max.y;
}

bool rayIntersectsBounds(Vec3 origin, Vec3 direction, const AABB& bounds) {
    double minimum = 0.0;
    double maximum = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        const double value = component(origin, axis);
        const double delta = component(direction, axis);
        const double low = component(bounds.min, axis);
        const double high = component(bounds.max, axis);
        if (std::abs(delta) < 1e-15) {
            if (value < low || value > high) return false;
            continue;
        }
        double first = (low - value) / delta;
        double second = (high - value) / delta;
        if (first > second) std::swap(first, second);
        minimum = std::max(minimum, first);
        maximum = std::min(maximum, second);
        if (maximum < minimum) return false;
    }
    return maximum > 1e-10;
}

bool rayTriangle(Vec3 origin, Vec3 direction, Vec3 a, Vec3 b, Vec3 c, double& distance) {
    constexpr double epsilon = 1e-10;
    const Vec3 edge1 = b - a;
    const Vec3 edge2 = c - a;
    const Vec3 h = cross(direction, edge2);
    const double determinant = dot(edge1, h);
    if (std::abs(determinant) < epsilon) return false;
    const double inverse = 1.0 / determinant;
    const Vec3 offset = origin - a;
    const double u = inverse * dot(offset, h);
    if (u < -epsilon || u > 1.0 + epsilon) return false;
    const Vec3 q = cross(offset, edge1);
    const double v = inverse * dot(direction, q);
    if (v < -epsilon || u + v > 1.0 + epsilon) return false;
    distance = inverse * dot(edge2, q);
    return distance > epsilon;
}

} // namespace

MeshSpatialIndex::MeshSpatialIndex(const TriangleMesh& mesh) : mesh_(&mesh) {
    struct EdgeFacing {
        uint32_t count = 0;
        uint8_t orientations = 0;
    };
    std::unordered_map<uint64_t, EdgeFacing> edgeFacings;
    edgeFacings.reserve(mesh.triangles.size() * 2);
    triangleIndices_.reserve(mesh.triangles.size());
    for (size_t index = 0; index < mesh.triangles.size(); ++index) {
        const auto& triangle = mesh.triangles[index];
        if (!validTriangle(mesh, triangle)) continue;
        triangleIndices_.push_back(static_cast<uint32_t>(index));
        const Vec3& a = mesh.vertices[triangle[0]];
        const Vec3& b = mesh.vertices[triangle[1]];
        const Vec3& c = mesh.vertices[triangle[2]];
        const double orientation = (b.x - a.x) * (c.y - a.y) -
                                   (b.y - a.y) * (c.x - a.x);
        const uint8_t flag = orientation > 1e-12 ? 1u : (orientation < -1e-12 ? 2u : 4u);
        for (int edge = 0; edge < 3; ++edge) {
            EdgeFacing& facing = edgeFacings[edgeKey(triangle[edge], triangle[(edge + 1) % 3])];
            ++facing.count;
            facing.orientations = static_cast<uint8_t>(facing.orientations | flag);
        }
    }
    projectionBoundaryEdges_.reserve(edgeFacings.size());
    for (const auto& [key, facing] : edgeFacings) {
        const bool mixedOrientation = facing.orientations != 1u &&
                                      facing.orientations != 2u &&
                                      facing.orientations != 4u;
        if (facing.count != 2 || mixedOrientation)
            projectionBoundaryEdges_.push_back(key);
    }
    std::sort(projectionBoundaryEdges_.begin(), projectionBoundaryEdges_.end());
    projectionBoundaryEdges_.shrink_to_fit();
    if (triangleIndices_.empty()) return;
    nodes_.reserve(triangleIndices_.size() * 2);
    buildNode(0, static_cast<uint32_t>(triangleIndices_.size()));
}

uint64_t MeshSpatialIndex::storageBytes() const {
    return static_cast<uint64_t>(triangleIndices_.capacity()) * sizeof(uint32_t) +
           static_cast<uint64_t>(nodes_.capacity()) * sizeof(Node) +
           static_cast<uint64_t>(projectionBoundaryEdges_.capacity()) * sizeof(uint64_t);
}

uint32_t MeshSpatialIndex::buildNode(uint32_t first, uint32_t count) {
    Node node;
    AABB centroidBounds;
    for (uint32_t offset = 0; offset < count; ++offset) {
        const auto& triangle = mesh_->triangles[triangleIndices_[first + offset]];
        Vec3 centroid{};
        for (uint32_t vertexIndex : triangle) {
            const Vec3 vertex = mesh_->vertices[vertexIndex];
            node.bounds.expand(vertex);
            centroid = centroid + vertex;
        }
        centroidBounds.expand(centroid / 3.0);
    }

    const uint32_t nodeIndex = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back(node);
    if (count <= kLeafTriangleCount) {
        nodes_[nodeIndex].first = first;
        nodes_[nodeIndex].count = count;
        return nodeIndex;
    }

    const Vec3 size = centroidBounds.size();
    const int axis = size.x >= size.y && size.x >= size.z ? 0 : (size.y >= size.z ? 1 : 2);
    const uint32_t middle = first + count / 2;
    std::nth_element(triangleIndices_.begin() + first,
                     triangleIndices_.begin() + middle,
                     triangleIndices_.begin() + first + count,
                     [&](uint32_t left, uint32_t right) {
        auto centroid = [&](uint32_t triangleIndex) {
            const auto& triangle = mesh_->triangles[triangleIndex];
            return (mesh_->vertices[triangle[0]] + mesh_->vertices[triangle[1]] +
                    mesh_->vertices[triangle[2]]) / 3.0;
        };
        return component(centroid(left), axis) < component(centroid(right), axis);
    });
    const uint32_t left = buildNode(first, middle - first);
    const uint32_t right = buildNode(middle, first + count - middle);
    nodes_[nodeIndex].left = left;
    nodes_[nodeIndex].right = right;
    return nodeIndex;
}

bool MeshSpatialIndex::pointInsideProjection(Vec2 point) const {
    if (nodes_.empty()) return false;
    NodeStack stack;
    stack.push(0);
    while (!stack.empty()) {
        const Node& node = nodes_[stack.pop()];
        if (point.x < node.bounds.min.x - 1e-9 || point.x > node.bounds.max.x + 1e-9 ||
            point.y < node.bounds.min.y - 1e-9 || point.y > node.bounds.max.y + 1e-9) continue;
        if (!node.leaf()) {
            stack.push(node.left);
            stack.push(node.right);
            continue;
        }
        for (uint32_t offset = 0; offset < node.count; ++offset) {
            const auto& triangle = mesh_->triangles[triangleIndices_[node.first + offset]];
            const Vec3 a3 = mesh_->vertices[triangle[0]];
            const Vec3 b3 = mesh_->vertices[triangle[1]];
            const Vec3 c3 = mesh_->vertices[triangle[2]];
            if (pointInTriangle(point, {a3.x, a3.y}, {b3.x, b3.y}, {c3.x, c3.y})) return true;
        }
    }
    return false;
}

double MeshSpatialIndex::closestPointOnProjectionBoundary(Vec2 point, Vec2& closest) const {
    if (nodes_.empty()) return std::numeric_limits<double>::infinity();
    NodeStack stack;
    stack.push(0);
    double bestDistance = std::numeric_limits<double>::infinity();
    while (!stack.empty()) {
        const uint32_t nodeIndex = stack.pop();
        const double boundDistance = pointBoundsDistance2(point, nodes_[nodeIndex].bounds);
        if (boundDistance >= bestDistance) continue;
        const Node& node = nodes_[nodeIndex];
        if (!node.leaf()) {
            stack.push(node.left);
            stack.push(node.right);
            continue;
        }
        for (uint32_t offset = 0; offset < node.count; ++offset) {
            const auto& triangle = mesh_->triangles[triangleIndices_[node.first + offset]];
            const Vec2 vertices[] = {
                {mesh_->vertices[triangle[0]].x, mesh_->vertices[triangle[0]].y},
                {mesh_->vertices[triangle[1]].x, mesh_->vertices[triangle[1]].y},
                {mesh_->vertices[triangle[2]].x, mesh_->vertices[triangle[2]].y}};
            for (int edge = 0; edge < 3; ++edge) {
                const uint64_t key = edgeKey(triangle[edge], triangle[(edge + 1) % 3]);
                if (!projectionBoundaryEdges_.empty() &&
                    !std::binary_search(projectionBoundaryEdges_.begin(),
                                        projectionBoundaryEdges_.end(), key)) continue;
                const Vec2 a = vertices[edge];
                const Vec2 delta = vertices[(edge + 1) % 3] - a;
                const double length2 = dot(delta, delta);
                if (length2 < 1e-12) continue;
                const double position = std::clamp(dot(point - a, delta) / length2, 0.0, 1.0);
                const Vec2 projected = a + delta * position;
                const double distance = dot(point - projected, point - projected);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    closest = projected;
                }
            }
        }
    }
    return bestDistance;
}

bool MeshSpatialIndex::polylineHitsProjection(const std::vector<Vec2>& points) const {
    if (points.size() < 2 || nodes_.empty()) return false;
    for (Vec2 point : points)
        if (pointInsideProjection(point)) return true;
    const double epsilon = std::max(mesh_->bounds.diagonal() * 1e-10, 1e-9);
    for (size_t segment = 1; segment < points.size(); ++segment) {
        const Vec2 a = points[segment - 1];
        const Vec2 b = points[segment];
        NodeStack stack;
        stack.push(0);
        while (!stack.empty()) {
            const Node& node = nodes_[stack.pop()];
            if (!segmentOverlapsBounds(a, b, node.bounds, epsilon)) continue;
            if (!node.leaf()) {
                stack.push(node.left);
                stack.push(node.right);
                continue;
            }
            for (uint32_t offset = 0; offset < node.count; ++offset) {
                const auto& triangle = mesh_->triangles[triangleIndices_[node.first + offset]];
                const Vec2 p0{mesh_->vertices[triangle[0]].x, mesh_->vertices[triangle[0]].y};
                const Vec2 p1{mesh_->vertices[triangle[1]].x, mesh_->vertices[triangle[1]].y};
                const Vec2 p2{mesh_->vertices[triangle[2]].x, mesh_->vertices[triangle[2]].y};
                if (segmentsIntersect(a, b, p0, p1, epsilon) ||
                    segmentsIntersect(a, b, p1, p2, epsilon) ||
                    segmentsIntersect(a, b, p2, p0, epsilon)) return true;
            }
        }
    }
    return false;
}

bool MeshSpatialIndex::pointInsideVolume(Vec3 point) const {
    if (nodes_.empty()) return false;
    const Vec3 direction = normalized(Vec3{1.0, 0.371390676, 0.52911317});
    RayHits hits;
    NodeStack stack;
    stack.push(0);
    while (!stack.empty()) {
        const Node& node = nodes_[stack.pop()];
        if (!rayIntersectsBounds(point, direction, node.bounds)) continue;
        if (!node.leaf()) {
            stack.push(node.left);
            stack.push(node.right);
            continue;
        }
        for (uint32_t offset = 0; offset < node.count; ++offset) {
            const auto& triangle = mesh_->triangles[triangleIndices_[node.first + offset]];
            double distance = 0.0;
            if (rayTriangle(point, direction,
                            mesh_->vertices[triangle[0]], mesh_->vertices[triangle[1]],
                            mesh_->vertices[triangle[2]], distance)) hits.push(distance);
        }
    }
    if (hits.empty()) return false;
    const double epsilon = std::max(1e-7 * std::max(mesh_->bounds.diagonal(), 1.0), 1e-6);
    const size_t uniqueHits = hits.uniqueCount(epsilon);
    return uniqueHits % 2 == 1;
}

size_t MeshSpatialIndex::closestTriangle(Vec3 point, double* distanceSquared) const {
    if (nodes_.empty()) {
        if (distanceSquared) *distanceSquared = std::numeric_limits<double>::infinity();
        return std::numeric_limits<size_t>::max();
    }
    NodeStack stack;
    stack.push(0);
    double bestDistance = std::numeric_limits<double>::infinity();
    size_t bestTriangle = std::numeric_limits<size_t>::max();
    while (!stack.empty()) {
        const uint32_t nodeIndex = stack.pop();
        const Node& node = nodes_[nodeIndex];
        if (pointBoundsDistance2(point, node.bounds) >= bestDistance) continue;
        if (!node.leaf()) {
            const double leftDistance = pointBoundsDistance2(point, nodes_[node.left].bounds);
            const double rightDistance = pointBoundsDistance2(point, nodes_[node.right].bounds);
            // LIFO stack: push the farther child first so the nearer one is
            // evaluated immediately and tightens pruning sooner.
            if (leftDistance < rightDistance) {
                if (rightDistance < bestDistance) stack.push(node.right);
                if (leftDistance < bestDistance) stack.push(node.left);
            } else {
                if (leftDistance < bestDistance) stack.push(node.left);
                if (rightDistance < bestDistance) stack.push(node.right);
            }
            continue;
        }
        for (uint32_t offset = 0; offset < node.count; ++offset) {
            const uint32_t triangleIndex = triangleIndices_[node.first + offset];
            const auto& triangle = mesh_->triangles[triangleIndex];
            const double candidate = pointTriangleDistance2(
                point, mesh_->vertices[triangle[0]], mesh_->vertices[triangle[1]],
                mesh_->vertices[triangle[2]]);
            if (candidate < bestDistance) {
                bestDistance = candidate;
                bestTriangle = triangleIndex;
            }
        }
    }
    if (distanceSquared) *distanceSquared = bestDistance;
    return bestTriangle;
}
