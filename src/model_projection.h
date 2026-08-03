#pragma once

#include "mesh_spatial_index.h"

#include <vector>

bool pointInsideMeshProjection(const TriangleMesh& mesh, Vec2 point);
bool pointInsideMeshProjection(const MeshSpatialIndex& index, Vec2 point);

// Returns the squared XY distance, or infinity when the mesh has no usable edge.
double closestPointOnMeshProjectionBoundary(const TriangleMesh& mesh, Vec2 point,
                                            Vec2& closest);
double closestPointOnMeshProjectionBoundary(const MeshSpatialIndex& index, Vec2 point,
                                            Vec2& closest);

bool polylineHitsMeshProjection(const TriangleMesh& mesh,
                                const std::vector<Vec2>& points);
bool polylineHitsMeshProjection(const MeshSpatialIndex& index,
                                const std::vector<Vec2>& points);
