#include "viewport_context_menu.h"

#include "connector_editor.h"
#include "generation_workflow.h"
#include "project_actions.h"
#include "viewport_interaction.h"

#include <imgui.h>
#include "localized_imgui.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

void drawViewportContextMenu(App& app) {
    if (app.openViewportContext) {
        ImGui::OpenPopup("Schnittpunkt-Menü##Viewport");
        app.openViewportContext = false;
    }
    const bool contextOpen = ImGui::BeginPopup("Schnittpunkt-Menü##Viewport");
    if (contextOpen) {
    if (app.contextConnectorCandidates.size() > 1) {
        ImGui::TextDisabled("%zu überlappende Verbinder", app.contextConnectorCandidates.size());
        if (ImGui::BeginMenu("Verbinder auswählen")) {
            for (const int candidate : app.contextConnectorCandidates) {
                if (candidate < 0 || static_cast<size_t>(candidate) >= app.placements.size()) continue;
                const std::string label = "Verbinder " + std::to_string(candidate + 1);
                if (ImGui::MenuItem(label.c_str(), nullptr, candidate == app.contextConnector)) {
                    app.contextConnector = candidate;
                    app.selectedConnector = candidate;
                    app.showConnectorEditor = true;
                    app.repositionConnectorEditor = true;
                    app.connectorEditorPosition = connectorPopupPosition(app);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
    }
    if (app.contextConnector >= 0) {
        const size_t index = static_cast<size_t>(app.contextConnector);
        if (index < app.placements.size()) {
            bool deleted = false;
            if (ImGui::MenuItem("Verbinder hinzufügen")) {
                app.captureUndo("Verbinder hinzufügen");
                const ConnectorPlacement prototype = app.placements[index];
                app.addConnector(prototype);
                app.showConnectorEditor = true;
                app.repositionConnectorEditor = true;
                app.connectorEditorPosition = connectorPopupPosition(app);
                app.dirty = true;
                app.status = "Neuer Verbinder wurde in einen freien, vorgeprüften Bereich eingefügt.";
            }
            if (ImGui::MenuItem("Verbinder löschen")) {
                app.captureUndo("Verbinder löschen");
                app.placements.erase(app.placements.begin() + static_cast<std::ptrdiff_t>(index));
                app.settings.count = std::max(1, static_cast<int>(app.placements.size()));
                app.selectedConnector = -1;
                app.showConnectorEditor = false;
                app.invalidateResult();
                app.updatePreview();
                app.dirty = true;
                app.status = "Verbinder gelöscht.";
                deleted = true;
            }
            if (!deleted && app.placements[index].locked) {
                if (ImGui::MenuItem("Verbinder lösen")) {
                    app.captureUndo("Verbinder lösen");
                    app.placements[index].locked = false;
                    app.dirty = true;
                }
            } else if (!deleted && ImGui::MenuItem("Verbinder einfrieren")) {
                app.captureUndo("Verbinder einfrieren");
                app.placements[index].locked = true;
                app.dirty = true;
            }
            if (!deleted && ImGui::MenuItem("Verbinder drehen")) {
                app.captureUndo("Verbinderrichtung ändern");
                app.placements[index].maleOnLeft = !app.placements[index].maleOnLeft;
                app.invalidateResult();
                app.updatePreview();
                app.dirty = true;
            }
            if (!deleted) ImGui::Separator();
            if (!deleted && ImGui::MenuItem("Verbinder kopieren")) {
                app.connectorClipboard = app.placements[index];
                app.status = "Verbinder kopiert. Rechtsklick auf die Linie fügt die Kopie dort ein.";
            }
        }
    } else if (app.contextCutPoint >= 0) {
        const size_t index = static_cast<size_t>(app.contextCutPoint);
        const bool interior = index > 0 && index + 1 < app.cutPoints.size();
        const bool locked = index < app.cutPointLocked.size() && app.cutPointLocked[index];
        if (interior && ImGui::MenuItem("Knickpunkt löschen")) {
            app.captureUndo("Knickpunkt löschen");
            app.removeCutPoint(index);
            app.dirty = true;
        }
        if (locked) {
            if (ImGui::MenuItem("Punkt lösen")) {
                app.captureUndo("Punkt lösen");
                app.cutPointLocked[index] = false;
                app.dirty = true;
            }
        } else if (ImGui::MenuItem("Punkt einfrieren")) {
            app.captureUndo("Punkt einfrieren");
            if (app.cutPointLocked.size() < app.cutPoints.size())
                app.cutPointLocked.resize(app.cutPoints.size(), false);
            app.cutPointLocked[index] = true;
            app.dirty = true;
        }
        ImGui::Separator();
        if (index < app.cutPoints.size() && ImGui::MenuItem("Koordinaten eingeben ...")) {
            app.coordinateCutPoint = static_cast<int>(index);
            app.coordinateX = app.cutPoints[index].x;
            app.coordinateY = app.cutPoints[index].y;
            app.showCutPointCoordinates = true;
        }
    } else if (app.contextLineSegment >= 0) {
        if (ImGui::MenuItem("Verbinder hier hinzufügen")) {
            app.captureUndo("Verbinder hinzufügen");
            app.addConnector(std::nullopt, closestPositionOnPolyline(app.cutPoints, app.contextWorld));
            app.showConnectorEditor = true;
            app.repositionConnectorEditor = true;
            app.connectorEditorPosition = connectorPopupPosition(app);
            app.dirty = true;
            app.status = "Neuer Verbinder wurde nahe der angeklickten Stelle in einen freien, vorgeprüften Bereich eingefügt.";
        }
        if (ImGui::MenuItem("Knickpunkt hinzufügen")) {
            app.captureUndo("Knickpunkt hinzufügen");
            app.insertCutPoint(static_cast<size_t>(app.contextLineSegment), app.contextWorld);
            app.snapOutsideCutPoints();
            app.updatePreview();
            app.dirty = true;
        }
        if (app.connectorClipboard && ImGui::MenuItem("Kopierten Verbinder hier einfügen")) {
            app.captureUndo("Verbinder einfügen");
            app.addConnector(app.connectorClipboard, closestPositionOnPolyline(app.cutPoints, app.contextWorld));
            app.showConnectorEditor = true;
            app.repositionConnectorEditor = true;
            app.connectorEditorPosition = connectorPopupPosition(app);
            app.dirty = true;
            app.status = "Kopierter Verbinder an dieser Stelle eingefügt.";
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Segment präzise ausrichten")) {
            const size_t segment = static_cast<size_t>(app.contextLineSegment);
            auto align = [&](double angle, const char* label) {
                app.captureUndo(label);
                if (app.alignCutSegment(segment, angle))
                    app.status = std::string(label) + ".";
                else
                    app.status = "Das Segment kann nicht ausgerichtet werden, weil beide Punkte eingefroren sind.";
            };
            if (ImGui::MenuItem("Horizontal (0°)")) align(0.0, "Segment horizontal ausgerichtet");
            if (ImGui::MenuItem("Vertikal (90°)")) align(90.0, "Segment vertikal ausgerichtet");
            if (segment + 1 < app.cutPoints.size()) {
                const Vec2 delta = app.cutPoints[segment + 1] - app.cutPoints[segment];
                const double current = std::atan2(delta.y, delta.x) * 180.0 / std::numbers::pi;
                const double step = std::clamp(app.snapAngleStep, 1.0, 90.0);
                const double snapped = std::round(current / step) * step;
                const std::string label = "Auf " + std::to_string(static_cast<int>(std::round(snapped))) + "° einrasten";
                if (ImGui::MenuItem(label.c_str())) align(snapped, "Segment auf Winkelschritt ausgerichtet");
            }
            ImGui::EndMenu();
        }
    } else if (!app.contextAppliedCutIds.empty()) {
        if (app.contextAppliedCutIds.size() == 1) {
            const int operationId = app.contextAppliedCutIds.front();
            const std::string label = "Schnitt " + std::to_string(operationId) + " erneut bearbeiten";
            ImGui::PushID(operationId);
            const bool colorClicked = splitColorButton("##PartPair", cutPartColorsAt(app, operationId, app.contextWorld));
            ImGui::SameLine();
            const bool itemClicked = ImGui::MenuItem(label.c_str());
            ImGui::PopID();
            if (colorClicked || itemClicked) requestEditCut(app, operationId);
        } else if (ImGui::BeginMenu("Schnitt erneut bearbeiten")) {
            for (int operationId : app.contextAppliedCutIds) {
                const std::string label = "Schnitt " + std::to_string(operationId);
                ImGui::PushID(operationId);
                const bool colorClicked = splitColorButton("##PartPair", cutPartColorsAt(app, operationId, app.contextWorld));
                ImGui::SameLine();
                const bool itemClicked = ImGui::MenuItem(label.c_str());
                ImGui::PopID();
                if (colorClicked || itemClicked) requestEditCut(app, operationId);
            }
            ImGui::EndMenu();
        }
    }
    if (app.contextModelSurface) {
        if (app.contextConnector >= 0 || app.contextCutPoint >= 0 || app.contextLineSegment >= 0 ||
            !app.contextAppliedCutIds.empty()) ImGui::Separator();
        ImGui::TextDisabled("Neuen Schnitt starten");
        if (ImGui::MenuItem("Gerader Schnitt")) requestBeginCut(app, CutMode::Straight);
        if (ImGui::MenuItem("Mehrpunkt-Schnitt")) requestBeginCut(app, CutMode::Polyline);
    }
    ImGui::EndPopup();
    }

    if (app.showCutPointCoordinates) {
        ImGui::OpenPopup("Schnittpunkt-Koordinaten##Precision");
        app.showCutPointCoordinates = false;
    }
    if (ImGui::BeginPopupModal("Schnittpunkt-Koordinaten##Precision", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Schnittpunkt %d", app.coordinateCutPoint + 1);
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputDouble("X [mm]", &app.coordinateX, 0.10, 1.0, "%.3f");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputDouble("Y [mm]", &app.coordinateY, 0.10, 1.0, "%.3f");
        const bool validIndex = app.coordinateCutPoint >= 0 &&
            static_cast<size_t>(app.coordinateCutPoint) < app.cutPoints.size();
        ImGui::BeginDisabled(!validIndex || !std::isfinite(app.coordinateX) || !std::isfinite(app.coordinateY));
        if (ImGui::Button("Übernehmen", {130.0f, 34.0f})) {
            app.captureUndo("Schnittpunkt numerisch setzen");
            std::vector<Vec2> centers;
            centers.reserve(app.preview.size());
            for (const ConnectorPreview& connector : app.preview) centers.push_back(connector.center);
            app.cutPoints[static_cast<size_t>(app.coordinateCutPoint)] = {app.coordinateX, app.coordinateY};
            for (size_t index = 0; index < app.placements.size() && index < centers.size(); ++index)
                app.placements[index].position = closestPositionOnPolyline(app.cutPoints, centers[index]);
            app.invalidateResult();
            app.updatePreview();
            app.dirty = true;
            app.status = "Schnittpunktkoordinaten übernommen.";
            app.coordinateCutPoint = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Abbrechen", {110.0f, 34.0f})) {
            app.coordinateCutPoint = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
