#pragma once

#include "stl_io.h"

#include <atomic>
#include <string>

struct WindowsMeshRepairResult {
    bool ok = false;
    bool canceled = false;
    TriangleMesh mesh;
    std::string message;
};

WindowsMeshRepairResult repairMeshWithWindowsService(
    const TriangleMesh& mesh, const std::atomic_bool* cancelRequested = nullptr);
