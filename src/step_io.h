#pragma once

#include "three_mf.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

struct StepLoadResult {
    bool ok = false;
    std::vector<ThreeMfObject> objects;
    std::string message;
    size_t repairedBodies = 0;
    size_t failedRepairBodies = 0;
    size_t removedDegenerate = 0;
};

StepLoadResult loadStep(const std::filesystem::path& path,
                        const std::atomic_bool* cancelRequested = nullptr);
