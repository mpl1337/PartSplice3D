#pragma once

#include "app_state.h"

void drawObjectSelector(App& app, double aspect);
void drawObjectSwitchPrompt(App& app);
void drawCutTreeOverlay(App& app);
void drawApplyCutsButton(App& app);
void drawCutModeHint(const App& app);
void drawViewToolbar(App& app, double aspect);
void drawPrecisionSettingsWindow(App& app);
void drawAssemblyMarkEditor(App& app);
bool drawExitConfirmation(App& app);
