#include "viewport_input.h"

#include "app_queries.h"
#include "connector_editor.h"
#include "generation_workflow.h"
#include "interaction_hit_test.h"
#include "viewport_interaction.h"
#include "viewport_render.h"

#include <imgui.h>
#include "localized_imgui.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

void handleViewportInput(App& app, ImGuiIO& io, double mx, double my,
                         int winW, int winH, bool mouseFree) {
    if (!app.loaded || !mouseFree) return;

    if (io.MouseWheel != 0.0f) {
        const double factor = std::pow(0.88, io.MouseWheel);
        if (app.camera.top) app.camera.topHeight = std::clamp(app.camera.topHeight * factor, 0.5, 100000.0);
        else app.camera.orbitDistance = std::clamp(app.camera.orbitDistance * factor, 0.5, 100000.0);
    }

    const double printBedPlaneZ = app.source->bounds.max.z +
        std::max(0.20, app.source->bounds.size().z * 0.03);
    const std::optional<Vec2> projected3d = app.camera.top
        ? std::optional<Vec2>{} : mouseTo3dPlane(app, mx, my, winW, winH, printBedPlaneZ);
    const bool hasMouseWorld = app.camera.top || projected3d.has_value();
    const Vec2 mouseWorld = app.camera.top ? mouseToTopWorld(app, mx, my, winW, winH)
                                           : projected3d.value_or(app.printBedCenter);
    const double endpointRadius = hitRadiusWorld(app, winH, 11.0);
    const double connectorRadius = hitRadiusWorld(app, winH, 13.0);
    const std::vector<ConnectorHitCandidate> hoveredConnectorHits =
        (app.camera.top && app.hasCompleteCut() && !app.preview.empty())
            ? hitTestConnectors(app.preview, mouseWorld, connectorRadius)
            : std::vector<ConnectorHitCandidate>{};
    const int hoveredConnector = hoveredConnectorHits.empty()
        ? -1 : hoveredConnectorHits.front().index;
    const int hoveredCutPoint = (app.camera.top && app.hasCompleteCut())
        ? hitTestCutPoint(app.cutPoints, mouseWorld, endpointRadius) : -1;
    const LineSegmentHit hoveredLine = (app.camera.top && app.hasCompleteCut())
        ? hitTestLineSegment(app.cutPoints, mouseWorld, endpointRadius) : LineSegmentHit{};
    const std::vector<int> hoveredAppliedCuts = app.camera.top
        ? appliedCutsAt(app, mouseWorld, endpointRadius) : std::vector<int>{};
    const AppliedConnectorHit hoveredAppliedConnector = (app.camera.top && !app.cuts.empty())
        ? appliedConnectorAt(app, mouseWorld, connectorRadius) : AppliedConnectorHit{};
    const AssemblyMarkPreview assemblyMark =
        (app.camera.top && app.hasCompleteCut() && app.settings.assemblyMarks)
            ? makeAssemblyMarkPreview(app.cutPoints, app.settings, app.activeAssemblyMarkCode())
            : AssemblyMarkPreview{};
    const size_t markDigits = std::to_string(app.activeAssemblyMarkCode()).size();
    const double markHitRadius = std::max(
        connectorRadius, app.settings.assemblyMarkSize * (0.75 + markDigits * 0.22));
    const double leftMarkDistance = assemblyMark.valid
        ? length(mouseWorld - assemblyMark.leftCenter) : std::numeric_limits<double>::infinity();
    const double rightMarkDistance = assemblyMark.valid
        ? length(mouseWorld - assemblyMark.rightCenter) : std::numeric_limits<double>::infinity();
    const int hoveredAssemblyMarkSide = std::min(leftMarkDistance, rightMarkDistance) <= markHitRadius
        ? (leftMarkDistance <= rightMarkDistance ? 0 : 1) : -1;
    const bool hoveredAssemblyMark = hoveredAssemblyMarkSide >= 0;
    int hoveredAppliedMarkCutId = -1;
    double hoveredAppliedMarkDistance = std::numeric_limits<double>::infinity();
    if (app.camera.top && !app.hasCompleteCut()) {
        std::unordered_set<int> inspectedCuts;
        for (const CutRecord& cut : app.cuts) {
            if (!cut.settings.assemblyMarks || !inspectedCuts.insert(cut.id).second) continue;
            const AssemblyMarkPreview appliedMark = makeAssemblyMarkPreview(
                cut.points, cut.settings, cut.settings.assemblyMarkCode);
            if (!appliedMark.valid) continue;
            const double distance = std::min(length(mouseWorld - appliedMark.leftCenter),
                                             length(mouseWorld - appliedMark.rightCenter));
            const double radius = std::max(connectorRadius, cut.settings.assemblyMarkSize * 1.1);
            if (distance <= radius && distance < hoveredAppliedMarkDistance) {
                hoveredAppliedMarkDistance = distance;
                hoveredAppliedMarkCutId = cut.id;
            }
        }
    }

    if (hoveredConnector >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right) && app.dragTarget == DragTarget::None) {
        const size_t index = static_cast<size_t>(hoveredConnector);
        if (index < app.placements.size() && index < app.preview.size()) {
            const ConnectorPlacement& placement = app.placements[index];
            const ConnectorPreview& preview = app.preview[index];
            ImGui::BeginTooltip();
            ImGui::Text("Verbinder %d", hoveredConnector + 1);
            const ImVec4 qualityColor = !preview.valid
                ? ImVec4{1.0f, 0.30f, 0.25f, 1.0f}
                : (preview.intersectsExistingCut ? ImVec4{0.88f, 0.35f, 1.0f, 1.0f}
                   : (preview.thinWall ? ImVec4{1.0f, 0.86f, 0.18f, 1.0f}
                                       : ImVec4{0.35f, 1.0f, 0.55f, 1.0f}));
            const char* qualityText = preview.crossesCutCorner
                ? "überschneidet einen Knick der Schnittlinie"
                : (!preview.valid ? "nicht vollständig im Material"
                : (preview.intersectsExistingCut ? "kollidiert mit einem früheren Schnitt"
                   : (preview.thinWall ? "Wandreserve unterschritten" : "Geometrieprüfung bestanden")));
            const std::string localizedQuality = localizedText(qualityText);
            const std::string localizedMovement = localizedText(
                placement.locked ? "eingefroren" : "beweglich");
            const std::string localizedSide = localizedText(
                placement.maleOnLeft ? "Zapfen lokal links" : "Zapfen lokal rechts");
            ImGui::TextColored(qualityColor, "%s", localizedQuality.c_str());
            ImGui::TextDisabled("%s · %s", localizedMovement.c_str(), localizedSide.c_str());
            if (hoveredConnectorHits.size() > 1)
                ImGui::TextColored({1.0f, 0.80f, 0.25f, 1.0f},
                                   "%zu Treffer: wiederholt klicken zum Durchschalten",
                                   hoveredConnectorHits.size());
            ImGui::EndTooltip();
        }
    } else if (hoveredAssemblyMark && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
               !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
               app.dragTarget == DragTarget::None) {
        ImGui::BeginTooltip();
        ImGui::Text("Schnittnummer %d", assemblyMark.code);
        ImGui::TextDisabled("Frei ziehen = nur diese Zahl verschieben · Anklicken = Einstellungen");
        ImGui::EndTooltip();
    } else if (hoveredAppliedMarkCutId >= 0 &&
               !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
               !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        ImGui::BeginTooltip();
        ImGui::Text("Gravur von Schnitt %d", hoveredAppliedMarkCutId);
        ImGui::TextDisabled("Doppelklick = Schnittnummer erneut bearbeiten");
        ImGui::EndTooltip();
    }

    if (app.measureMode && app.camera.top) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            app.measurePoints.clear();
            app.measureMode = false;
            app.blockRightOrbit = true;
            app.status = "Messen beendet.";
            return;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (app.measurePoints.size() >= 2) app.measurePoints.clear();
            const std::optional<Vec2> anchor = app.measurePoints.empty()
                ? std::nullopt : std::optional<Vec2>{app.measurePoints.back()};
            app.measurePoints.push_back(io.KeyCtrl
                ? mouseWorld : app.snappedCutPoint(mouseWorld, anchor, endpointRadius));
            if (app.measurePoints.size() == 1) {
                app.status = "Messpunkt 1 gesetzt. Zweiten Punkt anklicken.";
            } else {
                app.status = "Abstand: " + std::to_string(length(app.measurePoints[1] - app.measurePoints[0])) + " mm";
            }
            return;
        }
    }

    if (app.drawingLine && app.camera.top) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            app.blockRightOrbit = true;
            if (app.cutMode == CutMode::Polyline && app.cutPoints.size() >= 2) app.finishCut();
            else app.status = "Für einen Schnitt werden mindestens zwei Punkte benötigt.";
            return;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const std::optional<Vec2> anchor = app.cutPoints.empty()
                ? std::nullopt : std::optional<Vec2>{app.cutPoints.back()};
            const Vec2 snapped = io.KeyCtrl
                ? mouseWorld : app.snappedCutPoint(mouseWorld, anchor, endpointRadius);
            if (!app.cutPoints.empty() && length(snapped - app.cutPoints.back()) <= endpointRadius * 0.35) {
                app.status = "Der neue Punkt liegt zu dicht am vorherigen Punkt.";
                return;
            }
            app.captureUndo("Schnittpunkt hinzufügen");
            app.cutPoints.push_back(snapped);
            app.cutPointLocked.push_back(false);
            app.dirty = true;
            if (app.cutMode == CutMode::Straight && app.cutPoints.size() == 2) app.finishCut();
            else app.status = app.cutPoints.size() == 1
                ? "Nächsten Schnittpunkt anklicken."
                : "Weitere Ecke anklicken oder den Linienzug mit Rechtsklick abschließen.";
            return;
        }
    }

    if (app.camera.top && app.cutPoints.empty() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hoveredAppliedMarkCutId >= 0) {
        requestEditCut(app, hoveredAppliedMarkCutId);
        return;
    }

    if (app.camera.top && app.cutPoints.empty() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !hoveredAppliedCuts.empty()) {
        if (hoveredAppliedCuts.size() == 1) {
            requestEditCut(app, hoveredAppliedCuts.front());
        } else {
            app.contextConnector = -1;
            app.contextCutPoint = -1;
            app.contextLineSegment = -1;
            app.contextAppliedCutIds = hoveredAppliedCuts;
            app.contextModelSurface = app.pointInsideActiveProjection(mouseWorld);
            app.contextWorld = mouseWorld;
            app.openViewportContext = true;
        }
        return;
    }

    if (app.camera.top && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (app.hasCompleteCut() &&
            (hoveredConnector >= 0 || hoveredCutPoint >= 0 || hoveredLine.segment >= 0)) {
            app.contextConnector = hoveredConnector;
            app.contextConnectorCandidates.clear();
            for (const ConnectorHitCandidate& hit : hoveredConnectorHits)
                app.contextConnectorCandidates.push_back(hit.index);
            if (hoveredConnector >= 0) app.selectedConnector = hoveredConnector;
            app.contextCutPoint = hoveredConnector >= 0 ? -1 : hoveredCutPoint;
            app.contextLineSegment = (hoveredConnector >= 0 || hoveredCutPoint >= 0) ? -1 : hoveredLine.segment;
            app.contextAppliedCutIds.clear();
            app.contextModelSurface = app.pointInsideActiveProjection(mouseWorld);
            app.contextWorld = hoveredLine.segment >= 0 ? hoveredLine.projected : mouseWorld;
            app.openViewportContext = true;
            app.blockRightOrbit = true;
        } else if (!app.cuts.empty() &&
                   (!hoveredAppliedCuts.empty() || app.pointInsideActiveProjection(mouseWorld))) {
            app.contextConnector = -1;
            app.contextConnectorCandidates.clear();
            app.contextCutPoint = -1;
            app.contextLineSegment = -1;
            app.contextAppliedCutIds = hoveredAppliedCuts.empty() ? appliedCutIds(app) : hoveredAppliedCuts;
            app.contextModelSurface = app.pointInsideActiveProjection(mouseWorld);
            app.contextWorld = mouseWorld;
            app.openViewportContext = true;
            app.blockRightOrbit = true;
        } else if (app.pointInsideActiveProjection(mouseWorld)) {
            app.contextConnector = -1;
            app.contextConnectorCandidates.clear();
            app.contextCutPoint = -1;
            app.contextLineSegment = -1;
            app.contextAppliedCutIds.clear();
            app.contextModelSurface = true;
            app.contextWorld = mouseWorld;
            app.openViewportContext = true;
            app.blockRightOrbit = true;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) app.blockRightOrbit = false;

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) && !app.drawingLine && !app.blockRightOrbit && hoveredConnector < 0) {
        const ImVec2 d = io.MouseDelta;
        app.camera.top = false;
        app.camera.yaw -= d.x * 0.35;
        app.camera.pitch = std::clamp(app.camera.pitch + d.y * 0.35, -88.0, 88.0);
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const ImVec2 d = io.MouseDelta;
        const double unitsPerPixel = app.camera.topHeight / std::max(winH, 1);
        app.camera.pan.x -= d.x * unitsPerPixel;
        app.camera.pan.y += d.y * unitsPerPixel;
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hoveredCutPoint >= 0) {
            const bool locked = static_cast<size_t>(hoveredCutPoint) < app.cutPointLocked.size() &&
                                app.cutPointLocked[static_cast<size_t>(hoveredCutPoint)];
            if (locked) {
                app.status = "Dieser Schnittpunkt ist eingefroren. Mit Rechtsklick kann er wieder gelöst werden.";
            } else {
                app.captureUndo("Schnittpunkt verschieben");
                app.cutPointDragConnectorCenters.clear();
                app.cutPointDragConnectorCenters.reserve(app.preview.size());
                for (const ConnectorPreview& connector : app.preview)
                    app.cutPointDragConnectorCenters.push_back(connector.center);
                app.dragTarget = DragTarget::CutPoint;
                app.draggedCutPoint = hoveredCutPoint;
                app.draggedConnector = -1;
            }
        } else if (hoveredConnector >= 0) {
            int chosenConnector = hoveredConnector;
            std::vector<int> hitIds;
            hitIds.reserve(hoveredConnectorHits.size());
            for (const ConnectorHitCandidate& hit : hoveredConnectorHits)
                hitIds.push_back(hit.index);
            const bool sameHitSet = hitIds == app.connectorHitCycle &&
                length(mouseWorld - app.connectorHitCyclePoint) <= connectorRadius * 0.5;
            if (hitIds.size() > 1) {
                app.connectorHitCycleIndex = sameHitSet
                    ? (app.connectorHitCycleIndex + 1) % hitIds.size() : 0;
                app.connectorHitCycle = hitIds;
                app.connectorHitCyclePoint = mouseWorld;
                chosenConnector = hitIds[app.connectorHitCycleIndex];
                app.status = "Mehrere Verbinder überlappen. Erneutes Klicken wählt den nächsten Treffer.";
            } else {
                app.connectorHitCycle.clear();
                app.connectorHitCycleIndex = 0;
            }
            app.selectedConnector = chosenConnector;
            app.showConnectorEditor = true;
            app.repositionConnectorEditor = true;
            app.connectorEditorPosition = connectorPopupPosition(app);
            if (app.placements[static_cast<size_t>(chosenConnector)].locked) {
                app.status = "Dieser Verbinder ist eingefroren. Mit Rechtsklick kann er wieder gelöst werden.";
            } else {
                app.captureUndo("Verbinder verschieben");
                app.dragTarget = DragTarget::Connector;
                app.draggedConnector = chosenConnector;
            }
        } else if (hoveredAssemblyMark) {
            app.captureUndo("Schnittnummer verschieben");
            if (!app.settings.assemblyMarkPositionsCustom) {
                app.settings.assemblyMarkLeftX = assemblyMark.leftCenter.x;
                app.settings.assemblyMarkLeftY = assemblyMark.leftCenter.y;
                app.settings.assemblyMarkRightX = assemblyMark.rightCenter.x;
                app.settings.assemblyMarkRightY = assemblyMark.rightCenter.y;
                app.settings.assemblyMarkPositionsCustom = true;
            }
            app.dragTarget = DragTarget::AssemblyMark;
            app.draggedAssemblyMarkSide = hoveredAssemblyMarkSide;
            app.showAssemblyMarkEditor = true;
            app.status = hoveredAssemblyMarkSide == 0
                ? "Schnittnummer auf Teil A wird frei verschoben."
                : "Schnittnummer auf Teil B wird frei verschoben.";
        } else if (hoveredAppliedConnector.cutId >= 0 && hoveredAppliedConnector.connector >= 0) {
            const auto cut = std::find_if(app.cuts.begin(), app.cuts.end(), [&](const CutRecord& candidate) {
                return candidate.id == hoveredAppliedConnector.cutId;
            });
            const bool locked = cut != app.cuts.end() &&
                static_cast<size_t>(hoveredAppliedConnector.connector) < cut->placements.size() &&
                cut->placements[static_cast<size_t>(hoveredAppliedConnector.connector)].locked;
            if (locked) {
                app.status = "Dieser bereits ausgeführte Verbinder ist eingefroren.";
            } else if (app.pendingAppliedCutRebuildId >= 0 &&
                       app.pendingAppliedCutRebuildId != hoveredAppliedConnector.cutId) {
                app.status = "Zuerst Schnitt " + std::to_string(app.pendingAppliedCutRebuildId) +
                             " mit ‚Neu schneiden‘ übernehmen oder mit Strg+Z verwerfen.";
            } else if (cutHasDependentOperations(app, hoveredAppliedConnector.cutId)) {
                requestEditCut(app, hoveredAppliedConnector.cutId);
                app.status = "Dieser Schnitt besitzt abhängige Folgeschnitte und wird deshalb über den sicheren Bearbeitungsdialog geöffnet.";
            } else {
                if (app.pendingAppliedCutRebuildId < 0) {
                    app.captureUndo("Ausgeführten Verbinder verschieben", UndoKind::DeletedCut,
                                    hoveredAppliedConnector.cutId, true);
                    app.pendingAppliedCutRebuildId = hoveredAppliedConnector.cutId;
                }
                app.dragTarget = DragTarget::AppliedConnector;
                app.draggedAppliedCutId = hoveredAppliedConnector.cutId;
                app.draggedAppliedConnector = hoveredAppliedConnector.connector;
                app.showConnectorEditor = false;
                app.status = "Verbinder des früheren Schnitts wird verschoben. Der rote Schnitt bleibt erhalten.";
            }
        } else if (hasMouseWorld && app.showPrintBed && (length(mouseWorld - app.printBedCenter) <= connectorRadius ||
                                       printBedFrameAt(app, mouseWorld, endpointRadius))) {
            app.captureUndo("Druckbett verschieben");
            app.dragTarget = DragTarget::PrintBed;
            app.draggedConnector = -1;
            app.printBedDragOffset = app.printBedCenter - mouseWorld;
        }
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        switch (app.dragTarget) {
            case DragTarget::CutPoint:
                if (app.draggedCutPoint >= 0 && static_cast<size_t>(app.draggedCutPoint) < app.cutPoints.size()) {
                    const size_t pointIndex = static_cast<size_t>(app.draggedCutPoint);
                    std::optional<Vec2> anchor;
                    if (pointIndex > 0) anchor = app.cutPoints[pointIndex - 1];
                    else if (pointIndex + 1 < app.cutPoints.size()) anchor = app.cutPoints[pointIndex + 1];
                    app.cutPoints[pointIndex] = io.KeyCtrl
                        ? mouseWorld : app.snappedCutPoint(mouseWorld, anchor, endpointRadius);
                    const size_t count = std::min(app.placements.size(), app.cutPointDragConnectorCenters.size());
                    for (size_t index = 0; index < count; ++index)
                        app.placements[index].position = closestPositionOnPolyline(
                            app.cutPoints, app.cutPointDragConnectorCenters[index]);
                    app.invalidateResult();
                    // A full 3D material/wall validation can take seconds on
                    // highly tessellated, painted 3MF models. Keep dragging
                    // interactive and perform that validation exactly once
                    // when the mouse button is released below.
                    app.updatePreview(false);
                    app.status = io.KeyCtrl
                        ? "Schnittpunkt wird frei verschoben (Fang mit Strg ausgesetzt)."
                        : "Schnittpunkt wird verschoben.";
                    app.dirty = true;
                }
                break;
            case DragTarget::Connector:
                moveConnectorAlongLine(app, app.draggedConnector, mouseWorld);
                app.status = "Verbinder wird entlang der Schnittlinie verschoben.";
                app.dirty = true;
                break;
            case DragTarget::AppliedConnector:
                moveAppliedConnectorAlongLine(app, app.draggedAppliedCutId,
                                              app.draggedAppliedConnector, mouseWorld);
                app.status = "Verbinder des ausgeführten Schnitts wird entlang seiner Schnittlinie verschoben.";
                break;
            case DragTarget::AssemblyMark:
                if (app.draggedAssemblyMarkSide == 0) {
                    app.settings.assemblyMarkLeftX = mouseWorld.x;
                    app.settings.assemblyMarkLeftY = mouseWorld.y;
                } else if (app.draggedAssemblyMarkSide == 1) {
                    app.settings.assemblyMarkRightX = mouseWorld.x;
                    app.settings.assemblyMarkRightY = mouseWorld.y;
                }
                app.invalidateResult();
                app.status = "Schnittnummer wird frei auf der Fläche verschoben.";
                app.dirty = true;
                break;
            case DragTarget::PrintBed:
                app.printBedCenter = mouseWorld + app.printBedDragOffset;
                app.status = "Referenz-Druckbett wird verschoben.";
                app.dirty = true;
                break;
            case DragTarget::None:
                break;
        }
    } else if (app.dragTarget != DragTarget::None) {
        const DragTarget completedTarget = app.dragTarget;
        const int completedCutPoint = app.draggedCutPoint;
        const int completedAppliedCut = app.draggedAppliedCutId;
        app.dragTarget = DragTarget::None;
        app.draggedCutPoint = -1;
        app.draggedConnector = -1;
        app.draggedAppliedCutId = -1;
        app.draggedAppliedConnector = -1;
        app.draggedAssemblyMarkSide = -1;
        if (completedTarget == DragTarget::AppliedConnector) {
            const AppliedConnectorValidation validation =
                validateAppliedConnectorPlacements(app, completedAppliedCut);
            if (validation.invalid > 0 || validation.partiallyValid > 0) {
                const size_t count = validation.invalid + validation.partiallyValid;
                app.status = "Warnung: " + std::to_string(count) +
                             (count == 1 ? " Verbinder liegt" : " Verbinder liegen") +
                             " nicht vollständig in gültigem Material und ist rot markiert. "
                             "Ungültige Verbinder werden beim Neuschneiden verworfen.";
            } else if (validation.warnings > 0) {
                app.status = "Position übernommen, aber mindestens ein Verbinder hat zu wenig Wandreserve. "
                             "Die gelbe Markierung vor dem Neuschneiden prüfen.";
            } else {
                app.status = "Positionen für Schnitt " + std::to_string(completedAppliedCut) +
                             " geprüft. Weitere Verbinder können verschoben werden; "
                             "‚Neu schneiden‘ übernimmt alle Änderungen gemeinsam.";
            }
            app.cutPointDragConnectorCenters.clear();
            return;
        }
        if (completedTarget == DragTarget::PrintBed) {
            app.status = "Position des Referenz-Druckbetts übernommen.";
        } else {
            if (completedTarget == DragTarget::CutPoint && completedCutPoint >= 0 &&
                static_cast<size_t>(completedCutPoint) < app.cutPoints.size() &&
                !app.pointInsideActiveProjection(app.cutPoints[static_cast<size_t>(completedCutPoint)])) {
                app.cutPoints[static_cast<size_t>(completedCutPoint)] =
                    app.closestModelEdgePoint(app.cutPoints[static_cast<size_t>(completedCutPoint)]);
                const size_t count = std::min(app.placements.size(), app.cutPointDragConnectorCenters.size());
                for (size_t index = 0; index < count; ++index)
                    app.placements[index].position = closestPositionOnPolyline(
                        app.cutPoints, app.cutPointDragConnectorCenters[index]);
            }
            if (completedTarget == DragTarget::CutPoint ||
                completedTarget == DragTarget::Connector)
                app.updatePreview();
            if (completedTarget == DragTarget::AssemblyMark) {
                app.status = "Position der Schnittnummer übernommen.";
            } else {
                std::string validation;
                app.status = validateCutPolyline(app.cutPoints, validation)
                    ? "Position übernommen. Rechtsklick auf einen Verbinder wechselt seine Richtung."
                    : validation;
            }
        }
        app.cutPointDragConnectorCenters.clear();
    }
}
