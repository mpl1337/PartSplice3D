#include "connector_editor.h"

#include <imgui.h>
#include "localized_imgui.h"

#include <algorithm>
#include <string>

ImVec2 connectorPopupPosition(const App&) {
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = ImGui::GetMousePos();
    constexpr float editorWidth = 340.0f;
    constexpr float editorHeight = 510.0f;
    const float x = mouse.x < io.DisplaySize.x * 0.5f
        ? std::max(8.0f, io.DisplaySize.x - editorWidth - 12.0f)
        : 12.0f;
    const float y = mouse.y < io.DisplaySize.y * 0.5f
        ? std::max(ImGui::GetFrameHeight() + 8.0f, io.DisplaySize.y - editorHeight - 12.0f)
        : ImGui::GetFrameHeight() + 12.0f;
    return {x, y};
}

void drawConnectorEditor(App& app) {
    if (!app.showConnectorEditor) return;
    if (app.selectedConnector < 0 ||
        static_cast<size_t>(app.selectedConnector) >= app.placements.size()) {
        app.showConnectorEditor = false;
        return;
    }
    const bool positionedThisFrame = app.repositionConnectorEditor;
    if (app.repositionConnectorEditor) {
        const ImGuiIO& io = ImGui::GetIO();
        const float x = std::clamp(app.connectorEditorPosition.x, 8.0f, std::max(8.0f, io.DisplaySize.x - 340.0f));
        const float y = std::clamp(app.connectorEditorPosition.y, ImGui::GetFrameHeight() + 8.0f,
                                   std::max(ImGui::GetFrameHeight() + 8.0f, io.DisplaySize.y - 510.0f));
        ImGui::SetNextWindowPos({x, y}, ImGuiCond_Always);
        app.repositionConnectorEditor = false;
    }
    ImGui::SetNextWindowSize({330.0f, 0.0f}, ImGuiCond_Appearing);
    if (!ImGui::Begin("Verbinder-Eigenschaften", &app.showConnectorEditor,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    const bool editorHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    ConnectorPlacement& connector = app.placements[static_cast<size_t>(app.selectedConnector)];
    ImGui::TextColored({1.0f, 0.82f, 0.25f, 1.0f}, "Verbinder %d", app.selectedConnector + 1);
    const char* connectorTypes[] = {"Schwalbenschwanz", "Rechteck-Zapfen", "Puzzle-Kopf", "Rundzapfen"};
    const char* sides[] = {"Teil A / lokal links", "Teil B / lokal rechts"};
    int type = static_cast<int>(connector.type);
    double neck = connector.neckWidth > 0.0 ? connector.neckWidth : app.settings.neckWidth;
    double head = connector.headWidth > 0.0 ? connector.headWidth : app.settings.headWidth;
    double depth = connector.depth > 0.0 ? connector.depth : app.settings.depth;
    double embed = connector.embed > 0.0 ? connector.embed : app.settings.embed;
    double clearance = connector.clearance >= 0.0 ? connector.clearance : app.settings.clearance;
    bool leadChamfer = connector.leadChamfer >= 0
        ? connector.leadChamfer != 0 : app.settings.leadChamfer;
    double chamferWidth = connector.chamferWidth >= 0.0
        ? connector.chamferWidth : app.settings.chamferWidth;
    double chamferAngle = connector.chamferAngle >= 0.0
        ? connector.chamferAngle : app.settings.chamferAngle;
    int side = connector.maleOnLeft ? 0 : 1;
    bool locked = connector.locked;
    bool changed = false;
    changed |= ImGui::Combo("Bauart", &type, connectorTypes, IM_ARRAYSIZE(connectorTypes));
    ImGui::SetNextItemWidth(145.0f);
    changed |= ImGui::InputDouble("Breite [mm]", &neck, 0.10, 1.0, "%.2f");
    if (static_cast<ConnectorType>(type) == ConnectorType::Dovetail ||
        static_cast<ConnectorType>(type) == ConnectorType::Puzzle) {
        ImGui::SetNextItemWidth(145.0f);
        changed |= ImGui::InputDouble("Kopfbreite [mm]", &head, 0.10, 1.0, "%.2f");
    }
    ImGui::SetNextItemWidth(145.0f);
    changed |= ImGui::InputDouble("Tiefe [mm]", &depth, 0.10, 1.0, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ausladung des Verbinders senkrecht zur Schnittlinie.");
    ImGui::SetNextItemWidth(145.0f);
    changed |= ImGui::InputDouble("Einbindung [mm]", &embed, 0.05, 0.5, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zusätzliche Überdeckung am Hals für robuste Boolesche Geometrie.");
    ImGui::SetNextItemWidth(145.0f);
    changed |= ImGui::InputDouble("Spiel je Seite [mm]", &clearance, 0.01, 0.1, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Seitlicher Abstand zwischen Zapfen und Nut. Kleiner sitzt strammer.");
    changed |= ImGui::Checkbox("Einführfase", &leadChamfer);
    if (leadChamfer) {
        ImGui::SetNextItemWidth(145.0f);
        changed |= ImGui::InputDouble("Fasenbreite [mm]", &chamferWidth, 0.05, 0.25, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Breite der Einführfase an Zapfennase und Nuteinlauf.");
        ImGui::SetNextItemWidth(145.0f);
        changed |= ImGui::InputDouble("Fasenwinkel [°]", &chamferAngle, 1.0, 5.0, "%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Flankenwinkel der Einführfase; die Passung dahinter bleibt unverändert.");
        ImGui::TextDisabled("Erleichtert nur den Einlauf; die Passung bleibt erhalten.");
    }
    changed |= ImGui::Combo("Zapfen auf", &side, sides, IM_ARRAYSIZE(sides));
    changed |= ImGui::Checkbox("Eingefroren", &locked);
    if (changed) {
        app.captureUndo("Verbinder bearbeiten");
        connector.type = static_cast<ConnectorType>(type);
        connector.neckWidth = std::max(0.5, neck);
        connector.headWidth = std::max(connector.neckWidth + 0.2, head);
        connector.depth = std::max(0.5, depth);
        connector.embed = std::max(0.2, embed);
        connector.clearance = std::max(0.0, clearance);
        connector.leadChamfer = leadChamfer ? 1 : 0;
        connector.chamferWidth = std::clamp(
            chamferWidth, 0.05, std::max(0.05, std::min(connector.neckWidth, connector.headWidth) * 0.45));
        connector.chamferAngle = std::clamp(chamferAngle, 15.0, 75.0);
        connector.maleOnLeft = side == 0;
        connector.locked = locked;
        app.invalidateResult();
        app.updatePreview();
        app.dirty = true;
        app.status = "Verbinderparameter aktualisiert.";
    }

    if (ImGui::Button("Drehen", {92, 30})) {
        app.captureUndo("Verbinderrichtung ändern");
        connector.maleOnLeft = !connector.maleOnLeft;
        app.invalidateResult();
        app.updatePreview();
        app.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Kopieren", {92, 30})) {
        app.connectorClipboard = connector;
        app.status = "Verbinder kopiert. Rechtsklick auf die Linie fügt die Kopie dort ein.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Löschen", {-1, 30})) {
        app.captureUndo("Verbinder löschen");
        app.placements.erase(app.placements.begin() + app.selectedConnector);
        app.settings.count = std::max(1, static_cast<int>(app.placements.size()));
        app.selectedConnector = -1;
        app.showConnectorEditor = false;
        app.invalidateResult();
        app.updatePreview();
        app.dirty = true;
        app.status = "Verbinder gelöscht.";
    }
    ImGui::End();
    const bool anotherPopupOpen = ImGui::IsPopupOpen(nullptr,
        ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!positionedThisFrame && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !editorHovered && !anotherPopupOpen) {
        app.showConnectorEditor = false;
        app.selectedConnector = -1;
    }
}
