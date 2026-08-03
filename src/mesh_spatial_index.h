#pragma once

#include "stl_io.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Immutable bounding-volume hierarchy for repeated projection and volume
// queries. The referenced mesh must outlive the index.
class MeshSpatialIndex {
public:
    explicit MeshSpatialIndex(const TriangleMesh& mesh);

    bool empty() const { return triangleIndices_.empty(); }
    size_t triangleCount() const { return triangleIndices_.size(); }
    size_t nodeCount() const { return nodes_.size(); }
    size_t projectionBoundaryEdgeCount() const { return projectionBoundaryEdges_.size(); }
    uint64_t storageBytes() const;

    bool pointInsideProjection(Vec2 point) const;
    double closestPointOnProjectionBoundary(Vec2 point, Vec2& closest) const;
    bool polylineHitsProjection(const std::vector<Vec2>& points) const;
    bool pointInsideVolume(Vec3 point) const;
    // Returns the source triangle nearest to a 3D point. Used to inherit the
    // local filament/material on faces newly created by boolean operations.
    size_t closestTriangle(Vec3 point, double* distanceSquared = nullptr) const;

private:
    struct Node {
        AABB bounds;
        uint32_t first = 0;
        uint32_t count = 0;
        uint32_t left = 0;
        uint32_t right = 0;

        bool leaf() const { return count != 0; }
    };

    uint32_t buildNode(uint32_t first, uint32_t count);

    const TriangleMesh* mesh_ = nullptr;
    std::vector<uint32_t> triangleIndices_;
    std::vector<Node> nodes_;
    std::vector<uint64_t> projectionBoundaryEdges_;
};
