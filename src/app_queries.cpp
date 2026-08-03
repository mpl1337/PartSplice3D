#include "app_queries.h"
#include "mesh_spatial_index.h"

const PartRecord* partById(const App& app, int id) noexcept {
    for (const PartRecord& part : app.parts)
        if (part.id == id) return &part;
    return nullptr;
}

const std::vector<ConnectorPreview>& appliedConnectorPreview(const App& app, const CutRecord& cut) {
    static const std::vector<ConnectorPreview> empty;
    const PartRecord* source = partById(app, cut.sourcePartId);
    if (source == nullptr || cut.points.size() < 2) return empty;
    if (!cut.previewCacheValid) {
        DovetailSettings previewSettings = cut.settings;
        previewSettings.validateInsideModel = false;
        std::vector<ConnectorPlacement> placements = cut.placements;
        if (placements.empty()) placements = makeDefaultConnectorPlacements(cut.points, previewSettings);
        if (!source->mesh) return empty;
        cut.previewCache = makeConnectorPreview(*source->mesh, cut.points, previewSettings, placements);
        cut.previewCacheValid = true;
    }
    return cut.previewCache;
}

AppliedConnectorValidation validateAppliedConnectorPlacements(App& app, int operationId) {
    std::vector<ConnectorPreview> combined;
    bool foundTarget = false;
    for (const CutRecord& cut : app.cuts) {
        if (cut.id != operationId) continue;
        const PartRecord* source = partById(app, cut.sourcePartId);
        if (source == nullptr || !source->mesh || cut.points.size() < 2)
            continue;
        foundTarget = true;
        std::optional<MeshSpatialIndex> fallbackIndex;
        const MeshSpatialIndex* validationIndex = source->spatialIndex.get();
        if (validationIndex == nullptr) {
            fallbackIndex.emplace(*source->mesh);
            validationIndex = &*fallbackIndex;
        }
        DovetailSettings settings = cut.settings;
        settings.validateInsideModel = true;
        std::vector<ConnectorPlacement> placements = cut.placements;
        if (placements.empty()) placements = makeDefaultConnectorPlacements(cut.points, settings);
        std::vector<ConnectorPreview> local = makeConnectorPreview(
            *source->mesh, cut.points, settings, placements, validationIndex, false);
        if (combined.empty()) {
            combined = std::move(local);
            continue;
        }
        const size_t count = std::min(combined.size(), local.size());
        for (size_t index = 0; index < count; ++index) {
            const bool wasValidForEveryTarget = combined[index].valid;
            combined[index].partiallyValid = combined[index].partiallyValid ||
                (wasValidForEveryTarget != local[index].valid);
            combined[index].valid = wasValidForEveryTarget && local[index].valid;
            combined[index].overlap = combined[index].overlap || local[index].overlap;
            combined[index].crossesCutCorner = combined[index].crossesCutCorner ||
                                                local[index].crossesCutCorner;
            combined[index].thinWall = combined[index].thinWall || local[index].thinWall;
        }
    }

    if (foundTarget) {
        for (CutRecord& cut : app.cuts) {
            if (cut.id != operationId) continue;
            cut.previewCache = combined;
            cut.previewCacheValid = true;
        }
    }

    AppliedConnectorValidation validation;
    for (const ConnectorPreview& connector : combined) {
        if (connector.partiallyValid) ++validation.partiallyValid;
        else if (!connector.valid) ++validation.invalid;
        else if (connector.warning()) ++validation.warnings;
    }
    return validation;
}

bool cutHasDependentOperations(const App& app, int operationId) {
    std::unordered_set<int> childPartIds;
    std::unordered_set<int> dependentCutIds;
    for (const CutRecord& cut : app.cuts) {
        if (cut.id != operationId) continue;
        childPartIds.insert(cut.childPartIds.begin(), cut.childPartIds.end());
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (const CutRecord& cut : app.cuts) {
            if (cut.id == operationId || dependentCutIds.contains(cut.id)) continue;
            if (!childPartIds.contains(cut.sourcePartId)) continue;
            dependentCutIds.insert(cut.id);
            childPartIds.insert(cut.childPartIds.begin(), cut.childPartIds.end());
            changed = true;
        }
    }
    return !dependentCutIds.empty();
}
