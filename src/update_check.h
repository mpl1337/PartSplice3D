#pragma once

#include "version.h"

#include <string>

enum class UpdateCheckState { Current, Available, Failed };

struct UpdateCheckResult {
    UpdateCheckState state = UpdateCheckState::Failed;
    std::string latestVersion;
    std::string releaseUrl;
    std::string message;
};

bool isNewerPartSpliceVersion(const std::string& candidate,
                              const std::string& current = kPartSpliceVersion);
UpdateCheckResult checkForPartSpliceUpdate();
