#pragma once

#include "app_state.h"

struct GLFWwindow;

void drawStartupPrompt(App& app, double aspect);
void drawMainMenuBar(App& app, double aspect, GLFWwindow* window);
void drawInvalidConnectorPrompt(App& app);
void drawColorLossPrompt(App& app);
void drawEditCutPrompt(App& app);
void drawPendingCutPrompt(App& app);
void drawProjectConfirmation(App& app, double aspect);
void drawModelRepairPrompt(App& app);
void drawModelInfoWindow(App& app);
