#pragma once

#include <filesystem>
#include <string_view>

// Creates and exclusively reserves an empty file next to target. The caller
// owns the path and must remove it after success or failure.
std::filesystem::path reserveSecureTemporarySibling(const std::filesystem::path& target);

std::filesystem::path reserveSecureTemporaryFile(const std::filesystem::path& directory,
                                                  std::wstring_view prefix,
                                                  std::wstring_view extension);

std::filesystem::path createSecureTemporaryDirectory(const std::filesystem::path& parent,
                                                      std::wstring_view prefix);
