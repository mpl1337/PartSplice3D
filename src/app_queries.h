#pragma once

#include "app_state.h"

const PartRecord* partById(const App& app, int id) noexcept;
const std::vector<ConnectorPreview>& appliedConnectorPreview(const App& app, const CutRecord& cut);

struct AppliedConnectorValidation {
    size_t invalid = 0;
    size_t partiallyValid = 0;
    size_t warnings = 0;
};

AppliedConnectorValidation validateAppliedConnectorPlacements(App& app, int operationId);
bool cutHasDependentOperations(const App& app, int operationId);
