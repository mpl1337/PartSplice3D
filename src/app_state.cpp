#include "app_state.h"

#include "model_projection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace {

double cross2(Vec2 a, Vec2 b, Vec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointOnSegment2(Vec2 point, Vec2 a, Vec2 b, double epsilon) {
    return std::abs(cross2(a, b, point)) <= epsilon &&
           point.x >= std::min(a.x, b.x) - epsilon && point.x <= std::max(a.x, b.x) + epsilon &&
           point.y >= std::min(a.y, b.y) - epsilon && point.y <= std::max(a.y, b.y) + epsilon;
}

bool segmentsCross(Vec2 a, Vec2 b, Vec2 c, Vec2 d, double epsilon) {
    const double abC = cross2(a, b, c);
    const double abD = cross2(a, b, d);
    const double cdA = cross2(c, d, a);
    const double cdB = cross2(c, d, b);
    if (((abC > epsilon && abD < -epsilon) || (abC < -epsilon && abD > epsilon)) &&
        ((cdA > epsilon && cdB < -epsilon) || (cdA < -epsilon && cdB > epsilon))) return true;
    return (std::abs(abC) <= epsilon && pointOnSegment2(c, a, b, epsilon)) ||
           (std::abs(abD) <= epsilon && pointOnSegment2(d, a, b, epsilon)) ||
           (std::abs(cdA) <= epsilon && pointOnSegment2(a, c, d, epsilon)) ||
           (std::abs(cdB) <= epsilon && pointOnSegment2(b, c, d, epsilon));
}

bool pointInPolygon2(Vec2 point, const std::vector<Vec2>& polygon) {
    if (polygon.size() < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2 a = polygon[j];
        const Vec2 b = polygon[i];
        if (pointOnSegment2(point, a, b, 1e-8)) return true;
        if ((a.y > point.y) != (b.y > point.y)) {
            const double x = a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (x >= point.x) inside = !inside;
        }
    }
    return inside;
}

bool segmentHitsPolygon(Vec2 a, Vec2 b, const std::vector<Vec2>& polygon, double epsilon) {
    if (pointInPolygon2(a, polygon) || pointInPolygon2(b, polygon)) return true;
    for (size_t index = 0; index < polygon.size(); ++index)
        if (segmentsCross(a, b, polygon[index], polygon[(index + 1) % polygon.size()], epsilon)) return true;
    return false;
}

bool pointInPartWithTolerance(const PartRecord& part, Vec2 point, double tolerance) {
    if (!part.mesh) return false;
    const bool inside = part.spatialIndex
        ? pointInsideMeshProjection(*part.spatialIndex, point)
        : pointInsideMeshProjection(*part.mesh, point);
    if (inside) return true;
    Vec2 closest{};
    const double distanceSquared = part.spatialIndex
        ? closestPointOnMeshProjectionBoundary(*part.spatialIndex, point, closest)
        : closestPointOnMeshProjectionBoundary(*part.mesh, point, closest);
    return distanceSquared <= tolerance * tolerance;
}

bool outlineInsidePart(const PartRecord& part, const std::vector<Vec2>& outline, double tolerance) {
    if (outline.size() < 3) return false;
    for (size_t index = 0; index < outline.size(); ++index) {
        const Vec2 point = outline[index];
        const Vec2 midpoint = (point + outline[(index + 1) % outline.size()]) * 0.5;
        if (!pointInPartWithTolerance(part, point, tolerance) ||
            !pointInPartWithTolerance(part, midpoint, tolerance)) return false;
    }
    return true;
}

} // namespace

bool polylineHitsPartProjection(const PartRecord& part, const std::vector<Vec2>& points) {
    return part.mesh && (part.spatialIndex
        ? polylineHitsMeshProjection(*part.spatialIndex, points)
        : polylineHitsMeshProjection(*part.mesh, points));
}

void App::markConnectorCutConflicts() {
    if (preview.empty() || cuts.empty() || !source) return;
    const double epsilon = std::max(source->bounds.diagonal() * 1e-8, 1e-6);
    for (ConnectorPreview& connector : preview) {
        for (const CutRecord& cut : cuts) {
            for (size_t segment = 1; segment < cut.points.size(); ++segment) {
                if (segmentHitsPolygon(cut.points[segment - 1], cut.points[segment],
                                       connector.maleOutline, epsilon) ||
                    segmentHitsPolygon(cut.points[segment - 1], cut.points[segment],
                                       connector.socketOutline, epsilon)) {
                    connector.intersectsExistingCut = true;
                    break;
                }
            }
            if (connector.intersectsExistingCut) break;
        }
    }
}

std::optional<double> App::smartConnectorPosition(
    const ConnectorPlacement& prototype, std::optional<double> preferred) const {
    if (!hasCompleteCut() || !source) return std::nullopt;
    const double lineLength = polylineLength(cutPoints);
    if (lineLength < 1e-9) return std::nullopt;
    const double head = prototype.headWidth > 0.0 ? prototype.headWidth : settings.headWidth;
    const double clearance = prototype.clearance >= 0.0 ? prototype.clearance : settings.clearance;
    const double limit = std::min(std::max(0.5 * (head + 2.0 * clearance), 0.5) / lineLength, 0.47);

    std::vector<double> candidates;
    if (preferred) candidates.push_back(std::clamp(*preferred, limit, 1.0 - limit));
    constexpr int sampleCount = 81;
    for (int sample = 0; sample < sampleCount; ++sample) {
        const double fraction = sample / static_cast<double>(sampleCount - 1);
        candidates.push_back(limit + fraction * (1.0 - 2.0 * limit));
    }

    std::vector<std::pair<double, double>> scoredCandidates;
    scoredCandidates.reserve(candidates.size());
    for (double position : candidates) {
        ConnectorPlacement candidate = prototype;
        candidate.position = position;
        std::vector<ConnectorPlacement> trial = placements;
        trial.push_back(candidate);
        DovetailSettings quick = settings;
        quick.validateInsideModel = false;
        const std::vector<ConnectorPreview> trialPreview =
            makeConnectorPreview(*source, cutPoints, quick, trial);
        if (trialPreview.size() != trial.size() || !trialPreview.back().valid ||
            trialPreview.back().overlap || trialPreview.back().crossesCutCorner) continue;
        const ConnectorPreview& outline = trialPreview.back();
        std::optional<ConnectorPreview> reserveOutline;
        if (settings.validateWallThickness && settings.minimumWall > 0.0) {
            const double wall = std::clamp(settings.minimumWall, 0.0, 20.0);
            ConnectorPlacement reserve = candidate;
            const double neck = candidate.neckWidth > 0.0 ? candidate.neckWidth : settings.neckWidth;
            const double localHead = candidate.headWidth > 0.0 ? candidate.headWidth : settings.headWidth;
            const double depth = candidate.depth > 0.0 ? candidate.depth : settings.depth;
            const double embed = candidate.embed > 0.0 ? candidate.embed : settings.embed;
            reserve.neckWidth = neck + 2.0 * wall;
            reserve.headWidth = localHead + 2.0 * wall;
            reserve.depth = depth + wall;
            reserve.embed = embed + wall;
            const std::vector<ConnectorPreview> expanded =
                makeConnectorPreview(*source, cutPoints, quick, {reserve});
            if (expanded.size() != 1) continue;
            reserveOutline = expanded.front();
        }
        bool fits = true;
        bool hasTarget = false;
        const double boundaryTolerance = std::max(source->bounds.diagonal() * 2e-5, 0.02);
        for (const PartRecord& part : parts) {
            if (!part.active || (!cutAllActiveParts && part.id != selectedPartId) ||
                (cutAllActiveParts && !polylineHitsPartProjection(part, cutPoints))) continue;
            hasTarget = true;
            if (!outlineInsidePart(part, outline.maleOutline, boundaryTolerance) ||
                !outlineInsidePart(part, outline.socketOutline, boundaryTolerance) ||
                (reserveOutline &&
                 (!outlineInsidePart(part, reserveOutline->maleOutline, boundaryTolerance) ||
                  !outlineInsidePart(part, reserveOutline->socketOutline, boundaryTolerance)))) {
                fits = false;
                break;
            }
        }
        if (!hasTarget || !fits) continue;

        bool oldCutConflict = false;
        for (const CutRecord& cut : cuts) {
            for (size_t segment = 1; segment < cut.points.size(); ++segment) {
                if (segmentHitsPolygon(cut.points[segment - 1], cut.points[segment],
                                       outline.maleOutline, boundaryTolerance) ||
                    segmentHitsPolygon(cut.points[segment - 1], cut.points[segment],
                                       outline.socketOutline, boundaryTolerance)) {
                    oldCutConflict = true;
                    break;
                }
            }
            if (oldCutConflict) break;
        }
        if (oldCutConflict) continue;

        double separation = std::min(position - limit, 1.0 - limit - position);
        for (const ConnectorPlacement& occupied : placements)
            separation = std::min(separation, std::abs(position - occupied.position));
        const double preference = preferred ? (1.0 - std::abs(position - *preferred)) * 0.03 : 0.0;
        const double score = separation + preference;
        scoredCandidates.emplace_back(score, position);
    }
    std::sort(scoredCandidates.begin(), scoredCandidates.end(),
              [](const auto& first, const auto& second) {
                  return first.first > second.first;
              });

    // The inexpensive projection test above rejects most impossible places.
    // Confirm the remaining candidates with the same material/boolean check
    // used by the final preview. This catches holes or narrow voids that sit
    // completely inside a connector outline and therefore do not intersect
    // any of its sampled boundary points.
    for (const auto& [score, position] : scoredCandidates) {
        (void)score;
        ConnectorPlacement candidate = prototype;
        candidate.position = position;
        std::vector<ConnectorPlacement> trial = placements;
        trial.push_back(candidate);
        bool hasTarget = false;
        bool validForEveryTarget = true;
        DovetailSettings exact = settings;
        exact.validateInsideModel = true;
        for (const PartRecord& part : parts) {
            if (!part.active || !part.mesh ||
                (!cutAllActiveParts && part.id != selectedPartId) ||
                (cutAllActiveParts && !polylineHitsPartProjection(part, cutPoints))) continue;
            hasTarget = true;
            const std::vector<ConnectorPreview> exactPreview =
                makeConnectorPreview(*part.mesh, cutPoints, exact, trial);
            if (exactPreview.size() != trial.size()) {
                validForEveryTarget = false;
                break;
            }
            const ConnectorPreview& placed = exactPreview.back();
            if (!placed.valid || placed.overlap || placed.crossesCutCorner || placed.thinWall) {
                validForEveryTarget = false;
                break;
            }
        }
        if (hasTarget && validForEveryTarget) return position;
    }
    return std::nullopt;
}

size_t App::autoPlaceProblemConnectors() {
    if (!hasCompleteCut()) return 0;
    updatePreview();
    size_t moved = 0;
    for (size_t index = 0; index < placements.size();) {
        if (index >= preview.size()) break;
        const bool problem = !preview[index].valid || preview[index].partiallyValid ||
                             preview[index].warning();
        if (!problem || placements[index].locked) {
            ++index;
            continue;
        }
        const ConnectorPlacement original = placements[index];
        placements.erase(placements.begin() + static_cast<std::ptrdiff_t>(index));
        const std::optional<double> replacement = smartConnectorPosition(original, original.position);
        ConnectorPlacement restored = original;
        if (replacement) restored.position = *replacement;
        placements.insert(placements.begin() + static_cast<std::ptrdiff_t>(index), restored);
        if (replacement && std::abs(*replacement - original.position) > 1e-8) ++moved;
        updatePreview();
        ++index;
    }
    if (moved > 0) {
        invalidateResult();
        dirty = true;
    }
    return moved;
}

Vec2 App::snappedCutPoint(Vec2 raw, std::optional<Vec2> anchor,
                          double modelSnapTolerance) const {
    Vec2 result = raw;
    if (snapToGrid && std::isfinite(snapGridStep) && snapGridStep > 1e-6) {
        result.x = std::round(result.x / snapGridStep) * snapGridStep;
        result.y = std::round(result.y / snapGridStep) * snapGridStep;
    }
    if (anchor && snapToAngles && std::isfinite(snapAngleStep) && snapAngleStep > 0.0) {
        const Vec2 delta = result - *anchor;
        const double distance = length(delta);
        if (distance > 1e-9) {
            const double step = snapAngleStep * std::numbers::pi / 180.0;
            const double angle = std::round(std::atan2(delta.y, delta.x) / step) * step;
            result = *anchor + Vec2{std::cos(angle) * distance, std::sin(angle) * distance};
        }
    }
    if (snapToModelEdge && modelSnapTolerance > 0.0) {
        const Vec2 edge = closestModelEdgePoint(result);
        if (length(edge - result) <= modelSnapTolerance) result = edge;
    }
    return result;
}

bool App::alignCutSegment(size_t segmentIndex, double degrees) {
    if (!hasCompleteCut() || segmentIndex + 1 >= cutPoints.size()) return false;
    const size_t first = segmentIndex;
    const size_t second = segmentIndex + 1;
    const bool firstLocked = first < cutPointLocked.size() && cutPointLocked[first];
    const bool secondLocked = second < cutPointLocked.size() && cutPointLocked[second];
    if (firstLocked && secondLocked) return false;
    std::vector<Vec2> centers;
    centers.reserve(preview.size());
    for (const ConnectorPreview& connector : preview) centers.push_back(connector.center);
    const double distance = length(cutPoints[second] - cutPoints[first]);
    const double angle = degrees * std::numbers::pi / 180.0;
    const Vec2 direction{std::cos(angle), std::sin(angle)};
    if (!secondLocked) cutPoints[second] = cutPoints[first] + direction * distance;
    else cutPoints[first] = cutPoints[second] - direction * distance;
    for (size_t index = 0; index < placements.size() && index < centers.size(); ++index)
        placements[index].position = closestPositionOnPolyline(cutPoints, centers[index]);
    invalidateResult();
    updatePreview();
    dirty = true;
    return true;
}
