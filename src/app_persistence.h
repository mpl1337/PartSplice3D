#pragma once

#include "connector_types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

std::string pathToUtf8(const std::filesystem::path& path);
std::filesystem::path pathFromUtf8(const std::string& value);
std::string lowercaseExtension(const std::filesystem::path& path);
std::vector<uint8_t> readWholeFile(const std::filesystem::path& path);
bool saveConnectorDefaults(const DovetailSettings& settings);
bool loadConnectorDefaults(DovetailSettings& settings);
void loadRecentFiles(std::vector<std::filesystem::path>& recentFiles);
void rememberRecentFile(std::vector<std::filesystem::path>& recentFiles,
                        const std::filesystem::path& path);
