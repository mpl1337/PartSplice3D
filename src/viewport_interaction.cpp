#include "viewport_interaction.h"

#include "app_queries.h"
#include "interaction_hit_test.h"
#include "model_projection.h"
#include "visual_style.h"
#include "viewport_render.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

std::vector<int> appliedCutIds(const App& app) {
    std::vector<int> ids;
    std::unordered_set<int> seen;
    for (auto cut = app.cuts.rbegin(); cut != app.cuts.rend(); ++cut) {
        if (seen.insert(cut->id).second) ids.push_back(cut->id);
    }
    return ids;
}

std::vector<int> appliedCutsAt(const App& app, Vec2 point, double radius) {
    std::vector<std::pair<double, int>> hits;
    std::unordered_map<int, double> bestById;
    for (const CutRecord& cut : app.cuts) {
        for (size_t index = 1; index < cut.points.size(); ++index) {
            const double distance = segmentDistance(point, cut.points[index - 1], cut.points[index]);
            if (distance <= radius) {
                auto found = bestById.find(cut.id);
                if (found == bestById.end() || distance < found->second) bestById[cut.id] = distance;
            }
        }
    }
    hits.reserve(bestById.size());
    for (const auto& [id, distance] : bestById) hits.emplace_back(distance, id);
    std::sort(hits.begin(), hits.end());
    std::vector<int> ids;
    ids.reserve(hits.size());
    for (const auto& [distance, id] : hits) ids.push_back(id);
    return ids;
}

AppliedConnectorHit appliedConnectorAt(const App& app, Vec2 point, double radius) {
    AppliedConnectorHit hit;
    std::unordered_set<int> seen;
    for (auto cut = app.cuts.rbegin(); cut != app.cuts.rend(); ++cut) {
        if (!seen.insert(cut->id).second) continue;
        const std::vector<ConnectorPreview>& preview = appliedConnectorPreview(app, *cut);
        for (size_t index = 0; index < preview.size(); ++index) {
            const double centerDistance = length(preview[index].center - point);
            const bool inside = centerDistance <= radius ||
                hitTestConnectorOutline(preview[index].maleOutline, point, radius * 0.35) ||
                hitTestConnectorOutline(preview[index].socketOutline, point, radius * 0.35);
            if (inside && centerDistance < hit.distance) {
                hit.cutId = cut->id;
                hit.connector = static_cast<int>(index);
                hit.distance = centerDistance;
            }
        }
    }
    return hit;
}

void moveAppliedConnectorAlongLine(App& app, int operationId, int connectorIndex, Vec2 point) {
    for (CutRecord& cut : app.cuts) {
        if (cut.id != operationId || connectorIndex < 0 ||
            static_cast<size_t>(connectorIndex) >= cut.placements.size()) continue;
        ConnectorPlacement& placement = cut.placements[static_cast<size_t>(connectorIndex)];
        if (placement.locked) continue;
        const double lineLength = polylineLength(cut.points);
        if (lineLength < 1e-9) continue;
        double position = closestPositionOnPolyline(cut.points, point);
        const double headWidth = placement.headWidth > 0.0 ? placement.headWidth : cut.settings.headWidth;
        const double clearance = placement.clearance >= 0.0 ? placement.clearance : cut.settings.clearance;
        const double edge = std::max(0.5 * (headWidth + 2.0 * clearance), 0.5);
        const double limit = std::min(edge / lineLength, 0.49);
        placement.position = std::clamp(position, limit, 1.0 - limit);
        cut.previewCacheValid = false;
        cut.previewCache.clear();
    }
    app.dirty = true;
}

bool pointInsidePartProjection(const PartRecord& part, Vec2 point) {
    return part.mesh && (part.spatialIndex
        ? pointInsideMeshProjection(*part.spatialIndex, point)
        : pointInsideMeshProjection(*part.mesh, point));
}


int activePartAt(const App& app, Vec2 point) {
    for (const PartRecord& part : app.parts)
        if (part.active && pointInsidePartProjection(part, point)) return part.id;
    return -1;
}

ImVec4 activePartColor(const App& app, int partId, int cutId) {
    size_t colorIndex = 0;
    for (const PartRecord& part : app.parts) {
        if (!part.active) continue;
        if (part.id == partId) {
            const auto& color = kPartColors[colorIndex % kPartColors.size()];
            return {color[0], color[1], color[2], 1.0f};
        }
        ++colorIndex;
    }
    return cutColorImGui(cutId);
}

CutPartColorPair cutPartColorsAt(const App& app, int operationId, Vec2 reference) {
    const CutRecord* cut = nullptr;
    for (const CutRecord& candidate : app.cuts) if (candidate.id == operationId) { cut = &candidate; break; }
    if (cut == nullptr || cut->points.size() < 2)
        return {cutColorImGui(operationId), cutColorImGui(operationId)};
    Vec2 projected{};
    Vec2 tangent{1.0, 0.0};
    double best = std::numeric_limits<double>::infinity();
    for (size_t index = 1; index < cut->points.size(); ++index) {
        const Vec2 a = cut->points[index - 1];
        const Vec2 delta = cut->points[index] - a;
        const double length2 = dot(delta, delta);
        if (length2 < 1e-12) continue;
        const double local = std::clamp(dot(reference - a, delta) / length2, 0.0, 1.0);
        const Vec2 candidate = a + delta * local;
        const double distance = length(reference - candidate);
        if (distance < best) { best = distance; projected = candidate; tangent = normalized(delta); }
    }
    const Vec2 normal = leftNormal(tangent);
    const double baseDistance = std::max({1.0, cut->settings.depth * 1.35,
                                         app.source->bounds.diagonal() * 0.008});
    int leftPart = -1;
    int rightPart = -1;
    for (double factor : {1.0, 1.75, 2.5, 3.5}) {
        for (double along : {0.0, -0.5, 0.5}) {
            if (leftPart < 0) leftPart = activePartAt(app, projected + normal * (baseDistance * factor) + tangent * (baseDistance * along));
            if (rightPart < 0) rightPart = activePartAt(app, projected - normal * (baseDistance * factor) + tangent * (baseDistance * along));
        }
        if (leftPart >= 0 && rightPart >= 0) break;
    }
    return {activePartColor(app, leftPart, operationId), activePartColor(app, rightPart, operationId)};
}

bool splitColorButton(const char* id, CutPartColorPair colors) {
    const ImVec2 size{22.0f, 13.0f};
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 left = ImGui::ColorConvertFloat4ToU32(colors.left);
    const ImU32 right = ImGui::ColorConvertFloat4ToU32(colors.right);
    draw->AddRectFilled(start, {start.x + size.x * 0.5f, start.y + size.y}, left, 2.0f,
                        ImDrawFlags_RoundCornersLeft);
    draw->AddRectFilled({start.x + size.x * 0.5f, start.y}, {start.x + size.x, start.y + size.y}, right,
                        2.0f, ImDrawFlags_RoundCornersRight);
    draw->AddRect(start, {start.x + size.x, start.y + size.y}, IM_COL32(225, 225, 225, 190), 2.0f);
    return clicked;
}

bool printBedFrameAt(const App& app, Vec2 point, double radius) {
    const Vec2 local = rotate2(point - app.printBedCenter, -app.printBedRotation);
    const double halfW = app.printBedWidth * 0.5;
    const double halfD = app.printBedDepth * 0.5;
    const bool nearVertical = std::abs(std::abs(local.x) - halfW) <= radius &&
                              local.y >= -halfD - radius && local.y <= halfD + radius;
    const bool nearHorizontal = std::abs(std::abs(local.y) - halfD) <= radius &&
                                local.x >= -halfW - radius && local.x <= halfW + radius;
    return nearVertical || nearHorizontal;
}

void moveConnectorAlongLine(App& app, int index, Vec2 p) {
    if (index < 0 || static_cast<size_t>(index) >= app.placements.size()) return;
    if (app.placements[static_cast<size_t>(index)].locked) return;
    const double len = polylineLength(app.cutPoints);
    if (len < 1e-9) return;
    double t = closestPositionOnPolyline(app.cutPoints, p);
    const double edge = std::max(0.5 * (app.settings.headWidth + 2.0 * app.settings.clearance), 0.5);
    const double limit = std::min(edge / len, 0.49);
    t = std::clamp(t, limit, 1.0 - limit);
    app.placements[static_cast<size_t>(index)].position = t;
    app.invalidateResult();
    // Dragging only needs outlines and collision feedback. The expensive 3D
    // material check runs once when the mouse is released.
    app.updatePreview(false);
}
