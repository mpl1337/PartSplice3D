#pragma once

#include "app_domain.h"
#include "background_progress.h"
#include "model_projection.h"
#include "update_check.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

bool polylineHitsPartProjection(const PartRecord& part, const std::vector<Vec2>& points);

struct App {
    std::shared_ptr<const TriangleMesh> source;
    MeshDisplayList sourceGL;
    std::vector<PartRecord> parts;
    std::vector<CutRecord> cuts;
    int selectedPartId = -1;
    int nextPartId = 1;
    int nextCutId = 1;
    bool loaded = false;
    bool generated = false;
    bool drawingLine = false;
    CutMode cutMode = CutMode::Straight;
    std::vector<Vec2> cutPoints;
    std::vector<bool> cutPointLocked;
    DovetailSettings settings;
    std::vector<ConnectorPlacement> placements;
    std::vector<ConnectorPreview> preview;
    std::string status = "Oben links unter Datei / Projekt eine STL-, 3MF- oder STEP-Datei öffnen.";
    std::filesystem::path sourcePath;
    std::filesystem::path projectPath;
    std::string originalFileName;
    std::vector<uint8_t> originalFileData;
    std::string sourceThreeMfProjectSettings;
    std::vector<MeshMaterial> materials;
    std::vector<std::string> threeMfCompatibilityWarnings;
    Camera camera;
    bool showSource = true;
    bool wireframe = false;
    double modelTransparency = 0.0;
    double explode = 0.0;
    DragTarget dragTarget = DragTarget::None;
    int draggedCutPoint = -1;
    int draggedConnector = -1;
    int draggedAppliedCutId = -1;
    int draggedAppliedConnector = -1;
    // Placements of this already generated cut have been edited, while its
    // expensive result meshes still represent the previous positions.
    int pendingAppliedCutRebuildId = -1;
    int draggedAssemblyMarkSide = -1;
    int selectedConnector = -1;
    bool blockRightOrbit = false;
    bool cutAllActiveParts = true;
    bool showPrintBed = false;
    double printBedWidth = 256.0;
    double printBedDepth = 256.0;
    Vec2 printBedCenter{};
    Vec2 printBedDragOffset{};
    double printBedRotation = 0.0;
    std::vector<ObjectSession> objectSessions;
    int activeObjectIndex = -1;
    ExportFormat exportFormat = ExportFormat::ThreeMf;
    std::vector<UndoEntry> undoStack;
    bool dirty = false;
    bool showExitConfirmation = false;
    bool exitAfterSave = false;
    SaveContinuation backgroundSaveContinuation = SaveContinuation::None;
    bool openViewportContext = false;
    int contextCutPoint = -1;
    int contextLineSegment = -1;
    int contextConnector = -1;
    std::vector<int> contextConnectorCandidates;
    std::vector<int> connectorHitCycle;
    Vec2 connectorHitCyclePoint{};
    size_t connectorHitCycleIndex = 0;
    bool contextModelSurface = false;
    Vec2 contextWorld{};
    int printBedPreset = 0;
    double customBedWidth = 256.0;
    double customBedDepth = 256.0;
    std::optional<ConnectorPlacement> connectorClipboard;
    bool measureMode = false;
    std::vector<Vec2> measurePoints;
    MeshDiagnostics meshDiagnostics;
    bool showMeshIssues = false;
    bool backgroundRunning = false;
    BackgroundTaskKind backgroundKind = BackgroundTaskKind::None;
    std::string backgroundLabel;
    std::shared_ptr<std::atomic_bool> backgroundCancel;
    std::future<BackgroundTaskResult> backgroundFuture;
    std::shared_ptr<BackgroundProgress> backgroundProgress;
    std::chrono::steady_clock::time_point backgroundStarted{};
    std::shared_ptr<PartSpliceProject> backgroundSaveDocument;
    double backgroundAspect = 1.0;
    GenerateContinuation backgroundContinuation = GenerateContinuation::None;
    int backgroundReplaceOperationId = -1;
    std::optional<PendingCutRestoreState> backgroundPendingCutRestore;
    bool repairRunning = false;
    int repairTargetPartId = -1;
    std::future<IndexedRepairResult> repairFuture;
    std::shared_ptr<std::atomic_bool> repairCancel;
    bool closeAfterRepair = false;
    bool closeAfterBackground = false;
    bool showRepairPrompt = false;
    bool showProjectConfirmation = false;
    PendingProjectAction pendingProjectAction = PendingProjectAction::None;
    std::filesystem::path pendingOpenPath;
    std::vector<std::filesystem::path> recentFiles;
    bool showModelInfo = false;
    bool showConnectorEditor = false;
    bool repositionConnectorEditor = false;
    ImVec2 connectorEditorPosition{420.0f, 120.0f};
    std::vector<Vec2> cutPointDragConnectorCenters;
    bool showConnectorCalibration = false;
    bool connectorCalibrationConfigured = false;
    DovetailSettings calibrationSettings;
    double calibrationStart = 0.0;
    double calibrationStep = 0.025;
    int calibrationCount = 5;
    int calibrationSelected = 2;
    int calibrationPrinterPreset = 8;
    std::vector<int> calibrationRatings;
    bool showPendingCutPrompt = false;
    CutMode pendingCutMode = CutMode::Straight;
    bool showStartupPrompt = true;
    bool showObjectSwitchPrompt = false;
    int pendingObjectIndex = -1;
    double pendingObjectAspect = 1.0;
    bool showInvalidConnectorPrompt = false;
    bool showColorLossPrompt = false;
    bool colorLossExportConfirmed = false;
    size_t invalidConnectorCount = 0;
    size_t partiallyValidConnectorCount = 0;
    size_t warningConnectorCount = 0;
    bool snapToGrid = false;
    double snapGridStep = 1.0;
    bool snapToAngles = false;
    double snapAngleStep = 15.0;
    bool snapToModelEdge = true;
    bool showPrecisionSettings = false;
    bool showAssemblyMarkEditor = false;
    bool showCutPointCoordinates = false;
    int coordinateCutPoint = -1;
    double coordinateX = 0.0;
    double coordinateY = 0.0;
    GenerateContinuation generateContinuation = GenerateContinuation::None;
    bool showEditCutPrompt = false;
    int pendingEditCutId = -1;
    int editingCutOperationId = -1;
    std::vector<int> contextAppliedCutIds;
    bool updateCheckRunning = false;
    bool updateCheckManual = false;
    bool showUpdateResult = false;
    std::future<UpdateCheckResult> updateFuture;
    UpdateCheckResult updateResult;

    bool busy() const { return backgroundRunning || repairRunning; }

    void fitCamera(double aspect) {
        if (!loaded || !source || !source->bounds.valid()) return;
        AABB fitted = source->bounds;
        if (showPrintBed) {
            const double halfW = printBedWidth * 0.5;
            const double halfD = printBedDepth * 0.5;
            const double angle = printBedRotation * std::numbers::pi / 180.0;
            const double cosine = std::cos(angle);
            const double sine = std::sin(angle);
            for (Vec2 local : {Vec2{-halfW, -halfD}, Vec2{halfW, -halfD},
                               Vec2{halfW, halfD}, Vec2{-halfW, halfD}}) {
                const Vec2 rotated{local.x * cosine - local.y * sine,
                                   local.x * sine + local.y * cosine};
                fitted.expand({printBedCenter.x + rotated.x, printBedCenter.y + rotated.y,
                               source->bounds.min.z});
            }
        }
        const Vec3 s = fitted.size();
        const double fitY = std::max(s.y, s.x / std::max(aspect, 0.1));
        camera.topHeight = std::max(fitY * 1.25, 10.0);
        camera.orbitDistance = std::max(fitted.diagonal() * 1.8, 20.0);
        const Vec3 fittedCenter = fitted.center();
        const Vec3 sourceCenter = source->bounds.center();
        camera.pan = {fittedCenter.x - sourceCenter.x, fittedCenter.y - sourceCenter.y};
    }

    bool storeActiveObjectSession() {
        if (activeObjectIndex < 0 || static_cast<size_t>(activeObjectIndex) >= objectSessions.size() || !loaded)
            return true;
        std::vector<StoredPart> stagedParts;
        std::vector<CutRecord> stagedCuts;
        try {
            stagedParts.reserve(parts.size());
            for (const auto& part : parts)
                stagedParts.push_back({part.id, part.parentCutId, part.label, part.mesh,
                                       part.active, part.spatialIndex});
            stagedCuts = cuts;
        } catch (const std::bad_alloc&) {
            status = "Der aktuelle Objektzustand konnte wegen Speichermangels nicht synchronisiert werden.";
            return false;
        } catch (const std::exception& exception) {
            status = std::string("Der aktuelle Objektzustand konnte nicht synchronisiert werden: ") + exception.what();
            return false;
        } catch (...) {
            status = "Der aktuelle Objektzustand konnte wegen eines unbekannten Fehlers nicht synchronisiert werden.";
            return false;
        }
        ObjectSession& session = objectSessions[static_cast<size_t>(activeObjectIndex)];
        session.parts = std::move(stagedParts);
        session.cuts = std::move(stagedCuts);
        session.selectedPartId = selectedPartId;
        session.nextPartId = nextPartId;
        session.nextCutId = nextCutId;
        return true;
    }

    void activateObjectSession(size_t index, double aspect) {
        if (index >= objectSessions.size()) return;
        if (pendingAppliedCutRebuildId >= 0 && activeObjectIndex >= 0 &&
            static_cast<size_t>(activeObjectIndex) != index) {
            status = "Zuerst den geänderten Schnitt mit ‚Neu schneiden‘ übernehmen oder mit Strg+Z verwerfen.";
            return;
        }
        const bool firstActivation = activeObjectIndex < 0 || !loaded;
        if (!storeActiveObjectSession()) return;
        activeObjectIndex = static_cast<int>(index);
        const ObjectSession& session = objectSessions[index];
        if (session.parts.empty() || !session.parts.front().mesh) return;
        source = session.parts.front().mesh;
        sourceGL.set(*source);
        parts.clear();
        parts.reserve(session.parts.size());
        for (const auto& stored : session.parts) {
            PartRecord part;
            part.id = stored.id;
            part.parentCutId = stored.parentCutId;
            part.label = stored.label;
            part.mesh = stored.mesh;
            if (!part.mesh) continue;
            part.gl.set(*part.mesh);
            part.active = stored.active;
            part.spatialIndex = stored.spatialIndex
                ? stored.spatialIndex
                : std::make_shared<const MeshSpatialIndex>(*part.mesh);
            parts.push_back(std::move(part));
        }
        cuts = session.cuts;
        selectedPartId = session.selectedPartId;
        nextPartId = session.nextPartId;
        nextCutId = session.nextCutId;
        loaded = true;
        generated = !cuts.empty();
        clearCurrentCut();
        if (!firstActivation) {
            undoStack.clear();
        }
        explode = 0.0;
        if (firstActivation) printBedCenter = {source->bounds.center().x, source->bounds.center().y};
        status = "Objekt ausgewählt: " + session.name;
        refreshMeshDiagnostics();
        showMeshIssues = !meshDiagnostics.validSolid();
        showRepairPrompt = !meshDiagnostics.validSolid();
        fitCamera(aspect);
    }

    void newProject() {
        pendingAppliedCutRebuildId = -1;
        clearCurrentCut();
        settings.assemblyMarks = true;
        settings.assemblyMarkPositionsCustom = false;
        sourceGL.clear();
        source.reset();
        parts.clear();
        cuts.clear();
        objectSessions.clear();
        materials.clear();
        threeMfCompatibilityWarnings.clear();
        activeObjectIndex = -1;
        selectedPartId = -1;
        loaded = false;
        generated = false;
        sourcePath.clear();
        projectPath.clear();
        originalFileName.clear();
        originalFileData.clear();
        sourceThreeMfProjectSettings.clear();
        undoStack.clear();
        dirty = false;
        meshDiagnostics = {};
        showMeshIssues = false;
        showRepairPrompt = false;
        showProjectConfirmation = false;
        showColorLossPrompt = false;
        colorLossExportConfirmed = false;
        pendingProjectAction = PendingProjectAction::None;
        pendingOpenPath.clear();
        modelTransparency = 0.0;
        status = "Neues Projekt. Jetzt eine STL-, 3MF- oder STEP-Datei öffnen.";
    }

    void captureUndo(const std::string& label, UndoKind kind = UndoKind::Edit,
                     int operationId = -1, bool structural = false) {
        // All connector moves made before "Neu schneiden" form one atomic
        // edit. A later small UI snapshot would otherwise contain the new
        // placements together with the old result meshes and could restore an
        // inconsistent project state.
        if (pendingAppliedCutRebuildId >= 0) return;
        UndoEntry entry;
        entry.kind = kind;
        entry.label = label;
        entry.operationId = operationId;
        entry.selectedConnector = selectedConnector;
        entry.drawingLine = drawingLine;
        entry.cutMode = cutMode;
        entry.cutPoints = cutPoints;
        entry.cutPointLocked = cutPointLocked;
        entry.placements = placements;
        entry.settings = settings;
        entry.showPrintBed = showPrintBed;
        entry.printBedWidth = printBedWidth;
        entry.printBedDepth = printBedDepth;
        entry.printBedCenter = printBedCenter;
        entry.printBedRotation = printBedRotation;
        entry.selectedPartId = selectedPartId;
        entry.nextPartId = nextPartId;
        entry.nextCutId = nextCutId;
        if (structural) {
            undoStack.erase(std::remove_if(undoStack.begin(), undoStack.end(), [](const UndoEntry& old) {
                return old.kind == UndoKind::DeletedCut;
            }), undoStack.end());
            entry.structuralParts.reserve(parts.size());
            for (const auto& part : parts)
                entry.structuralParts.push_back(
                    {part.id, part.parentCutId, part.label, part.mesh,
                     part.active, part.spatialIndex});
            entry.structuralCuts = cuts;
        }
        undoStack.push_back(std::move(entry));
        auto historyMeshBytes = [&]() {
            uint64_t bytes = 0;
            std::unordered_set<const TriangleMesh*> counted;
            std::unordered_set<const MeshSpatialIndex*> countedIndices;
            for (const UndoEntry& undo : undoStack) {
                for (const StoredPartSnapshot& part : undo.structuralParts) {
                    if (part.mesh && counted.insert(part.mesh.get()).second)
                        bytes += meshStorageBytes(*part.mesh);
                    if (part.spatialIndex && countedIndices.insert(part.spatialIndex.get()).second)
                        bytes += part.spatialIndex->storageBytes();
                }
            }
            return bytes;
        };
        constexpr uint64_t kUndoMeshBudget = 768ull * 1024ull * 1024ull;
        while (undoStack.size() > 30 ||
               (undoStack.size() > 1 && historyMeshBytes() > kUndoMeshBudget))
            undoStack.erase(undoStack.begin());
    }

    void restoreSmallState(const UndoEntry& entry) {
        cutPoints = entry.cutPoints;
        cutPointLocked = entry.cutPointLocked;
        placements = entry.placements;
        selectedConnector = entry.selectedConnector;
        settings = entry.settings;
        showPrintBed = entry.showPrintBed;
        printBedWidth = entry.printBedWidth;
        printBedDepth = entry.printBedDepth;
        printBedCenter = entry.printBedCenter;
        printBedRotation = entry.printBedRotation;
        drawingLine = entry.drawingLine;
        cutMode = entry.cutMode;
        dragTarget = DragTarget::None;
        invalidateResult();
        updatePreview();
    }

    void invalidateResult() {
        generated = false;
    }

    PartRecord* selectedPart() {
        for (auto& part : parts) if (part.id == selectedPartId && part.active) return &part;
        return nullptr;
    }

    const PartRecord* selectedPart() const {
        for (const auto& part : parts) if (part.id == selectedPartId && part.active) return &part;
        return nullptr;
    }

    void refreshMeshDiagnostics() {
        const PartRecord* part = selectedPart();
        meshDiagnostics = part != nullptr && part->mesh ? analyzeMesh(*part->mesh) : MeshDiagnostics{};
        if (meshDiagnostics.validSolid()) showMeshIssues = false;
    }

    size_t activePartCount() const {
        size_t count = 0;
        for (const auto& part : parts) if (part.active) ++count;
        return count;
    }

    void clearCurrentCut() {
        drawingLine = false;
        cutPoints.clear();
        cutPointLocked.clear();
        placements.clear();
        preview.clear();
        dragTarget = DragTarget::None;
        draggedCutPoint = -1;
        draggedConnector = -1;
        draggedAppliedCutId = -1;
        draggedAppliedConnector = -1;
        draggedAssemblyMarkSide = -1;
        selectedConnector = -1;
        showConnectorEditor = false;
        cutPointDragConnectorCenters.clear();
        editingCutOperationId = -1;
    }

    void selectPart(int id) {
        if (selectedPartId == id) return;
        for (const auto& part : parts) {
            if (part.id == id && part.active) {
                selectedPartId = id;
                clearCurrentCut();
                explode = 0.0;
                status = part.label + " ausgewählt. Hier kann der nächste Schnitt gezeichnet werden.";
                refreshMeshDiagnostics();
                showMeshIssues = !meshDiagnostics.validSolid();
                return;
            }
        }
    }

    void updatePreview(bool validateMaterial = true) {
        preview.clear();
        if (drawingLine || cutPoints.size() < 2) return;
        DovetailSettings previewSettings = settings;
        if (!validateMaterial) previewSettings.validateInsideModel = false;
        else if (cutAllActiveParts) previewSettings.validateInsideModel = true;
        for (const auto& part : parts) {
            if (!part.active || !part.mesh || (!cutAllActiveParts && part.id != selectedPartId)) continue;
            if (cutAllActiveParts && !polylineHitsPartProjection(part, cutPoints)) continue;
            std::vector<ConnectorPreview> local = makeConnectorPreview(*part.mesh, cutPoints, previewSettings, placements);
            if (preview.empty()) {
                preview = std::move(local);
            } else {
                const size_t count = std::min(preview.size(), local.size());
                for (size_t i = 0; i < count; ++i) {
                    const bool wasValidForEveryTarget = preview[i].valid;
                    preview[i].partiallyValid = preview[i].partiallyValid ||
                        (wasValidForEveryTarget != local[i].valid);
                    // A connector is globally valid only when it can be created
                    // in every part hit by the same cut operation.
                    preview[i].valid = wasValidForEveryTarget && local[i].valid;
                    preview[i].overlap = preview[i].overlap || local[i].overlap;
                    preview[i].crossesCutCorner = preview[i].crossesCutCorner ||
                                                  local[i].crossesCutCorner;
                    preview[i].thinWall = preview[i].thinWall || local[i].thinWall;
                }
            }
        }
        markConnectorCutConflicts();
    }

    size_t initializeConnectors() {
        placements.clear();
        const std::vector<ConnectorPlacement> defaults =
            (!drawingLine && cutPoints.size() >= 2)
                ? makeDefaultConnectorPlacements(cutPoints, settings)
                : std::vector<ConnectorPlacement>{};
        for (const ConnectorPlacement& prototype : defaults) {
            if (const std::optional<double> position =
                    smartConnectorPosition(prototype, prototype.position)) {
                ConnectorPlacement placement = prototype;
                placement.position = *position;
                placements.push_back(placement);
            }
        }
        settings.count = std::max(1, static_cast<int>(placements.size()));
        invalidateResult();
        updatePreview();
        return defaults.size();
    }

    void adjustConnectorCount(int requestedCount) {
        const int target = std::clamp(requestedCount, 1, 100);
        settings.count = target;
        if (!hasCompleteCut()) return;
        while (static_cast<int>(placements.size()) > target) placements.pop_back();
        if (selectedConnector >= static_cast<int>(placements.size()))
            selectedConnector = placements.empty() ? -1 : static_cast<int>(placements.size()) - 1;
        const double total = polylineLength(cutPoints);
        const double edge = std::max(0.5 * (settings.headWidth + 2.0 * settings.clearance), 0.5);
        const double limit = total > 1e-9 ? std::min(edge / total, 0.49) : 0.49;
        while (static_cast<int>(placements.size()) < target) {
            std::vector<double> occupied;
            occupied.reserve(placements.size() + 2);
            occupied.push_back(limit);
            for (const auto& placement : placements)
                occupied.push_back(std::clamp(placement.position, limit, 1.0 - limit));
            occupied.push_back(1.0 - limit);
            std::sort(occupied.begin(), occupied.end());
            double bestGap = -1.0;
            double bestPosition = 0.5;
            for (size_t i = 1; i < occupied.size(); ++i) {
                const double gap = occupied[i] - occupied[i - 1];
                if (gap > bestGap) {
                    bestGap = gap;
                    bestPosition = 0.5 * (occupied[i] + occupied[i - 1]);
                }
            }
            ConnectorPlacement placement;
            placement.position = bestPosition;
            placement.maleOnLeft = settings.maleOnLeft;
            placement.type = settings.type;
            placements.push_back(placement);
        }
        invalidateResult();
        updatePreview();
    }

    double freeConnectorPosition() const {
        if (placements.empty()) return 0.5;
        std::vector<double> occupied;
        occupied.reserve(placements.size() + 2);
        occupied.push_back(0.02);
        for (const auto& placement : placements) occupied.push_back(std::clamp(placement.position, 0.02, 0.98));
        occupied.push_back(0.98);
        std::sort(occupied.begin(), occupied.end());
        double bestGap = -1.0;
        double best = 0.5;
        for (size_t index = 1; index < occupied.size(); ++index) {
            const double gap = occupied[index] - occupied[index - 1];
            if (gap > bestGap) { bestGap = gap; best = 0.5 * (occupied[index] + occupied[index - 1]); }
        }
        return best;
    }

    void addConnector(std::optional<ConnectorPlacement> prototype = std::nullopt,
                      std::optional<double> requestedPosition = std::nullopt) {
        if (!hasCompleteCut()) return;
        ConnectorPlacement placement;
        if (prototype) placement = *prototype;
        else {
            placement.maleOnLeft = settings.maleOnLeft;
            placement.type = settings.type;
        }
        placement.position = smartConnectorPosition(placement, requestedPosition)
            .value_or(std::clamp(requestedPosition.value_or(freeConnectorPosition()), 0.01, 0.99));
        placement.locked = false;
        placements.push_back(placement);
        settings.count = static_cast<int>(placements.size());
        selectedConnector = static_cast<int>(placements.size()) - 1;
        invalidateResult();
        updatePreview();
    }

    std::optional<double> smartConnectorPosition(
        const ConnectorPlacement& prototype,
        std::optional<double> preferred = std::nullopt) const;
    size_t autoPlaceProblemConnectors();
    void markConnectorCutConflicts();
    Vec2 snappedCutPoint(Vec2 raw, std::optional<Vec2> anchor,
                         double modelSnapTolerance) const;
    bool alignCutSegment(size_t segmentIndex, double degrees);

    void adjustConnectorsForCurrentDistribution() {
        if (settings.distribution == DovetailSettings::Distribution::Count) {
            adjustConnectorCount(settings.count);
        } else {
            const int desired = std::max(1, static_cast<int>(makeDefaultConnectorPlacements(cutPoints, settings).size()));
            adjustConnectorCount(desired);
        }
    }

    bool hasCompleteCut() const { return !drawingLine && cutPoints.size() >= 2; }

    int nextAssemblyMarkCode() const {
        std::unordered_set<int> used;
        for (const CutRecord& cut : cuts)
            if (cut.settings.assemblyMarkCode > 0)
                used.insert(cut.settings.assemblyMarkCode);
        for (int candidate = 1; candidate < 1'000'000'000; ++candidate)
            if (!used.contains(candidate)) return candidate;
        return 1;
    }

    int activeAssemblyMarkCode() const {
        return editingCutOperationId >= 0
            ? std::max(settings.assemblyMarkCode, 1)
            : nextAssemblyMarkCode();
    }

    bool pointInsideActiveProjection(Vec2 point) const {
        for (const auto& part : parts) {
            if (!part.active || !part.mesh || (!cutAllActiveParts && part.id != selectedPartId)) continue;
            if (part.spatialIndex
                    ? pointInsideMeshProjection(*part.spatialIndex, point)
                    : pointInsideMeshProjection(*part.mesh, point)) return true;
        }
        return false;
    }

    Vec2 closestModelEdgePoint(Vec2 point) const {
        Vec2 best = point;
        double bestDistance = std::numeric_limits<double>::infinity();
        for (const auto& part : parts) {
            if (!part.active || !part.mesh || (!cutAllActiveParts && part.id != selectedPartId)) continue;
            Vec2 candidate = point;
            const double distance = part.spatialIndex
                ? closestPointOnMeshProjectionBoundary(*part.spatialIndex, point, candidate)
                : closestPointOnMeshProjectionBoundary(*part.mesh, point, candidate);
            if (distance < bestDistance) { bestDistance = distance; best = candidate; }
        }
        return best;
    }

    bool snapOutsideCutPoints() {
        bool snapped = false;
        for (size_t index = 0; index < cutPoints.size(); ++index) {
            if (index < cutPointLocked.size() && cutPointLocked[index]) continue;
            if (!pointInsideActiveProjection(cutPoints[index])) {
                cutPoints[index] = closestModelEdgePoint(cutPoints[index]);
                snapped = true;
            }
        }
        return snapped;
    }

    void beginCut(CutMode mode) {
        if (!selectedPart()) {
            status = "Zuerst ein aktuelles Teil im Baum auswählen.";
            return;
        }
        camera.top = true;
        explode = 0.0;
        cutMode = mode;
        drawingLine = true;
        cutPoints.clear();
        cutPointLocked.clear();
        dragTarget = DragTarget::None;
        draggedCutPoint = -1;
        draggedConnector = -1;
        placements.clear();
        invalidateResult();
        preview.clear();
        status = mode == CutMode::Straight
            ? "Start- und Endpunkt der geraden Schnittlinie anklicken."
            : "Eckpunkte anklicken. Mit Rechtsklick oder der Schaltfläche den Linienzug abschließen.";
    }

    void finishCut() {
        if (cutPointLocked.size() < cutPoints.size()) cutPointLocked.resize(cutPoints.size(), false);
        const bool snapped = snapOutsideCutPoints();
        std::string validation;
        if (!validateCutPolyline(cutPoints, validation)) {
            status = validation;
            return;
        }
        drawingLine = false;
        const size_t requestedConnectors = initializeConnectors();
        status = cutPoints.size() == 2
            ? "Gerade Schnittlinie gesetzt. Punkte und Verbinder können gezogen werden."
            : "Mehrpunkt-Schnitt gesetzt. Alle Eckpunkte und Verbinder können gezogen werden.";
        if (placements.size() < requestedConnectors) {
            status += " " + std::to_string(placements.size()) + " von " +
                      std::to_string(requestedConnectors) +
                      " Verbindern wurden an gültigen Positionen platziert; für weitere war kein sicherer Platz vorhanden.";
        }
        if (snapped) status += " Außen liegende Punkte wurden am Modellrand eingerastet.";
    }

    void insertCutPoint(size_t segmentIndex, Vec2 point) {
        if (!hasCompleteCut() || segmentIndex + 1 >= cutPoints.size()) return;
        if (cutPointLocked.size() < cutPoints.size()) cutPointLocked.resize(cutPoints.size(), false);
        std::vector<Vec2> connectorCenters;
        connectorCenters.reserve(preview.size());
        for (const auto& connector : preview) connectorCenters.push_back(connector.center);
        cutPoints.insert(cutPoints.begin() + static_cast<std::ptrdiff_t>(segmentIndex + 1), point);
        cutPointLocked.insert(cutPointLocked.begin() + static_cast<std::ptrdiff_t>(segmentIndex + 1), false);
        for (size_t i = 0; i < placements.size() && i < connectorCenters.size(); ++i)
            placements[i].position = closestPositionOnPolyline(cutPoints, connectorCenters[i]);
        invalidateResult();
        updatePreview();
        status = "Neuer Knickpunkt eingefügt. Der rote Griff kann jetzt verschoben werden.";
    }

    void removeCutPoint(size_t pointIndex) {
        if (!hasCompleteCut() || cutPoints.size() <= 2 || pointIndex == 0 || pointIndex + 1 >= cutPoints.size()) {
            status = "Nur innere Knickpunkte können gelöscht werden; Start und Ende bleiben erhalten.";
            return;
        }
        std::vector<Vec2> connectorCenters;
        connectorCenters.reserve(preview.size());
        for (const auto& connector : preview) connectorCenters.push_back(connector.center);
        cutPoints.erase(cutPoints.begin() + static_cast<std::ptrdiff_t>(pointIndex));
        if (pointIndex < cutPointLocked.size()) cutPointLocked.erase(cutPointLocked.begin() + static_cast<std::ptrdiff_t>(pointIndex));
        for (size_t i = 0; i < placements.size() && i < connectorCenters.size(); ++i)
            placements[i].position = closestPositionOnPolyline(cutPoints, connectorCenters[i]);
        invalidateResult();
        updatePreview();
        status = "Knickpunkt gelöscht. Die Verbinder behalten ihre bisherige Position möglichst bei.";
    }

    void deleteCutOperation(int operationId, bool recordUndo = true) {
        if (pendingAppliedCutRebuildId >= 0) {
            status = "Zuerst den verschobenen Verbinder mit ‚Neu schneiden‘ übernehmen oder mit Strg+Z verwerfen.";
            return;
        }
        std::unordered_set<int> removedPartIds;
        std::unordered_set<int> removedCutIds{operationId};
        std::unordered_set<int> restoredParentIds;
        bool foundOperation = false;
        for (const auto& cut : cuts) if (cut.id == operationId) foundOperation = true;
        if (!foundOperation) return;
        if (recordUndo) captureUndo("Schnitt löschen", UndoKind::DeletedCut, operationId, true);

        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& cut : cuts) {
                if (removedCutIds.contains(cut.id)) {
                    restoredParentIds.insert(cut.sourcePartId);
                    for (int childId : cut.childPartIds)
                        if (removedPartIds.insert(childId).second) changed = true;
                } else if (removedPartIds.contains(cut.sourcePartId)) {
                    if (removedCutIds.insert(cut.id).second) changed = true;
                }
            }
        }

        parts.erase(std::remove_if(parts.begin(), parts.end(), [&](const PartRecord& part) {
            return removedPartIds.contains(part.id);
        }), parts.end());
        for (auto& part : parts) {
            if (restoredParentIds.contains(part.id))
                part.active = true;
        }
        cuts.erase(std::remove_if(cuts.begin(), cuts.end(), [&](const CutRecord& cut) {
            return removedCutIds.contains(cut.id) || removedPartIds.contains(cut.sourcePartId);
        }), cuts.end());

        clearCurrentCut();
        generated = !cuts.empty();
        selectedPartId = -1;
        for (int id : restoredParentIds) {
            for (const auto& part : parts) {
                if (part.id == id && part.active) {
                    selectedPartId = id;
                    break;
                }
            }
            if (selectedPartId >= 0) break;
        }
        if (selectedPartId < 0) {
            for (const auto& part : parts) if (part.active) { selectedPartId = part.id; break; }
        }
        dirty = true;
        refreshMeshDiagnostics();
        showMeshIssues = !meshDiagnostics.validSolid();
        status = "Schnitt " + std::to_string(operationId) + " und davon abhängige Folgeschnitte wurden gelöscht.";
    }

    bool editCutOperation(int operationId) {
        if (pendingAppliedCutRebuildId >= 0) {
            status = "Zuerst den verschobenen Verbinder mit ‚Neu schneiden‘ übernehmen oder mit Strg+Z verwerfen.";
            return false;
        }
        const auto stored = std::find_if(cuts.begin(), cuts.end(), [&](const CutRecord& cut) {
            return cut.id == operationId;
        });
        if (stored == cuts.end()) {
            status = "Der ausgewählte Schnitt ist nicht mehr vorhanden.";
            return false;
        }
        CutRecord editable = *stored;
        std::unordered_set<int> earlierOperations;
        for (const CutRecord& cut : cuts)
            if (cut.id < operationId) earlierOperations.insert(cut.id);
        // Visible assembly numbers follow the order of actual cut operations,
        // never the internal ID (which can contain gaps after undo/editing).
        editable.settings.assemblyMarkCode =
            static_cast<int>(earlierOperations.size()) + 1;
        captureUndo("Schnitt erneut bearbeiten", UndoKind::DeletedCut, operationId, true);
        deleteCutOperation(operationId, false);
        // Explosion is a display-only transform. Cut points are deliberately
        // stored in stable model coordinates, so editing always returns to the
        // assembled view before exposing those coordinates again.
        explode = 0.0;
        camera.top = true;
        cutPoints = editable.points;
        cutPointLocked = editable.pointLocked;
        if (cutPointLocked.size() != cutPoints.size()) cutPointLocked.assign(cutPoints.size(), false);
        settings = editable.settings;
        cutMode = editable.mode;
        cutAllActiveParts = editable.cutAllActiveParts;
        drawingLine = false;
        placements = editable.placements;
        if (placements.empty() && cutPoints.size() >= 2)
            placements = makeDefaultConnectorPlacements(cutPoints, settings);
        settings.count = std::max(1, static_cast<int>(placements.size()));
        selectedConnector = -1;
        showConnectorEditor = false;
        invalidateResult();
        updatePreview();
        dirty = true;
        status = "Schnitt " + std::to_string(operationId) +
                 " ist wieder bearbeitbar. Folgeschnitte wurden entfernt und können danach neu angelegt werden.";
        editingCutOperationId = operationId;
        return true;
    }

    void undoLast() {
        if (undoStack.empty()) {
            status = "Es gibt keinen Schritt zum Rückgängigmachen.";
            return;
        }
        UndoEntry entry = std::move(undoStack.back());
        undoStack.pop_back();
        if (entry.kind == UndoKind::GeneratedCut) {
            deleteCutOperation(entry.operationId, false);
        } else if (entry.kind == UndoKind::DeletedCut || entry.kind == UndoKind::MeshRepair) {
            parts.clear();
            parts.reserve(entry.structuralParts.size());
            for (const auto& stored : entry.structuralParts) {
                PartRecord part;
                part.id = stored.id;
                part.parentCutId = stored.parentCutId;
                part.label = stored.label;
                if (!stored.mesh) continue;
                part.mesh = stored.mesh;
                part.gl.set(*part.mesh);
                part.active = stored.active;
                part.spatialIndex = stored.spatialIndex
                    ? stored.spatialIndex
                    : std::make_shared<const MeshSpatialIndex>(*part.mesh);
                parts.push_back(std::move(part));
            }
            cuts = entry.structuralCuts;
            selectedPartId = entry.selectedPartId;
            nextPartId = entry.nextPartId;
            nextCutId = entry.nextCutId;
            generated = !cuts.empty();
            if (cuts.empty() && !parts.empty()) {
                source = parts.front().mesh;
                if (source) sourceGL.set(*source);
            }
        }
        restoreSmallState(entry);
        pendingAppliedCutRebuildId = -1;
        refreshMeshDiagnostics();
        showMeshIssues = !meshDiagnostics.validSolid();
        storeActiveObjectSession();
        dirty = true;
        status = "Rückgängig: " + entry.label;
    }
};
