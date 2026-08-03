#include "ui_runtime.h"

#include "app_log.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resources.h"
#endif

#include <imgui.h>

#include <cstdio>
#include <limits>

void glfwErrorCallback(int errorCode, const char* description) noexcept {
    char message[2048]{};
    std::snprintf(message, sizeof(message), "GLFW-Fehler %d: %s", errorCode,
                  description != nullptr ? description : "keine Beschreibung");
    appLogError(message);
}

void loadUiFont(ImGuiIO& io) {
#ifdef _WIN32
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource = FindResourceW(
        module, MAKEINTRESOURCEW(IDR_UI_FONT), MAKEINTRESOURCEW(10));
    if (resource != nullptr) {
        const HGLOBAL loaded = LoadResource(module, resource);
        const DWORD size = SizeofResource(module, resource);
        void* data = loaded != nullptr ? LockResource(loaded) : nullptr;
        if (data != nullptr && size > 0 &&
            size <= static_cast<DWORD>(std::numeric_limits<int>::max())) {
            static constexpr ImWchar glyphRanges[] = {
                0x0020, 0x00FF,
                0x2000, 0x206F,
                0x2190, 0x21FF,
                0x2500, 0x25FF,
                0x2700, 0x27BF,
                0
            };
            ImFontConfig config;
            config.FontDataOwnedByAtlas = false;
            config.OversampleH = 2;
            config.OversampleV = 2;
            if (ImFont* font = io.Fonts->AddFontFromMemoryTTF(
                    data, static_cast<int>(size), 15.5f, &config, glyphRanges)) {
                io.FontDefault = font;
                return;
            }
        }
    }
#endif
    appLogWarning("Eingebettete UI-Schrift konnte nicht geladen werden; Standardschrift wird verwendet.");
    io.Fonts->AddFontDefault();
}
