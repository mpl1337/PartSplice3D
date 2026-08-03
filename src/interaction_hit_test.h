#pragma once

#include "mesh_ops.h"

#include <vector>

struct ConnectorHitCandidate {
    int index = -1;
    double distance = 0.0;
};

struct LineSegmentHit {
    int segment = -1;
    Vec2 projected{};
    double distance = 1e300;
};

double segmentDistance(Vec2 point, Vec2 a, Vec2 b);
bool hitTestConnectorOutline(const std::vector<Vec2>& outline, Vec2 point,
                             double edgeRadius);
std::vector<ConnectorHitCandidate> hitTestConnectors(
    const std::vector<ConnectorPreview>& connectors, Vec2 point, double radius);
int hitTestCutPoint(const std::vector<Vec2>& points, Vec2 point, double radius);
LineSegmentHit hitTestLineSegment(const std::vector<Vec2>& points, Vec2 point,
                                  double radius);
