#pragma once

#include "app_state.h"

void performProjectAction(App& app, PendingProjectAction action, double aspect);
void requestOpenPath(App& app, const std::filesystem::path& path, double aspect);
void requestProjectAction(App& app, PendingProjectAction action, double aspect);
void requestBeginCut(App& app, CutMode mode);
void cancelCurrentCut(App& app);
