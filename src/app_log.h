#pragma once

#include <filesystem>
#include <string_view>

void initializeAppLog() noexcept;
void appLogInfo(std::string_view message) noexcept;
void appLogWarning(std::string_view message) noexcept;
void appLogError(std::string_view message) noexcept;
void appLogSystemError(std::string_view context, unsigned long errorCode) noexcept;
std::filesystem::path appLogDirectory() noexcept;
std::filesystem::path currentAppLogPath() noexcept;
