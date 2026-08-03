#pragma once

#include "connector_types.h"
#include "stl_io.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ProjectPartState {
    int id = 0;
    int parentCutId = -1;
    std::string label;
    TriangleMesh mesh;
    bool active = true;
    std::shared_ptr<const TriangleMesh> meshReference;

    const TriangleMesh& meshData() const {
        return meshReference ? *meshReference : mesh;
    }
};

struct ProjectCutState {
    int id = 0;
    int sourcePartId = 0;
    std::string sourceLabel;
    std::vector<int> childPartIds;
    std::vector<std::string> childLabels;
    std::vector<Vec2> points;
    std::vector<bool> pointLocked;
    std::vector<ConnectorPlacement> placements;
    DovetailSettings settings;
    int mode = 0;
    bool cutAllActiveParts = true;
    bool forcedInvalidConnectors = false;
};

struct ProjectObjectState {
    std::string name;
    int plateIndex = 1;
    std::vector<ProjectPartState> parts;
    std::vector<ProjectCutState> cuts;
    int selectedPartId = -1;
    int nextPartId = 1;
    int nextCutId = 1;
};

struct PartSpliceProject {
    std::string originalPath;
    std::string originalFileName;
    std::vector<uint8_t> originalFileData;
    std::string sourceThreeMfProjectSettings;
    std::vector<MeshMaterial> materials;
    std::vector<std::string> threeMfCompatibilityWarnings;
    // Internal response payload used only by PartSpliceWorker. Keeping this
    // separate prevents geometry-worker data from masquerading as 3MF metadata.
    std::vector<uint8_t> workerConnectorFlags;
    std::vector<ProjectObjectState> objects;
    int activeObjectIndex = 0;
    int exportFormat = 0;

    bool drawingLine = false;
    int cutMode = 0;
    std::vector<Vec2> cutPoints;
    std::vector<bool> cutPointLocked;
    std::vector<ConnectorPlacement> placements;
    DovetailSettings settings;
    bool cutAllActiveParts = true;

    bool showSource = false;
    bool wireframe = false;
    double modelTransparency = 0.0;
    bool showPrintBed = false;
    double printBedWidth = 256.0;
    double printBedDepth = 256.0;
    Vec2 printBedCenter{};
    double printBedRotation = 0.0;
    int printBedPreset = 0;
    double customBedWidth = 256.0;
    double customBedDepth = 256.0;

    bool cameraTop = true;
    double cameraYaw = 42.0;
    double cameraPitch = 55.0;
    double cameraOrbitDistance = 100.0;
    double cameraTopHeight = 100.0;
    Vec2 cameraPan{};
};

struct ProjectLoadResult {
    bool ok = false;
    std::string message;
    PartSpliceProject project;
};

bool savePartSpliceProject(const std::filesystem::path& path,
                           const PartSpliceProject& project,
                           std::string& error);
ProjectLoadResult loadPartSpliceProject(const std::filesystem::path& path,
                                        const std::atomic_bool* cancelRequested = nullptr);
