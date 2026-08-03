#pragma once

#include "math3d.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct MeshMaterial {
    std::string name;
    std::string color = "#00AE42";
};

struct TriangleMesh {
    std::vector<Vec3> vertices;
    std::vector<std::array<uint32_t, 3>> triangles;
    // Filament slots are one-based. An empty vector means that every triangle
    // uses defaultMaterial; a zero entry inherits defaultMaterial as well.
    uint32_t defaultMaterial = 1;
    std::vector<uint32_t> triangleMaterials;
    AABB bounds;
};

struct StlLoadResult {
    bool ok = false;
    TriangleMesh mesh;
    std::string message;
    size_t removedDegenerate = 0;
    size_t nonManifoldEdges = 0;
    bool orientationConflict = false;
};

StlLoadResult loadStl(const std::filesystem::path& path,
                      const std::atomic_bool* cancelRequested = nullptr);
bool saveBinaryStl(const std::filesystem::path& path, const TriangleMesh& mesh, std::string& error,
                   const std::atomic_bool* cancelRequested = nullptr);
void orientMeshConsistently(TriangleMesh& mesh, size_t& nonManifoldEdges, bool& conflict,
                            const std::atomic_bool* cancelRequested = nullptr);
