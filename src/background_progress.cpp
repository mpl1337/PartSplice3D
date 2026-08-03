#include "background_progress.h"

#include <imgui.h>
#include "localized_imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

void drawBackgroundProgress(
    const std::shared_ptr<BackgroundProgress>& progress,
    std::chrono::steady_clock::time_point started) {
    const size_t total = progress
        ? progress->total.load(std::memory_order_relaxed) : 0;
    const size_t completed = progress
        ? progress->completed.load(std::memory_order_relaxed) : 0;
    if (total > 0) {
        const float fraction = std::clamp(
            static_cast<float>(completed) / static_cast<float>(total), 0.0f, 1.0f);
        const std::string overlay = std::to_string(std::min(completed, total)) +
                                    " / " + std::to_string(total);
        ImGui::ProgressBar(fraction, {-1.0f, 18.0f}, overlay.c_str());
    } else {
        const float phase = static_cast<float>(
            std::fmod(ImGui::GetTime() * 0.35, 1.0));
        ImGui::ProgressBar(phase, {-1.0f, 18.0f}, "Arbeite ...");
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started).count();
    ImGui::TextDisabled("Laufzeit: %lld s",
                        static_cast<long long>(std::max<int64_t>(0, elapsed)));
}
