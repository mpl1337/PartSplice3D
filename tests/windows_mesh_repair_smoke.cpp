// Portable smoke test (Linux): c++ -std=c++20 -Isrc tests/windows_mesh_repair_smoke.cpp
// src/windows_mesh_repair.cpp src/app_log.cpp -o /tmp/repair-smoke && /tmp/repair-smoke
#include "windows_mesh_repair.h"
#include <cassert>
#include <set>

int main() {
    std::set<std::string> descriptions;
    for (auto stage : {WindowsRepairStage::Preparing, WindowsRepairStage::Initializing,
                      WindowsRepairStage::Loading, WindowsRepairStage::StartingRepair,
                      WindowsRepairStage::Repairing, WindowsRepairStage::Saving,
                      WindowsRepairStage::Reading, WindowsRepairStage::Indexing}) {
        assert(descriptions.insert(windowsRepairStageText(stage)).second);
    }
#ifndef _WIN32
    TriangleMesh original;
    original.vertices.push_back({1, 2, 3});
    std::atomic<WindowsRepairStage> progress{WindowsRepairStage::Repairing};
    const auto result = repairMeshWithWindowsService(original, nullptr, &progress);
    assert(!result.ok && !result.canceled);
    assert(result.message.find("nur unter Windows") != std::string::npos);
    assert(progress.load() == WindowsRepairStage::Preparing);
    assert(original.vertices.size() == 1);
    assert(result.mesh.vertices.empty());
#endif
}
