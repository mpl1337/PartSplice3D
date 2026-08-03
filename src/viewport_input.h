#pragma once

#include "app_state.h"

#include <imgui.h>

void handleViewportInput(App& app, ImGuiIO& io, double mouseX, double mouseY,
                         int windowWidth, int windowHeight, bool mouseFree);
