#include "calibration_panel.h"

#include "app_persistence.h"
#include "file_dialogs.h"
#include "printer_presets.h"
#include "stl_io.h"
#include "three_mf.h"

#include <imgui.h>
#include "localized_imgui.h"

#include <algorithm>
#include <string>
#include <vector>

std::vector<ThreeMfObject> makeCalibrationObjects(const App& app, std::string& error) {
    std::vector<ConnectorCalibrationSample> samples = createConnectorCalibrationSamples(
        app.calibrationSettings, app.calibrationStart, app.calibrationStep,
        app.calibrationCount, error);
    if (samples.empty()) return {};
    const BambuPrinterPreset& printer =
        kBambuPrinterPresets[static_cast<size_t>(std::clamp(
            app.calibrationPrinterPreset, 0, static_cast<int>(kBambuPrinterPresets.size()) - 1))];
    if (!arrangeConnectorCalibrationSamples(
            samples, printer.bedWidth, printer.bedDepth, 8.0, 6.0, error))
        return {};
    std::vector<ThreeMfObject> objects;
    objects.reserve(samples.size() * 2);
    for (const ConnectorCalibrationSample& sample : samples) {
        std::ostringstream value;
        value << std::fixed << std::setprecision(3) << sample.clearance;
        objects.push_back({"Spiel " + value.str() + " mm - Zapfen", sample.male});
        objects.push_back({"Spiel " + value.str() + " mm - Nut", sample.socket});
    }
    return objects;
}

TriangleMesh mergeCalibrationObjects(const std::vector<ThreeMfObject>& objects) {
    TriangleMesh merged;
    size_t vertexCount = 0;
    size_t triangleCount = 0;
    for (const ThreeMfObject& object : objects) {
        const TriangleMesh& mesh = object.meshData();
        vertexCount += mesh.vertices.size();
        triangleCount += mesh.triangles.size();
    }
    merged.vertices.reserve(vertexCount);
    merged.triangles.reserve(triangleCount);
    for (const ThreeMfObject& object : objects) {
        const TriangleMesh& mesh = object.meshData();
        const uint32_t offset = static_cast<uint32_t>(merged.vertices.size());
        for (const Vec3& vertex : mesh.vertices) {
            merged.vertices.push_back(vertex);
            merged.bounds.expand(vertex);
        }
        for (const auto& triangle : mesh.triangles)
            merged.triangles.push_back({triangle[0] + offset, triangle[1] + offset, triangle[2] + offset});
    }
    return merged;
}

void drawConnectorCalibrationWindow(App& app) {
    if (!app.showConnectorCalibration) return;
    ImGui::SetNextWindowSize({660.0f, 0.0f}, ImGuiCond_Appearing);
    if (!ImGui::Begin("Verbinder-Testmuster", &app.showConnectorCalibration,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Erzeugt mehrere zusammengehörige Zapfen-/Nut-Proben mit unterschiedlichen "
                       "Spielwerten als STL- oder 3MF-Datei. Die Anzahl kleiner Markierungslöcher "
                       "entspricht der Probennummer.");
    ImGui::SeparatorText("Geometrie");
    const char* connectorTypes[] = {"Schwalbenschwanz", "Rechteck-Zapfen", "Puzzle-Kopf", "Rundzapfen"};
    int type = static_cast<int>(app.calibrationSettings.type);
    if (ImGui::Combo("Bauart", &type, connectorTypes, IM_ARRAYSIZE(connectorTypes)))
        app.calibrationSettings.type = static_cast<ConnectorType>(type);
    ImGui::InputDouble("Breite [mm]", &app.calibrationSettings.neckWidth, 0.10, 1.0, "%.2f");
    if (app.calibrationSettings.type == ConnectorType::Dovetail ||
        app.calibrationSettings.type == ConnectorType::Puzzle)
        ImGui::InputDouble("Kopfbreite [mm]", &app.calibrationSettings.headWidth, 0.10, 1.0, "%.2f");
    ImGui::InputDouble("Tiefe [mm]", &app.calibrationSettings.depth, 0.10, 1.0, "%.2f");
    ImGui::InputDouble("Einbindung [mm]", &app.calibrationSettings.embed, 0.05, 0.5, "%.2f");
    ImGui::Checkbox("Einführfase", &app.calibrationSettings.leadChamfer);
    if (app.calibrationSettings.leadChamfer) {
        ImGui::InputDouble("Fasenbreite [mm]", &app.calibrationSettings.chamferWidth,
                           0.05, 0.25, "%.2f");
        ImGui::InputDouble("Fasenwinkel [°]", &app.calibrationSettings.chamferAngle,
                           1.0, 5.0, "%.1f");
        ImGui::TextDisabled("Zapfen-Nase und Nut-Einlauf werden angefast; die Passung dahinter bleibt nominal.");
    }
    app.calibrationSettings.neckWidth = std::max(0.5, app.calibrationSettings.neckWidth);
    app.calibrationSettings.headWidth = std::max(app.calibrationSettings.neckWidth + 0.2,
                                                 app.calibrationSettings.headWidth);
    app.calibrationSettings.depth = std::max(0.5, app.calibrationSettings.depth);
    app.calibrationSettings.embed = std::max(0.2, app.calibrationSettings.embed);
    app.calibrationSettings.chamferWidth = std::clamp(
        app.calibrationSettings.chamferWidth, 0.05,
        std::max(0.05, std::min(app.calibrationSettings.neckWidth,
                                app.calibrationSettings.headWidth) * 0.45));
    app.calibrationSettings.chamferAngle = std::clamp(
        app.calibrationSettings.chamferAngle, 15.0, 75.0);

    ImGui::SeparatorText("Spielreihe");
    ImGui::InputDouble("Kleinster Wert [mm]", &app.calibrationStart, 0.01, 0.05, "%.3f");
    ImGui::InputDouble("Schrittweite [mm]", &app.calibrationStep, 0.005, 0.025, "%.3f");
    const int previousCount = app.calibrationCount;
    ImGui::InputInt("Anzahl Proben", &app.calibrationCount, 1, 1);
    app.calibrationStart = std::max(0.0, app.calibrationStart);
    app.calibrationStep = std::max(0.01, app.calibrationStep);
    app.calibrationCount = std::clamp(app.calibrationCount, 2, 12);
    if (app.calibrationCount != previousCount)
        app.calibrationRatings.resize(static_cast<size_t>(app.calibrationCount), -1);
    app.calibrationSelected = std::clamp(app.calibrationSelected, 0, app.calibrationCount - 1);
    if (ImGui::Button("Engere Reihe erzeugen", {-1.0f, 30.0f})) {
        const double previousTightest = app.calibrationStart;
        app.calibrationStep = std::max(0.01, app.calibrationStep * 0.5);
        app.calibrationStart = std::max(
            0.0, previousTightest - app.calibrationStep * (app.calibrationCount - 1));
        app.calibrationSelected = app.calibrationCount - 1;
        app.calibrationRatings.assign(static_cast<size_t>(app.calibrationCount), -1);
        app.status = "Engere Testreihe vorbereitet; die bisher engste Probe bildet nun das lockere Ende.";
    }
    ImGui::TextDisabled("Wenn selbst Probe 1 zu locker ist, hiermit den Bereich nach unten verschieben.");

    ImGui::SeparatorText("3MF-Druckerprofil");
    app.calibrationPrinterPreset = std::clamp(
        app.calibrationPrinterPreset, 0, static_cast<int>(kBambuPrinterPresets.size()) - 1);
    const BambuPrinterPreset& selectedPrinter =
        kBambuPrinterPresets[static_cast<size_t>(app.calibrationPrinterPreset)];
    if (ImGui::BeginCombo("Bambu-Drucker", selectedPrinter.label)) {
        for (size_t index = 0; index < kBambuPrinterPresets.size(); ++index) {
            const bool selected = static_cast<int>(index) == app.calibrationPrinterPreset;
            if (ImGui::Selectable(kBambuPrinterPresets[index].label, selected))
                app.calibrationPrinterPreset = static_cast<int>(index);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Profil und Testkörper werden passend zum gewählten Druckbett angeordnet.");

    const float exportButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("STL speichern ...", {exportButtonWidth, 36})) {
        if (auto target = saveStlDialog(L"Verbinder-Testmuster.stl")) {
            std::string error;
            const std::vector<ThreeMfObject> objects = makeCalibrationObjects(app, error);
            if (objects.empty()) {
                app.status = "Testmuster konnte nicht erzeugt werden: " + error;
            } else {
                const TriangleMesh merged = mergeCalibrationObjects(objects);
                app.status = saveBinaryStl(*target, merged, error)
                    ? std::to_string(objects.size() / 2) + " Testpaare als STL gespeichert: " + target->string()
                    : "STL-Testmuster konnte nicht gespeichert werden: " + error;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("3MF speichern ...", {exportButtonWidth, 36})) {
        if (auto target = saveThreeMfDialog(L"Verbinder-Testmuster.3mf")) {
            std::string error;
            const std::vector<ThreeMfObject> objects = makeCalibrationObjects(app, error);
            if (objects.empty()) {
                app.status = "Testmuster konnte nicht erzeugt werden: " + error;
            } else {
                const BambuPrinterPreset& printer =
                    kBambuPrinterPresets[static_cast<size_t>(app.calibrationPrinterPreset)];
                app.status = saveThreeMf(*target, objects, error, {}, printer.printerSettingsId,
                                         printer.printSettingsId, printer.filamentSettingsId)
                    ? std::to_string(objects.size() / 2) + " Testpaare als 3MF für " +
                          printer.label + " gespeichert: " + target->string()
                    : "3MF-Testmuster konnte nicht gespeichert werden: " + error;
            }
        }
    }

    ImGui::SeparatorText("Nach dem Druck zusammenbauen und bewerten");
    ImGui::TextWrapped("1. Zapfen und Nut mit derselben Probennummer zusammenstecken. "
                       "2. Jede Passung nach deinem eigenen Gefühl unten bewerten. "
                       "3. Den Radiopunkt bei der insgesamt besten Passung setzen und diese "
                       "anschließend als Standard übernehmen.");
    ImGui::TextDisabled("Der Radiopunkt kennzeichnet die Probe, die du als optimale Passung ausgewählt hast.");
    const char* ratings[] = {"Nicht bewertet", "Zu stramm", "Optimal", "Zu locker"};
    if (ImGui::BeginTable("Kalibrierbewertungen", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Beste", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("Spiel", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Bewertung");
        ImGui::TableHeadersRow();
        for (int index = 0; index < app.calibrationCount; ++index) {
            ImGui::PushID(index);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::RadioButton("##Best", app.calibrationSelected == index))
                app.calibrationSelected = index;
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("Probe %d: %.3f mm", index + 1,
                        app.calibrationStart + app.calibrationStep * index);
            ImGui::TableSetColumnIndex(2);
            int ratingIndex = app.calibrationRatings[static_cast<size_t>(index)] + 1;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##Rating", &ratingIndex, ratings, IM_ARRAYSIZE(ratings)))
                app.calibrationRatings[static_cast<size_t>(index)] = ratingIndex - 1;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    const double chosenClearance = app.calibrationStart + app.calibrationStep * app.calibrationSelected;
    ImGui::Text("Als optimale Probe gewählt: %.3f mm Spiel je Seite", chosenClearance);
    if (ImGui::Button("Beste Probe als Standard übernehmen", {-1, 36})) {
        const DovetailSettings previous = app.settings;
        for (ConnectorPlacement& placement : app.placements) {
            if (placement.neckWidth <= 0.0) placement.neckWidth = previous.neckWidth;
            if (placement.headWidth <= 0.0) placement.headWidth = previous.headWidth;
            if (placement.depth <= 0.0) placement.depth = previous.depth;
            if (placement.embed <= 0.0) placement.embed = previous.embed;
            if (placement.clearance < 0.0) placement.clearance = previous.clearance;
            if (placement.leadChamfer < 0) placement.leadChamfer = previous.leadChamfer ? 1 : 0;
            if (placement.chamferWidth < 0.0) placement.chamferWidth = previous.chamferWidth;
            if (placement.chamferAngle < 0.0) placement.chamferAngle = previous.chamferAngle;
        }
        app.settings = app.calibrationSettings;
        app.settings.clearance = chosenClearance;
        const bool persisted = saveConnectorDefaults(app.settings);
        app.status = "Verbinderstandard übernommen: " + std::to_string(chosenClearance) +
                     " mm Spiel je Seite" +
                     (persisted ? " (dauerhaft gespeichert)." : " (für diese Sitzung; Speichern der Vorgabe fehlgeschlagen). ");
        app.updatePreview();
        app.dirty = true;
    }
    ImGui::End();
}
