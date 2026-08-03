#pragma once

#include "stl_io.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ThreeMfObject {
    std::string name;
    TriangleMesh mesh;
    int plateIndex = 1;
    std::shared_ptr<const TriangleMesh> meshReference;

    const TriangleMesh& meshData() const {
        return meshReference ? *meshReference : mesh;
    }
};

struct ThreeMfLoadResult {
    bool ok = false;
    std::vector<ThreeMfObject> objects;
    std::vector<MeshMaterial> materials;
    std::vector<std::string> compatibilityWarnings;
    std::string projectSettings;
    std::vector<uint8_t> originalFileData;
    std::string message;
};

ThreeMfLoadResult loadThreeMf(const std::filesystem::path& path,
                               const std::atomic_bool* cancelRequested = nullptr);
bool saveThreeMf(const std::filesystem::path& path,
                 const std::vector<ThreeMfObject>& objects,
                 std::string& error,
                 const std::string& projectSettings = {},
                 const std::string& fallbackPrinterSettingsId = {},
                 const std::string& fallbackPrintSettingsId = {},
                 const std::string& fallbackFilamentSettingsId = {},
                 const std::atomic_bool* cancelRequested = nullptr,
                 const std::vector<uint8_t>* preservedSourceArchive = nullptr,
                 const std::vector<MeshMaterial>* materials = nullptr);
