#pragma once

#include "mesh_ops.h"
#include "step_io.h"

#include <atomic>
#include <filesystem>
#include <memory>

StepLoadResult loadStepIsolated(const std::filesystem::path& path,
                                const std::atomic_bool* cancelRequested = nullptr);

DovetailResult createDovetailSplitIsolated(
    const TriangleMesh& source,
    const std::vector<Vec2>& cutPoints,
    const DovetailSettings& settings,
    const std::vector<ConnectorPlacement>& placements,
    bool requireConnector,
    const std::atomic_bool* cancelRequested = nullptr);

DovetailResult createDovetailSplitIsolated(
    const std::shared_ptr<const TriangleMesh>& source,
    const std::vector<Vec2>& cutPoints,
    const DovetailSettings& settings,
    const std::vector<ConnectorPlacement>& placements,
    bool requireConnector,
    const std::atomic_bool* cancelRequested = nullptr);
