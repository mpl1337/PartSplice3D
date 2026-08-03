#include "app_persistence.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <iomanip>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::filesystem::path connectorDefaultsPath() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(),
                                                  static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size())
        return std::filesystem::path(buffer.data()) /
               L"PartSplice3D" / L"connector-defaults.txt";
#endif
    return std::filesystem::temp_directory_path() / "PartSplice3D-connector-defaults.txt";
}

std::filesystem::path recentFilesPath() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(),
                                                  static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size())
        return std::filesystem::path(buffer.data()) / L"PartSplice3D" / L"recent-files.txt";
#endif
    return std::filesystem::temp_directory_path() / "PartSplice3D-recent-files.txt";
}

std::wstring normalizedPathKey(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::absolute(path, error);
    if (error) normalized = path;
    std::wstring value = normalized.lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

void saveRecentFiles(const std::vector<std::filesystem::path>& recentFiles) {
    const std::filesystem::path path = recentFilesPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return;
    std::ofstream output(path, std::ios::trunc);
    if (!output) return;
    for (const auto& recent : recentFiles) output << pathToUtf8(recent) << '\n';
}

}  // namespace

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::filesystem::path pathFromUtf8(const std::string& value) {
    const auto* begin = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(begin, begin + value.size()));
}

std::string lowercaseExtension(const std::filesystem::path& path) {
    std::string extension = pathToUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

std::vector<uint8_t> readWholeFile(const std::filesystem::path& path) {
    constexpr uint64_t maximumSize = 384ull * 1024ull * 1024ull;
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return {};
        const std::streamoff size = input.tellg();
        if (size <= 0 || static_cast<uint64_t>(size) > maximumSize) return {};
        input.seekg(0);
        std::vector<uint8_t> data(static_cast<size_t>(size));
        input.read(reinterpret_cast<char*>(data.data()), size);
        if (!input) return {};
        return data;
    } catch (const std::bad_alloc&) {
        return {};
    } catch (const std::exception&) {
        return {};
    }
}

bool saveConnectorDefaults(const DovetailSettings& settings) {
    const bool finiteValues = std::isfinite(settings.neckWidth) && std::isfinite(settings.headWidth) &&
        std::isfinite(settings.depth) && std::isfinite(settings.embed) &&
        std::isfinite(settings.clearance) && std::isfinite(settings.chamferWidth) &&
        std::isfinite(settings.chamferAngle) && std::isfinite(settings.minimumWall) &&
        std::isfinite(settings.assemblyMarkSize) && std::isfinite(settings.assemblyMarkDepth);
    if (!finiteValues || settings.neckWidth < 0.5 || settings.neckWidth > 1'000'000.0 ||
        settings.headWidth < 0.5 || settings.headWidth > 1'000'000.0 ||
        settings.depth < 0.5 || settings.depth > 1'000'000.0 ||
        settings.embed < 0.2 || settings.embed > 1'000'000.0 ||
        settings.clearance < 0.0 || settings.clearance > 1000.0 ||
        settings.chamferWidth < 0.0 || settings.chamferWidth > 1'000'000.0 ||
        settings.chamferAngle < 0.0 || settings.chamferAngle > 90.0 ||
        settings.minimumWall < 0.0 || settings.minimumWall > 20.0 ||
        settings.assemblyMarkSize < 2.0 || settings.assemblyMarkSize > 30.0 ||
        settings.assemblyMarkDepth < 0.05 || settings.assemblyMarkDepth > 5.0) return false;
    const std::filesystem::path path = connectorDefaultsPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << static_cast<int>(settings.type) << ' '
           << std::setprecision(17) << settings.neckWidth << ' '
           << settings.headWidth << ' ' << settings.depth << ' '
           << settings.embed << ' ' << settings.clearance << ' '
           << (settings.maleOnLeft ? 1 : 0) << ' '
           << (settings.leadChamfer ? 1 : 0) << ' '
           << settings.chamferWidth << ' ' << settings.chamferAngle << ' '
           << (settings.validateWallThickness ? 1 : 0) << ' '
           << settings.minimumWall << ' ' << (settings.assemblyMarks ? 1 : 0) << ' '
           << settings.assemblyMarkSize << ' ' << settings.assemblyMarkDepth << '\n';
    return static_cast<bool>(output);
}

bool loadConnectorDefaults(DovetailSettings& settings) {
    std::ifstream input(connectorDefaultsPath());
    if (!input) return false;
    int type = 0;
    int maleOnLeft = 1;
    DovetailSettings loaded = settings;
    if (!(input >> type >> loaded.neckWidth >> loaded.headWidth >> loaded.depth >>
          loaded.embed >> loaded.clearance >> maleOnLeft)) return false;
    if (type < static_cast<int>(ConnectorType::Dovetail) ||
        type > static_cast<int>(ConnectorType::RoundPin) ||
        !std::isfinite(loaded.neckWidth) || loaded.neckWidth < 0.5 || loaded.neckWidth > 1'000'000.0 ||
        !std::isfinite(loaded.headWidth) || loaded.headWidth < 0.5 || loaded.headWidth > 1'000'000.0 ||
        !std::isfinite(loaded.depth) || loaded.depth < 0.5 || loaded.depth > 1'000'000.0 ||
        !std::isfinite(loaded.embed) || loaded.embed < 0.2 || loaded.embed > 1'000'000.0 ||
        !std::isfinite(loaded.clearance) || loaded.clearance < 0.0 || loaded.clearance > 1000.0) return false;
    loaded.type = static_cast<ConnectorType>(type);
    loaded.maleOnLeft = maleOnLeft != 0;
    int leadChamfer = loaded.leadChamfer ? 1 : 0;
    double chamferWidth = loaded.chamferWidth;
    double chamferAngle = loaded.chamferAngle;
    if (input >> leadChamfer >> chamferWidth >> chamferAngle) {
        loaded.leadChamfer = leadChamfer != 0;
        loaded.chamferWidth = chamferWidth;
        loaded.chamferAngle = chamferAngle;
    }
    int validateWall = loaded.validateWallThickness ? 1 : 0;
    int assemblyMarks = loaded.assemblyMarks ? 1 : 0;
    double minimumWall = loaded.minimumWall;
    if (input >> validateWall >> minimumWall >> assemblyMarks) {
        loaded.validateWallThickness = validateWall != 0;
        loaded.minimumWall = minimumWall;
        loaded.assemblyMarks = assemblyMarks != 0;
    }
    double markSize = loaded.assemblyMarkSize;
    double markDepth = loaded.assemblyMarkDepth;
    if (input >> markSize >> markDepth) {
        loaded.assemblyMarkSize = markSize;
        loaded.assemblyMarkDepth = markDepth;
    }
    if ((loaded.type == ConnectorType::Dovetail || loaded.type == ConnectorType::Puzzle) &&
        loaded.headWidth <= loaded.neckWidth) return false;
    if (!std::isfinite(loaded.chamferWidth) || loaded.chamferWidth < 0.0 ||
        loaded.chamferWidth > 1'000'000.0 || !std::isfinite(loaded.chamferAngle) ||
        loaded.chamferAngle < 15.0 || loaded.chamferAngle > 75.0 ||
        !std::isfinite(loaded.minimumWall) || loaded.minimumWall < 0.0 ||
        loaded.minimumWall > 20.0 || !std::isfinite(loaded.assemblyMarkSize) ||
        loaded.assemblyMarkSize < 2.0 || loaded.assemblyMarkSize > 30.0 ||
        !std::isfinite(loaded.assemblyMarkDepth) || loaded.assemblyMarkDepth < 0.05 ||
        loaded.assemblyMarkDepth > 5.0) return false;
    settings = loaded;
    return true;
}

void loadRecentFiles(std::vector<std::filesystem::path>& recentFiles) {
    recentFiles.clear();
    std::ifstream input(recentFilesPath());
    std::string line;
    while (recentFiles.size() < 10 && std::getline(input, line)) {
        if (line.empty() || line.size() > 32'768) continue;
        try {
            const std::filesystem::path candidate = pathFromUtf8(line);
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) recentFiles.push_back(candidate);
        } catch (const std::exception&) {
            // Ignore malformed or unrepresentable entries in this non-critical list.
        }
    }
}

void rememberRecentFile(std::vector<std::filesystem::path>& recentFiles,
                        const std::filesystem::path& path) {
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) absolute = path;
    absolute = absolute.lexically_normal();
    const std::wstring key = normalizedPathKey(absolute);
    recentFiles.erase(std::remove_if(recentFiles.begin(), recentFiles.end(),
        [&](const std::filesystem::path& old) { return normalizedPathKey(old) == key; }), recentFiles.end());
    recentFiles.insert(recentFiles.begin(), absolute);
    if (recentFiles.size() > 10) recentFiles.resize(10);
    saveRecentFiles(recentFiles);
}
