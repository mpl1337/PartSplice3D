#pragma once

#include "stl_io.h"

#include <atomic>
#include <string>

enum class WindowsRepairStage {
    Preparing, Initializing, Loading, StartingRepair, Repairing, Saving, Reading, Indexing
};

const char* windowsRepairStageText(WindowsRepairStage stage);

struct WindowsMeshRepairResult {
    bool ok = false;
    bool canceled = false;
    TriangleMesh mesh;
    std::string message;
};

WindowsMeshRepairResult repairMeshWithWindowsService(
    const TriangleMesh& mesh, const std::atomic_bool* cancelRequested = nullptr,
    std::atomic<WindowsRepairStage>* progress = nullptr);
