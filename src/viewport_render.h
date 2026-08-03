#pragma once

#include "app_state.h"

#include <optional>

void renderScene(App& app, int fbWidth, int fbHeight, int winWidth);
Vec2 rotate2(Vec2 point, double degrees);
Vec2 mouseToTopWorld(const App& app, double mouseX, double mouseY, int winWidth, int winHeight);
std::optional<Vec2> mouseTo3dPlane(const App& app, double mouseX, double mouseY,
                                   int winWidth, int winHeight, double planeZ);
double hitRadiusWorld(const App& app, int winHeight, double pixels);
