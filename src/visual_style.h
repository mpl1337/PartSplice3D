#pragma once

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>

inline constexpr std::array<std::array<float, 3>, 6> kPartColors{{
    {{0.25f, 0.72f, 0.95f}}, {{0.95f, 0.58f, 0.24f}}, {{0.42f, 0.86f, 0.46f}},
    {{0.88f, 0.42f, 0.78f}}, {{0.85f, 0.82f, 0.30f}}, {{0.48f, 0.55f, 0.95f}}
}};

inline constexpr std::array<std::array<float, 3>, 8> kCutColors{{
    {{1.00f, 0.38f, 0.22f}}, {{0.18f, 0.78f, 1.00f}}, {{1.00f, 0.72f, 0.12f}},
    {{0.72f, 0.46f, 1.00f}}, {{0.20f, 0.92f, 0.52f}}, {{1.00f, 0.34f, 0.72f}},
    {{0.78f, 0.92f, 0.22f}}, {{0.28f, 0.62f, 1.00f}}
}};

inline const std::array<float, 3>& cutColor(int operationId) noexcept {
    const size_t index = static_cast<size_t>(std::max(1, operationId) - 1) % kCutColors.size();
    return kCutColors[index];
}

inline ImVec4 cutColorImGui(int operationId) noexcept {
    const auto& color = cutColor(operationId);
    return {color[0], color[1], color[2], 1.0f};
}
