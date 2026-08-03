#include "model_projection.h"

bool pointInsideMeshProjection(const TriangleMesh& mesh, Vec2 point) {
    return MeshSpatialIndex(mesh).pointInsideProjection(point);
}

bool pointInsideMeshProjection(const MeshSpatialIndex& index, Vec2 point) {
    return index.pointInsideProjection(point);
}

double closestPointOnMeshProjectionBoundary(const TriangleMesh& mesh, Vec2 point,
                                            Vec2& closest) {
    return MeshSpatialIndex(mesh).closestPointOnProjectionBoundary(point, closest);
}

double closestPointOnMeshProjectionBoundary(const MeshSpatialIndex& index, Vec2 point,
                                            Vec2& closest) {
    return index.closestPointOnProjectionBoundary(point, closest);
}

bool polylineHitsMeshProjection(const TriangleMesh& mesh,
                                const std::vector<Vec2>& points) {
    return MeshSpatialIndex(mesh).polylineHitsProjection(points);
}

bool polylineHitsMeshProjection(const MeshSpatialIndex& index,
                                const std::vector<Vec2>& points) {
    return index.polylineHitsProjection(points);
}
