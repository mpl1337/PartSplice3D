#include "project_workflow.h"

#include "app_log.h"
#include "app_persistence.h"
#include "file_dialogs.h"
#include "step_io.h"
#include "stl_io.h"
#include "three_mf.h"
#include "worker_client.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <utility>

ProjectCutState projectCutFrom(const CutRecord& cut) {
    ProjectCutState stored;
    stored.id = cut.id;
    stored.sourcePartId = cut.sourcePartId;
    stored.sourceLabel = cut.sourceLabel;
    stored.childPartIds = cut.childPartIds;
    stored.childLabels = cut.childLabels;
    stored.points = cut.points;
    stored.pointLocked = cut.pointLocked;
    stored.placements = cut.placements;
    stored.settings = cut.settings;
    stored.mode = static_cast<int>(cut.mode);
    stored.cutAllActiveParts = cut.cutAllActiveParts;
    stored.forcedInvalidConnectors = cut.forcedInvalidConnectors;
    return stored;
}

CutRecord cutFromProject(const ProjectCutState& stored) {
    CutRecord cut;
    cut.id = stored.id;
    cut.sourcePartId = stored.sourcePartId;
    cut.sourceLabel = stored.sourceLabel;
    cut.childPartIds = stored.childPartIds;
    cut.childLabels = stored.childLabels;
    cut.points = stored.points;
    cut.pointLocked = stored.pointLocked;
    cut.placements = stored.placements;
    cut.settings = stored.settings;
    cut.mode = stored.mode == static_cast<int>(CutMode::Polyline) ? CutMode::Polyline : CutMode::Straight;
    cut.cutAllActiveParts = stored.cutAllActiveParts;
    cut.forcedInvalidConnectors = stored.forcedInvalidConnectors;
    return cut;
}

PartSpliceProject makeProjectDocument(App& app) {
    if (!app.storeActiveObjectSession())
        throw std::runtime_error("Der aktuelle Objektzustand konnte nicht für das Projekt übernommen werden.");
    PartSpliceProject document;
    // Absolute source paths are private machine data and must not be trusted
    // when a project is opened on another computer.
    document.originalPath.clear();
    document.originalFileName = app.originalFileName.empty()
        ? pathToUtf8(app.sourcePath.filename()) : app.originalFileName;
    document.originalFileData = app.originalFileData;
    document.sourceThreeMfProjectSettings = app.sourceThreeMfProjectSettings;
    document.materials = app.materials;
    document.threeMfCompatibilityWarnings = app.threeMfCompatibilityWarnings;
    document.objects.reserve(app.objectSessions.size());
    for (const ObjectSession& session : app.objectSessions) {
        ProjectObjectState object;
        object.name = session.name;
        object.plateIndex = session.plateIndex;
        object.selectedPartId = session.selectedPartId;
        object.nextPartId = session.nextPartId;
        object.nextCutId = session.nextCutId;
        object.parts.reserve(session.parts.size());
        for (const StoredPart& part : session.parts) {
            ProjectPartState stored;
            stored.id = part.id;
            stored.parentCutId = part.parentCutId;
            stored.label = part.label;
            stored.active = part.active;
            stored.meshReference = part.mesh;
            object.parts.push_back(std::move(stored));
        }
        object.cuts.reserve(session.cuts.size());
        for (const CutRecord& cut : session.cuts) object.cuts.push_back(projectCutFrom(cut));
        document.objects.push_back(std::move(object));
    }
    document.activeObjectIndex = app.activeObjectIndex;
    document.exportFormat = static_cast<int>(app.exportFormat);
    document.drawingLine = app.drawingLine;
    document.cutMode = static_cast<int>(app.cutMode);
    document.cutPoints = app.cutPoints;
    document.cutPointLocked = app.cutPointLocked;
    document.placements = app.placements;
    document.settings = app.settings;
    document.cutAllActiveParts = app.cutAllActiveParts;
    document.showSource = app.showSource;
    document.wireframe = app.wireframe;
    document.modelTransparency = app.modelTransparency;
    document.showPrintBed = app.showPrintBed;
    document.printBedWidth = app.printBedWidth;
    document.printBedDepth = app.printBedDepth;
    document.printBedCenter = app.printBedCenter;
    document.printBedRotation = app.printBedRotation;
    document.printBedPreset = app.printBedPreset;
    document.customBedWidth = app.customBedWidth;
    document.customBedDepth = app.customBedDepth;
    document.cameraTop = app.camera.top;
    document.cameraYaw = app.camera.yaw;
    document.cameraPitch = app.camera.pitch;
    document.cameraOrbitDistance = app.camera.orbitDistance;
    document.cameraTopHeight = app.camera.topHeight;
    document.cameraPan = app.camera.pan;
    return document;
}

bool saveProject(App& app, bool saveAs,
                 SaveContinuation continuation) {
    if (app.busy()) {
        app.status = "Projekt kann erst nach Abschluss des laufenden Vorgangs gespeichert werden.";
        return false;
    }
    if (!app.loaded) {
        app.status = "Es ist kein Projekt zum Speichern geöffnet.";
        return false;
    }
    if (app.pendingAppliedCutRebuildId >= 0) {
        app.status = "Projekt erst speichern, nachdem ‚Neu schneiden‘ die geänderten Verbinder übernommen hat.";
        return false;
    }
    try {
        std::filesystem::path target = app.projectPath;
        if (saveAs || target.empty()) {
            const std::wstring base = (app.sourcePath.empty() ? std::filesystem::path(L"PartSplice-Projekt")
                                                              : app.sourcePath.stem()).wstring() + L".ps3d";
            const auto selected = saveProjectDialog(base.c_str());
            if (!selected) {
                app.status = "Projekt speichern abgebrochen.";
                return false;
            }
            target = *selected;
        }
        if (app.originalFileData.empty()) {
            app.status = "Projekt konnte nicht gespeichert werden: Die ursprüngliche Eingangsdatei konnte nicht eingebettet werden.";
            return false;
        }
        PartSpliceProject document = makeProjectDocument(app);
        // Meshes are retained through shared immutable references. The embedded
        // input is moved into the task and returned with its result, so saving
        // never doubles hundreds of MiB on the UI thread.
        document.originalFileData.swap(app.originalFileData);
        document.sourceThreeMfProjectSettings.swap(app.sourceThreeMfProjectSettings);
        auto documentTask = std::make_shared<PartSpliceProject>(std::move(document));
        app.backgroundSaveDocument = documentTask;
        app.backgroundRunning = true;
        app.backgroundProgress = std::make_shared<BackgroundProgress>();
        app.backgroundStarted = std::chrono::steady_clock::now();
        app.backgroundKind = BackgroundTaskKind::SaveProject;
        app.backgroundLabel = "PartSplice-Projekt wird im Hintergrund gespeichert ...";
        app.backgroundSaveContinuation = continuation;
        app.backgroundCancel = std::make_shared<std::atomic_bool>(false);
        app.status = app.backgroundLabel;
        try {
            app.backgroundFuture = std::async(std::launch::async,
                [target, documentTask]() mutable {
                    BackgroundTaskResult task;
                    task.kind = BackgroundTaskKind::SaveProject;
                    task.projectPath = target;
                    try {
                        std::string error;
                        task.ok = savePartSpliceProject(target, *documentTask, error);
                        task.message = task.ok
                            ? "PartSplice-Projekt einschließlich Originaldatei gespeichert: " + pathToUtf8(target)
                            : "Projekt konnte nicht gespeichert werden: " + error;
                    } catch (const std::bad_alloc&) {
                        task.message = "Projekt konnte wegen Speichermangels nicht gespeichert werden.";
                    } catch (const std::exception& exception) {
                        task.message = std::string("Projekt konnte nicht gespeichert werden: ") + exception.what();
                    } catch (...) {
                        task.message = "Projekt konnte wegen eines unbekannten Fehlers nicht gespeichert werden.";
                    }
                    return task;
                });
        } catch (...) {
            app.backgroundRunning = false;
            app.backgroundKind = BackgroundTaskKind::None;
            app.backgroundLabel.clear();
            app.backgroundSaveContinuation = SaveContinuation::None;
            app.backgroundCancel.reset();
            app.originalFileData.swap(documentTask->originalFileData);
            app.sourceThreeMfProjectSettings.swap(documentTask->sourceThreeMfProjectSettings);
            app.backgroundSaveDocument.reset();
            throw;
        }
        return true;
    } catch (const std::bad_alloc&) {
        app.status = "Projekt konnte wegen Speichermangels nicht gespeichert werden.";
    } catch (const std::exception& exception) {
        app.status = std::string("Projekt konnte nicht gespeichert werden: ") + exception.what();
    } catch (...) {
        app.status = "Projekt konnte wegen eines unbekannten Fehlers nicht gespeichert werden.";
    }
    return false;
}

void applyProjectLoad(App& app, ProjectLoadResult&& result,
                      std::vector<std::vector<std::shared_ptr<const MeshSpatialIndex>>>&& spatialIndices,
                      const std::filesystem::path& path, double aspect) {
    if (!result.ok) { app.status = "Projekt laden fehlgeschlagen: " + result.message; return; }
    PartSpliceProject& document = result.project;
    app.newProject();
    app.projectPath = path;
    // Never follow a path supplied by a project.  The embedded original is the
    // authoritative source; only retain a harmless base name for UI/export.
    app.sourcePath = pathFromUtf8(document.originalFileName).filename();
    app.originalFileName = document.originalFileName;
    app.originalFileData = std::move(document.originalFileData);
    app.sourceThreeMfProjectSettings = std::move(document.sourceThreeMfProjectSettings);
    app.materials = std::move(document.materials);
    app.threeMfCompatibilityWarnings = std::move(document.threeMfCompatibilityWarnings);
    app.objectSessions.reserve(document.objects.size());
    for (size_t objectIndex = 0; objectIndex < document.objects.size(); ++objectIndex) {
        ProjectObjectState& storedObject = document.objects[objectIndex];
        ObjectSession session;
        session.name = std::move(storedObject.name);
        session.plateIndex = std::max(1, storedObject.plateIndex);
        session.selectedPartId = storedObject.selectedPartId;
        session.nextPartId = storedObject.nextPartId;
        session.nextCutId = storedObject.nextCutId;
        session.parts.reserve(storedObject.parts.size());
        for (size_t partIndex = 0; partIndex < storedObject.parts.size(); ++partIndex) {
            ProjectPartState& part = storedObject.parts[partIndex];
            std::shared_ptr<const TriangleMesh> mesh = part.meshReference
                ? std::move(part.meshReference)
                : std::make_shared<const TriangleMesh>(std::move(part.mesh));
            std::shared_ptr<const MeshSpatialIndex> spatialIndex;
            if (objectIndex < spatialIndices.size() &&
                partIndex < spatialIndices[objectIndex].size())
                spatialIndex = std::move(spatialIndices[objectIndex][partIndex]);
            if (!spatialIndex) spatialIndex = std::make_shared<const MeshSpatialIndex>(*mesh);
            session.parts.push_back({part.id, part.parentCutId, std::move(part.label),
                                     std::move(mesh), part.active, std::move(spatialIndex)});
        }
        session.cuts.reserve(storedObject.cuts.size());
        for (const ProjectCutState& cut : storedObject.cuts) session.cuts.push_back(cutFromProject(cut));
        app.objectSessions.push_back(std::move(session));
    }
    const int active = std::clamp(document.activeObjectIndex, 0,
                                  static_cast<int>(app.objectSessions.size()) - 1);
    app.activateObjectSession(static_cast<size_t>(active), aspect);
    app.exportFormat = document.exportFormat == static_cast<int>(ExportFormat::Stl)
        ? ExportFormat::Stl : ExportFormat::ThreeMf;
    app.drawingLine = document.drawingLine;
    app.cutMode = document.cutMode == static_cast<int>(CutMode::Polyline) ? CutMode::Polyline : CutMode::Straight;
    app.cutPoints = std::move(document.cutPoints);
    app.cutPointLocked = std::move(document.cutPointLocked);
    app.placements = std::move(document.placements);
    app.settings = document.settings;
    app.cutAllActiveParts = document.cutAllActiveParts;
    app.showSource = document.showSource;
    app.wireframe = document.wireframe;
    app.modelTransparency = std::clamp(document.modelTransparency, 0.0, 85.0);
    app.showPrintBed = document.showPrintBed;
    app.printBedWidth = document.printBedWidth;
    app.printBedDepth = document.printBedDepth;
    app.printBedCenter = document.printBedCenter;
    app.printBedRotation = document.printBedRotation;
    app.printBedPreset = document.printBedPreset;
    app.customBedWidth = document.customBedWidth;
    app.customBedDepth = document.customBedDepth;
    app.camera.top = document.cameraTop;
    app.camera.yaw = document.cameraYaw;
    app.camera.pitch = document.cameraPitch;
    app.camera.orbitDistance = document.cameraOrbitDistance;
    app.camera.topHeight = document.cameraTopHeight;
    app.camera.pan = document.cameraPan;
    app.updatePreview();
    app.undoStack.clear();
    app.dirty = false;
    rememberRecentFile(app.recentFiles, path);
    app.status = result.message + " Alle Schnitte und Verbinder können weiterbearbeitet werden.";
}

void applyModelLoad(App& app, LoadedModelPayload&& loaded, double aspect) {
    app.newProject();
    app.sourcePath = loaded.path;
    app.originalFileName = pathToUtf8(loaded.path.filename());
    app.originalFileData = std::move(loaded.originalFileData);
    app.sourceThreeMfProjectSettings = std::move(loaded.projectSettings);
    app.materials = std::move(loaded.materials);
    app.threeMfCompatibilityWarnings = std::move(loaded.compatibilityWarnings);
    app.exportFormat = loaded.exportFormat;
    app.objectSessions.reserve(loaded.objects.size());
    for (size_t index = 0; index < loaded.objects.size(); ++index) {
        ObjectSession session;
        session.name = loaded.objects[index].name.empty()
            ? "Objekt " + std::to_string(index + 1) : loaded.objects[index].name;
        session.plateIndex = std::max(1, loaded.objects[index].plateIndex);
        auto mesh = loaded.objects[index].meshReference
            ? std::move(loaded.objects[index].meshReference)
            : std::make_shared<const TriangleMesh>(std::move(loaded.objects[index].mesh));
        std::shared_ptr<const MeshSpatialIndex> spatialIndex;
        if (index < loaded.spatialIndices.size())
            spatialIndex = std::move(loaded.spatialIndices[index]);
        if (!spatialIndex) spatialIndex = std::make_shared<const MeshSpatialIndex>(*mesh);
        session.parts.push_back({1, -1, "Original", std::move(mesh), true,
                                 std::move(spatialIndex)});
        session.selectedPartId = 1;
        session.nextPartId = 2;
        app.objectSessions.push_back(std::move(session));
    }
    app.activateObjectSession(0, aspect);
    app.showSource = false;
    app.modelTransparency = 0.0;
    if (app.meshDiagnostics.validSolid()) {
        app.status = "Geladen: " + loaded.message + " — Modellprüfung bestanden: wasserdichter Volumenkörper.";
        appLogInfo("Modell geladen und geprüft: " + pathToUtf8(app.sourcePath));
    } else {
        app.showRepairPrompt = true;
        app.status = "Geladen: " + loaded.message + " — Modellprüfung: Reparatur erforderlich.";
        appLogWarning("Modellreparatur empfohlen für " + pathToUtf8(app.sourcePath) +
                      ": " + std::to_string(app.meshDiagnostics.problemEdgeCount()) +
                      " problematische Kanten.");
    }
    if (app.originalFileData.empty())
        app.status += " Die Quelldatei konnte nicht für ein späteres PS3D-Projekt eingebettet werden.";
    if (!app.threeMfCompatibilityWarnings.empty())
        app.status += " Kompatibilitätshinweise: " +
                      std::to_string(app.threeMfCompatibilityWarnings.size()) +
                      " (unter Informationen > Modellstatus & Details).";
    app.dirty = false;
    app.undoStack.clear();
    rememberRecentFile(app.recentFiles, loaded.path);
}

BackgroundTaskResult loadFileInBackground(const std::filesystem::path& path,
                                          const std::atomic_bool* cancelRequested,
                                          const std::shared_ptr<BackgroundProgress>& progress) {
    BackgroundTaskResult task;
    task.kind = BackgroundTaskKind::LoadFile;
    try {
        if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
            task.canceled = true;
            task.message = "Ladevorgang abgebrochen.";
            return task;
        }
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (extension == ".ps3d") {
            task.projectPath = path;
            task.project = loadPartSpliceProject(path, cancelRequested);
            task.ok = task.project.ok;
            task.message = task.project.message;
            if (task.ok) {
                size_t partCount = 0;
                for (const ProjectObjectState& object : task.project.project.objects)
                    partCount += object.parts.size();
                if (progress) {
                    progress->completed.store(0, std::memory_order_relaxed);
                    progress->total.store(partCount, std::memory_order_relaxed);
                }
                task.projectSpatialIndices.resize(task.project.project.objects.size());
                for (size_t objectIndex = 0;
                     objectIndex < task.project.project.objects.size(); ++objectIndex) {
                    ProjectObjectState& object = task.project.project.objects[objectIndex];
                    auto& indices = task.projectSpatialIndices[objectIndex];
                    indices.reserve(object.parts.size());
                    for (ProjectPartState& part : object.parts) {
                        if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
                            task.ok = false;
                            task.canceled = true;
                            task.message = "Ladevorgang abgebrochen.";
                            return task;
                        }
                        std::shared_ptr<const TriangleMesh> mesh = part.meshReference
                            ? part.meshReference
                            : std::make_shared<const TriangleMesh>(std::move(part.mesh));
                        part.meshReference = mesh;
                        indices.push_back(std::make_shared<const MeshSpatialIndex>(*mesh));
                        if (progress) progress->completed.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        } else {
            LoadedModelPayload loaded;
            loaded.path = path;
            if (extension == ".3mf") {
                ThreeMfLoadResult result = loadThreeMf(path, cancelRequested);
                if (!result.ok) { task.message = "Laden fehlgeschlagen: " + result.message; return task; }
                loaded.objects = std::move(result.objects);
                loaded.originalFileData = std::move(result.originalFileData);
                loaded.projectSettings = std::move(result.projectSettings);
                loaded.materials = std::move(result.materials);
                loaded.compatibilityWarnings = std::move(result.compatibilityWarnings);
                loaded.message = result.message;
                loaded.exportFormat = ExportFormat::ThreeMf;
            } else if (extension == ".step" || extension == ".stp") {
                StepLoadResult result = loadStepIsolated(path, cancelRequested);
                if (!result.ok) { task.message = "Laden fehlgeschlagen: " + result.message; return task; }
                loaded.objects = std::move(result.objects);
                loaded.message = result.message;
                loaded.exportFormat = ExportFormat::Stl;
            } else if (extension == ".stl") {
                StlLoadResult result = loadStl(path, cancelRequested);
                if (!result.ok) { task.message = "Laden fehlgeschlagen: " + result.message; return task; }
                loaded.objects.push_back({pathToUtf8(path.stem()), std::move(result.mesh)});
                loaded.message = result.message;
                if (result.nonManifoldEdges > 0)
                    loaded.message += " — CSG kann nur mit einem wasserdichten Modell ausgeführt werden.";
                loaded.exportFormat = ExportFormat::Stl;
            } else {
                task.message = "Laden fehlgeschlagen: Unterstützt werden STL, 3MF, STEP, STP und PS3D.";
                return task;
            }
            if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
                task.canceled = true;
                task.message = "Ladevorgang abgebrochen.";
                return task;
            }
            if (loaded.originalFileData.empty()) loaded.originalFileData = readWholeFile(path);
            if (loaded.objects.empty()) {
                task.message = "Laden fehlgeschlagen: Die Datei enthält keine verwendbaren Objekte.";
                return task;
            }
            loaded.spatialIndices.reserve(loaded.objects.size());
            if (progress) {
                progress->completed.store(0, std::memory_order_relaxed);
                progress->total.store(loaded.objects.size(), std::memory_order_relaxed);
            }
            for (ThreeMfObject& object : loaded.objects) {
                if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
                    task.canceled = true;
                    task.message = "Ladevorgang abgebrochen.";
                    return task;
                }
                std::shared_ptr<const TriangleMesh> mesh = object.meshReference
                    ? object.meshReference
                    : std::make_shared<const TriangleMesh>(std::move(object.mesh));
                object.meshReference = mesh;
                loaded.spatialIndices.push_back(
                    std::make_shared<const MeshSpatialIndex>(*mesh));
                if (progress) progress->completed.fetch_add(1, std::memory_order_relaxed);
            }
            task.model = std::move(loaded);
            task.ok = true;
            task.message = task.model.message;
        }
    } catch (const std::bad_alloc&) {
        task.message = "Nicht genügend Arbeitsspeicher zum Laden der Datei.";
    } catch (const std::exception& exception) {
        task.message = std::string("Laden fehlgeschlagen: ") + exception.what();
    } catch (...) {
        task.message = "Laden ist mit einem unbekannten Fehler fehlgeschlagen.";
    }
    if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
        task.ok = false;
        task.canceled = true;
        task.message = "Ladevorgang abgebrochen.";
    }
    return task;
}

void loadFileOrProject(App& app, const std::filesystem::path& path, double aspect) {
    if (app.busy()) {
        app.status = "Es läuft bereits ein Vorgang.";
        return;
    }
    app.backgroundRunning = true;
    app.backgroundProgress = std::make_shared<BackgroundProgress>();
    app.backgroundStarted = std::chrono::steady_clock::now();
    app.backgroundKind = BackgroundTaskKind::LoadFile;
    app.backgroundLabel = "Datei wird geladen und geprüft ...";
    app.backgroundAspect = aspect;
    app.backgroundCancel = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = app.backgroundCancel;
    const std::shared_ptr<BackgroundProgress> progress = app.backgroundProgress;
    app.status = app.backgroundLabel;
    try {
        app.backgroundFuture = std::async(std::launch::async, [path, cancel, progress]() {
            return loadFileInBackground(path, cancel.get(), progress);
        });
    } catch (const std::exception& exception) {
        app.backgroundRunning = false;
        app.backgroundKind = BackgroundTaskKind::None;
        app.backgroundLabel.clear();
        app.backgroundCancel.reset();
        app.status = std::string("Ladevorgang konnte nicht gestartet werden: ") + exception.what();
    } catch (...) {
        app.backgroundRunning = false;
        app.backgroundKind = BackgroundTaskKind::None;
        app.backgroundLabel.clear();
        app.backgroundCancel.reset();
        app.status = "Ladevorgang konnte nicht gestartet werden.";
    }
}
