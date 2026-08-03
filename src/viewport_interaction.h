#pragma once

#include "app_state.h"

#include <imgui.h>

struct AppliedConnectorHit {
    int cutId = -1;
    int connector = -1;
    double distance = std::numeric_limits<double>::infinity();
};

struct CutPartColorPair {
    ImVec4 left;
    ImVec4 right;
};

std::vector<int> appliedCutIds(const App& app);
std::vector<int> appliedCutsAt(const App& app, Vec2 point, double radius);
AppliedConnectorHit appliedConnectorAt(const App& app, Vec2 point, double radius);
void moveAppliedConnectorAlongLine(App& app, int operationId, int connectorIndex, Vec2 point);
bool pointInsidePartProjection(const PartRecord& part, Vec2 point);
int activePartAt(const App& app, Vec2 point);
ImVec4 activePartColor(const App& app, int partId, int cutId);
CutPartColorPair cutPartColorsAt(const App& app, int operationId, Vec2 reference);
bool splitColorButton(const char* id, CutPartColorPair colors);
bool printBedFrameAt(const App& app, Vec2 point, double radius);
void moveConnectorAlongLine(App& app, int index, Vec2 point);
