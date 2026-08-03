#include "workspace_panels.h"

#include "generation_workflow.h"
#include "project_workflow.h"
#include "visual_style.h"

#include <imgui.h>
#include "localized_imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace {
ImVec4 materialColor(const App& app, uint32_t slot, float brightness) {
    ImVec4 result{0.72f, 0.72f, 0.72f, 1.0f};
    if (slot == 0 || slot > app.materials.size()) return result;
    const std::string& value = app.materials[slot - 1].color;
    if (value.size() < 7 || value[0] != '#') return result;
    auto byte = [&](size_t offset) -> int {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int high = nibble(value[offset]);
        const int low = nibble(value[offset + 1]);
        return high < 0 || low < 0 ? -1 : high * 16 + low;
    };
    const int red = byte(1), green = byte(3), blue = byte(5);
    if (red < 0 || green < 0 || blue < 0) return result;
    result.x = std::min(red / 255.0f * brightness, 1.0f);
    result.y = std::min(green / 255.0f * brightness, 1.0f);
    result.z = std::min(blue / 255.0f * brightness, 1.0f);
    return result;
}
}

void drawObjectSelector(App& app, double aspect) {
    if (!app.loaded) return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x - 430.0f, ImGui::GetFrameHeight() + 8.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({420.0f, 0.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("Objektauswahl", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
    int active = -1;
    int requested = -1;
    if (app.loaded && !app.objectSessions.empty()) {
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Objekt");
    ImGui::SameLine();
    active = std::clamp(app.activeObjectIndex, 0, static_cast<int>(app.objectSessions.size()) - 1);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##ObjectSelector", app.objectSessions[static_cast<size_t>(active)].name.c_str())) {
        for (size_t index = 0; index < app.objectSessions.size(); ++index) {
            const bool selected = static_cast<int>(index) == active;
            if (ImGui::SelectableRaw(app.objectSessions[index].name.c_str(), selected))
                requested = static_cast<int>(index);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    bool showPrintBed = app.showPrintBed;
    if (ImGui::Checkbox("##ShowPrintBedTop", &showPrintBed)) {
        app.captureUndo("Druckbettrahmen ein-/ausblenden");
        app.showPrintBed = showPrintBed;
        app.dirty = true;
        app.status = app.showPrintBed ? "Referenz-Druckbett eingeblendet."
                                      : "Referenz-Druckbett ausgeblendet.";
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blendet den verschiebbaren Referenzrahmen des Druckbetts ein oder aus.");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Druckbett");
    ImGui::SameLine();
    const char* bedPresets[] = {
        "P1S – 256 × 256 mm", "P1P – 256 × 256 mm", "P2S – 256 × 256 mm",
        "A1 – 256 × 256 mm", "A1 mini – 180 × 180 mm", "A2L – 330 × 320 mm",
        "X1C – 256 × 256 mm", "X1E – 256 × 256 mm", "X2D – 256 × 256 mm",
        "H2S – 340 × 320 mm", "H2D Einzel – 325 × 320 mm", "H2D Dual – 300 × 320 mm",
        "H2C Einzel – 305 × 320 mm", "H2C Dual – 300 × 320 mm", "Eigene ..."};
    ImGui::SetNextItemWidth(-42.0f);
    if (ImGui::Combo("##PrintBedPreset", &app.printBedPreset, bedPresets, IM_ARRAYSIZE(bedPresets))) {
        if (app.printBedPreset == 14) {
            app.customBedWidth = app.printBedWidth;
            app.customBedDepth = app.printBedDepth;
            ImGui::OpenPopup("Eigene Druckbettmaße");
        } else {
            app.captureUndo("Druckbettvorlage ändern");
            const double widths[] = {256.0, 256.0, 256.0, 256.0, 180.0, 330.0, 256.0,
                                     256.0, 256.0, 340.0, 325.0, 300.0, 305.0, 300.0};
            const double depths[] = {256.0, 256.0, 256.0, 256.0, 180.0, 320.0, 256.0,
                                     256.0, 256.0, 320.0, 320.0, 320.0, 320.0, 320.0};
            app.printBedWidth = widths[app.printBedPreset];
            app.printBedDepth = depths[app.printBedPreset];
            app.showPrintBed = true;
            app.dirty = true;
            app.status = std::string("Druckbettvorlage gewählt: ") + bedPresets[app.printBedPreset];
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("...", {34, 0})) ImGui::OpenPopup("DruckbettOptionen");
    if (ImGui::BeginPopup("DruckbettOptionen")) {
        if (ImGui::Checkbox("Rahmen anzeigen", &app.showPrintBed)) app.dirty = true;
        if (ImGui::DragScalar("Drehung", ImGuiDataType_Double, &app.printBedRotation, 1.0, nullptr, nullptr, "%.1f°"))
            app.dirty = true;
        if (ImGui::MenuItem("Am Modell zentrieren")) {
            app.captureUndo("Druckbett zentrieren");
            app.printBedCenter = {app.source->bounds.center().x, app.source->bounds.center().y};
            app.dirty = true;
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Eigene Druckbettmaße", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Eigene nutzbare Druckbettfläche");
        ImGui::InputDouble("Breite [mm]", &app.customBedWidth, 1.0, 10.0, "%.1f");
        ImGui::InputDouble("Tiefe [mm]", &app.customBedDepth, 1.0, 10.0, "%.1f");
        if (ImGui::Button("Übernehmen", {130, 0})) {
            app.captureUndo("Eigene Druckbettmaße");
            app.printBedWidth = std::max(1.0, app.customBedWidth);
            app.printBedDepth = std::max(1.0, app.customBedDepth);
            app.showPrintBed = true;
            app.dirty = true;
            app.status = "Eigene Druckbettmaße übernommen.";
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Abbrechen", {130, 0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    }
    ImGui::End();
    if (!app.repairRunning && requested >= 0 && requested != active) {
        if (!app.cutPoints.empty()) {
            app.pendingObjectIndex = requested;
            app.pendingObjectAspect = aspect;
            app.showObjectSwitchPrompt = true;
        } else {
            app.activateObjectSession(static_cast<size_t>(requested), aspect);
        }
    }
}

void drawObjectSwitchPrompt(App& app) {
    if (app.showObjectSwitchPrompt) {
        ImGui::OpenPopup("Objekt wechseln?##PendingCut");
        app.showObjectSwitchPrompt = false;
    }
    if (!ImGui::BeginPopupModal("Objekt wechseln?##PendingCut", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;
    const bool complete = app.hasCompleteCut() && !app.placements.empty();
    ImGui::TextUnformatted("Am aktuellen Objekt gibt es einen noch nicht angewendeten Schnitt.");
    ImGui::TextWrapped("Soll dieser Schnitt vor dem Wechsel ausgeführt oder verworfen werden?");
    ImGui::Spacing();
    ImGui::BeginDisabled(!complete || app.repairRunning);
    if (ImGui::Button("Schnitt anwenden und wechseln", {250, 36})) {
        requestGenerate(app, GenerateContinuation::SwitchObject);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    if (!complete) ImGui::TextDisabled("Der Schnitt ist noch nicht vollständig und kann nur verworfen werden.");
    if (ImGui::Button("Schnitt verwerfen und wechseln", {250, 34})) {
        app.clearCurrentCut();
        if (app.pendingObjectIndex >= 0) {
            const int target = app.pendingObjectIndex;
            app.pendingObjectIndex = -1;
            app.activateObjectSession(static_cast<size_t>(target), app.pendingObjectAspect);
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Abbrechen", {120, 34})) {
        app.pendingObjectIndex = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawCutTreeOverlay(App& app) {
    if (!app.loaded) return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({12.0f, std::max(90.0f, io.DisplaySize.y - 315.0f)}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({315.0f, 300.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({260.0f, 180.0f},
        {std::max(260.0f, io.DisplaySize.x - 24.0f),
         std::max(180.0f, io.DisplaySize.y - ImGui::GetFrameHeight() - 24.0f)});
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("Modellstruktur", nullptr,
                 ImGuiWindowFlags_NoCollapse);
    ImGui::TextColored({0.35f, 0.78f, 1.0f, 1.0f}, "MODELL & SCHNITTE");
    ImGui::Separator();
    int pendingCutDeletion = -1;
    int pendingCutEdit = -1;
    if (ImGui::TreeNodeEx("Schnitte", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        if (app.cuts.empty()) ImGui::TextDisabled("Noch kein Schnitt");
        for (size_t cutIndex = 0; cutIndex < app.cuts.size();) {
            const int operationId = app.cuts[cutIndex].id;
            size_t operationEnd = cutIndex;
            while (operationEnd < app.cuts.size() && app.cuts[operationEnd].id == operationId) ++operationEnd;
            const std::string label = "Schnitt " + std::to_string(operationId) + "##overlaycut" + std::to_string(operationId);
            ImGui::PushID(operationId);
            ImGui::ColorButton("##CutColor", cutColorImGui(operationId),
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, {13.0f, 13.0f});
            ImGui::SameLine();
            const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Schnitt erneut bearbeiten")) pendingCutEdit = operationId;
                ImGui::Separator();
                if (ImGui::MenuItem("Schnitt löschen")) pendingCutDeletion = operationId;
                ImGui::EndPopup();
            }
            if (open) {
                for (size_t branch = cutIndex; branch < operationEnd; ++branch) {
                    const CutRecord& cut = app.cuts[branch];
                    const std::string sourceLabel = localizedText(cut.sourceLabel);
                    ImGui::TextDisabled("%s", sourceLabel.c_str());
                    for (const std::string& child : cut.childLabels) ImGui::BulletText("%s", child.c_str());
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
            cutIndex = operationEnd;
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Aktuelle Teile", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        size_t colorIndex = 0;
        for (const auto& part : app.parts) {
            if (!part.active) continue;
            const bool selected = part.id == app.selectedPartId;
            const float boost = selected ? 1.12f : 0.88f;
            const auto& base = kPartColors[colorIndex % kPartColors.size()];
            const ImVec4 color = app.materials.size() > 1 && part.mesh
                ? materialColor(app, std::max(part.mesh->defaultMaterial, 1u), boost)
                : ImVec4{std::min(base[0] * boost, 1.0f),
                         std::min(base[1] * boost, 1.0f),
                         std::min(base[2] * boost, 1.0f), 1.0f};
            ImGui::PushID(part.id);
            const bool colorClicked = ImGui::ColorButton("##PartColor", color,
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                 {14.0f, 14.0f});
            if (ImGui::IsItemHovered() && app.materials.size() > 1 && part.mesh) {
                const uint32_t slot = std::max(part.mesh->defaultMaterial, 1u);
                const std::string name = slot <= app.materials.size()
                    ? app.materials[slot - 1].name : "Unbekannt";
                ImGui::SetTooltip("Filament %u: %s", slot, name.c_str());
            }
            ImGui::SameLine();
            const bool labelClicked = ImGui::Selectable(part.label.c_str(), selected);
            ImGui::PopID();
            if (colorClicked || labelClicked) app.selectPart(part.id);
            ++colorIndex;
        }
        ImGui::TreePop();
    }
    if (!app.cuts.empty()) {
        ImGui::Separator();
        ImGui::Checkbox("Original anzeigen", &app.showSource);
        const double explodeMin = 0.0;
        const double explodeMax = 100.0;
        ImGui::TextUnformatted("Explosionsabstand");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderScalar("##Explosionsabstand", ImGuiDataType_Double, &app.explode,
                            &explodeMin, &explodeMax, "%.1f mm");
    }
    ImGui::End();
    if (pendingCutEdit >= 0) requestEditCut(app, pendingCutEdit);
    else if (pendingCutDeletion >= 0) app.deleteCutOperation(pendingCutDeletion);
}

void drawApplyCutsButton(App& app) {
    const bool rebuildAppliedCut = app.pendingAppliedCutRebuildId >= 0;
    if (!app.loaded || (!app.hasCompleteCut() && !rebuildAppliedCut)) return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f - 125.0f, io.DisplaySize.y - 64.0f},
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize({250.0f, 54.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("Schnitt anwenden##Bottom", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BeginDisabled((!rebuildAppliedCut && app.placements.empty()) ||
                         app.repairRunning || app.backgroundRunning);
    const char* label = rebuildAppliedCut ? "Neu schneiden" : "Schnitte anwenden";
    if (ImGui::Button(label, {-1, 34})) {
        if (rebuildAppliedCut)
            rebuildMovedAppliedConnector(app, app.pendingAppliedCutRebuildId);
        else
            requestGenerate(app);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(rebuildAppliedCut
            ? "Berechnet den vorhandenen Schnitt einmalig mit allen geänderten Verbinderpositionen neu."
            : "Berechnet den vorbereiteten Schnitt und alle Verbinder im Hintergrund.");
    ImGui::EndDisabled();
    ImGui::End();
}

void drawCutModeHint(const App& app) {
    if (!app.loaded || !app.drawingLine) return;
    const char* instruction = nullptr;
    if (app.cutPoints.empty()) {
        instruction = "Ersten Schnittpunkt platzieren";
    } else if (app.cutPoints.size() == 1) {
        instruction = "Zweiten Schnittpunkt platzieren";
    } else {
        instruction = "Weiteren Knickpunkt platzieren · Rechtsklick beendet";
    }
    const char* mode = app.cutMode == CutMode::Straight ? "GERADER SCHNITT" : "MEHRPUNKT-SCHNITT";
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, ImGui::GetFrameHeight() + 14.0f},
                            ImGuiCond_Always, {0.5f, 0.0f});
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18.0f, 10.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.055f, 0.075f, 0.10f, 0.96f});
    ImGui::PushStyleColor(ImGuiCol_Border, {0.18f, 0.68f, 1.0f, 1.0f});
    ImGui::Begin("Schnittmodus-Hinweis", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored({0.30f, 0.78f, 1.0f, 1.0f}, "%s", localizedCString(mode));
    ImGui::SameLine();
    ImGui::TextUnformatted("·");
    ImGui::SameLine();
    ImGui::TextUnformatted(instruction);
    if (app.snapToAngles || app.snapToGrid || app.snapToModelEdge) {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        const std::string gridSnap = app.snapToGrid ? localizedText("Raster ") : "";
        const std::string angleSnap = app.snapToAngles ? localizedText("Winkel ") : "";
        const std::string edgeSnap = app.snapToModelEdge ? localizedText("Modellkante") : "";
        ImGui::TextDisabled("Fang: %s%s%s",
            gridSnap.c_str(), angleSnap.c_str(), edgeSnap.c_str());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("| Strg = Fang aus");
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void drawPrecisionSettingsWindow(App& app) {
    if (!app.showPrecisionSettings) return;
    ImGui::SetNextWindowSize({390.0f, 0.0f}, ImGuiCond_Appearing);
    if (!ImGui::Begin("Raster", &app.showPrecisionSettings,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::Checkbox("Am Raster einrasten", &app.snapToGrid);
    ImGui::BeginDisabled(!app.snapToGrid);
    ImGui::SetNextItemWidth(125.0f);
    ImGui::InputDouble("Rasterweite [mm]", &app.snapGridStep, 0.10, 1.0, "%.2f");
    ImGui::EndDisabled();
    ImGui::Checkbox("Segmentwinkel einrasten", &app.snapToAngles);
    ImGui::BeginDisabled(!app.snapToAngles);
    ImGui::SetNextItemWidth(125.0f);
    ImGui::InputDouble("Winkelschritt [°]", &app.snapAngleStep, 1.0, 5.0, "%.1f");
    ImGui::EndDisabled();
    ImGui::Checkbox("An Modellkanten einrasten", &app.snapToModelEdge);
    app.snapGridStep = std::clamp(app.snapGridStep, 0.01, 1000.0);
    app.snapAngleStep = std::clamp(app.snapAngleStep, 1.0, 90.0);
    ImGui::Separator();
    ImGui::TextWrapped("Punkte können zusätzlich per Rechtsklick numerisch bearbeitet werden. "
                       "Auf einem Segment stehen Horizontal, Vertikal und der nächste Winkelschritt zur Verfügung. "
                       "Beim Platzieren oder Ziehen setzt Strg den Fang vorübergehend aus.");
    ImGui::End();
}

void drawAssemblyMarkEditor(App& app) {
    if (!app.showAssemblyMarkEditor) return;
    if (!app.hasCompleteCut()) {
        app.showAssemblyMarkEditor = false;
        return;
    }
    ImGui::SetNextWindowSize({390.0f, 0.0f}, ImGuiCond_Appearing);
    if (!ImGui::Begin("Schnittnummern", &app.showAssemblyMarkEditor,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::TextColored({0.30f, 0.78f, 1.0f, 1.0f}, "Kennzeichnung: %d",
                       app.activeAssemblyMarkCode());
    ImGui::TextWrapped("Beide zusammengehörigen Teile erhalten dieselbe eingravierte Schnittnummer.");
    const bool wasEnabled = app.settings.assemblyMarks;
    bool enabled = wasEnabled;
    double positionPercent = app.settings.assemblyMarkPosition * 100.0;
    double size = app.settings.assemblyMarkSize;
    double depth = app.settings.assemblyMarkDepth;
    const AssemblyMarkPreview currentPreview = makeAssemblyMarkPreview(
        app.cutPoints, app.settings, app.activeAssemblyMarkCode());
    double leftX = currentPreview.leftCenter.x;
    double leftY = currentPreview.leftCenter.y;
    double rightX = currentPreview.rightCenter.x;
    double rightY = currentPreview.rightCenter.y;
    bool changed = ImGui::Checkbox("Schnittnummer eingravieren", &enabled);
    ImGui::BeginDisabled(!enabled);
    constexpr double minimumPosition = 2.0;
    constexpr double maximumPosition = 98.0;
    ImGui::SetNextItemWidth(190.0f);
    const bool automaticPositionChanged = ImGui::SliderScalar(
        "Automatische Position [%]", ImGuiDataType_Double,
        &positionPercent, &minimumPosition, &maximumPosition,
        "%.0f %%", ImGuiSliderFlags_AlwaysClamp);
    changed |= automaticPositionChanged;
    ImGui::SetNextItemWidth(145.0f);
    changed |= ImGui::InputDouble("Ziffernhöhe [mm]", &size, 0.5, 2.0, "%.2f");
    ImGui::SetNextItemWidth(145.0f);
    changed |= ImGui::InputDouble("Gravurtiefe [mm]", &depth, 0.05, 0.25, "%.2f");
    ImGui::SeparatorText("Position Zahl auf Teil A");
    ImGui::SetNextItemWidth(145.0f);
    const bool leftXChanged = ImGui::InputDouble("X##MarkLeft", &leftX, 0.5, 2.0, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(145.0f);
    const bool leftYChanged = ImGui::InputDouble("Y##MarkLeft", &leftY, 0.5, 2.0, "%.2f");
    ImGui::SeparatorText("Position Zahl auf Teil B");
    ImGui::SetNextItemWidth(145.0f);
    const bool rightXChanged = ImGui::InputDouble("X##MarkRight", &rightX, 0.5, 2.0, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(145.0f);
    const bool rightYChanged = ImGui::InputDouble("Y##MarkRight", &rightY, 0.5, 2.0, "%.2f");
    const bool coordinatesChanged = leftXChanged || leftYChanged || rightXChanged || rightYChanged;
    changed |= coordinatesChanged;
    const bool resetAutomatic = ImGui::Button("Beide automatisch an der Schnittlinie platzieren", {-1.0f, 30.0f});
    changed |= resetAutomatic;
    ImGui::EndDisabled();
    if (changed) {
        app.captureUndo("Schnittnummer bearbeiten");
        app.settings.assemblyMarks = enabled;
        if (enabled && !wasEnabled) {
            app.settings.assemblyMarkCode = app.activeAssemblyMarkCode();
            app.settings.assemblyMarkPositionsCustom = false;
        }
        app.settings.assemblyMarkPosition = std::clamp(positionPercent / 100.0, 0.02, 0.98);
        app.settings.assemblyMarkSize = std::clamp(size, 2.0, 30.0);
        app.settings.assemblyMarkDepth = std::clamp(depth, 0.05, 5.0);
        if (automaticPositionChanged || resetAutomatic) {
            app.settings.assemblyMarkPositionsCustom = false;
        } else if (coordinatesChanged) {
            app.settings.assemblyMarkPositionsCustom = true;
            app.settings.assemblyMarkLeftX = leftX;
            app.settings.assemblyMarkLeftY = leftY;
            app.settings.assemblyMarkRightX = rightX;
            app.settings.assemblyMarkRightY = rightY;
        }
        app.invalidateResult();
        app.dirty = true;
        app.status = enabled ? "Schnittnummer aktualisiert. Die Zahl kann direkt im Modell verschoben werden."
                             : "Schnittnummer deaktiviert.";
    }
    ImGui::Separator();
    ImGui::TextDisabled("Beide Zahlen können im Modell unabhängig und frei in X/Y verschoben werden.");
    ImGui::TextDisabled("Bei dünnen Bauteilen wird die Gravurtiefe automatisch begrenzt.");
    ImGui::End();
}

double niceScaleLength(double raw) {
    if (raw <= 0.0) return 1.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double normalized = raw / magnitude;
    const double nice = normalized < 2.0 ? 1.0 : (normalized < 5.0 ? 2.0 : 5.0);
    return nice * magnitude;
}

void drawViewToolbar(App& app, double aspect) {
    if (!app.loaded) return;
    constexpr double kZero = 0.0;
    constexpr double kMaxTransparency = 85.0;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x - 505.0f, io.DisplaySize.y - 66.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({495.0f, 56.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);
    ImGui::Begin("Ansichtswerkzeuge", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoTitleBar);
    if (ImGui::Button("Oben", {62, 30})) app.camera.top = true;
    ImGui::SameLine();
    if (ImGui::Button("3D", {52, 30})) app.camera.top = false;
    ImGui::SameLine();
    if (ImGui::Button("Einpassen", {78, 30})) app.fitCamera(aspect);
    ImGui::SameLine();
    if (ImGui::Button(app.wireframe ? "Flächen" : "Draht", {62, 30})) app.wireframe = !app.wireframe;
    ImGui::SameLine();
    if (ImGui::Button(app.showPrintBed ? "Bett aus" : "Bett", {65, 30})) {
        app.showPrintBed = !app.showPrintBed;
        app.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(app.measureMode ? "Messen ✓" : "Messen", {-1, 30})) {
        app.measureMode = !app.measureMode;
        app.camera.top = true;
        if (!app.measureMode) app.measurePoints.clear();
        app.status = app.measureMode ? "Messen: zwei Punkte im Raster anklicken." : "Messen beendet.";
    }
    ImGui::End();

    ImGui::SetNextWindowPos({io.DisplaySize.x - 315.0f, io.DisplaySize.y - 116.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({305.0f, 44.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);
    ImGui::Begin("Modelltransparenz", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoSavedSettings);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Transparenz");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderScalar("##Modelltransparenz", ImGuiDataType_Double,
                        &app.modelTransparency, &kZero, &kMaxTransparency,
                        "%.0f %%", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("0 %% = vollständig deckend; höhere Werte machen das Modell durchsichtig.");
    ImGui::End();

    if (app.camera.top) {
        const double unitsPerPixel = app.camera.topHeight / std::max(io.DisplaySize.y, 1.0f);
        const double scaleLength = niceScaleLength(unitsPerPixel * 130.0);
        const float pixelLength = static_cast<float>(scaleLength / unitsPerPixel);
        const float viewportCenter = 0.5f * io.DisplaySize.x;
        const ImVec2 start{viewportCenter - pixelLength * 0.5f, io.DisplaySize.y - 82.0f};
        const ImVec2 end{start.x + pixelLength, start.y};
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        const ImU32 color = IM_COL32(225, 235, 245, 230);
        draw->AddLine(start, end, color, 2.0f);
        draw->AddLine({start.x, start.y - 5.0f}, {start.x, start.y + 5.0f}, color, 2.0f);
        draw->AddLine({end.x, end.y - 5.0f}, {end.x, end.y + 5.0f}, color, 2.0f);
        char label[64];
        std::snprintf(label, sizeof(label), "%.3g mm", scaleLength);
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        draw->AddText({0.5f * (start.x + end.x - textSize.x), start.y - 22.0f}, color, label);
        if (app.measurePoints.size() == 2) {
            const double distance = length(app.measurePoints[1] - app.measurePoints[0]);
            char distanceText[96];
            const std::string measurementFormat = localizedText("Messung: %.3f mm");
            std::snprintf(distanceText, sizeof(distanceText), measurementFormat.c_str(), distance);
            draw->AddText({io.DisplaySize.x - 500.0f, io.DisplaySize.y - 112.0f}, IM_COL32(90, 255, 145, 255), distanceText);
        }
    }
}

bool drawExitConfirmation(App& app) {
    if (app.showExitConfirmation) {
        ImGui::OpenPopup("Programm beenden?");
        app.showExitConfirmation = false;
    }
    bool shouldExit = false;
    ImGui::SetNextWindowSizeConstraints({560.0f, 0.0f},
                                        {560.0f, std::numeric_limits<float>::max()});
    if (ImGui::BeginPopupModal("Programm beenden?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Es gibt Änderungen, die noch nicht als Projekt gespeichert wurden. Möchtest du die Bearbeitung vor dem Beenden speichern?");
        ImGui::Spacing();
        const float buttonWidth =
            (ImGui::GetContentRegionAvail().x - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
        if (ImGui::Button("Speichern und beenden", {buttonWidth, 34})) {
            if (saveProject(app, false, SaveContinuation::ExitApplication))
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Ohne Speichern", {buttonWidth, 34})) shouldExit = true;
        ImGui::SameLine();
        if (ImGui::Button("Abbrechen", {buttonWidth, 34})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return shouldExit;
}
