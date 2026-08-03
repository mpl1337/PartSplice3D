#include "update_workflow.h"

#include "app_log.h"
#include "update_check.h"
#include "version.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include <imgui.h>
#include "localized_imgui.h"

#include <chrono>
#include <future>
#include <limits>
#include <string>

void startUpdateCheck(App& app, bool manual) {
    if (app.updateCheckRunning) return;
    app.updateCheckManual = manual;
    app.updateCheckRunning = true;
    try {
        app.updateFuture = std::async(std::launch::async, checkForPartSpliceUpdate);
    } catch (const std::exception& exception) {
        app.updateCheckRunning = false;
        app.updateResult.state = UpdateCheckState::Failed;
        app.updateResult.message = std::string("Updateprüfung konnte nicht gestartet werden: ") + exception.what();
        if (manual) app.showUpdateResult = true;
        return;
    } catch (...) {
        app.updateCheckRunning = false;
        app.updateResult.state = UpdateCheckState::Failed;
        app.updateResult.message = "Updateprüfung konnte nicht gestartet werden.";
        if (manual) app.showUpdateResult = true;
        return;
    }
    if (manual) app.status = "GitHub wird im Hintergrund nach einer neueren Version durchsucht ...";
}

void pollUpdateCheck(App& app) {
    if (!app.updateCheckRunning || !app.updateFuture.valid()) return;
    if (app.updateFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
    try {
        app.updateResult = app.updateFuture.get();
    } catch (const std::exception& exception) {
        app.updateResult.state = UpdateCheckState::Failed;
        app.updateResult.message = std::string("Updateprüfung fehlgeschlagen: ") + exception.what();
    } catch (...) {
        app.updateResult.state = UpdateCheckState::Failed;
        app.updateResult.message = "Updateprüfung ist unerwartet fehlgeschlagen.";
    }
    app.updateCheckRunning = false;
    if (app.updateResult.state == UpdateCheckState::Failed)
        appLogWarning(app.updateResult.message);
    if (app.updateResult.state == UpdateCheckState::Available) {
        app.showUpdateResult = true;
        app.status = "PartSplice 3D " + app.updateResult.latestVersion + " ist verfügbar.";
    } else if (app.updateCheckManual) {
        app.showUpdateResult = true;
        app.status = app.updateResult.message;
    }
}

void drawUpdateResult(App& app) {
    const bool anotherPopupOpen = ImGui::IsPopupOpen(
        nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (app.showUpdateResult && !anotherPopupOpen) {
        ImGui::OpenPopup("PartSplice 3D aktualisieren##Update");
        app.showUpdateResult = false;
    }
    ImGui::SetNextWindowSizeConstraints({500.0f, 0.0f},
                                        {500.0f, std::numeric_limits<float>::max()});
    if (!ImGui::BeginPopupModal("PartSplice 3D aktualisieren##Update", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    if (app.updateResult.state == UpdateCheckState::Available) {
        ImGui::TextColored({0.30f, 0.88f, 0.48f, 1.0f}, "Neue Version verfügbar");
        ImGui::Spacing();
        ImGui::Text("Installiert: %s", kPartSpliceVersion);
        ImGui::Text("Verfügbar: %s", app.updateResult.latestVersion.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("Die offizielle GitHub-Release-Seite wird im Browser geöffnet. "
                           "PartSplice 3D lädt oder startet aus Sicherheitsgründen nichts ungefragt.");
        ImGui::Spacing();
        if (ImGui::Button("Update herunterladen ...", {245.0f, 38.0f})) {
#ifdef _WIN32
            const std::wstring releaseUrl(app.updateResult.releaseUrl.begin(),
                                          app.updateResult.releaseUrl.end());
            const auto result = reinterpret_cast<intptr_t>(
                ShellExecuteW(nullptr, L"open", releaseUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
            app.status = result > 32
                ? "GitHub-Release-Seite wurde im Browser geöffnet."
                : "Die GitHub-Release-Seite konnte nicht geöffnet werden.";
#endif
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Später", {-1.0f, 38.0f})) ImGui::CloseCurrentPopup();
    } else if (app.updateResult.state == UpdateCheckState::Current) {
        ImGui::TextColored({0.30f, 0.88f, 0.48f, 1.0f}, "PartSplice 3D ist aktuell.");
        ImGui::Text("Installierte Version: %s", kPartSpliceVersion);
        if (!app.updateResult.latestVersion.empty())
            ImGui::Text("Neuestes Release: %s", app.updateResult.latestVersion.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK", {-1.0f, 36.0f})) ImGui::CloseCurrentPopup();
    } else {
        ImGui::TextColored({1.0f, 0.58f, 0.22f, 1.0f}, "Repo nicht gefunden.");
        ImGui::Spacing();
        if (ImGui::Button("Schließen", {-1.0f, 36.0f})) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
