#include "generation_workflow.h"

#include "app_log.h"
#include "app_persistence.h"
#include "app_queries.h"
#include "background_progress.h"
#include "project_actions.h"
#include "project_workflow.h"
#include "windows_mesh_repair.h"
#include "worker_client.h"

#include <imgui.h>
#include "localized_imgui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

std::vector<GenerateTargetSnapshot> collectGenerateTargets(const App& app) {
    std::vector<GenerateTargetSnapshot> targets;
    targets.reserve(app.parts.size());
    for (const PartRecord& part : app.parts) {
        if (!part.active) continue;
        if (!app.cutAllActiveParts) {
            if (part.id == app.selectedPartId)
                targets.push_back({part.id, part.label, part.mesh});
        } else if (polylineHitsPartProjection(part, app.cutPoints)) {
            targets.push_back({part.id, part.label, part.mesh});
        }
    }
    return targets;
}

BackgroundTaskResult computeGenerateTask(std::vector<GenerateTargetSnapshot> targets,
                                         std::vector<Vec2> cutPoints,
                                         std::vector<bool> pointLocked,
                                         std::vector<ConnectorPlacement> placements,
                                         DovetailSettings settings,
                                         CutMode mode,
                                         bool cutAllActiveParts,
                                         bool forceInvalidConnectors,
                                         int fixedOperationId,
                                         bool recordUndo,
                                         const std::atomic_bool* cancelRequested,
                                         const std::shared_ptr<BackgroundProgress>& progress) {
    BackgroundTaskResult task;
    task.kind = BackgroundTaskKind::Generate;
    task.generation.cutPoints = std::move(cutPoints);
    task.generation.pointLocked = std::move(pointLocked);
    task.generation.placements = std::move(placements);
    task.generation.settings = settings;
    task.generation.mode = mode;
    task.generation.cutAllActiveParts = cutAllActiveParts;
    task.generation.forceInvalidConnectors = forceInvalidConnectors;
    task.generation.fixedOperationId = fixedOperationId;
    task.generation.recordUndo = recordUndo;
    if (progress) {
        progress->completed.store(0, std::memory_order_relaxed);
        progress->total.store(targets.size(), std::memory_order_relaxed);
    }

    if (targets.empty()) {
        task.message = "Der Schnittlinienzug trifft kein aktives Teil.";
        return task;
    }
    task.generation.splits.reserve(targets.size());
    for (GenerateTargetSnapshot& parent : targets) {
        struct ProgressStep {
            const std::shared_ptr<BackgroundProgress>& progress;
            ~ProgressStep() {
                if (progress) progress->completed.fetch_add(1, std::memory_order_relaxed);
            }
        } progressStep{progress};
        if (!parent.mesh) {
            task.generation.failures.push_back(parent.parentLabel +
                ": Quelldaten sind nicht mehr verfügbar.");
            continue;
        }
        if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
            task.canceled = true;
            task.message = "Schnittberechnung abgebrochen; das Modell blieb unverändert.";
            return task;
        }
        DovetailSettings splitSettings = settings;
        if (forceInvalidConnectors) splitSettings.validateInsideModel = false;
        else if (cutAllActiveParts) splitSettings.validateInsideModel = true;
        try {
            DovetailResult result = createDovetailSplitIsolated(
                parent.mesh, task.generation.cutPoints, splitSettings,
                task.generation.placements, false, cancelRequested);
            if (!result.ok) {
                task.generation.failures.push_back(parent.parentLabel + ": " + result.message);
                continue;
            }
            GeneratedSplitPayload split;
            split.parentId = parent.parentId;
            split.parentLabel = std::move(parent.parentLabel);
            split.partA = std::make_shared<const TriangleMesh>(std::move(result.partA));
            split.partB = std::make_shared<const TriangleMesh>(std::move(result.partB));
            split.indexA = std::make_shared<const MeshSpatialIndex>(*split.partA);
            split.indexB = std::make_shared<const MeshSpatialIndex>(*split.partB);
            split.connectors = std::move(result.connectors);
            task.generation.splits.push_back(std::move(split));
        } catch (const std::bad_alloc&) {
            task.generation.failures.push_back(parent.parentLabel +
                ": Nicht genügend Arbeitsspeicher für die Geometrieberechnung.");
        } catch (const std::exception& exception) {
            task.generation.failures.push_back(parent.parentLabel + ": " + exception.what());
        } catch (...) {
            task.generation.failures.push_back(parent.parentLabel +
                ": Unbekannter Fehler bei der Geometrieberechnung.");
        }
    }

    if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
        task.canceled = true;
        task.message = "Schnittberechnung abgebrochen; das Modell blieb unverändert.";
        return task;
    }
    if (!task.generation.failures.empty() || task.generation.splits.size() != targets.size()) {
        std::ostringstream message;
        message << "Schnitt nicht übernommen; alle Zielteile bleiben unverändert.";
        const size_t shown = std::min<size_t>(task.generation.failures.size(), 4);
        for (size_t index = 0; index < shown; ++index)
            message << " " << task.generation.failures[index];
        if (task.generation.failures.size() > shown)
            message << " Weitere Fehler: " << (task.generation.failures.size() - shown) << ".";
        task.message = message.str();
        return task;
    }
    task.ok = true;
    task.message = "Schnittberechnung abgeschlossen.";
    return task;
}

bool commitGenerateTask(App& app, GenerateJobPayload&& generation) {
    if (generation.splits.empty()) {
        app.status = "Der Schnittlinienzug trifft kein aktives Teil.";
        return false;
    }
    for (const GeneratedSplitPayload& split : generation.splits) {
        const auto parent = std::find_if(app.parts.begin(), app.parts.end(), [&](const PartRecord& part) {
            return part.id == split.parentId && part.active;
        });
        if (parent == app.parts.end()) {
            app.status = "Schnitt nicht übernommen: Ein Zielteil hat sich während der Berechnung geändert.";
            return false;
        }
    }

    const int cutId = generation.fixedOperationId >= 0
        ? generation.fixedOperationId : app.nextCutId;
    if (cutId <= 0 || cutId >= std::numeric_limits<int>::max()) {
        app.status = "Schnitt nicht übernommen: Die interne Schnitt-ID ist ausgeschöpft.";
        return false;
    }
    const int previouslySelected = app.selectedPartId;
    int stagedNextPartId = app.nextPartId;
    int selectedChild = -1;
    bool selectedPreferred = false;
    size_t newPartCount = 0;

    std::vector<PartRecord> stagedParts;
    std::vector<CutRecord> stagedCuts;
    try {
        if (generation.splits.size() > (std::numeric_limits<size_t>::max() / 2)) {
            app.status = "Schnitt nicht übernommen: Zu viele Ergebnisbauteile.";
            return false;
        }
        stagedParts.reserve(generation.splits.size() * 2);
        stagedCuts.reserve(generation.splits.size());

        for (GeneratedSplitPayload& split : generation.splits) {
            CutRecord cut;
            cut.id = cutId;
            cut.sourcePartId = split.parentId;
            cut.sourceLabel = split.parentLabel;
            cut.points = generation.cutPoints;
            cut.pointLocked = generation.pointLocked;
            cut.placements = generation.placements;
            cut.settings = generation.settings;
            cut.mode = generation.mode;
            cut.cutAllActiveParts = generation.cutAllActiveParts;
            cut.forcedInvalidConnectors = generation.forceInvalidConnectors;
            cut.previewCache = std::move(split.connectors);
            cut.previewCacheValid = true;
            cut.childPartIds.reserve(2);
            cut.childLabels.reserve(2);

            struct IndexedResult {
                const char* side;
                std::shared_ptr<const TriangleMesh>* mesh;
                std::shared_ptr<const MeshSpatialIndex>* index;
            };
            std::array<IndexedResult, 2> results{{
                {"A", &split.partA, &split.indexA},
                {"B", &split.partB, &split.indexB}
            }};
            for (IndexedResult& result : results) {
                if (stagedNextPartId <= 0 || stagedNextPartId == std::numeric_limits<int>::max()) {
                    app.status = "Schnitt nicht übernommen: Die interne Bauteil-ID ist ausgeschöpft.";
                    return false;
                }
                PartRecord child;
                child.id = stagedNextPartId++;
                child.parentCutId = cutId;
                child.label = split.parentLabel == "Original"
                    ? std::string("Teil ") + result.side
                    : split.parentLabel + "." + result.side;
                child.mesh = std::move(*result.mesh);
                child.spatialIndex = std::move(*result.index);
                if (!child.mesh || !child.spatialIndex) {
                    app.status = "Schnitt nicht übernommen: Indizierte Ergebnisgeometrie fehlt.";
                    return false;
                }
                child.gl.set(*child.mesh);
                if (split.parentId == previouslySelected && !selectedPreferred) {
                    selectedChild = child.id;
                    selectedPreferred = true;
                } else if (selectedChild < 0) {
                    selectedChild = child.id;
                }
                cut.childPartIds.push_back(child.id);
                cut.childLabels.push_back(child.label);
                stagedParts.push_back(std::move(child));
                ++newPartCount;
            }
            stagedCuts.push_back(std::move(cut));
        }

        app.parts.reserve(app.parts.size() + stagedParts.size());
        app.cuts.reserve(app.cuts.size() + stagedCuts.size());
        if (generation.recordUndo)
            app.captureUndo("Schnitt erzeugen", UndoKind::GeneratedCut, cutId);
    } catch (const std::bad_alloc&) {
        app.status = "Schnitt nicht übernommen: Nicht genügend Arbeitsspeicher für die Ergebnisbauteile.";
        return false;
    } catch (const std::exception& exception) {
        app.status = std::string("Schnitt nicht übernommen: ") + exception.what();
        return false;
    } catch (...) {
        app.status = "Schnitt nicht übernommen: Unbekannter Fehler beim Vorbereiten der Ergebnisbauteile.";
        return false;
    }

    if (generation.fixedOperationId < 0) app.nextCutId = cutId + 1;
    else app.nextCutId = std::max(app.nextCutId, generation.fixedOperationId + 1);
    app.nextPartId = stagedNextPartId;
    for (const GeneratedSplitPayload& split : generation.splits) {
        for (PartRecord& candidate : app.parts)
            if (candidate.id == split.parentId) candidate.active = false;
    }
    for (PartRecord& child : stagedParts) app.parts.push_back(std::move(child));
    for (CutRecord& cut : stagedCuts) app.cuts.push_back(std::move(cut));

    const size_t splitCount = generation.splits.size();
    app.selectedPartId = selectedChild;
    app.clearCurrentCut();
    app.refreshMeshDiagnostics();
    app.generated = false;
    app.dirty = true;
    app.storeActiveObjectSession();
    app.status = std::to_string(splitCount) + " getroffene Teile wurden atomar in " +
                 std::to_string(newPartCount) +
                 " Ergebnisbauteile geschnitten. Getrennte Inseln bleiben vollständig erhalten.";
    if (generation.forceInvalidConnectors)
        app.status += " Die roten Materialwarnungen wurden auf ausdrücklichen Wunsch übergangen.";
    return true;
}

bool startGenerateTask(App& app, bool forceInvalidConnectors,
                       GenerateContinuation continuation) {
    if (app.busy()) {
        app.status = "Es läuft bereits ein Vorgang.";
        return false;
    }
    if (!app.selectedPart()) {
        app.status = "Kein aktuelles Teil für den Schnitt ausgewählt.";
        return false;
    }
    std::vector<GenerateTargetSnapshot> targets = collectGenerateTargets(app);
    if (targets.empty()) {
        app.status = "Der Schnittlinienzug trifft kein aktives Teil.";
        return false;
    }
    const std::vector<Vec2> cutPoints = app.cutPoints;
    const std::vector<bool> pointLocked = app.cutPointLocked;
    const std::vector<ConnectorPlacement> placements = app.placements;
    DovetailSettings settings = app.settings;
    settings.assemblyMarkCode = app.activeAssemblyMarkCode();
    const CutMode mode = app.cutMode;
    const bool cutAll = app.cutAllActiveParts;

    app.backgroundRunning = true;
    app.backgroundProgress = std::make_shared<BackgroundProgress>();
    app.backgroundProgress->total.store(targets.size(), std::memory_order_relaxed);
    app.backgroundStarted = std::chrono::steady_clock::now();
    app.backgroundKind = BackgroundTaskKind::Generate;
    app.backgroundLabel = "Schnitt und Verbinder werden im Hintergrund berechnet ...";
    app.backgroundContinuation = continuation;
    app.backgroundReplaceOperationId = -1;
    app.backgroundPendingCutRestore.reset();
    app.backgroundCancel = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = app.backgroundCancel;
    const std::shared_ptr<BackgroundProgress> progress = app.backgroundProgress;
    app.status = app.backgroundLabel;
    try {
        app.backgroundFuture = std::async(std::launch::async,
            [targets = std::move(targets), cutPoints, pointLocked, placements,
             settings, mode, cutAll, forceInvalidConnectors, cancel, progress]() mutable {
                return computeGenerateTask(std::move(targets), cutPoints, pointLocked, placements,
                                           settings, mode, cutAll, forceInvalidConnectors,
                                           -1, true, cancel.get(), progress);
            });
    } catch (const std::exception& exception) {
        app.backgroundRunning = false;
        app.backgroundKind = BackgroundTaskKind::None;
        app.backgroundLabel.clear();
        app.backgroundCancel.reset();
        app.backgroundContinuation = GenerateContinuation::None;
        app.status = std::string("Schnittberechnung konnte nicht gestartet werden: ") + exception.what();
        return false;
    } catch (...) {
        app.backgroundRunning = false;
        app.backgroundKind = BackgroundTaskKind::None;
        app.backgroundLabel.clear();
        app.backgroundCancel.reset();
        app.backgroundContinuation = GenerateContinuation::None;
        app.status = "Schnittberechnung konnte nicht gestartet werden.";
        return false;
    }
    return true;
}

void finishGenerateContinuation(App& app, GenerateContinuation continuation) {
    if (continuation == GenerateContinuation::BeginPendingCut) {
        app.captureUndo("Neue Schnittlinie beginnen");
        app.beginCut(app.pendingCutMode);
    } else if (continuation == GenerateContinuation::SwitchObject && app.pendingObjectIndex >= 0) {
        const int target = app.pendingObjectIndex;
        app.pendingObjectIndex = -1;
        app.activateObjectSession(static_cast<size_t>(target), app.pendingObjectAspect);
    }
}

void restorePendingCutAfterReplacement(App& app, const PendingCutRestoreState& pending) {
    if (!pending.hadPendingCut) {
        app.status = "Der Verbinder des ausgeführten Schnitts wurde verschoben. "
                     "Die neue Position wurde in die Teile und den Export übernommen.";
        app.storeActiveObjectSession();
        app.dirty = true;
        return;
    }

    if (!pending.selectedPartLabel.empty()) {
        for (const PartRecord& part : app.parts) {
            if (part.active && part.label == pending.selectedPartLabel) {
                app.selectedPartId = part.id;
                break;
            }
        }
    }
    app.cutPoints = pending.points;
    app.cutPointLocked = pending.pointLocked;
    if (app.cutPointLocked.size() != app.cutPoints.size())
        app.cutPointLocked.assign(app.cutPoints.size(), false);
    app.placements = pending.placements;
    app.settings = pending.settings;
    app.cutMode = pending.mode;
    app.cutAllActiveParts = pending.cutAllActiveParts;
    app.drawingLine = pending.drawingLine;
    app.selectedConnector = pending.selectedConnector;
    app.showConnectorEditor = false;
    app.invalidateResult();
    app.updatePreview();
    app.status = "Der frühere Verbinder wurde verschoben und neu berechnet; "
                 "der aktuelle rote Schnitt bleibt vorbereitet.";
    app.storeActiveObjectSession();
    app.dirty = true;
}

void cancelBackgroundTask(App& app) {
    if (!app.backgroundRunning || !app.backgroundCancel) return;
    if (app.backgroundKind == BackgroundTaskKind::SaveProject) {
        app.status = "Das atomare Projektspeichern wird noch abgeschlossen ...";
        return;
    }
    app.backgroundCancel->store(true, std::memory_order_relaxed);
    app.status = "Der laufende Vorgang wird abgebrochen ...";
}

void pollBackgroundTask(App& app) {
    if (!app.backgroundRunning || !app.backgroundFuture.valid()) return;
    if (app.backgroundFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;

    BackgroundTaskResult result;
    try {
        result = app.backgroundFuture.get();
    } catch (const std::bad_alloc&) {
        result.kind = app.backgroundKind;
        result.message = "Der Vorgang konnte wegen Speichermangels nicht abgeschlossen werden.";
    } catch (const std::exception& exception) {
        result.kind = app.backgroundKind;
        result.message = std::string("Der Vorgang ist fehlgeschlagen: ") + exception.what();
    } catch (...) {
        result.kind = app.backgroundKind;
        result.message = "Der Vorgang ist mit einem unbekannten Fehler fehlgeschlagen.";
    }

    const double aspect = app.backgroundAspect;
    const GenerateContinuation continuation = app.backgroundContinuation;
    const SaveContinuation saveContinuation = app.backgroundSaveContinuation;
    std::shared_ptr<PartSpliceProject> savedProjectDocument =
        std::move(app.backgroundSaveDocument);
    const int replaceOperationId = app.backgroundReplaceOperationId;
    std::optional<PendingCutRestoreState> pendingCutRestore =
        std::move(app.backgroundPendingCutRestore);
    app.backgroundRunning = false;
    app.backgroundKind = BackgroundTaskKind::None;
    app.backgroundLabel.clear();
    app.backgroundCancel.reset();
    app.backgroundProgress.reset();
    app.backgroundContinuation = GenerateContinuation::None;
    app.backgroundSaveContinuation = SaveContinuation::None;
    app.backgroundReplaceOperationId = -1;
    app.backgroundPendingCutRestore.reset();

    if (savedProjectDocument) {
        app.originalFileData = std::move(savedProjectDocument->originalFileData);
        app.sourceThreeMfProjectSettings =
            std::move(savedProjectDocument->sourceThreeMfProjectSettings);
    }

    if (result.canceled) {
        if (replaceOperationId >= 0) {
            app.undoLast();
            app.status = "Neuberechnung abgebrochen. Der verschobene Verbinder und alle Teile wurden auf den vorherigen Zustand zurückgesetzt.";
        } else {
            app.status = result.message.empty() ? "Vorgang abgebrochen." : result.message;
        }
        return;
    }

    if (!result.ok && !result.message.empty()) appLogError(result.message);

    switch (result.kind) {
        case BackgroundTaskKind::LoadFile:
            if (!result.projectPath.empty())
                applyProjectLoad(app, std::move(result.project),
                                 std::move(result.projectSpatialIndices),
                                 result.projectPath, aspect);
            else if (result.ok)
                applyModelLoad(app, std::move(result.model), aspect);
            else
                app.status = result.message.empty() ? "Datei konnte nicht geladen werden." : result.message;
            break;
        case BackgroundTaskKind::Generate:
            if (replaceOperationId >= 0) {
                bool committed = false;
                if (result.ok) {
                    app.deleteCutOperation(replaceOperationId, false);
                    committed = commitGenerateTask(app, std::move(result.generation));
                }
                if (committed) {
                    if (pendingCutRestore)
                        restorePendingCutAfterReplacement(app, *pendingCutRestore);
                    else
                        app.storeActiveObjectSession();
                } else {
                    app.undoLast();
                    app.status = result.message.empty()
                        ? "Der verschobene Verbinder konnte nicht neu berechnet werden. Der vorherige Zustand wurde wiederhergestellt."
                        : result.message + " Der vorherige Zustand wurde wiederhergestellt.";
                }
            } else if (result.ok && commitGenerateTask(app, std::move(result.generation))) {
                finishGenerateContinuation(app, continuation);
            } else {
                app.status = result.message.empty() ? "Schnittberechnung fehlgeschlagen." : result.message;
                app.refreshMeshDiagnostics();
                if (!app.meshDiagnostics.validSolid()) {
                    app.showMeshIssues = true;
                    app.status += " Die fehlerhaften Kanten sind im Modell farbig markiert.";
                }
            }
            break;
        case BackgroundTaskKind::Export:
            app.status = result.message.empty()
                ? (result.ok ? "Export abgeschlossen." : "Export fehlgeschlagen.")
                : result.message;
            break;
        case BackgroundTaskKind::SaveProject:
            app.status = result.message.empty()
                ? (result.ok ? "Projekt gespeichert." : "Projekt konnte nicht gespeichert werden.")
                : result.message;
            if (result.ok) {
                app.projectPath = result.projectPath;
                app.dirty = false;
                rememberRecentFile(app.recentFiles, result.projectPath);
                if (saveContinuation == SaveContinuation::PendingProjectAction) {
                    const PendingProjectAction action = app.pendingProjectAction;
                    app.pendingProjectAction = PendingProjectAction::None;
                    performProjectAction(app, action, aspect);
                } else if (saveContinuation == SaveContinuation::ExitApplication) {
                    app.exitAfterSave = true;
                }
            } else if (saveContinuation == SaveContinuation::PendingProjectAction) {
                app.pendingProjectAction = PendingProjectAction::None;
            }
            break;
        case BackgroundTaskKind::None:
            app.status = result.message.empty() ? "Vorgang beendet." : result.message;
            break;
    }
}

void drawBackgroundTaskWindow(App& app) {
    if (!app.backgroundRunning) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({430.0f, 0.0f}, ImGuiCond_Always);
    ImGui::Begin("Vorgang läuft##BackgroundTask", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    const std::string localizedBackground = localizedText(
        app.backgroundLabel.empty() ? "Vorgang wird ausgeführt ..." : app.backgroundLabel);
    ImGui::TextWrapped("%s", localizedBackground.c_str());
    ImGui::Spacing();
    drawBackgroundProgress(app.backgroundProgress, app.backgroundStarted);
    ImGui::Spacing();
    const bool canceling = app.backgroundCancel &&
        app.backgroundCancel->load(std::memory_order_relaxed);
    const bool savingProject = app.backgroundKind == BackgroundTaskKind::SaveProject;
    ImGui::BeginDisabled(canceling || savingProject);
    const char* actionLabel = savingProject ? "Projekt wird atomar gespeichert ..."
        : (canceling ? "Abbruch angefordert ..." : "Abbrechen");
    if (ImGui::Button(actionLabel, {-1.0f, 34.0f})) cancelBackgroundTask(app);
    ImGui::EndDisabled();
    ImGui::End();
}

void requestGenerate(App& app, GenerateContinuation continuation) {
    app.invalidConnectorCount = static_cast<size_t>(std::count_if(
        app.preview.begin(), app.preview.end(), [](const ConnectorPreview& connector) {
            return !connector.valid && !connector.partiallyValid;
        }));
    app.partiallyValidConnectorCount = static_cast<size_t>(std::count_if(
        app.preview.begin(), app.preview.end(), [](const ConnectorPreview& connector) {
            return connector.partiallyValid;
        }));
    app.warningConnectorCount = static_cast<size_t>(std::count_if(
        app.preview.begin(), app.preview.end(), [](const ConnectorPreview& connector) {
            return connector.valid && !connector.partiallyValid && connector.warning();
        }));
    if (app.invalidConnectorCount > 0 || app.partiallyValidConnectorCount > 0) {
        app.generateContinuation = continuation;
        app.showInvalidConnectorPrompt = true;
        return;
    }
    startGenerateTask(app, false, continuation);
}

void requestEditCut(App& app, int operationId) {
    if (operationId < 0) return;
    if (app.pendingAppliedCutRebuildId >= 0) {
        app.status = "Zuerst den verschobenen Verbinder mit ‚Neu schneiden‘ übernehmen oder mit Strg+Z verwerfen.";
        return;
    }
    app.pendingEditCutId = operationId;
    app.showEditCutPrompt = true;
}

bool rebuildMovedAppliedConnector(App& app, int operationId) {
    if (app.busy()) {
        app.undoLast();
        app.status = "Der Verbinder wurde nicht verschoben, weil bereits ein anderer Vorgang läuft.";
        return false;
    }
    if (cutHasDependentOperations(app, operationId)) {
        app.undoLast();
        app.status = "Der Verbinder wurde nicht verschoben, weil inzwischen abhängige Folgeschnitte vorhanden sind.";
        return false;
    }

    const auto found = std::find_if(app.cuts.begin(), app.cuts.end(), [&](const CutRecord& cut) {
        return cut.id == operationId;
    });
    if (found == app.cuts.end()) {
        app.undoLast();
        app.status = "Der ausgeführte Schnitt ist nicht mehr vorhanden; die Änderung wurde zurückgesetzt.";
        return false;
    }
    const CutRecord editedCut = *found;

    std::vector<GenerateTargetSnapshot> targets;
    std::unordered_set<int> sourceIds;
    for (const CutRecord& cut : app.cuts) {
        if (cut.id != operationId || !sourceIds.insert(cut.sourcePartId).second) continue;
        const PartRecord* sourcePart = partById(app, cut.sourcePartId);
        if (sourcePart == nullptr) {
            app.undoLast();
            app.status = "Die ursprünglichen Quelldaten des Schnitts fehlen; die Änderung wurde zurückgesetzt.";
            return false;
        }
        targets.push_back({sourcePart->id, sourcePart->label, sourcePart->mesh});
    }
    if (targets.empty()) {
        app.undoLast();
        app.status = "Für den ausgeführten Schnitt wurden keine Quellteile gefunden; die Änderung wurde zurückgesetzt.";
        return false;
    }

    PendingCutRestoreState pending;
    pending.hadPendingCut = !app.cutPoints.empty();
    pending.drawingLine = app.drawingLine;
    pending.mode = app.cutMode;
    pending.points = app.cutPoints;
    pending.pointLocked = app.cutPointLocked;
    pending.placements = app.placements;
    pending.settings = app.settings;
    pending.cutAllActiveParts = app.cutAllActiveParts;
    pending.selectedConnector = app.selectedConnector;
    if (const PartRecord* selected = app.selectedPart()) pending.selectedPartLabel = selected->label;

    app.backgroundRunning = true;
    app.backgroundProgress = std::make_shared<BackgroundProgress>();
    app.backgroundProgress->total.store(targets.size(), std::memory_order_relaxed);
    app.backgroundStarted = std::chrono::steady_clock::now();
    app.backgroundKind = BackgroundTaskKind::Generate;
    app.backgroundLabel = "Schnitt und alle geänderten Verbinder werden im Hintergrund neu berechnet ...";
    app.backgroundContinuation = GenerateContinuation::None;
    app.backgroundReplaceOperationId = operationId;
    app.backgroundPendingCutRestore = std::move(pending);
    app.backgroundCancel = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = app.backgroundCancel;
    const std::shared_ptr<BackgroundProgress> progress = app.backgroundProgress;
    app.status = app.backgroundLabel;

    try {
        app.backgroundFuture = std::async(std::launch::async,
            [targets = std::move(targets), editedCut, cancel, progress]() mutable {
                return computeGenerateTask(std::move(targets), editedCut.points,
                                           editedCut.pointLocked, editedCut.placements,
                                           editedCut.settings, editedCut.mode,
                                           editedCut.cutAllActiveParts,
                                           editedCut.forcedInvalidConnectors,
                                           editedCut.id, false, cancel.get(), progress);
            });
        app.pendingAppliedCutRebuildId = -1;
    } catch (const std::exception& exception) {
        app.backgroundRunning = false;
        app.backgroundKind = BackgroundTaskKind::None;
        app.backgroundLabel.clear();
        app.backgroundCancel.reset();
        app.backgroundReplaceOperationId = -1;
        app.backgroundPendingCutRestore.reset();
        app.undoLast();
        app.status = std::string("Die Neuberechnung konnte nicht gestartet werden: ") +
                     exception.what() + ". Der vorherige Zustand wurde wiederhergestellt.";
        return false;
    } catch (...) {
        app.backgroundRunning = false;
        app.backgroundKind = BackgroundTaskKind::None;
        app.backgroundLabel.clear();
        app.backgroundCancel.reset();
        app.backgroundReplaceOperationId = -1;
        app.backgroundPendingCutRestore.reset();
        app.undoLast();
        app.status = "Die Neuberechnung konnte nicht gestartet werden. Der vorherige Zustand wurde wiederhergestellt.";
        return false;
    }
    return true;
}

void startWindowsMeshRepair(App& app) {
    const PartRecord* target = app.selectedPart();
    if (target == nullptr || app.busy()) return;
    app.repairTargetPartId = target->id;
    if (!target->mesh) return;
    std::shared_ptr<const TriangleMesh> mesh = target->mesh;
    app.repairCancel = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = app.repairCancel;
    app.closeAfterRepair = false;
    app.repairRunning = true;
    app.status = "Windows-3D-Reparaturdienst arbeitet. Das Original auf der Festplatte bleibt unverändert ...";
    try {
        app.repairFuture = std::async(std::launch::async,
            [mesh = std::move(mesh), cancel]() -> IndexedRepairResult {
                IndexedRepairResult prepared;
                try {
                    prepared.repair = repairMeshWithWindowsService(*mesh, cancel.get());
                    if (prepared.repair.ok && !prepared.repair.canceled) {
                        prepared.mesh = std::make_shared<const TriangleMesh>(
                            std::move(prepared.repair.mesh));
                        prepared.spatialIndex =
                            std::make_shared<const MeshSpatialIndex>(*prepared.mesh);
                    }
                } catch (const std::bad_alloc&) {
                    prepared.repair.message =
                        "Nicht genügend Arbeitsspeicher für die Windows-Reparatur.";
                } catch (const std::exception& exception) {
                    prepared.repair.message =
                        std::string("Windows-Reparatur fehlgeschlagen: ") + exception.what();
                } catch (...) {
                    prepared.repair.message =
                        "Windows-Reparatur ist mit einem unbekannten Fehler fehlgeschlagen.";
                }
                return prepared;
            });
    } catch (const std::exception& exception) {
        app.repairRunning = false;
        app.repairCancel.reset();
        app.repairTargetPartId = -1;
        app.status = std::string("Windows-Reparatur konnte nicht gestartet werden: ") + exception.what();
    } catch (...) {
        app.repairRunning = false;
        app.repairCancel.reset();
        app.repairTargetPartId = -1;
        app.status = "Windows-Reparatur konnte nicht gestartet werden.";
    }
}

void cancelWindowsMeshRepair(App& app) {
    if (!app.repairRunning || !app.repairCancel) return;
    app.repairCancel->store(true, std::memory_order_relaxed);
    app.status = "Windows-3D-Reparatur wird abgebrochen ...";
}

void pollWindowsMeshRepair(App& app) {
    if (!app.repairRunning || !app.repairFuture.valid()) return;
    if (app.repairFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;

    IndexedRepairResult prepared;
    try {
        prepared = app.repairFuture.get();
    } catch (const std::exception& exception) {
        prepared.repair.message =
            std::string("Windows-Reparatur konnte nicht abgeschlossen werden: ") + exception.what();
    } catch (...) {
        prepared.repair.message =
            "Windows-Reparatur konnte wegen eines unbekannten Fehlers nicht abgeschlossen werden.";
    }
    WindowsMeshRepairResult& result = prepared.repair;
    app.repairRunning = false;
    app.repairCancel.reset();

    if (result.canceled) {
        app.status = result.message.empty() ? "Windows-Reparatur abgebrochen." : result.message;
        app.repairTargetPartId = -1;
        return;
    }
    if (!result.ok) {
        appLogError(result.message.empty() ? "Windows-Modellreparatur fehlgeschlagen."
                                           : result.message);
        app.status = result.message + " Die Datei kann alternativ in Bambu Studio repariert und danach erneut geöffnet werden.";
        app.showRepairPrompt = true;
        app.repairTargetPartId = -1;
        return;
    }

    PartRecord* target = nullptr;
    for (PartRecord& part : app.parts) {
        if (part.id == app.repairTargetPartId && part.active) {
            target = &part;
            break;
        }
    }
    if (target == nullptr) {
        app.status = "Die Reparatur wurde beendet, das Zielteil ist inzwischen jedoch nicht mehr aktiv.";
        app.repairTargetPartId = -1;
        return;
    }

    app.captureUndo("Modell reparieren", UndoKind::MeshRepair, -1, true);
    if (!prepared.mesh || !prepared.spatialIndex) {
        appLogError("Windows-Modellreparatur lieferte keine indizierte Ergebnisgeometrie.");
        app.status = "Windows-Reparatur fehlgeschlagen: Ergebnisgeometrie ist unvollständig.";
        app.showRepairPrompt = true;
        app.repairTargetPartId = -1;
        return;
    }
    target->mesh = std::move(prepared.mesh);
    target->spatialIndex = std::move(prepared.spatialIndex);
    target->gl.set(*target->mesh);
    if (target->id == 1 && app.cuts.empty()) {
        app.source = target->mesh;
        app.sourceGL.set(*app.source);
    }
    for (CutRecord& cut : app.cuts) {
        if (cut.sourcePartId == target->id) {
            cut.previewCacheValid = false;
            cut.previewCache.clear();
        }
    }
    app.selectedPartId = target->id;
    app.refreshMeshDiagnostics();
    app.showMeshIssues = !app.meshDiagnostics.validSolid();
    app.showRepairPrompt = !app.meshDiagnostics.validSolid();
    app.storeActiveObjectSession();
    app.dirty = true;
    app.repairTargetPartId = -1;

    if (app.meshDiagnostics.validSolid()) {
        app.status = result.message + " Das Modell ist jetzt ein wasserdichter Volumenkörper.";
        appLogInfo("Windows-Modellreparatur erfolgreich abgeschlossen.");
    } else {
        app.status = result.message + " Verbleibend: " +
            std::to_string(app.meshDiagnostics.problemEdgeCount()) + " problematische Kanten.";
        appLogWarning("Windows-Modellreparatur unvollständig: " +
                      std::to_string(app.meshDiagnostics.problemEdgeCount()) +
                      " problematische Kanten verbleiben.");
    }
}
