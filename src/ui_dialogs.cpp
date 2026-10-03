#include "ui_dialogs.h"

#include "app_log.h"
#include "app_persistence.h"
#include "export_workflow.h"
#include "file_dialogs.h"
#include "generation_workflow.h"
#include "project_actions.h"
#include "project_workflow.h"
#include "update_workflow.h"
#include "version.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include "localized_imgui.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>

void drawStartupPrompt(App& app, double aspect) {
    if (app.showStartupPrompt) {
        ImGui::OpenPopup("Datei oder Projekt öffnen##Start");
        app.showStartupPrompt = false;
    }
    ImGui::SetNextWindowSizeConstraints({520.0f, 0.0f},
                                        {520.0f, std::numeric_limits<float>::max()});
    if (!ImGui::BeginPopupModal("Datei oder Projekt öffnen##Start", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (app.loaded) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::TextColored({0.30f, 0.78f, 1.0f, 1.0f}, "PARTSPLICE 3D");
    const float languageButtonWidth =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Deutsch", {languageButtonWidth, 28.0f})) {
        setCurrentLanguage(AppLanguage::German);
        saveLanguagePreference();
    }
    ImGui::SameLine();
    if (ImGui::Button("Englisch", {languageButtonWidth, 28.0f})) {
        setCurrentLanguage(AppLanguage::English);
        saveLanguagePreference();
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Öffne ein 3D-Modell oder ein gespeichertes PartSplice-Projekt.");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Modelle werden beim Laden automatisch geprüft. Gespeicherte Projekte setzen die Bearbeitung mit allen Schnitten fort.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    if (ImGui::Button("Datei oder Projekt öffnen ...", {-1.0f, 42.0f})) {
        if (auto path = openModelDialog()) {
            loadFileOrProject(app, *path, aspect);
            if (app.loaded) ImGui::CloseCurrentPopup();
        }
    }
    if (ImGui::Button("Ohne Datei fortfahren", {-1.0f, 34.0f})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawMainMenuBar(App& app, double aspect, GLFWwindow* window) {
    if (!ImGui::BeginMainMenuBar()) return;
    ImGui::BeginDisabled(app.busy());
    if (ImGui::BeginMenu("Datei / Projekt")) {
        if (ImGui::MenuItem("Neues Projekt", "Ctrl+N"))
            requestProjectAction(app, PendingProjectAction::NewProject, aspect);
        if (ImGui::MenuItem("Datei oder Projekt öffnen ...", "Ctrl+O"))
            requestProjectAction(app, PendingProjectAction::OpenModel, aspect);
        std::optional<std::filesystem::path> recentToOpen;
        if (ImGui::BeginMenu("Zuletzt verwendet")) {
            if (app.recentFiles.empty()) ImGui::MenuItem("Keine Einträge", nullptr, false, false);
            for (size_t index = 0; index < app.recentFiles.size(); ++index) {
                const std::string label = pathToUtf8(app.recentFiles[index]) + "##recent" + std::to_string(index);
                if (ImGui::MenuItem(label.c_str())) recentToOpen = app.recentFiles[index];
            }
            ImGui::EndMenu();
        }
        if (recentToOpen) requestOpenPath(app, *recentToOpen, aspect);
        ImGui::Separator();
        ImGui::BeginDisabled(!app.loaded);
        if (ImGui::MenuItem("Projekt speichern", "Ctrl+S")) saveProject(app);
        if (ImGui::MenuItem("Projekt speichern unter ...", "Ctrl+Shift+S")) saveProject(app, true);
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::BeginMenu("Exportformat")) {
            if (ImGui::MenuItem("3MF – alle Objekte in einer Datei", nullptr,
                                app.exportFormat == ExportFormat::ThreeMf))
                app.exportFormat = ExportFormat::ThreeMf;
            if (ImGui::MenuItem("STL – jedes Teil als eigene Datei", nullptr,
                                app.exportFormat == ExportFormat::Stl))
                app.exportFormat = ExportFormat::Stl;
            ImGui::EndMenu();
        }
        ImGui::BeginDisabled(!app.loaded);
        if (ImGui::MenuItem("Druckdateien exportieren ...", "Ctrl+E")) exportAllParts(app);
        if (ImGui::MenuItem("Projekt schließen"))
            requestProjectAction(app, PendingProjectAction::CloseProject, aspect);
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem("Programm beenden")) glfwSetWindowShouldClose(window, GLFW_TRUE);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Hinzufügen")) {
        ImGui::BeginDisabled(!app.loaded || app.repairRunning);
        if (ImGui::MenuItem("Gerader Schnitt")) {
            requestBeginCut(app, CutMode::Straight);
        }
        if (ImGui::MenuItem("Mehrpunkt-Schnitt")) {
            requestBeginCut(app, CutMode::Polyline);
        }
        ImGui::BeginDisabled(!app.drawingLine && app.cutPoints.empty());
        if (ImGui::MenuItem(app.editingCutOperationId >= 0
                                ? "Schnittbearbeitung abbrechen"
                                : "Aktuellen Schnitt abbrechen",
                            "Esc")) {
            cancelCurrentCut(app);
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem("Alle getroffenen Teile schneiden", nullptr, app.cutAllActiveParts)) {
            app.cutAllActiveParts = !app.cutAllActiveParts;
            app.updatePreview();
            app.dirty = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Aktiv: Der Linienzug schneidet jedes berührte aktive Teil.\nInaktiv: Nur das im Modellbaum ausgewählte Teil wird geschnitten.");
        if (ImGui::MenuItem("Verbinder nur vollständig im Material", nullptr,
                            app.settings.validateInsideModel, !app.cutAllActiveParts)) {
            app.settings.validateInsideModel = !app.settings.validateInsideModel;
            app.updatePreview();
            app.dirty = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Prüft Zapfen und Nut gegen das Material. Rote Verbinder werden vor dem Anwenden nochmals bestätigt.");
        if (ImGui::MenuItem("Schnittnummern eingravieren", nullptr,
                            app.settings.assemblyMarks)) {
            app.captureUndo("Montagekennzeichnung ändern");
            app.settings.assemblyMarks = !app.settings.assemblyMarks;
            if (app.settings.assemblyMarks) {
                app.settings.assemblyMarkCode = app.activeAssemblyMarkCode();
                app.settings.assemblyMarkPosition = app.freeConnectorPosition();
                app.settings.assemblyMarkPositionsCustom = false;
                app.showAssemblyMarkEditor = true;
            }
            app.invalidateResult();
            app.dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Graviert die Schnittnummer sichtbar in beide zusammengehörigen Teile.");
        ImGui::Separator();
        ImGui::BeginDisabled(!(app.hasCompleteCut() && !app.placements.empty()));
        if (ImGui::MenuItem("Schnitt und Verbinder erzeugen")) requestGenerate(app);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Bearbeiten")) {
        ImGui::BeginDisabled(app.undoStack.empty() || app.repairRunning);
        if (ImGui::MenuItem("Rückgängig", "Ctrl+Z")) app.undoLast();
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Sprache")) {
        if (ImGui::MenuItem("Deutsch", nullptr,
                            currentLanguage() == AppLanguage::German)) {
            setCurrentLanguage(AppLanguage::German);
            saveLanguagePreference();
        }
        if (ImGui::MenuItem("Englisch", nullptr,
                            currentLanguage() == AppLanguage::English)) {
            setCurrentLanguage(AppLanguage::English);
            saveLanguagePreference();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Informationen")) {
        if (ImGui::MenuItem("Modellstatus & Details", nullptr, app.showModelInfo))
            app.showModelInfo = !app.showModelInfo;
        const std::filesystem::path logPath = currentAppLogPath();
        ImGui::BeginDisabled(logPath.empty());
        if (ImGui::MenuItem("Fehlerprotokoll öffnen ...")) {
#ifdef _WIN32
            const auto result = reinterpret_cast<intptr_t>(
                ShellExecuteW(nullptr, L"open", logPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
            app.status = result > 32 ? "Fehlerprotokoll geöffnet."
                                     : "Fehlerprotokoll konnte nicht geöffnet werden.";
#endif
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Lokales, auf 3 Sicherungskopien begrenztes Protokoll unter %%LOCALAPPDATA%%\\PartSplice3D\\logs.");
        }
        ImGui::Separator();
        ImGui::MenuItem("PartSplice 3D v" PARTSPLICE_VERSION_STRING, nullptr, false, false);
        ImGui::BeginDisabled(app.updateCheckRunning);
        if (ImGui::MenuItem(app.updateCheckRunning
                                ? "Updateprüfung läuft ..."
                                : "Nach Updates suchen ..."))
            startUpdateCheck(app, true);
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Werkzeuge")) {
        ImGui::BeginDisabled(!app.hasCompleteCut());
        if (ImGui::MenuItem("Schnittnummern bearbeiten ...", nullptr,
                            app.showAssemblyMarkEditor))
            app.showAssemblyMarkEditor = true;
        ImGui::EndDisabled();
        if (ImGui::MenuItem("Raster ...", nullptr, app.showPrecisionSettings))
            app.showPrecisionSettings = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Verbinder-Testmuster ...")) {
            if (!app.connectorCalibrationConfigured) {
                app.calibrationSettings = app.settings;
                // Start with a deliberately tight range. A 0.10 mm-per-side
                // minimum proved too loose on otherwise well calibrated printers.
                app.calibrationStep = 0.025;
                app.calibrationStart = 0.0;
                app.calibrationCount = 5;
                app.calibrationSelected = 2;
                app.calibrationRatings.assign(static_cast<size_t>(app.calibrationCount), -1);
                app.connectorCalibrationConfigured = true;
            }
            app.showConnectorCalibration = true;
        }
        ImGui::EndMenu();
    }
    ImGui::EndDisabled();
    ImGui::EndMainMenuBar();
}

void drawInvalidConnectorPrompt(App& app) {
    if (app.showInvalidConnectorPrompt) {
        ImGui::OpenPopup("Rote Verbinder behandeln?##InvalidConnectors");
        app.showInvalidConnectorPrompt = false;
    }
    if (!ImGui::BeginPopupModal("Rote Verbinder behandeln?##InvalidConnectors", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextColored({1.0f, 0.30f, 0.22f, 1.0f}, "%zu Verbinder sind rot markiert.",
                       app.invalidConnectorCount);
    if (app.partiallyValidConnectorCount > 0)
        ImGui::TextColored({1.0f, 0.58f, 0.12f, 1.0f},
                           "%zu Verbinder sind nur in einem Teil gültig und orange markiert.",
                           app.partiallyValidConnectorCount);
    if (app.warningConnectorCount > 0)
        ImGui::TextColored({1.0f, 0.84f, 0.16f, 1.0f},
                           "%zu Verbinder haben eine Wand- oder Schnittkonfliktwarnung.",
                           app.warningConnectorCount);
    ImGui::TextWrapped("Rot oder orange markierte Verbinder sind geometrisch nicht sicher ausführbar. "
                       "Dazu zählen auch Verbinder, deren vollständiges Werkzeug einen Knick der Schnittlinie überschneidet. "
                       "Gelbe beziehungsweise violette Verbinder sind möglich, besitzen aber zu wenig "
                       "Wandreserve oder berühren einen früheren Schnitt.");
    ImGui::Spacing();
    if (ImGui::Button("Ungültige verwerfen und Schnitt ausführen", {310, 38})) {
        const GenerateContinuation continuation = app.generateContinuation;
        app.generateContinuation = GenerateContinuation::None;
        startGenerateTask(app, false, continuation);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Materialprüfung erzwingen", {190, 38})) {
        const GenerateContinuation continuation = app.generateContinuation;
        app.generateContinuation = GenerateContinuation::None;
        startGenerateTask(app, true, continuation);
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button("Problematische Verbinder automatisch platzieren", {-1.0f, 34.0f})) {
        app.captureUndo("Verbinder intelligent platzieren");
        const size_t moved = app.autoPlaceProblemConnectors();
        app.status = moved > 0 ? std::to_string(moved) + " Verbinder wurden automatisch verschoben."
                               : "Keine bessere freie Position gefunden.";
        ImGui::CloseCurrentPopup();
    }
    ImGui::TextDisabled("Materialwarnungen können erzwungen werden. Überschneidungen mit Schnittknicken oder anderen Verbindern\n"
                        "werden zum Schutz vor Hohlräumen weiterhin verworfen.");
    if (ImGui::Button("Abbrechen", {-1, 30})) {
        app.generateContinuation = GenerateContinuation::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawColorLossPrompt(App& app) {
    if (app.showColorLossPrompt) {
        ImGui::OpenPopup("Farben beim STL-Export verlieren?##ColorLoss");
        app.showColorLossPrompt = false;
    }
    if (!ImGui::BeginPopupModal("Farben beim STL-Export verlieren?##ColorLoss", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextColored({1.0f, 0.55f, 0.18f, 1.0f}, "STL unterstützt keine Farben oder Filamentzuweisungen.");
    ImGui::TextWrapped("Beim Export entstehen einzelne, einfarbige STL-Dateien. "
                       "Wähle 3MF, wenn die Mehrfarben-Zuordnung erhalten bleiben soll.");
    if (ImGui::Button("Trotzdem als STL exportieren", {240.0f, 34.0f})) {
        app.colorLossExportConfirmed = true;
        ImGui::CloseCurrentPopup();
        exportAllParts(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Abbrechen", {120.0f, 34.0f})) {
        app.colorLossExportConfirmed = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawEditCutPrompt(App& app) {
    if (app.showEditCutPrompt) {
        ImGui::OpenPopup("Schnitt erneut bearbeiten?##EditAppliedCut");
        app.showEditCutPrompt = false;
    }
    if (!ImGui::BeginPopupModal("Schnitt erneut bearbeiten?##EditAppliedCut", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Schnitt %d wird wieder in den Bearbeitungsmodus versetzt.", app.pendingEditCutId);
    ImGui::TextWrapped("Das aktuelle Schnittergebnis und davon abhängige spätere Schnitte werden dabei zurückgenommen. "
                       "Linie, Knickpunkte, Verbinderpositionen und Einzelparameter werden wiederhergestellt.");
    if (!app.cutPoints.empty())
        ImGui::TextColored({1.0f, 0.72f, 0.22f, 1.0f},
                           "Der momentan vorbereitete, noch nicht angewendete Schnitt wird verworfen.");
    ImGui::Spacing();
    if (ImGui::Button("Erneut bearbeiten", {190, 36})) {
        const int operationId = app.pendingEditCutId;
        app.pendingEditCutId = -1;
        app.editCutOperation(operationId);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Abbrechen", {130, 36})) {
        app.pendingEditCutId = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawPendingCutPrompt(App& app) {
    if (app.showPendingCutPrompt) {
        ImGui::OpenPopup("Vorherigen Schnitt behandeln?");
        app.showPendingCutPrompt = false;
    }
    if (!ImGui::BeginPopupModal("Vorherigen Schnitt behandeln?", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("Es ist bereits ein Schnitt vorbereitet.");
    ImGui::TextUnformatted("Soll dieser zuerst angewendet werden?");
    ImGui::Spacing();
    const bool canApply = app.hasCompleteCut() && !app.placements.empty();
    ImGui::BeginDisabled(!canApply);
    if (ImGui::Button("Anwenden und neuen Schnitt beginnen", {285, 36})) {
        requestGenerate(app, GenerateContinuation::BeginPendingCut);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Verwerfen und neu beginnen", {230, 36})) {
        app.captureUndo("Vorherigen Schnitt verwerfen");
        app.beginCut(app.pendingCutMode);
        ImGui::CloseCurrentPopup();
    }
    if (!canApply) ImGui::TextDisabled("Der bisherige Linienzug ist noch nicht vollständig und kann nur verworfen werden.");
    if (ImGui::Button("Abbrechen", {-1, 30})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawProjectConfirmation(App& app, double aspect) {
    if (app.showProjectConfirmation) {
        ImGui::OpenPopup("Projektänderungen speichern?");
        app.showProjectConfirmation = false;
    }
    ImGui::SetNextWindowSizeConstraints({560.0f, 0.0f},
                                        {560.0f, std::numeric_limits<float>::max()});
    if (!ImGui::BeginPopupModal("Projektänderungen speichern?", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("Das aktuelle Projekt enthält ungespeicherte Änderungen. Möchtest du die Bearbeitung als PartSplice-Projekt speichern?");
    ImGui::Spacing();
    const float projectButtonWidth =
        (ImGui::GetContentRegionAvail().x - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    if (ImGui::Button("Projekt speichern", {projectButtonWidth, 34})) {
        if (saveProject(app, false, SaveContinuation::PendingProjectAction))
            ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Nicht speichern", {projectButtonWidth, 34})) {
        const PendingProjectAction action = app.pendingProjectAction;
        app.pendingProjectAction = PendingProjectAction::None;
        app.dirty = false;
        performProjectAction(app, action, aspect);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Abbrechen", {projectButtonWidth, 34})) {
        app.pendingProjectAction = PendingProjectAction::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawModelRepairPrompt(App& app) {
    if (app.showRepairPrompt) {
        ImGui::OpenPopup("Modellprüfung##Reparatur");
        app.showRepairPrompt = false;
    }
    if (!ImGui::BeginPopupModal("Modellprüfung##Reparatur", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;
    const bool valid = app.meshDiagnostics.validSolid();
    if (app.repairRunning) {
        ImGui::TextUnformatted("Windows-Modellreparatur läuft ...");
        if (app.repairProgress) {
            const std::string phase = localizedText(windowsRepairStageText(
                app.repairProgress->load(std::memory_order_relaxed)));
            ImGui::TextWrapped("%s", phase.c_str());
        }
        if (ImGui::Button("Reparatur abbrechen")) cancelWindowsMeshRepair(app);
        ImGui::EndPopup();
        return;
    }
    if (app.repairReportPartId == app.selectedPartId && !app.repairReport.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560.0f);
        const std::string report = localizedText(app.repairReport);
        ImGui::TextWrapped("%s", report.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
    }
    if (valid)
        ImGui::TextColored({0.30f, 0.92f, 0.48f, 1.0f}, "Wasserdichter Volumenkörper");
    else
        ImGui::TextColored({1.0f, 0.48f, 0.18f, 1.0f}, "Das Modell ist kein wasserdichter Volumenkörper.");
    ImGui::Text("Entartete Dreiecke: %zu", app.meshDiagnostics.degenerateTriangles);
    ImGui::Text("Offene Kanten: %zu", app.meshDiagnostics.openEdges);
    ImGui::Text("Mehrfach belegte Kanten: %zu", app.meshDiagnostics.nonManifoldEdges);
    if (app.meshDiagnostics.orientationConflicts > 0)
        ImGui::Text("Falsch ausgerichtete Kanten: %zu", app.meshDiagnostics.orientationConflicts);
    ImGui::Spacing();
    if (!valid) ImGui::TextWrapped("Für saubere Schnitte und Verbinder sollte das Modell zuerst repariert werden. "
                       "Die Originaldatei auf der Festplatte bleibt unverändert.");
    if (const PartRecord* selected = app.selectedPart(); selected && selected->mesh &&
        !selected->mesh->triangleMaterials.empty()) {
        ImGui::TextColored({1.0f, 0.67f, 0.20f, 1.0f},
                           "Der Windows-Dienst kann Flächen-Farbzuweisungen verändern. "
                           "PartSplice prüft und meldet das Ergebnis.");
    }
    ImGui::Spacing();
    if (valid) {
        if (ImGui::Button("OK", {215, 36})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (ImGui::Button("Jetzt mit Windows reparieren", {245, 36})) {
        startWindowsMeshRepair(app);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(app.repairRunning);
    if (ImGui::Button("Ohne Reparatur fortfahren", {215, 36})) {
        app.status = "Reparatur übersprungen. Die problematischen Kanten bleiben markiert; Schnitte können fehlschlagen.";
        app.showMeshIssues = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void drawModelInfoWindow(App& app) {
    if (!app.showModelInfo) return;
    ImGui::SetNextWindowSize({520.0f, 560.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({400.0f, 300.0f}, {900.0f, 900.0f});
    if (!ImGui::Begin("Modellstatus & Details", &app.showModelInfo,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (!app.loaded) {
        ImGui::TextUnformatted("Kein Modell geladen.");
        ImGui::End();
        return;
    }

    ImGui::Text("Datei: %s", app.sourcePath.filename().string().c_str());
    if (app.activeObjectIndex >= 0 &&
        static_cast<size_t>(app.activeObjectIndex) < app.objectSessions.size())
        ImGui::Text("Objekt: %s", app.objectSessions[static_cast<size_t>(app.activeObjectIndex)].name.c_str());
    const PartRecord* part = app.selectedPart();
    if (part != nullptr && part->mesh) {
        const Vec3 size = part->mesh->bounds.size();
        ImGui::Text("Aktives Teil: %s", part->label.c_str());
        ImGui::Text("Dreiecke: %zu | Eckpunkte: %zu",
                    part->mesh->triangles.size(), part->mesh->vertices.size());
        ImGui::Text("Abmessungen: %.2f × %.2f × %.2f mm", size.x, size.y, size.z);
    }
    ImGui::Text("Aktuelle Teile: %zu | Schnitte: %zu", app.activePartCount(), app.cuts.size());

    if (!app.materials.empty()) {
        ImGui::SeparatorText("3MF-Farben & Filamente");
        ImGui::Text("Erkannte Slots: %zu", app.materials.size());
        for (size_t index = 0; index < app.materials.size(); ++index) {
            const MeshMaterial& material = app.materials[index];
            ImGui::BulletText("%zu: %s (%s)", index + 1,
                              material.name.c_str(), material.color.c_str());
        }
    }
    if (!app.threeMfCompatibilityWarnings.empty()) {
        ImGui::SeparatorText("3MF-Kompatibilität");
        ImGui::TextColored({1.0f, 0.67f, 0.20f, 1.0f},
                           "%zu Eigenschaftshinweis(e)",
                           app.threeMfCompatibilityWarnings.size());
        for (const std::string& warning : app.threeMfCompatibilityWarnings) {
            const std::string translated = localizedText(warning);
            ImGui::BulletText("%s", translated.c_str());
        }
    }

    ImGui::SeparatorText("Modellprüfung");
    if (app.meshDiagnostics.validSolid()) {
        ImGui::TextColored({0.30f, 0.92f, 0.48f, 1.0f}, "Wasserdichter Volumenkörper");
    } else {
        ImGui::TextColored({1.0f, 0.48f, 0.18f, 1.0f}, "%zu problematische Kanten",
                           app.meshDiagnostics.problemEdgeCount());
        ImGui::Text("Offen: %zu | Mehrfach: %zu | Ausrichtung: %zu",
                    app.meshDiagnostics.openEdges, app.meshDiagnostics.nonManifoldEdges,
                    app.meshDiagnostics.orientationConflicts);
        ImGui::Text("Entartete Dreiecke: %zu", app.meshDiagnostics.degenerateTriangles);
        ImGui::Checkbox("Problemstellen im Modell anzeigen", &app.showMeshIssues);
        if (app.repairRunning) {
            if (ImGui::Button("Reparatur abbrechen", {-1, 34})) cancelWindowsMeshRepair(app);
            ImGui::TextDisabled("Windows-3D-Dienst arbeitet ...");
        } else if (ImGui::Button("Mit Windows reparieren", {-1, 34})) {
            startWindowsMeshRepair(app);
        }
    }

    ImGui::End();
}
