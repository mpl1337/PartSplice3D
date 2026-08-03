#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>

struct BackgroundProgress {
    std::atomic<size_t> completed{0};
    std::atomic<size_t> total{0};
};

void drawBackgroundProgress(
    const std::shared_ptr<BackgroundProgress>& progress,
    std::chrono::steady_clock::time_point started);
