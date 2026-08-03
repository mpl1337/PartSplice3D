#include "interaction_hit_test.h"

#include <algorithm>
#include <cmath>

double segmentDistance(Vec2 point, Vec2 a, Vec2 b) {
    const Vec2 delta = b - a;
    const double length2 = dot(delta, delta);
    if (length2 < 1e-12) return length(point - a);
    const double position = std::clamp(dot(point - a, delta) / length2, 0.0, 1.0);
    return length(point - (a + delta * position));
}

bool hitTestConnectorOutline(const std::vector<Vec2>& outline, Vec2 point,
                             double edgeRadius) {
    if (outline.size() < 3) return false;
    bool inside = false;
    for (size_t index = 0, previous = outline.size() - 1;
         index < outline.size(); previous = index++) {
        const Vec2 a = outline[previous];
        const Vec2 b = outline[index];
        if (segmentDistance(point, a, b) <= edgeRadius) return true;
        const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
            (point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x);
        if (crosses) inside = !inside;
    }
    return inside;
}

std::vector<ConnectorHitCandidate> hitTestConnectors(
    const std::vector<ConnectorPreview>& connectors, Vec2 point, double radius) {
    std::vector<ConnectorHitCandidate> hits;
    hits.reserve(connectors.size());
    for (size_t index = 0; index < connectors.size(); ++index) {
        const double distance = length(connectors[index].center - point);
        if (distance <= radius ||
            hitTestConnectorOutline(connectors[index].maleOutline, point, radius * 0.35) ||
            hitTestConnectorOutline(connectors[index].socketOutline, point, radius * 0.35)) {
            hits.push_back({static_cast<int>(index), distance});
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const ConnectorHitCandidate& first,
                                                  const ConnectorHitCandidate& second) {
        return first.distance < second.distance;
    });
    return hits;
}

int hitTestCutPoint(const std::vector<Vec2>& points, Vec2 point, double radius) {
    int found = -1;
    double best = radius;
    for (size_t index = 0; index < points.size(); ++index) {
        const double distance = length(points[index] - point);
        if (distance <= best) {
            best = distance;
            found = static_cast<int>(index);
        }
    }
    return found;
}

LineSegmentHit hitTestLineSegment(const std::vector<Vec2>& points, Vec2 point,
                                  double radius) {
    LineSegmentHit hit;
    for (size_t index = 1; index < points.size(); ++index) {
        const Vec2 a = points[index - 1];
        const Vec2 delta = points[index] - a;
        const double length2 = dot(delta, delta);
        if (length2 < 1e-12) continue;
        const double local = std::clamp(dot(point - a, delta) / length2, 0.0, 1.0);
        const Vec2 projected = a + delta * local;
        const double distance = length(point - projected);
        if (distance <= radius && distance < hit.distance) {
            hit.segment = static_cast<int>(index - 1);
            hit.projected = projected;
            hit.distance = distance;
        }
    }
    return hit;
}
