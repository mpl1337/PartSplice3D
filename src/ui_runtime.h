#pragma once

struct ImGuiIO;

void glfwErrorCallback(int errorCode, const char* description) noexcept;
void loadUiFont(ImGuiIO& io);
