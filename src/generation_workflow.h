#pragma once

#include "app_state.h"

bool startGenerateTask(App& app, bool forceInvalidConnectors,
                       GenerateContinuation continuation);
void cancelBackgroundTask(App& app);
void pollBackgroundTask(App& app);
void drawBackgroundTaskWindow(App& app);
void requestGenerate(App& app, GenerateContinuation continuation = GenerateContinuation::None);
void requestEditCut(App& app, int operationId);
bool rebuildMovedAppliedConnector(App& app, int operationId);
void startWindowsMeshRepair(App& app);
void cancelWindowsMeshRepair(App& app);
void pollWindowsMeshRepair(App& app);
