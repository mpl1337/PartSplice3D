#pragma once

#include "mesh_ops.h"
#include "mesh_spatial_index.h"
#include "project_io.h"
#include "scene_core.h"
#include "three_mf.h"
#include "windows_mesh_repair.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

enum class DragTarget { None, CutPoint, Connector, AppliedConnector, AssemblyMark, PrintBed };
enum class CutMode { Straight, Polyline };
enum class ExportFormat { Stl, ThreeMf };
enum class GenerateContinuation { None, BeginPendingCut, SwitchObject };

struct PartRecord {
    int id = 0;
    int parentCutId = -1;
    std::string label;
    std::shared_ptr<const TriangleMesh> mesh;
    MeshDisplayList gl;
    bool active = true;
    std::shared_ptr<const MeshSpatialIndex> spatialIndex;
};

struct CutRecord {
    int id = 0;
    int sourcePartId = 0;
    std::string sourceLabel;
    std::vector<int> childPartIds;
    std::vector<std::string> childLabels;
    std::vector<Vec2> points;
    std::vector<bool> pointLocked;
    std::vector<ConnectorPlacement> placements;
    DovetailSettings settings;
    CutMode mode = CutMode::Straight;
    bool cutAllActiveParts = true;
    bool forcedInvalidConnectors = false;
    mutable bool previewCacheValid = false;
    mutable std::vector<ConnectorPreview> previewCache;
};

struct StoredPart {
    int id = 0;
    int parentCutId = -1;
    std::string label;
    std::shared_ptr<const TriangleMesh> mesh;
    bool active = true;
    std::shared_ptr<const MeshSpatialIndex> spatialIndex;
};

using StoredPartSnapshot = StoredPart;

struct ObjectSession {
    std::string name;
    int plateIndex = 1;
    std::vector<StoredPart> parts;
    std::vector<CutRecord> cuts;
    int selectedPartId = -1;
    int nextPartId = 1;
    int nextCutId = 1;
};

enum class BackgroundTaskKind { None, LoadFile, Generate, Export, SaveProject };
enum class SaveContinuation { None, PendingProjectAction, ExitApplication };

struct LoadedModelPayload {
    std::filesystem::path path;
    std::vector<ThreeMfObject> objects;
    std::vector<std::shared_ptr<const MeshSpatialIndex>> spatialIndices;
    std::vector<uint8_t> originalFileData;
    std::string projectSettings;
    std::vector<MeshMaterial> materials;
    std::vector<std::string> compatibilityWarnings;
    std::string message;
    ExportFormat exportFormat = ExportFormat::Stl;
};

struct GenerateTargetSnapshot {
    int parentId = 0;
    std::string parentLabel;
    // Parts cannot be edited while a background operation is active. Borrowing
    // the immutable mesh avoids copying every target before the worker starts.
    std::shared_ptr<const TriangleMesh> mesh;
};

struct GeneratedSplitPayload {
    int parentId = 0;
    std::string parentLabel;
    std::shared_ptr<const TriangleMesh> partA;
    std::shared_ptr<const TriangleMesh> partB;
    std::shared_ptr<const MeshSpatialIndex> indexA;
    std::shared_ptr<const MeshSpatialIndex> indexB;
    std::vector<ConnectorPreview> connectors;
};

struct GenerateJobPayload {
    std::vector<GeneratedSplitPayload> splits;
    std::vector<std::string> failures;
    std::vector<Vec2> cutPoints;
    std::vector<bool> pointLocked;
    std::vector<ConnectorPlacement> placements;
    DovetailSettings settings;
    CutMode mode = CutMode::Straight;
    bool cutAllActiveParts = true;
    bool forceInvalidConnectors = false;
    int fixedOperationId = -1;
    bool recordUndo = true;
};

struct PendingCutRestoreState {
    bool hadPendingCut = false;
    bool drawingLine = false;
    CutMode mode = CutMode::Straight;
    std::vector<Vec2> points;
    std::vector<bool> pointLocked;
    std::vector<ConnectorPlacement> placements;
    DovetailSettings settings;
    bool cutAllActiveParts = true;
    int selectedConnector = -1;
    std::string selectedPartLabel;
};

struct BackgroundTaskResult {
    BackgroundTaskKind kind = BackgroundTaskKind::None;
    bool ok = false;
    bool canceled = false;
    std::string message;
    LoadedModelPayload model;
    ProjectLoadResult project;
    std::vector<std::vector<std::shared_ptr<const MeshSpatialIndex>>> projectSpatialIndices;
    std::filesystem::path projectPath;
    GenerateJobPayload generation;
};

struct IndexedRepairResult {
    WindowsMeshRepairResult repair;
    std::shared_ptr<const TriangleMesh> mesh;
    std::shared_ptr<const MeshSpatialIndex> spatialIndex;
};

enum class UndoKind { Edit, GeneratedCut, DeletedCut, MeshRepair };
enum class PendingProjectAction { None, NewProject, OpenModel, OpenSpecific, CloseProject };

struct UndoEntry {
    UndoKind kind = UndoKind::Edit;
    std::string label;
    int operationId = -1;
    int selectedConnector = -1;
    bool drawingLine = false;
    CutMode cutMode = CutMode::Straight;
    std::vector<Vec2> cutPoints;
    std::vector<bool> cutPointLocked;
    std::vector<ConnectorPlacement> placements;
    DovetailSettings settings;
    bool showPrintBed = false;
    double printBedWidth = 256.0;
    double printBedDepth = 256.0;
    Vec2 printBedCenter{};
    double printBedRotation = 0.0;
    std::vector<StoredPartSnapshot> structuralParts;
    std::vector<CutRecord> structuralCuts;
    int selectedPartId = -1;
    int nextPartId = 1;
    int nextCutId = 1;
};

uint64_t meshStorageBytes(const TriangleMesh& mesh) noexcept;
