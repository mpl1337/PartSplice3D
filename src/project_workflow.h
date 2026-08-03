#pragma once

#include "app_state.h"

bool saveProject(App& app, bool saveAs = false,
                 SaveContinuation continuation = SaveContinuation::None);
void applyProjectLoad(
    App& app, ProjectLoadResult&& result,
    std::vector<std::vector<std::shared_ptr<const MeshSpatialIndex>>>&& spatialIndices,
    const std::filesystem::path& path, double aspect);
void applyModelLoad(App& app, LoadedModelPayload&& loaded, double aspect);
void loadFileOrProject(App& app, const std::filesystem::path& path, double aspect);
