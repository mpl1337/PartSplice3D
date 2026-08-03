#pragma once

#include "app_state.h"

void startUpdateCheck(App& app, bool manual);
void pollUpdateCheck(App& app);
void drawUpdateResult(App& app);
