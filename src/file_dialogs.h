#pragma once

#include <filesystem>
#include <optional>

std::optional<std::filesystem::path> openModelDialog();
std::optional<std::filesystem::path> saveProjectDialog(const wchar_t* defaultName);
std::optional<std::filesystem::path> saveStlDialog(const wchar_t* defaultName);
std::optional<std::filesystem::path> saveThreeMfDialog(const wchar_t* defaultName);
std::optional<std::filesystem::path> selectFolderDialog();
