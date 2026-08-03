#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include "resources.h"
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl2.h>

#include <GL/gl.h>

#include "app_log.h"
#include "app_state.h"
#include "app_persistence.h"
#include "calibration_panel.h"
#include "connector_editor.h"
#include "export_workflow.h"
#include "generation_workflow.h"
#include "localization.h"
#include "project_actions.h"
#include "project_workflow.h"
#include "update_workflow.h"
#include "ui_runtime.h"
#include "ui_dialogs.h"
#include "viewport_render.h"
#include "viewport_context_menu.h"
#include "viewport_input.h"
#include "workspace_panels.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <new>
#include <string>
#include <vector>















namespace {

std::string imguiSettingsPath() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        const std::filesystem::path directory =
            std::filesystem::path(buffer.data()) / L"PartSplice3D";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (!error) return pathToUtf8(directory / L"imgui.ini");
    }
#endif
    // Never let Dear ImGui fall back to a relative "imgui.ini" next to an
    // exported model. If the settings directory is unavailable, persistence
    // is disabled for this run instead.
    return {};
}

#ifdef _WIN32
void applyWindowsWindowIcon(GLFWwindow* window) {
    if (!window) return;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const auto loadIcon = [instance](int width, int height) -> HICON {
        return static_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
            width, height, LR_DEFAULTCOLOR | LR_SHARED));
    };
    const HWND nativeWindow = glfwGetWin32Window(window);
    if (!nativeWindow) return;
    if (const HICON largeIcon = loadIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON)))
        SendMessageW(nativeWindow, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
    if (const HICON smallIcon = loadIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)))
        SendMessageW(nativeWindow, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
}
#endif

} // namespace

int appMain() {
#ifdef _WIN32
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                  LOAD_LIBRARY_SEARCH_SYSTEM32 |
                                  LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        appLogSystemError("Sichere Windows-DLL-Suche konnte nicht aktiviert werden",
                          GetLastError());
        MessageBoxW(nullptr, L"Die sichere Windows-DLL-Suche konnte nicht aktiviert werden.",
                    L"PartSplice 3D", MB_OK | MB_ICONERROR);
        return 4;
    }
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(comResult))
        appLogSystemError("COM-Initialisierung für Windows-Dateidialoge fehlgeschlagen",
                          static_cast<unsigned long>(comResult));
#endif
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        appLogError("GLFW konnte nicht initialisiert werden.");
#ifdef _WIN32
        if (SUCCEEDED(comResult)) CoUninitialize();
#endif
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#ifdef _WIN32
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(
        1320, 840, "PartSplice 3D v" PARTSPLICE_VERSION_STRING, nullptr, nullptr);
    if (!window) {
        appLogError("Das PartSplice-3D-Hauptfenster konnte nicht erzeugt werden.");
        glfwTerminate();
#ifdef _WIN32
        if (SUCCEEDED(comResult)) CoUninitialize();
#endif
        return 2;
    }
#ifdef _WIN32
    applyWindowsWindowIcon(window);
#endif
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    const std::string imguiIniPath = imguiSettingsPath();
    io.IniFilename = imguiIniPath.empty() ? nullptr : imguiIniPath.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    loadUiFont(io);
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.PopupRounding = 7.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.ScrollbarRounding = 7.0f;
    style.WindowPadding = {12.0f, 11.0f};
    style.ItemSpacing = {8.0f, 7.0f};
    style.Colors[ImGuiCol_WindowBg] = {0.075f, 0.085f, 0.105f, 0.97f};
    style.Colors[ImGuiCol_FrameBg] = {0.12f, 0.16f, 0.21f, 1.0f};
    style.Colors[ImGuiCol_Button] = {0.12f, 0.28f, 0.46f, 1.0f};
    style.Colors[ImGuiCol_ButtonHovered] = {0.16f, 0.42f, 0.68f, 1.0f};
    style.Colors[ImGuiCol_Header] = {0.12f, 0.31f, 0.52f, 1.0f};
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        appLogError("ImGui-GLFW-Plattformbackend konnte nicht initialisiert werden.");
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
#ifdef _WIN32
        if (SUCCEEDED(comResult)) CoUninitialize();
#endif
        return 5;
    }
    if (!ImGui_ImplOpenGL2_Init()) {
        appLogError("ImGui-OpenGL-Rendererbackend konnte nicht initialisiert werden.");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
#ifdef _WIN32
        if (SUCCEEDED(comResult)) CoUninitialize();
#endif
        return 6;
    }
    appLogInfo("Fenster, OpenGL und ImGui erfolgreich initialisiert.");

    App app;
    loadLanguagePreference();
    loadConnectorDefaults(app.settings);
    // Engravings are a project workflow default, not part of a calibrated
    // connector fit. New projects therefore start with them enabled even if
    // an older connector-defaults file stored the former disabled default.
    app.settings.assemblyMarks = true;
    loadRecentFiles(app.recentFiles);
    app.calibrationSettings = app.settings;
    // Update checks are user initiated.  This avoids an unexpected network
    // request and prevents application shutdown from waiting on WinHTTP.

    bool exitRequested = false;
    while (!exitRequested) {
        glfwPollEvents();
        pollWindowsMeshRepair(app);
        pollBackgroundTask(app);
        pollUpdateCheck(app);
        if (app.exitAfterSave && !app.backgroundRunning) {
            app.exitAfterSave = false;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (app.closeAfterBackground && !app.backgroundRunning) {
            app.closeAfterBackground = false;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (app.closeAfterRepair && !app.repairRunning) {
            app.closeAfterRepair = false;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (glfwWindowShouldClose(window)) {
            if (app.backgroundRunning) {
                glfwSetWindowShouldClose(window, GLFW_FALSE);
                app.closeAfterBackground = true;
                cancelBackgroundTask(app);
            } else if (app.repairRunning) {
                glfwSetWindowShouldClose(window, GLFW_FALSE);
                app.closeAfterRepair = true;
                cancelWindowsMeshRepair(app);
            } else if (app.dirty) {
                glfwSetWindowShouldClose(window, GLFW_FALSE);
                app.showExitConfirmation = true;
            } else {
                break;
            }
        }
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (!app.busy() && !io.WantTextInput &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            // Tool windows consume Escape before the current cut. Otherwise a
            // user closing a settings window would also discard cut work.
            if (app.showPrecisionSettings) app.showPrecisionSettings = false;
            else if (app.showConnectorCalibration) app.showConnectorCalibration = false;
            else if (app.drawingLine || !app.cutPoints.empty()) cancelCurrentCut(app);
        }
        if (!app.busy() && io.KeyCtrl && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) app.undoLast();
        }
        if (!app.busy() && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
            app.selectedConnector >= 0 &&
            static_cast<size_t>(app.selectedConnector) < app.placements.size()) {
            app.captureUndo("Verbinder löschen");
            app.placements.erase(app.placements.begin() + app.selectedConnector);
            app.settings.count = std::max(1, static_cast<int>(app.placements.size()));
            app.selectedConnector = -1;
            app.showConnectorEditor = false;
            app.dragTarget = DragTarget::None;
            app.draggedConnector = -1;
            app.invalidateResult();
            app.updatePreview();
            app.dirty = true;
            app.status = "Ausgewählter Verbinder wurde mit Entf gelöscht.";
        }

        int winW = 0;
        int winH = 0;
        int fbW = 0;
        int fbH = 0;
        glfwGetWindowSize(window, &winW, &winH);
        glfwGetFramebufferSize(window, &fbW, &fbH);
        const double aspect = std::max(1, winW) / static_cast<double>(std::max(winH, 1));
        if (!app.busy() && io.KeyCtrl && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_N, false))
                requestProjectAction(app, PendingProjectAction::NewProject, aspect);
            if (ImGui::IsKeyPressed(ImGuiKey_O, false))
                requestProjectAction(app, PendingProjectAction::OpenModel, aspect);
            if (ImGui::IsKeyPressed(ImGuiKey_S, false) && app.loaded)
                saveProject(app, io.KeyShift);
            if (ImGui::IsKeyPressed(ImGuiKey_E, false) && app.loaded)
                exportAllParts(app);
        }

        double mx = 0.0;
        double my = 0.0;
        glfwGetCursorPos(window, &mx, &my);
        const bool inViewport = mx >= 0.0 && mx < winW &&
                                my >= ImGui::GetFrameHeight() && my < winH;
        const bool mouseFree = inViewport && !io.WantCaptureMouse;

        handleViewportInput(app, io, mx, my, winW, winH, mouseFree && !app.busy());
        drawMainMenuBar(app, aspect, window);
        if (app.backgroundRunning) {
            drawBackgroundTaskWindow(app);
        } else {
            drawStartupPrompt(app, aspect);
            drawUpdateResult(app);
            drawCutModeHint(app);
            drawCutTreeOverlay(app);
            drawApplyCutsButton(app);
            drawViewToolbar(app, aspect);
            drawObjectSelector(app, aspect);
            drawObjectSwitchPrompt(app);
            drawViewportContextMenu(app);
            drawInvalidConnectorPrompt(app);
            drawColorLossPrompt(app);
            drawEditCutPrompt(app);
            drawConnectorEditor(app);
            drawPrecisionSettingsWindow(app);
            drawAssemblyMarkEditor(app);
            drawConnectorCalibrationWindow(app);
            drawModelInfoWindow(app);
            drawProjectConfirmation(app, aspect);
            drawModelRepairPrompt(app);
            drawPendingCutPrompt(app);
            if (drawExitConfirmation(app)) exitRequested = true;
        }
        renderScene(app, fbW, fbH, winW);

        ImGui::Render();
        glViewport(0, 0, fbW, fbH);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    app.sourceGL.clear();
    for (auto& part : app.parts) part.gl.clear();
    app.parts.clear();
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
#ifdef _WIN32
    if (SUCCEEDED(comResult)) CoUninitialize();
#endif
    appLogInfo("PartSplice 3D regulär beendet.");
    return 0;
}

int safeAppMain() noexcept {
    initializeAppLog();
    try {
        return appMain();
    } catch (const std::bad_alloc&) {
        appLogError("Fataler Fehler: Nicht genügend Arbeitsspeicher.");
#ifdef _WIN32
        MessageBoxW(nullptr, L"PartSplice 3D musste wegen unzureichendem Arbeitsspeicher beendet werden.",
                    L"PartSplice 3D", MB_OK | MB_ICONERROR);
#else
        std::fputs("PartSplice 3D: insufficient memory.\n", stderr);
#endif
    } catch (const std::exception& exception) {
        appLogError(std::string("Fataler Ausnahmefehler: ") + exception.what());
#ifdef _WIN32
        std::string message = std::string("PartSplice 3D musste wegen eines unerwarteten Fehlers beendet werden:\n") +
                              exception.what();
        const std::wstring wide(message.begin(), message.end());
        MessageBoxW(nullptr, wide.c_str(), L"PartSplice 3D", MB_OK | MB_ICONERROR);
#else
        std::fprintf(stderr, "PartSplice 3D: unexpected error: %s\n", exception.what());
#endif
    } catch (...) {
        appLogError("Fataler unbekannter Ausnahmefehler.");
#ifdef _WIN32
        MessageBoxW(nullptr, L"PartSplice 3D musste wegen eines unbekannten Fehlers beendet werden.",
                    L"PartSplice 3D", MB_OK | MB_ICONERROR);
#else
        std::fputs("PartSplice 3D: unknown fatal error.\n", stderr);
#endif
    }
    return 3;
}

#ifdef _WIN32
int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    return safeAppMain();
}
#else
int main() {
    return safeAppMain();
}
#endif
